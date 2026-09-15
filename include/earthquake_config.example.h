#pragma once

// Copy this file to earthquake_config.h and select up to eight prefectures.
// Values must use the official Japanese prefecture names shown below.
constexpr const char* EARTHQUAKE_TARGET_PREFECTURES[] = {
    "東京都",
    "神奈川県",
};

// Use only while validating the feature with P2PQuake's sandbox.
// Sandbox messages are always shown as tests.
constexpr bool EARTHQUAKE_USE_SANDBOX = false;

// Keep this false in normal use. Set it to true only during an attended
// sandbox test when alert sounds must also be verified.
constexpr bool EARTHQUAKE_ALLOW_SANDBOX_AUDIO = false;
