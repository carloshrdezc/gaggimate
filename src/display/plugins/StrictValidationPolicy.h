#ifndef STRICTVALIDATIONPOLICY_H
#define STRICTVALIDATIONPOLICY_H

#include <cmath>
#include <cstdlib>
#include <string>
#include <utility>
#include <vector>

namespace strict_validation {

struct Error {
    std::string field;
    std::string message;
};
struct Field {
    std::string name;
    std::string value;
    bool isNumeric = false;
};
using Fields = std::vector<Field>;

inline bool parseInteger(const std::string &text, long &value) {
    if (text.empty())
        return false;
    char *end = nullptr;
    const char *begin = text.c_str();
    value = std::strtol(begin, &end, 10);
    return end != begin && *end == '\0';
}

inline bool parseNumber(const std::string &text, double &value) {
    if (text.empty())
        return false;
    char *end = nullptr;
    const char *begin = text.c_str();
    value = std::strtod(begin, &end);
    return end != begin && *end == '\0' && std::isfinite(value);
}

inline bool inRange(const std::string &field, const std::string &value, long min, long max, Error &error) {
    long parsed = 0;
    if (!parseInteger(value, parsed) || parsed < min || parsed > max) {
        error = {field, "must be an integer between " + std::to_string(min) + " and " + std::to_string(max)};
        return false;
    }
    return true;
}

inline bool numberInRange(const std::string &field, const std::string &value, double min, double max, Error &error) {
    double parsed = 0;
    if (!parseNumber(value, parsed) || parsed < min || parsed > max) {
        error = {field, "must be a finite number in range"};
        return false;
    }
    return true;
}

inline bool validSchedule(const std::string &value) {
    if (value.empty())
        return true;
    size_t start = 0;
    while (start < value.size()) {
        const size_t end = value.find(';', start);
        const std::string entry = value.substr(start, end == std::string::npos ? std::string::npos : end - start);
        if (entry.size() != 13 || entry[2] != ':' || entry[5] != '|')
            return false;
        if (entry[0] < '0' || entry[0] > '2' || entry[1] < '0' || entry[1] > '9' || entry[3] < '0' || entry[3] > '5' ||
            entry[4] < '0' || entry[4] > '9')
            return false;
        if ((entry[0] - '0') * 10 + entry[1] - '0' > 23)
            return false;
        for (size_t i = 6; i < entry.size(); ++i)
            if (entry[i] != '0' && entry[i] != '1')
                return false;
        if (end == std::string::npos)
            return true;
        start = end + 1;
    }
    return false;
}

inline bool requireFields(const Fields &fields, const std::vector<std::string> &required, Error &error) {
    for (const std::string &requiredName : required) {
        bool found = false;
        for (const auto &field : fields) {
            if (field.name == requiredName) {
                found = true;
                break;
            }
        }
        if (!found) {
            error = {requiredName, "is required"};
            return false;
        }
    }
    return true;
}

inline bool validateWebSocketRequest(const std::string &type, const Fields &fields, Error &error) {
    if (type == "req:change-grind-target" && !requireFields(fields, {"target"}, error))
        return false;
    if (type == "req:change-mode" && !requireFields(fields, {"mode"}, error))
        return false;
    if (type == "req:change-brew-target" && !requireFields(fields, {"target"}, error))
        return false;
    if (type == "req:dose:set" && !requireFields(fields, {"grams"}, error))
        return false;
    for (const auto &field : fields) {
        const std::string &name = field.name;
        const std::string &value = field.value;
        const bool numericField = (type == "req:change-grind-target" && name == "target") ||
                                  (type == "req:change-mode" && name == "mode") ||
                                  (type == "req:change-brew-target" && name == "target") ||
                                  (type == "req:dose:set" && name == "grams") ||
                                  (type == "req:manual:update" &&
                                   (name == "pressure" || name == "flow" || name == "temperature")) ||
                                  (type == "req:autotune-start" && (name == "time" || name == "samples"));
        if (numericField && !field.isNumeric) {
            error = {name, "must be a JSON number"};
            return false;
        }
        if (type == "req:manual:update" && name == "targetType" && field.isNumeric) {
            error = {name, "must be a JSON string"};
            return false;
        }
        if (type == "req:change-grind-target" && name == "target" && !inRange(name, value, 0, 1, error))
            return false;
        if (type == "req:change-mode" && name == "mode" && !inRange(name, value, 0, 5, error))
            return false;
        if (type == "req:change-brew-target" && name == "target" && !numberInRange(name, value, 0, 200, error))
            return false;
        if (type == "req:dose:set" && name == "grams" && !numberInRange(name, value, 0.000001, 200, error))
            return false;
        if (type == "req:manual:update" && name == "targetType" && value != "flow" && value != "pressure") {
            error = {name, "must be 'flow' or 'pressure'"};
            return false;
        }
        if (type == "req:manual:update" && name == "pressure" && !numberInRange(name, value, 0, 12, error))
            return false;
        if (type == "req:manual:update" && name == "flow" && !numberInRange(name, value, 0, 20, error))
            return false;
        if (type == "req:manual:update" && name == "temperature" && !inRange(name, value, 0, 150, error))
            return false;
        if (type == "req:autotune-start" && name == "time" && !inRange(name, value, 1, 3600, error))
            return false;
        if (type == "req:autotune-start" && name == "samples" && !inRange(name, value, 1, 1000, error))
            return false;
    }
    return true;
}

inline bool validateSettings(const Fields &fields, Error &error) {
    for (const auto &field : fields) {
        const std::string &name = field.name;
        const std::string &value = field.value;
        if (name == "startupMode") {
            if (value != "brew" && value != "standby") {
                error = {name, "must be 'brew' or 'standby'"};
                return false;
            }
        } else if (name == "targetSteamTemp") {
            if (!inRange(name, value, 0, 200, error))
                return false;
        } else if (name == "targetWaterTemp") {
            if (!inRange(name, value, 0, 150, error))
                return false;
        } else if (name == "temperatureOffset") {
            if (!inRange(name, value, -30, 30, error))
                return false;
        } else if (name == "pressureScaling") {
            if (!numberInRange(name, value, 0, 10, error))
                return false;
        } else if (name == "startupFillTime" || name == "steamFillTime" || name == "standbyTimeout" ||
                   name == "standbyBrightnessTimeout") {
            if (!inRange(name, value, 0, 86400, error))
                return false;
        } else if (name == "smartGrindMode" || name == "themeMode" || name == "altRelayFunction") {
            if (!inRange(name, value, 0, 2, error))
                return false;
        } else if (name == "haPort") {
            if (!inRange(name, value, 1, 65535, error))
                return false;
        } else if (name == "brewDelay" || name == "grindDelay") {
            if (!numberInRange(name, value, 0, 4000, error))
                return false;
        } else if (name == "mainBrightness" || name == "standbyBrightness" || name == "sunriseR" || name == "sunriseG" ||
                   name == "sunriseB" || name == "sunriseW" || name == "sunriseExtBrightness") {
            if (!inRange(name, value, 0, 255, error))
                return false;
        } else if (name == "steamPumpPercentage" || name == "steamPumpCutoff") {
            if (!numberInRange(name, value, 0, 100, error))
                return false;
        } else if (name == "emptyTankDistance" || name == "fullTankDistance") {
            if (!inRange(name, value, 0, 10000, error))
                return false;
        } else if (name == "flushDuration") {
            if (!inRange(name, value, 1, 60, error))
                return false;
        } else if (name == "autowakeupSchedules" && !validSchedule(value)) {
            error = {name, "must be HH:MM|[01]{7} entries separated by ';'"};
            return false;
        }
    }
    return true;
}
} // namespace strict_validation

#endif
