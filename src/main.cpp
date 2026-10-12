#include <iostream>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <stdexcept>
#include "PerformanceCalculator.h"
#include "StockDataProcessor.h"
#include "json.hpp"
#include "httplib.h"

namespace fs = std::filesystem;
using json = nlohmann::json;

bool compareDates(const std::string& date1, const std::string& date2) {
    // ISO dates have the same lexical and chronological order.
    return date1 < date2;
}

fs::path getProjectRoot(fs::path start) {
    start = fs::absolute(start);
    while (!start.empty()) {
        if (fs::is_directory(start / "dataa") && fs::exists(start / "src/html/dashboard.html"))
            return fs::canonical(start);
        const auto parent = start.parent_path();
        if (parent == start) break;
        start = parent;
    }
    throw std::runtime_error("Run from the project directory or pass its path as the first argument.");
}

void setCORSHeaders(httplib::Response& res) {
    res.set_header("Access-Control-Allow-Origin", "*");
    res.set_header("Access-Control-Allow-Methods", "POST, GET, OPTIONS");
    res.set_header("Access-Control-Allow-Headers", "Content-Type");
}

json readYahooManifest(const fs::path& projectRoot) {
    std::ifstream stream(projectRoot / ".cache/yahoo/manifest.json");
    if (!stream) throw std::runtime_error(
        "No refreshed Yahoo history. Run: python scripts/refresh_yahoo.py --tickers AMZN MSFT --period 1y");
    json manifest;
    stream >> manifest;
    if (!manifest.contains("datasets") || !manifest["datasets"].is_array())
        throw std::runtime_error("Invalid Yahoo cache catalog. Run the refresh command again.");
    return manifest;
}

// Resolve symlinks and keep comparison inputs within the supported data directories.
fs::path resolveDataset(const fs::path& root, const std::string& requested) {
    const fs::path relative(requested);
    if (relative.empty() || relative.is_absolute() || relative.has_root_name() || relative.extension() != ".csv")
        throw std::runtime_error("Select a CSV dataset from the dashboard.");
    const auto full = fs::weakly_canonical(root / relative);
    for (const auto& directory : {root / "dataa", root / ".cache/yahoo"}) {
        const auto within = full.lexically_relative(fs::weakly_canonical(directory));
        if (!within.empty() && *within.begin() != ".." && !within.is_absolute()) {
            // Most original dashboard entries point to dataa/, but their CSVs are at the repository root.
            if (!fs::exists(full) && relative.parent_path() == fs::path("dataa")) {
                const auto saved = fs::weakly_canonical(root / relative.filename());
                if (saved.parent_path() == root && fs::is_regular_file(saved)) return saved;
            }
            return full;
        }
    }
    throw std::runtime_error("Dataset must be inside the saved CSV or Yahoo cache directory.");
}

int main(int argc, char** argv) {
    fs::path projectRoot;
    try {
        projectRoot = getProjectRoot(argc > 1 ? fs::path(argv[1]) : fs::current_path());
    } catch (const std::exception& e) {
        std::cerr << e.what() << std::endl;
        return 1;
    }
    httplib::Server svr;
    svr.Options("/.*", [&](const httplib::Request&, httplib::Response& res) {
        setCORSHeaders(res);
        res.status = 204;
    });

    // Serve the existing dashboard without requiring an additional web server.
    svr.Get("/", [&](const httplib::Request&, httplib::Response& res) {
        std::ifstream stream(projectRoot / "src/html/dashboard.html");
        std::ostringstream html;
        html << stream.rdbuf();
        res.set_content(html.str(), "text/html");
    });
    svr.Get("/yahoo-history", [&](const httplib::Request&, httplib::Response& res) {
        setCORSHeaders(res);
        res.set_header("Cache-Control", "no-store");
        try {
            res.set_content(readYahooManifest(projectRoot).dump(), "application/json");
        } catch (const std::exception& e) {
            res.status = fs::exists(projectRoot / ".cache/yahoo/manifest.json") ? 500 : 404;
            res.set_content(e.what(), "text/plain");
        }
    });

    svr.Post("/compare", [&](const httplib::Request& req, httplib::Response& res) {
        setCORSHeaders(res);
        try {
            const auto input = json::parse(req.body);
            const std::string file1 = input.at("file1").get<std::string>();
            const std::string file2 = input.at("file2").get<std::string>();
            json results;
            json manifest;
            for (const auto& requested : {file1, file2}) {
                const auto file = resolveDataset(projectRoot, requested);
                StockDataProcessor processor;
                PerformanceCalculator calculator;
                const auto data = processor.readCSV(file.string());
                if (data.empty()) throw std::runtime_error("Dataset unavailable. Refresh or select another dataset.");
                auto aggregated = calculator.aggregateByDate(data);
                if (aggregated.size() < 4)
                    throw std::runtime_error("At least three valid daily prices are required for comparison.");
                json info = {{"source", "Saved CSV"}, {"fetched_at_utc", nullptr},
                             {"first_date", aggregated[1][1]}, {"as_of", aggregated.back()[1]},
                             {"rows", aggregated.size() - 1}, {"interval", "1d"},
                             {"price_basis", "Close from saved CSV"},
                             {"delay_note", "Stored historical data; freshness is not verified."}};
                const auto relative = file.lexically_relative(projectRoot).generic_string();
                if (relative.rfind(".cache/yahoo/", 0) == 0) {
                    if (manifest.is_null()) manifest = readYahooManifest(projectRoot);
                    bool found = false;
                    for (const auto& dataset : manifest["datasets"]) {
                        if (dataset.at("file").get<std::string>() == relative) {
                            for (const auto* key : {"source", "fetched_at_utc", "interval", "price_basis", "delay_note"}) {
                                if (manifest.contains(key)) info[key] = manifest[key];
                                if (dataset.contains(key)) info[key] = dataset[key];
                            }
                            found = true;
                            break;
                        }
                    }
                    if (!found) throw std::runtime_error("Yahoo history was refreshed. Reload the Yahoo data source and select again.");
                }
                calculator.calculateMergeSortTime(aggregated, compareDates);
                calculator.calculateHeapSortTime(aggregated, compareDates);
                // Example assumption: annual 1.5%, converted to daily by the calculator.
                const double sharpeRatio = calculator.calculateSharpeRatio(aggregated, 1.5);
                results[requested] = {
                    {"merge_sort_time_ms", calculator.getMergeSortTime()},
                    {"heap_sort_time_ms", calculator.getHeapSortTime()},
                    {"sharpe_ratio", sharpeRatio},
                    {"total_return", calculator.calculateTotalReturn(aggregated)},
                    {"data_info", info}
                };
            }
            res.set_content(results.dump(), "application/json");
        } catch (const std::exception& e) {
            res.status = 400;
            res.set_content(e.what(), "text/plain");
        }
    });
    svr.Get("/health", [&](const httplib::Request&, httplib::Response& res) {
        setCORSHeaders(res);
        res.set_content("{\"status\":\"OK\"}", "application/json");
    });
    std::cout << "Open http://localhost:8000 to compare saved or refreshed stock history." << std::endl;
    if (!svr.listen("localhost", 8000)) {
        std::cerr << "Could not listen on localhost:8000. Check whether another server is using it." << std::endl;
        return 1;
    }
    return 0;
}
