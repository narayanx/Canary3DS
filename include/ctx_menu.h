#pragma once

#include <functional>
#include <string>
#include <vector>

// Lightweight context menu: a list of (label, action) pairs.
// s_sub is used as a second instance for the playlist-picker sub-menu.
//
// Button shortcuts (always active when menu is open):
//   A  – executes actions[idx]  (cursor-following; hint label tracks idx)
//   X  – executes actions[1]    (fixed shortcut, shown when n >= 2)
//   Y  – executes actions[2]    (fixed shortcut, shown when n >= 3);
//        if n < 3, Y closes the menu and falls through to the view-switch.
//   B  – close / back
// D-pad UP/DOWN scrolls the cursor; scrollOffset tracks the visible window.
struct CtxMenu {
    std::vector<std::string> labels;
    std::vector<std::function<void()>> actions;
    size_t idx = 0;
    size_t scrollOffset = 0;
    float x = 0, y = 0;
    bool active = false;

    // F is deduced so callers never write std::function<void()> explicitly.
    template <typename F> void add(std::string lbl, F fn) {
        labels.push_back(std::move(lbl));
        actions.emplace_back(std::move(fn));
    }

    void open(float ax, float ay) {
        idx = 0;
        scrollOffset = 0;
        x = ax;
        y = ay;
        active = true;
    }

    void close() {
        active = false;
        labels.clear();
        actions.clear();
        idx = 0;
        scrollOffset = 0;
    }
};
