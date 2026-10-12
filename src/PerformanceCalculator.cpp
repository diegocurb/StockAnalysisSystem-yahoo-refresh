#include "PerformanceCalculator.h"
#include <chrono>  // For time calculation
#include "Mergesort.h"
#include "Heap.h"
#include <iostream>
#include <numeric>
#include <map>
#include <cmath>
#include <limits>

namespace {
bool readClose(const std::vector<std::string>& row, double& close) {
    if (row.size() <= 5) {
        return false;
    }
    try {
        size_t parsed = 0;
        close = std::stod(row[5], &parsed);
        return parsed == row[5].size() && std::isfinite(close) && close > 0.0;
    } catch (...) {
        return false;
    }
}

std::vector<std::vector<std::string>> benchmarkRows(const std::vector<std::vector<std::string>>& data) {
    std::vector<std::vector<std::string>> rows;
    for (size_t i = 1; i < data.size(); ++i) {
        if (data[i].size() > 1) {
            rows.push_back(data[i]);
        }
    }
    return rows;
}
}



std::vector<double> PerformanceCalculator::calculateDailyReturns(const std::vector<std::vector<std::string>>& aggregatedData) {
    std::vector<double> dailyReturns;

    for (size_t i = 2; i < aggregatedData.size(); ++i) { // Start from 2 to skip the header row
        double closeToday, closeYesterday;
        if (readClose(aggregatedData[i], closeToday) && readClose(aggregatedData[i - 1], closeYesterday)) {
            double dailyReturn = ((closeToday - closeYesterday) / closeYesterday) * 100.0;
            if (std::isfinite(dailyReturn)) {
                dailyReturns.push_back(dailyReturn);
            }
        }
    }

    return dailyReturns;
}


double PerformanceCalculator::calculateSharpeRatio(const std::vector<std::vector<std::string>>& aggregatedData, double riskFreeRate) {
    const auto dailyReturns = calculateDailyReturns(aggregatedData);
    if (dailyReturns.size() < 2 || !std::isfinite(riskFreeRate) || riskFreeRate <= -100.0) {
        return std::numeric_limits<double>::quiet_NaN();
    }

    // Calculate average daily return
    double totalReturn = std::accumulate(dailyReturns.begin(), dailyReturns.end(), 0.0);
    double avgReturn = totalReturn / dailyReturns.size();

    // Calculate standard deviation of daily returns
    double variance = 0.0;
    for (double dailyReturn : dailyReturns) {
        variance += std::pow(dailyReturn - avgReturn, 2);
    }
    variance /= dailyReturns.size();
    double stdDev = std::sqrt(variance);

    const double roundingTolerance = std::numeric_limits<double>::epsilon() * (std::abs(avgReturn) + 1.0) * 8.0;
    if (!std::isfinite(stdDev) || stdDev <= roundingTolerance) {
        return std::numeric_limits<double>::quiet_NaN();
    }

    // Daily returns are percentages; convert the annual percentage rate to the same period and units.
    const double dailyRiskFreeRate = (std::pow(1.0 + riskFreeRate / 100.0, 1.0 / 252.0) - 1.0) * 100.0;
    double sharpeRatio = (avgReturn - dailyRiskFreeRate) / stdDev;

    return sharpeRatio;
}

std::vector<std::vector<std::string>> PerformanceCalculator::aggregateByDate(const std::vector<std::vector<std::string>>& data) {
    if (data.empty() || data[0].size() <= 5) {
        return {};
    }
    std::map<std::string, std::vector<double>> dateToCloseValues;

    // Group Close values by Date
    for (size_t i = 1; i < data.size(); ++i) { // Start from 1 to skip the header
        double close;
        if (readClose(data[i], close) && !data[i][1].empty()) {
            std::string date = data[i][1]; // Date column
            dateToCloseValues[date].push_back(close);
        }
    }

    // Create a new dataset with aggregated values
    std::vector<std::vector<std::string>> aggregatedData;
    aggregatedData.push_back(data[0]); // Copy the header row

    for (const auto& [date, closes] : dateToCloseValues) {
        double aggregatedClose = std::accumulate(closes.begin(), closes.end(), 0.0) / closes.size(); // Average
        aggregatedData.push_back({data[0][0], date, "0", "0", "0", std::to_string(aggregatedClose), "0"}); // Fill other columns with placeholders
    }

    return aggregatedData;
}


double PerformanceCalculator::calculateTotalReturn(const std::vector<std::vector<std::string>>& data) {
    if (data.size() < 2) {
        return 0.0; // Insufficient data, return 0.0
    }

    // Initialize variables
    double startPrice = -1.0;
    double endPrice = -1.0;

    // Find the first positive "Close" value from the beginning
    for (size_t i = 1; i < data.size(); ++i) { // Start from row 1 to skip the header
        if (readClose(data[i], startPrice)) {
            break;
        }
        startPrice = -1.0;
    }

    // Find the last positive "Close" value from the end
    for (size_t i = data.size() - 1; i > 0; --i) { // Start from the last row
        if (readClose(data[i], endPrice)) {
            break;
        }
        endPrice = -1.0;
    }

    // Ensure both prices are valid
    if (startPrice <= 0.0 || endPrice <= 0.0) {
        return 0.0; // Return 0 if valid prices cannot be found
    }

    // Calculate the total return
    double totalReturn = ((endPrice - startPrice) / startPrice) * 100;

    return totalReturn;
}




// Method to calculate and store Merge Sort time
void PerformanceCalculator::calculateMergeSortTime(const std::vector<std::vector<std::string>>& data, bool (*comp)(const std::string&, const std::string&)) {
    MergeSort mergesort;
    auto rows = benchmarkRows(data);

    auto start = std::chrono::high_resolution_clock::now();
    if (rows.size() > 1) {
        mergesort.mergeSort(rows, 0, rows.size() - 1, comp);
    }
    auto end = std::chrono::high_resolution_clock::now();

    mergeSortTime = std::chrono::duration<double, std::milli>(end - start).count();
}

// Method to calculate and store Heap Sort time
void PerformanceCalculator::calculateHeapSortTime(const std::vector<std::vector<std::string>>& data, bool (*comp)(const std::string&, const std::string&)) {
    Heap heapSort;
    auto rows = benchmarkRows(data);

    auto start = std::chrono::high_resolution_clock::now();
    if (rows.size() > 1) {
        heapSort.heapSort(rows, 0, rows.size() - 1, comp);
    }
    auto end = std::chrono::high_resolution_clock::now();

    heapSortTime = std::chrono::duration<double, std::milli>(end - start).count();
}

// Get the recorded time for Merge Sort
double PerformanceCalculator::getMergeSortTime() const {
    return mergeSortTime;
}

// Get the recorded time for Heap Sort
double PerformanceCalculator::getHeapSortTime() const {
    return heapSortTime;
}
