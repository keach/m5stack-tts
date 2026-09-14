#pragma once

// Copy this file to earthquake_config.h and select up to eight prefectures.
// Values must use the official Japanese prefecture names shown below.
constexpr const char* EARTHQUAKE_TARGET_PREFECTURES[] = {
    "東京都",
    "神奈川県",
};

// Use only while validating the feature with P2PQuake's sandbox.
// Sandbox messages are always shown as tests and never play the warning tone.
constexpr bool EARTHQUAKE_USE_SANDBOX = false;
