#pragma once

#include <functional>
#include <string>
#include <vector>

// Lightweight context menu: a list of (label, action) pairs.
// s_sub is used as a second instance for the playlist-picker sub-menu.
//
// Usage:
//   s_ctx.add("Label", [&]{ ... });
//   s_ctx.open(x, y);
//   // input loop: A → actions[idx](), B → close(), UP/DOWN → --/++ idx
//   // render: printContextMenu(s_ctx.labels, s_ctx.idx, s_ctx.x, s_ctx.y)
struct CtxMenu {
    std::vector<std::string> labels;
    std::vector<std::function<void()>> actions;
    size_t idx = 0;
    float x = 0, y = 0;
    bool active = false;

    // F is deduced so callers never write std::function<void()> explicitly.
    template <typename F> void add(std::string lbl, F fn) {
        labels.push_back(std::move(lbl));
        actions.emplace_back(std::move(fn));
    }

    void open(float ax, float ay) {
        idx = 0;
        x = ax;
        y = ay;
        active = true;
    }

    void close() {
        active = false;
        labels.clear();
        actions.clear();
        idx = 0;
    }
};
