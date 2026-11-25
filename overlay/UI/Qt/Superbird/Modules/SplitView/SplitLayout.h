/*
 * Copyright (c) 2026, Superbird contributors.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

namespace Superbird::Split {

// A tab, by an id the caller picks (Split View uses stable per-window tab keys).
using PaneId = int64_t;

enum class Orientation : uint8_t {
    // Panes side by side, left to right.
    Horizontal,
    // Panes stacked, top to bottom.
    Vertical,
};

inline constexpr size_t MAXIMUM_PANES = 4;
// No pane gets less than this share of its split.
inline constexpr double MINIMUM_RATIO = 0.1;

// The tiling of a Split View group: a tree whose leaves are panes and whose inner nodes split their area between their
// children along one orientation, by ratios that add up to 1. Pure data, unit-tested on its own; the Qt host mirrors it
// with nested QSplitters.
class SplitLayout {
public:
    struct Node {
        // A leaf has a pane; an inner node has two or more children.
        std::optional<PaneId> pane;
        Orientation orientation { Orientation::Horizontal };
        std::vector<std::unique_ptr<Node>> children;
        std::vector<double> ratios;

        bool is_leaf() const { return pane.has_value(); }
    };

    // Two panes side by side (or stacked). `first` gets focus.
    static std::optional<SplitLayout> create(PaneId first, PaneId second, Orientation = Orientation::Horizontal);
    // A 2x2 grid (or as much of it as there are panes, 2 to 4): rows of two.
    static std::optional<SplitLayout> grid(std::vector<PaneId> const& panes);

    SplitLayout(SplitLayout&&) = default;
    SplitLayout& operator=(SplitLayout&&) = default;

    Node const& root() const { return *m_root; }

    // Panes in reading order (depth first).
    std::vector<PaneId> panes() const;
    size_t pane_count() const;
    bool contains(PaneId) const;

    PaneId focused() const { return m_focused; }
    bool focus(PaneId);
    // The next pane in reading order (wrapping), for keyboard focus cycling.
    PaneId next_pane(int direction) const;

    // Splits the pane `beside` to add `pane` after it along `orientation` (joining `beside`'s split if that already runs
    // that way). Fails past MAXIMUM_PANES or for a pane already in the layout. The new pane gets focus.
    bool add_pane(PaneId pane, PaneId beside, Orientation);
    // Removes a pane; a split left with one child collapses into that child. The focus moves to the pane that took its
    // place. Returns false if the pane isn't here. Removing the second-to-last pane leaves a single pane, which callers
    // treat as "unsplit".
    bool remove_pane(PaneId);
    // Exchanges two panes' places (and keeps their ratios where they are).
    bool swap(PaneId, PaneId);
    // Replaces a pane's id (a tab was replaced by another standing in for it).
    bool replace_pane(PaneId old_pane, PaneId new_pane);
    // Flips the root split between side-by-side and stacked.
    void toggle_orientation();

    // Ratios of the split that directly contains this pane. Normalized to add up to 1 with each at least
    // MINIMUM_RATIO; fails if the count doesn't match.
    bool set_ratios_around(PaneId, std::vector<double> ratios);
    std::vector<double> ratios_around(PaneId) const;
    // Every split gets equal shares.
    void equalize();

private:
    SplitLayout() = default;
    Node* find_leaf(PaneId) const;
    Node* parent_of(Node const&) const;

    std::unique_ptr<Node> m_root;
    PaneId m_focused { 0 };
};

// Normalizes ratios to add up to 1, each at least MINIMUM_RATIO (when possible). Non-finite or negative entries count as
// zero; all-zero input becomes equal shares.
std::vector<double> normalize_ratios(std::vector<double>);

}
