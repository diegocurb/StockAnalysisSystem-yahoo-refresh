#!/usr/bin/env python3
"""Fetch daily Yahoo history into an isolated, atomically published CSV cache."""

import argparse
import csv
from datetime import date, datetime, timezone
import json
import math
import os
from pathlib import Path
import re
import shutil
import sys
import tempfile


SOURCE = "Yahoo Finance via yfinance"
DELAY_NOTE = (
    "Daily history; latest bar may be in progress. Provider/exchange delays apply."
)
FIELDS = ("Open", "High", "Low", "Close", "Volume")
HEADER = ("Symbol", "Date", *FIELDS)
PERIODS = ("1mo", "3mo", "6mo", "1y", "2y", "5y", "10y", "ytd", "max")
SYMBOL_PATTERN = re.compile(r"[A-Z][A-Z0-9]{0,9}(?:[.-][A-Z0-9]{1,5})?\Z")


class RefreshError(Exception):
    """The refresh cannot safely replace the currently published cache."""


def validate_symbols(tickers):
    symbols = []
    for value in tickers:
        symbol = value.strip().upper()
        if not SYMBOL_PATTERN.fullmatch(symbol):
            raise RefreshError(f"Invalid stock ticker: {value!r}")
        if symbol not in symbols:
            symbols.append(symbol)
    if not symbols or len(symbols) > 20:
        raise RefreshError("Request between 1 and 20 different stock tickers.")
    return symbols


def _date_string(value):
    # Keep the exchange's daily calendar date, without converting to UTC.
    if isinstance(value, datetime):
        result = value.date()
    elif isinstance(value, date):
        result = value
    elif isinstance(value, str) and re.fullmatch(r"\d{4}-\d{2}-\d{2}", value):
        result = date.fromisoformat(value)
    else:
        raise RefreshError(f"Invalid daily date: {value!r}")
    encoded = result.isoformat()
    if not re.fullmatch(r"\d{4}-\d{2}-\d{2}", encoded):
        raise RefreshError(f"Invalid daily date: {value!r}")
    return encoded


def normalize_history(frame, symbol):
    """Return validated rows in the existing C++ application's CSV schema."""
    if frame is None or frame.empty:
        raise RefreshError(f"{symbol}: Yahoo returned no daily history.")

    selected = {}
    for column in frame.columns:
        if isinstance(column, tuple):
            # Older/newer provider versions can return (Price, Ticker) or
            # (Ticker, Price), despite the requested flat-column preference.
            if len(column) != 2 or symbol not in column:
                raise RefreshError(f"{symbol}: unexpected multi-ticker columns.")
            field = column[1] if column[0] == symbol else column[0]
        else:
            field = column
        if field in FIELDS:
            if field in selected:
                raise RefreshError(f"{symbol}: duplicate {field} column.")
            selected[field] = column
    if set(selected) != set(FIELDS):
        raise RefreshError(f"{symbol}: daily history is missing OHLC/Volume columns.")

    rows = []
    dates = set()
    for index, row in frame.iterrows():
        try:
            day = _date_string(index)
            values = [float(row[selected[field]]) for field in FIELDS]
        except (TypeError, ValueError, OverflowError) as exc:
            raise RefreshError(f"{symbol}: invalid date or numeric value.") from exc
        if day in dates:
            raise RefreshError(f"{symbol}: duplicate daily date {day}.")
        if not all(math.isfinite(value) for value in values):
            raise RefreshError(f"{symbol}: non-finite value on {day}.")
        opening, high, low, close, volume = values
        if min(opening, high, low, close) <= 0 or volume < 0 or not volume.is_integer():
            raise RefreshError(f"{symbol}: invalid price or volume on {day}.")
        tolerance = high * 1e-8
        if low > min(opening, close) + tolerance or high + tolerance < max(opening, close, low):
            raise RefreshError(f"{symbol}: inconsistent OHLC prices on {day}.")
        dates.add(day)
        rows.append((symbol, day, opening, high, low, close, int(volume)))
    if len(rows) < 3:
        raise RefreshError(f"{symbol}: at least 3 valid daily rows are required.")
    return sorted(rows, key=lambda row: row[1])


