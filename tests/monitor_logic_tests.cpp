#include "../src/monitor_logic.h"

#include <cmath>
#include <iostream>
#include <string>
#include <vector>

namespace {

int failures = 0;

void expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

void testJsonExtraction() {
    const std::string json = R"({
        "access_token": "token-value",
        "rate_limit": {
            "primary_window": {"used_percent": 41, "reset_at": 1781752500}
        },
        "account_ordering": ["account-a"]
    })";

    expect(monitor_logic::extractJsonString(json, "access_token") == "token-value",
           "extracts a direct JSON string");
    expect(monitor_logic::extractJsonObjectForKey(json, "rate_limit").find("primary_window") != std::string::npos,
           "extracts a nested JSON object");
    expect(monitor_logic::extractFirstArrayString(json, "account_ordering") == "account-a",
           "extracts the first array string");

    double used = 0.0;
    const std::string window = monitor_logic::extractJsonObjectForKey(json, "primary_window");
    expect(monitor_logic::extractNumberAfter(window, 0, {"used_percent"}, used) && used == 41.0,
           "extracts a numeric quota value");

    expect(monitor_logic::extractJsonString(R"({"access_token": null, "later": "wrong"})", "access_token").empty(),
           "does not scan ahead when the requested value has the wrong type");
    expect(monitor_logic::extractJsonObjectForKey(R"({"rate_limit": null, "later": {}})", "rate_limit").empty(),
           "does not scan ahead for a later object");
}

void testCounterWrap() {
    expect(monitor_logic::counterDelta32(125, 100) == 25, "handles a normal 32-bit counter delta");
    expect(monitor_logic::counterDelta32(20, 0xFFFFFFF0ULL) == 36, "handles a 32-bit counter wrap");
}

void testGpuAggregation() {
    const std::vector<std::pair<std::uint32_t, double>> samples = {
        {10, 70.0},
        {10, 40.0},
        {20, 55.0},
        {0, 80.0},
        {30, 130.0},
    };
    const auto result = monitor_logic::aggregateGpuSamples(samples);
    expect(std::abs(result.overall - 100.0) < 0.001, "caps overall GPU usage at 100 percent");
    expect(std::abs(result.byProcess.at(10) - 70.0) < 0.001,
           "uses the busiest GPU engine for each process");
    expect(std::abs(result.byProcess.at(20) - 55.0) < 0.001, "keeps independent process usage");
    expect(std::abs(result.byProcess.at(30) - 100.0) < 0.001, "caps per-process GPU usage");
}

} // namespace

int main() {
    testJsonExtraction();
    testCounterWrap();
    testGpuAggregation();
    if (failures != 0) {
        std::cerr << failures << " test(s) failed\n";
        return 1;
    }
    std::cout << "All monitor logic tests passed\n";
    return 0;
}
