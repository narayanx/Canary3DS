#pragma once

#include <cstddef>

#include "settings.h"

struct InfoState;

// Toggle switch setting
struct ToggleSetting {
    bool Settings::*value;
    size_t row;
    const char *key;                          // settings file key
    const char *label;                        // display label
    void (*onToggle)(InfoState &) = nullptr;  // optional side effect after flip
};

extern const ToggleSetting TOGGLE_SETTINGS[];
extern const size_t TOGGLE_SETTINGS_COUNT;

// Returns the descriptor for a given settings row, or nullptr if it is not toggle row
const ToggleSetting *findToggle(size_t row);