def _write_atomic(path, writer):
    """Write and flush a neighboring temporary file before atomic replacement."""
    temporary = None
    try:
        with tempfile.NamedTemporaryFile(
            mode="w", encoding="utf-8", newline="", dir=path.parent,
            prefix=path.name + ".", suffix=".tmp", delete=False,
        ) as handle:
            temporary = Path(handle.name)
            writer(handle)
            handle.flush()
            os.fsync(handle.fileno())
        os.replace(temporary, path)
    finally:
        if temporary is not None:
            temporary.unlink(missing_ok=True)


def refresh(tickers, period="1y", root=None, downloader=None):
    """Publish a new complete cache; a failed refresh leaves the old cache intact."""
    symbols = validate_symbols(tickers)
    if period not in PERIODS:
        raise RefreshError(f"Unsupported period: {period!r}")
    if downloader is None:
        try:
            import yfinance
        except ImportError as exc:
            raise RefreshError(
                "Install refresh dependencies: python -m pip install -r requirements-refresh.txt"
            ) from exc
        downloader = yfinance.download

    histories = {}
    for symbol in symbols:
        try:
            frame = downloader(
                symbol, period=period, interval="1d", auto_adjust=False,
                threads=False, progress=False, multi_level_index=False, timeout=20,
            )
            histories[symbol] = normalize_history(frame, symbol)
        except RefreshError:
            raise
        except Exception as exc:
            raise RefreshError(f"{symbol}: download failed ({exc}). Existing cache kept.") from exc

    fetched_at = datetime.now(timezone.utc).isoformat(timespec="seconds").replace("+00:00", "Z")
    project_root = Path(root) if root is not None else Path(__file__).resolve().parents[1]
    cache = project_root / ".cache" / "yahoo"
    cache.mkdir(parents=True, exist_ok=True)
    generation = Path(tempfile.mkdtemp(prefix="generation-", dir=cache))
    committed = False
    try:
        common = {
            "source": SOURCE, "fetched_at_utc": fetched_at,
            "interval": "1d", "price_basis": "Yahoo Close (auto_adjust=False; excludes dividend adjustment)",
            "delay_note": DELAY_NOTE,
        }
        datasets = []
        for symbol, rows in histories.items():
            destination = generation / f"{symbol}.csv"

            def write_csv(handle, rows=rows):
                writer = csv.writer(handle)
                writer.writerow(HEADER)
                writer.writerows(rows)

            _write_atomic(destination, write_csv)
            datasets.append({
                **common, "symbol": symbol, "name": symbol,
                "file": f".cache/yahoo/{generation.name}/{symbol}.csv",
                "as_of": rows[-1][1], "first_date": rows[0][1], "rows": len(rows),
            })
        manifest = {**common, "period": period, "datasets": datasets}
        _write_atomic(cache / "manifest.json", lambda handle: json.dump(manifest, handle, indent=2))
        committed = True
        return manifest
    except OSError as exc:
        raise RefreshError(f"Could not publish refreshed files ({exc}). Existing cache kept.") from exc
    finally:
        if not committed:
            # Only remove the new directory created by this invocation.
            shutil.rmtree(generation, ignore_errors=True)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--tickers", nargs="+", default=["AMZN", "MSFT"], help="Stock symbols (default: AMZN MSFT)")
    parser.add_argument("--period", choices=PERIODS, default="1y", help="History period (default: 1y)")
    args = parser.parse_args(argv)
    try:
        manifest = refresh(args.tickers, args.period)
    except (RefreshError, OSError) as exc:
        print(f"Refresh failed: {exc}", file=sys.stderr)
        return 1
    print(f"Fetched at {manifest['fetched_at_utc']} from {SOURCE}.")
    for item in manifest["datasets"]:
        print(f"{item['symbol']}: {item['rows']} daily rows; data as of {item['as_of']}; {item['file']}")
    print(DELAY_NOTE)
    print("Reload the dashboard to select the refreshed datasets.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
