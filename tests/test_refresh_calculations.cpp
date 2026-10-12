#include "PerformanceCalculator.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

using Rows = std::vector<std::vector<std::string>>;
const std::vector<std::string> header = {"Company", "Date", "Open", "High", "Low", "Close", "Volume"};
int checks = 0;
int comparisons = 0;

void check(bool condition, const char* message) {
    ++checks;
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(1);
    }
}

bool closeTo(double actual, double expected) {
    return std::isfinite(actual) && std::abs(actual - expected) < 1e-10;
}

std::vector<std::string> row(const std::string& date, const std::string& close) {
    return {"TEST", date, "0", "0", "0", close, "0"};
}

bool datesAscending(const std::string& left, const std::string& right) {
    ++comparisons;
    check(left.size() == 10 && right.size() == 10 && left != "Date" && right != "Date",
          "Benchmarks must compare only date rows, never the header");
    return left < right;
}

int main() {
    PerformanceCalculator calculator;
    const Rows unsorted = {header, row("2026-01-07", "130"), row("2026-01-05", "100"),
                           row("2026-01-06", "110"), row("2026-01-07", "134")};
    const auto aggregated = calculator.aggregateByDate(unsorted);
    check(aggregated.size() == 4 && aggregated.front() == header, "Aggregation retains the header");
    check(aggregated[1][1] == "2026-01-05" && aggregated[3][1] == "2026-01-07",
          "Aggregation orders dates chronologically");
    check(closeTo(std::stod(aggregated[3][5]), 132.0), "Aggregation averages duplicate-date closes");

    const auto daily = calculator.calculateDailyReturns(aggregated);
    check(daily.size() == 2 && closeTo(daily[0], 10.0) && closeTo(daily[1], 20.0),
          "Known price history produces daily percentage returns");
    check(closeTo(calculator.calculateTotalReturn(aggregated), 32.0), "Known history has 32 percent total return");
    const double dailyRiskFree = (std::pow(1.0 + 1.5 / 100.0, 1.0 / 252.0) - 1.0) * 100.0;
    check(closeTo(calculator.calculateSharpeRatio(aggregated, 1.5), (15.0 - dailyRiskFree) / 5.0),
          "Sharpe converts annual percentage risk-free rate into a daily percentage");
    check(closeTo(calculator.calculateSharpeRatio(aggregated, 0.0), 3.0),
          "Sharpe remains daily and uses population standard deviation");

    const auto snapshot = aggregated;
    calculator.calculateMergeSortTime(aggregated, datesAscending);
    check(comparisons > 0, "Merge benchmark runs the comparator");
    check(aggregated == snapshot, "Merge benchmark preserves header and price order");
    comparisons = 0;
    calculator.calculateHeapSortTime(aggregated, datesAscending);
    check(comparisons > 0, "Heap benchmark runs the comparator");
    check(aggregated == snapshot, "Heap benchmark preserves header and price order");
    check(closeTo(calculator.calculateTotalReturn(aggregated), 32.0), "Benchmarks do not change calculated return");
    check(calculator.getMergeSortTime() >= 0 && calculator.getHeapSortTime() >= 0, "Timings are nonnegative");

    const Rows flat = {header, row("2026-01-05", "100"), row("2026-01-06", "100"), row("2026-01-07", "100")};
    const Rows equalGrowth = {header, row("2026-01-05", "100"), row("2026-01-06", "200"), row("2026-01-07", "400")};
    check(std::isnan(calculator.calculateSharpeRatio(flat, 1.5)), "Flat prices have undefined Sharpe");
    check(std::isnan(calculator.calculateSharpeRatio(equalGrowth, 1.5)), "Identical nonzero returns have undefined Sharpe");
    check(std::isnan(calculator.calculateSharpeRatio({header, row("2026-01-05", "100"), row("2026-01-06", "110"), row("2026-01-07", "121")}, 1.5)),
          "Floating-point rounding in identical percentage growth does not imply useful variation");
    check(std::isnan(calculator.calculateSharpeRatio({header, row("2026-01-05", "100"), row("2026-01-06", "110")}, 1.5)),
          "One return is insufficient to estimate variation");
    check(std::isnan(calculator.calculateSharpeRatio(aggregated, -100.0)), "Invalid annual risk-free rate is rejected");
    check(std::isnan(calculator.calculateSharpeRatio(aggregated, std::numeric_limits<double>::infinity())),
          "Nonfinite annual risk-free rate is rejected");

    const Rows malformed = {header, {}, {"TEST", "2026-01-01"}, row("2026-01-02", "nan"),
                            row("2026-01-03", "inf"), row("2026-01-04", "0"),
                            row("2026-01-05", "-10"), row("2026-01-06", "100junk"),
                            row("", "100"), row("2026-01-07", "100"), row("2026-01-08", "110")};
    const auto clean = calculator.aggregateByDate(malformed);
    check(clean.size() == 3, "Aggregation skips short rows and nonfinite, nonpositive, malformed, or undated values");
    check(closeTo(calculator.calculateTotalReturn(malformed), 10.0), "Total return skips malformed closes safely");
    check(calculator.calculateDailyReturns({header, {}, row("2026-01-07", "100")}).empty(),
          "Daily returns skip adjacent malformed rows safely");
    check(calculator.aggregateByDate({}).empty(), "Empty input aggregates safely");
    check(calculator.aggregateByDate({{"bad header"}, row("2026-01-07", "100")}).empty(), "Short header is rejected safely");
    check(calculator.calculateDailyReturns({}).empty(), "Empty input produces no daily returns");
    check(std::isnan(calculator.calculateSharpeRatio({}, 1.5)), "Empty input has undefined Sharpe");
    check(closeTo(calculator.calculateTotalReturn({}), 0.0), "Empty input retains existing zero total-return fallback");
    check(closeTo(calculator.calculateTotalReturn({header, {}, row("2026-01-07", "nan")}), 0.0),
          "No valid prices retains zero total-return fallback");
    calculator.calculateMergeSortTime({}, datesAscending);
    calculator.calculateHeapSortTime({}, datesAscending);
    calculator.calculateMergeSortTime({header}, datesAscending);
    calculator.calculateHeapSortTime({header}, datesAscending);
    calculator.calculateMergeSortTime({header, {}}, datesAscending);
    calculator.calculateHeapSortTime({header, {}}, datesAscending);
    check(true, "Empty/header-only/malformed input benchmarks safely");

    std::cout << "PASS: " << checks << " calculation and benchmark checks\n";
}
