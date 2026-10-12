# Stock Analysis System: Yahoo daily-history extension

This isolated copy adds an on-demand Yahoo Finance history download to the existing C++ stock comparison application. The original app reads stored CSV files; this recent extension supplies fresh CSV files to the same comparison flow. It fetches the latest available **daily history**, not streaming or guaranteed real-time quotes, and does not forecast stock prices.

Original project: [hassantennn/StockAnalysisSystem](https://github.com/hassantennn/StockAnalysisSystem), credited upstream to HHD Squad (Muhammad Hassan, Haya Shaikh, Diego Curbelo). This copy is on local branch `feature/yahoo-history-refresh`; no upstream files or repositories were changed.

## Build and open

Use CMake 3.16+ and a C++17 compiler with `<filesystem>` support. The current CLion-bundled compiler is suitable; the older Downloads GCC 7 installation is not supported by this build.

Run these commands from the project directory:

```text
cmake -S . -B build -DBUILD_TESTING=ON
cmake --build build --config Release
```

Start the built application from the project directory. With a single-configuration Windows compiler:

```powershell
.\build\StockMarketAnalysisSystem.exe
```

For Visual Studio builds, use `.\build\Release\StockMarketAnalysisSystem.exe`; on Linux/macOS, use `./build/StockMarketAnalysisSystem`. If starting elsewhere, pass the project directory as the first argument, for example `StockMarketAnalysisSystem.exe "C:\path\to\project"`.

Open [http://localhost:8000](http://localhost:8000). The dashboard initially uses **Saved data**. Select two companies and click **Compare**. The original stored datasets work without installing Python or fetching Yahoo data. The sorting-time graph works offline and displays fractional milliseconds.

## Fetch and compare recent daily history

Use Python 3.10+ with internet access. In a second terminal at the project directory:

```text
python -m pip install -r requirements-refresh.txt
python scripts/refresh_yahoo.py --tickers AMZN MSFT --period 1y
```

Reload the dashboard page, select **Yahoo Finance**, choose two symbols, and compare. After another download, reload the page or switch to **Saved data** and back to the Yahoo source, then select your symbols again. The command downloads data; changing the selected data source reads the saved catalog. A comparison itself does not fetch new quotes.

You can substitute other stock symbols and periods, for example `--tickers AAPL NVDA --period 6mo`. The default is AMZN and MSFT over one year. Refreshing replaces the available Yahoo selection with the requested symbols, so request every symbol you want in that selection together.

The command and dashboard show the source, fetch time in UTC, each dataset's last available date, and the delay note. The latest daily bar may still be in progress. Exchange/provider delays vary; there is no fixed freshness guarantee. See the [yfinance download documentation](https://ranaroussi.github.io/yfinance/reference/api/yfinance.download.html) and [Yahoo exchange/data-delay reference](https://help.yahoo.com/kb/finance/article-exchanges-data-delays-sln2310.html).

## What changed

- `scripts/refresh_yahoo.py` uses `yfinance.download` with daily intervals, `auto_adjust=False`, a timeout, and explicit download options. It validates dates, OHLC prices, volume, and at least three daily rows, then writes `Symbol,Date,Open,High,Low,Close,Volume` for the existing parser.
- Downloads go to separate generation directories under `.cache/yahoo`. The manifest is replaced atomically only after every requested symbol succeeds. A failed download leaves the previous cache and original CSV files usable. Previous generations are retained; after one successful download, cached comparison works offline.
- The C++ server exposes the cached catalog and serves the existing dashboard. The dashboard adds a data-source selector and source/freshness details while keeping saved CSV mode as the default.

Small calculation fixes support trustworthy comparison: both sorting benchmarks use independent copies without the header, leaving prices chronological for the metrics. The annual example risk-free rate of 1.5% is converted to daily percentage units using 252 trading days. Invalid prices are ignored by the calculation path; zero or insufficient return variation makes the Sharpe ratio unavailable.

Some original dashboard entries point to `dataa/<name>.csv` even though their files are stored at the project root. When that selected file is absent from `dataa`, the server checks the same filename at the root, preserving those original choices without moving their datasets.

## Metric interpretation

The displayed return is the percentage change between the first and last valid Close price. Yahoo datasets use **Yahoo Close with `auto_adjust=False`**, excluding dividend adjustment; Yahoo's Close may already reflect stock splits. The app performs no additional corporate-action adjustment. This is a price return, not a dividend-inclusive investment total return.

Sharpe is a **daily, unannualized historical ratio** using the existing population-standard-deviation convention and the example 1.5% annual risk-free assumption. It describes past returns relative to their variability; it does not predict future prices. Compare datasets with the same requested period and check their displayed date ranges. The dashboard's higher-return comparison is a historical comparison, not an investment recommendation. Sort timings retain fractional milliseconds so small daily datasets remain visible on the chart.

## Verification

```text
python -m unittest discover -s tests -p test_refresh_yahoo.py
ctest --test-dir build -C Release --output-on-failure
```

The Python suite covers schema compatibility, flat/MultiIndex provider responses, invalid data, CLI status, and preservation of the existing cache after partial downloads or publication failures. CTest covers the metric calculation and sorting integration. Set `-DBUILD_TESTING=OFF` when configuring if only the app is needed.

A real refresh on October 12, 2026 with yfinance 1.7.0 and pandas 3.0.6 returned 251 daily rows each for AMZN and MSFT, with data as of 2026-10-12, fetched at `2026-10-12T22:10:03Z`. This records that test run; refresh again to obtain the latest available history.

## Interview walkthrough

“The original project compared stock data from stored CSV files in C++. A recent extension adds a Python helper that requests the latest available Yahoo daily history and converts it into the same CSV format. The dashboard can select that refreshed cache, while the existing C++ code still calculates historical return and Sharpe and compares merge-sort and heap-sort timing. The helper validates the download before publishing it so a failed refresh preserves the previous usable data, and the dashboard displays the source and data dates. It uses daily historical data; it does not stream live prices or forecast them.”

