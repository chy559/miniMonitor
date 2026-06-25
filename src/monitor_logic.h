#pragma once

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace monitor_logic {

inline size_t skipJsonWhitespace(const std::string& json, size_t pos) {
    while (pos < json.size() &&
           (json[pos] == ' ' || json[pos] == '\t' || json[pos] == '\r' || json[pos] == '\n')) {
        ++pos;
    }
    return pos;
}

inline size_t findJsonCloser(const std::string& json, size_t openPos, char openChar, char closeChar) {
    if (openPos >= json.size() || json[openPos] != openChar) {
        return std::string::npos;
    }

    int depth = 0;
    bool inString = false;
    bool escape = false;
    for (size_t i = openPos; i < json.size(); ++i) {
        const char ch = json[i];
        if (inString) {
            if (escape) {
                escape = false;
            } else if (ch == '\\') {
                escape = true;
            } else if (ch == '"') {
                inString = false;
            }
            continue;
        }

        if (ch == '"') {
            inString = true;
        } else if (ch == openChar) {
            ++depth;
        } else if (ch == closeChar) {
            --depth;
            if (depth == 0) {
                return i;
            }
        }
    }
    return std::string::npos;
}

inline std::string extractJsonString(const std::string& json, const std::string& key) {
    const std::string needle = "\"" + key + "\"";
    const size_t keyPos = json.find(needle);
    if (keyPos == std::string::npos) {
        return "";
    }
    const size_t colon = json.find(':', keyPos + needle.size());
    if (colon == std::string::npos) {
        return "";
    }
    const size_t quote = skipJsonWhitespace(json, colon + 1);
    if (quote >= json.size() || json[quote] != '"') {
        return "";
    }

    std::string out;
    bool escape = false;
    for (size_t i = quote + 1; i < json.size(); ++i) {
        const char ch = json[i];
        if (escape) {
            out.push_back(ch);
            escape = false;
        } else if (ch == '\\') {
            escape = true;
        } else if (ch == '"') {
            return out;
        } else {
            out.push_back(ch);
        }
    }
    return "";
}

inline bool extractNumberAfter(const std::string& json, size_t start,
                               const std::vector<std::string>& keys, double& value) {
    size_t best = std::string::npos;
    for (const auto& key : keys) {
        const size_t pos = json.find("\"" + key + "\"", start);
        if (pos != std::string::npos && (best == std::string::npos || pos < best)) {
            best = pos;
        }
    }
    if (best == std::string::npos || best > start + 900) {
        return false;
    }
    const size_t colon = json.find(':', best);
    if (colon == std::string::npos) {
        return false;
    }
    const size_t begin = skipJsonWhitespace(json, colon + 1);
    if (begin >= json.size() || std::string("-0123456789").find(json[begin]) == std::string::npos) {
        return false;
    }

    char* end = nullptr;
    value = std::strtod(json.c_str() + begin, &end);
    return end && end != json.c_str() + begin;
}

inline std::string extractJsonObjectForKey(const std::string& json, const std::string& key) {
    const std::string needle = "\"" + key + "\"";
    const size_t keyPos = json.find(needle);
    if (keyPos == std::string::npos) {
        return "";
    }
    const size_t colon = json.find(':', keyPos + needle.size());
    if (colon == std::string::npos) {
        return "";
    }
    const size_t open = skipJsonWhitespace(json, colon + 1);
    if (open >= json.size() || json[open] != '{') {
        return "";
    }
    const size_t close = findJsonCloser(json, open, '{', '}');
    return close == std::string::npos ? "" : json.substr(open, close - open + 1);
}

inline std::string extractFirstArrayString(const std::string& json, const std::string& key) {
    const std::string needle = "\"" + key + "\"";
    const size_t keyPos = json.find(needle);
    if (keyPos == std::string::npos) {
        return "";
    }
    const size_t colon = json.find(':', keyPos + needle.size());
    const size_t open = colon == std::string::npos ? std::string::npos : skipJsonWhitespace(json, colon + 1);
    if (open == std::string::npos || open >= json.size() || json[open] != '[') {
        return "";
    }
    const size_t quote = skipJsonWhitespace(json, open + 1);
    if (quote >= json.size() || json[quote] != '"') {
        return "";
    }

    std::string out;
    bool escape = false;
    for (size_t i = quote + 1; i < json.size(); ++i) {
        const char ch = json[i];
        if (escape) {
            out.push_back(ch);
            escape = false;
        } else if (ch == '\\') {
            escape = true;
        } else if (ch == '"') {
            return out;
        } else {
            out.push_back(ch);
        }
    }
    return "";
}

inline std::uint64_t counterDelta32(std::uint64_t current, std::uint64_t previous) {
    return current >= previous ? current - previous : (0x100000000ULL - previous) + current;
}

struct GpuAggregate {
    double overall = 0.0;
    std::map<std::uint32_t, double> byProcess;
};

inline GpuAggregate aggregateGpuSamples(const std::vector<std::pair<std::uint32_t, double>>& samples) {
    GpuAggregate result;
    for (const auto& [pid, rawUsage] : samples) {
        const double usage = std::clamp(rawUsage, 0.0, 100.0);
        result.overall = std::max(result.overall, usage);
        if (pid != 0) {
            result.byProcess[pid] = std::max(result.byProcess[pid], usage);
        }
    }
    return result;
}

} // namespace monitor_logic
