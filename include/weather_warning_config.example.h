#pragma once

// Copy this file to weather_warning_config.h and select JMA forecast areas.
// The code and name at each index must describe the same area.
constexpr const char* WEATHER_WARNING_TARGET_CODES[] = {
    "130000",  // Tokyo
    "140000",  // Kanagawa
    "430000",  // Kumamoto
};

constexpr const char* WEATHER_WARNING_TARGET_NAMES[] = {
    "東京都",
    "神奈川県",
    "熊本県",
};

constexpr size_t WEATHER_WARNING_TARGET_COUNT =
    sizeof(WEATHER_WARNING_TARGET_CODES) /
    sizeof(WEATHER_WARNING_TARGET_CODES[0]);
