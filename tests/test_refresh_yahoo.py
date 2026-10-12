"""Offline provider/transaction tests: python -m unittest discover -s tests -p test_refresh_yahoo.py"""

from contextlib import redirect_stderr, redirect_stdout
import csv
import importlib.util
import io
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import Mock, patch

import pandas as pd


SCRIPT = Path(__file__).resolve().parents[1] / "scripts" / "refresh_yahoo.py"
SPEC = importlib.util.spec_from_file_location("refresh_yahoo", SCRIPT)
helper = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(helper)


def sample(symbol="AMZN", multi=False, reversed_levels=False):
    frame = pd.DataFrame({
        "Open": [100.0, 101.0, 102.0], "High": [103.0, 104.0, 105.0],
        "Low": [99.0, 100.0, 101.0], "Close": [102.0, 103.0, 104.0],
        "Adj Close": [101.5, 102.5, 103.5], "Volume": [1000, 1100, 1200],
    }, index=pd.to_datetime(["2026-10-07", "2026-10-08", "2026-10-09"]))
    if multi:
        tuples = [(symbol, field) if reversed_levels else (field, symbol) for field in frame.columns]
        frame.columns = pd.MultiIndex.from_tuples(tuples)
    return frame


class RefreshTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name)
        self.addCleanup(self.temporary.cleanup)

    def refresh(self, *args, **kwargs):
        return helper.refresh(*args, root=self.root, **kwargs)

    def test_csv_schema_close_position_and_metadata(self):
        download = Mock(return_value=sample())
        manifest = self.refresh(["amzn", "MSFT"], downloader=download)
        self.assertEqual(download.call_count, 2)
        download.assert_any_call("AMZN", period="1y", interval="1d", auto_adjust=False,
                                 threads=False, progress=False, multi_level_index=False, timeout=20)
        self.assertEqual(manifest["source"], helper.SOURCE)
        self.assertEqual(manifest["price_basis"], "Yahoo Close (auto_adjust=False; excludes dividend adjustment)")
        self.assertTrue(manifest["fetched_at_utc"].endswith("Z"))
        self.assertIn("delays", manifest["delay_note"])
        self.assertEqual(len(manifest["datasets"]), 2)
        for dataset in manifest["datasets"]:
            self.assertEqual(dataset["as_of"], "2026-10-09")
            self.assertEqual(dataset["first_date"], "2026-10-07")
            self.assertEqual(dataset["rows"], 3)
            self.assertEqual(dataset["fetched_at_utc"], manifest["fetched_at_utc"])
            with (self.root / dataset["file"]).open(newline="", encoding="utf-8") as handle:
                rows = list(csv.reader(handle))
            self.assertEqual(rows[0], ["Symbol", "Date", "Open", "High", "Low", "Close", "Volume"])
            self.assertEqual(rows[1][0], dataset["symbol"])
            self.assertEqual(rows[1][1], "2026-10-07")
            self.assertEqual(float(rows[1][5]), 102.0)
        on_disk = json.loads((self.root / ".cache/yahoo/manifest.json").read_text())
        self.assertEqual(on_disk, manifest)

    def test_multi_index_in_both_orders(self):
        for reversed_levels in (False, True):
            with self.subTest(reversed_levels=reversed_levels):
                rows = helper.normalize_history(sample(multi=True, reversed_levels=reversed_levels), "AMZN")
                self.assertEqual(rows[0][5], 102.0)

    def test_wrong_ticker_multi_index_rejected(self):
        with self.assertRaises(helper.RefreshError):
            helper.normalize_history(sample("MSFT", multi=True), "AMZN")

    def test_unsorted_dates_sorted_in_output(self):
        rows = helper.normalize_history(sample().iloc[::-1], "AMZN")
        self.assertEqual([row[1] for row in rows], ["2026-10-07", "2026-10-08", "2026-10-09"])

    def test_timezone_keeps_exchange_calendar_day(self):
        frame = sample()
        frame.index = frame.index.tz_localize("Pacific/Auckland")
        self.assertEqual(helper.normalize_history(frame, "AMZN")[0][1], "2026-10-07")

    def test_bad_history_rejected(self):
        cases = {"empty": pd.DataFrame(), "too_short": sample().iloc[:2],
                 "missing_column": sample().drop(columns=["Close"])}
        for label, field, value in (("nan", "Close", float("nan")),
                                    ("infinity", "Open", float("inf")),
                                    ("zero_price", "Close", 0),
                                    ("negative_volume", "Volume", -1),
                                    ("fractional_volume", "Volume", 0.5),
                                    ("low_too_high", "Low", 150),
                                    ("high_too_low", "High", 1)):
            frame = sample().astype(float)
            frame.loc[frame.index[0], field] = value
            cases[label] = frame
        duplicate = sample()
        duplicate.index = pd.to_datetime(["2026-10-07", "2026-10-07", "2026-10-09"])
        cases["duplicate_date"] = duplicate
        invalid_date = sample()
        invalid_date.index = ["not-a-date", "2026-10-08", "2026-10-09"]
        cases["bad_date"] = invalid_date
        cases["missing_response"] = None
        for label, frame in cases.items():
            with self.subTest(label=label), self.assertRaises(helper.RefreshError):
                helper.normalize_history(frame, "AMZN")

    def test_tickers_and_period_are_validated_before_network(self):
        download = Mock()
        for symbol in ("../AMZN", "AMZN/MSFT", "A;bad", "A\\B", "^GSPC", "", "CON:bad"):
            with self.subTest(symbol=symbol), self.assertRaises(helper.RefreshError):
                self.refresh([symbol], downloader=download)
        with self.assertRaises(helper.RefreshError):
            self.refresh(["AMZN"], period="nonsense", downloader=download)
        with self.assertRaises(helper.RefreshError):
            self.refresh([], downloader=download)
        download.assert_not_called()
        self.assertEqual(helper.validate_symbols(["amzn", "AMZN", "BRK-B"]), ["AMZN", "BRK-B"])

    def _existing_cache(self):
        manifest = self.refresh(["AMZN"], downloader=Mock(return_value=sample()))
        manifest_path = self.root / ".cache/yahoo/manifest.json"
        data_path = self.root / manifest["datasets"][0]["file"]
        return manifest_path, manifest_path.read_bytes(), data_path, data_path.read_bytes()

    def test_network_and_partial_failure_preserve_previous_files(self):
        manifest_path, manifest_bytes, data_path, data_bytes = self._existing_cache()
        cases = [OSError("network unavailable"), pd.DataFrame(), sample().iloc[:1]]
        for failure in cases:
            with self.subTest(failure=str(failure)):
                download = Mock(side_effect=[sample(), failure])
                with self.assertRaises(helper.RefreshError):
                    self.refresh(["AMZN", "MSFT"], downloader=download)
                self.assertEqual(manifest_path.read_bytes(), manifest_bytes)
                self.assertEqual(data_path.read_bytes(), data_bytes)
        self.assertEqual(len(list(manifest_path.parent.glob("generation-*"))), 1)

    def test_manifest_write_failure_preserves_previous_and_cleans_own_generation(self):
        manifest_path, manifest_bytes, data_path, data_bytes = self._existing_cache()
        real_replace = helper.os.replace

        def fail_manifest(source, destination):
            if Path(destination).name == "manifest.json":
                raise OSError("simulated disk failure")
            return real_replace(source, destination)

        with patch.object(helper.os, "replace", side_effect=fail_manifest):
            with self.assertRaises(helper.RefreshError):
                self.refresh(["AMZN", "MSFT"], downloader=Mock(return_value=sample()))
        self.assertEqual(manifest_path.read_bytes(), manifest_bytes)
        self.assertEqual(data_path.read_bytes(), data_bytes)
        self.assertEqual(len(list(manifest_path.parent.glob("generation-*"))), 1)
        self.assertFalse(list(manifest_path.parent.glob("*.tmp")))

    def test_successful_refresh_keeps_old_generation_for_inflight_readers(self):
        manifest_path, manifest_bytes, data_path, data_bytes = self._existing_cache()
        manifest = self.refresh(["MSFT"], downloader=Mock(return_value=sample()))
        self.assertNotEqual(manifest_path.read_bytes(), manifest_bytes)
        self.assertEqual(data_path.read_bytes(), data_bytes)
        self.assertEqual(manifest["datasets"][0]["symbol"], "MSFT")

    def test_cli_defaults_success_and_nonzero_failure(self):
        expected = {"fetched_at_utc": "2026-10-12T20:00:00Z", "datasets": [
            {"symbol": "AMZN", "rows": 3, "as_of": "2026-10-09", "file": "example.csv"}]}
        output = io.StringIO()
        with patch.object(helper, "refresh", return_value=expected) as refresh, redirect_stdout(output):
            self.assertEqual(helper.main([]), 0)
        refresh.assert_called_once_with(["AMZN", "MSFT"], "1y")
        self.assertIn("as of 2026-10-09", output.getvalue())
        errors = io.StringIO()
        with patch.object(helper, "refresh", side_effect=helper.RefreshError("unavailable")), redirect_stderr(errors):
            self.assertEqual(helper.main(["--tickers", "MSFT", "--period", "6mo"]), 1)
        self.assertIn("Refresh failed", errors.getvalue())


if __name__ == "__main__":
    unittest.main()
