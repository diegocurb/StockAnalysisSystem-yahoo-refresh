#ifndef PERFORMANCECALCULATOR_H
#define PERFORMANCECALCULATOR_H

#include <vector>
#include <string>

class PerformanceCalculator {
public:

    std::vector<double> calculateDailyReturns(const std::vector<std::vector<std::string>>& aggregatedData);

    // Daily, unannualized Sharpe using population standard deviation. riskFreeRate is an annual
    // percentage (e.g. 1.5), converted to a daily percentage with 252 trading days per year.
    // Returns NaN when fewer than two valid returns exist or their variance is zero.
    double calculateSharpeRatio(const std::vector<std::vector<std::string>>& aggregatedData, double riskFreeRate);

    std::vector<std::vector<std::string>> aggregateByDate(const std::vector<std::vector<std::string>>& data);

    static double calculateTotalReturn(const std::vector<std::vector<std::string>>& data);

    // Benchmarks sort independent copies of data rows, preserving the header and chronological prices.
    void calculateMergeSortTime(const std::vector<std::vector<std::string>>& data, bool (*comp)(const std::string&, const std::string&));

    // Method to calculate and store Heap Sort time
    void calculateHeapSortTime(const std::vector<std::vector<std::string>>& data, bool (*comp)(const std::string&, const std::string&));

    // Get the recorded time for Merge Sort
    double getMergeSortTime() const;

    // Get the recorded time for Heap Sort
    double getHeapSortTime() const;

private:
    double mergeSortTime = 0;  // Time for Merge Sort in fractional milliseconds
    double heapSortTime = 0;   // Time for Heap Sort in fractional milliseconds
};

#endif
