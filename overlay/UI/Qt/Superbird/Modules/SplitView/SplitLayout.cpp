/*
 * Copyright (c) 2026, Superbird contributors.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <UI/Qt/Superbird/Modules/SplitView/SplitLayout.h>

#include <algorithm>
#include <charconv>
#include <cmath>
#include <set>

namespace Superbird::Split {

namespace {

std::unique_ptr<SplitLayout::Node> make_leaf(PaneId pane)
{
    auto node = std::make_unique<SplitLayout::Node>();
    node->pane = pane;
    return node;
}

std::unique_ptr<SplitLayout::Node> make_split(Orientation orientation, std::vector<std::unique_ptr<SplitLayout::Node>> children)
{
    auto node = std::make_unique<SplitLayout::Node>();
    node->orientation = orientation;
    node->ratios.assign(children.size(), 1.0 / static_cast<double>(children.size()));
    node->children = std::move(children);
    return node;
}

void collect_panes(SplitLayout::Node const& node, std::vector<PaneId>& panes)
{
    if (node.is_leaf()) {
        panes.push_back(*node.pane);
        return;
    }
    for (auto const& child : node.children)
        collect_panes(*child, panes);
}

SplitLayout::Node* find_leaf_in(SplitLayout::Node& node, PaneId pane)
{
    if (node.is_leaf())
        return *node.pane == pane ? &node : nullptr;
    for (auto& child : node.children) {
        if (auto* found = find_leaf_in(*child, pane))
            return found;
    }
    return nullptr;
}

SplitLayout::Node* parent_in(SplitLayout::Node& node, SplitLayout::Node const& target)
{
    for (auto& child : node.children) {
        if (child.get() == &target)
            return &node;
        if (auto* found = parent_in(*child, target))
            return found;
    }
    return nullptr;
}

PaneId first_pane(SplitLayout::Node const& node)
{
    if (node.is_leaf())
        return *node.pane;
    return first_pane(*node.children.front());
}

void equalize_node(SplitLayout::Node& node)
{
    if (node.is_leaf())
        return;
    node.ratios.assign(node.children.size(), 1.0 / static_cast<double>(node.children.size()));
    for (auto& child : node.children)
        equalize_node(*child);
}

}

std::vector<double> normalize_ratios(std::vector<double> ratios)
{
    if (ratios.empty())
        return ratios;
    auto count = ratios.size();
    double sum = 0;
    for (auto& ratio : ratios) {
        if (!std::isfinite(ratio) || ratio < 0)
            ratio = 0;
        sum += ratio;
    }
    if (sum <= 0 || static_cast<double>(count) * MINIMUM_RATIO >= 1.0) {
        ratios.assign(count, 1.0 / static_cast<double>(count));
        return ratios;
    }
    for (auto& ratio : ratios)
        ratio /= sum;

    // Raise the too-small shares to the minimum, taking the difference from the others in proportion; repeat, since
    // that can push another share under the minimum.
    for (size_t round = 0; round < count; ++round) {
        double deficit = 0;
        double flexible = 0;
        for (auto ratio : ratios) {
            if (ratio < MINIMUM_RATIO)
                deficit += MINIMUM_RATIO - ratio;
            else
                flexible += ratio - MINIMUM_RATIO;
        }
        if (deficit <= 1e-12)
            break;
        for (auto& ratio : ratios) {
            if (ratio < MINIMUM_RATIO)
                ratio = MINIMUM_RATIO;
            else if (flexible > 0)
                ratio -= deficit * ((ratio - MINIMUM_RATIO) / flexible);
        }
    }
    return ratios;
}

std::optional<SplitLayout> SplitLayout::create(PaneId first, PaneId second, Orientation orientation)
{
    if (first == second)
        return {};
    SplitLayout layout;
    std::vector<std::unique_ptr<Node>> children;
    children.push_back(make_leaf(first));
    children.push_back(make_leaf(second));
    layout.m_root = make_split(orientation, std::move(children));
    layout.m_focused = first;
    return layout;
}

std::optional<SplitLayout> SplitLayout::grid(std::vector<PaneId> const& panes)
{
    if (panes.size() < 2 || panes.size() > MAXIMUM_PANES)
        return {};
    for (size_t i = 0; i < panes.size(); ++i) {
        for (size_t j = i + 1; j < panes.size(); ++j) {
            if (panes[i] == panes[j])
                return {};
        }
    }
    if (panes.size() == 2)
        return create(panes[0], panes[1]);

    SplitLayout layout;
    std::vector<std::unique_ptr<Node>> top_row;
    top_row.push_back(make_leaf(panes[0]));
    top_row.push_back(make_leaf(panes[1]));
    std::vector<std::unique_ptr<Node>> rows;
    rows.push_back(make_split(Orientation::Horizontal, std::move(top_row)));
    if (panes.size() == 3) {
        rows.push_back(make_leaf(panes[2]));
    } else {
        std::vector<std::unique_ptr<Node>> bottom_row;
        bottom_row.push_back(make_leaf(panes[2]));
        bottom_row.push_back(make_leaf(panes[3]));
        rows.push_back(make_split(Orientation::Horizontal, std::move(bottom_row)));
    }
    layout.m_root = make_split(Orientation::Vertical, std::move(rows));
    layout.m_focused = panes[0];
    return layout;
}

SplitLayout::Node* SplitLayout::find_leaf(PaneId pane) const
{
    return find_leaf_in(*m_root, pane);
}

SplitLayout::Node* SplitLayout::parent_of(Node const& node) const
{
    return parent_in(*m_root, node);
}

std::vector<PaneId> SplitLayout::panes() const
{
    std::vector<PaneId> result;
    collect_panes(*m_root, result);
    return result;
}

size_t SplitLayout::pane_count() const
{
    return panes().size();
}

bool SplitLayout::contains(PaneId pane) const
{
    return find_leaf(pane) != nullptr;
}

bool SplitLayout::focus(PaneId pane)
{
    if (!contains(pane))
        return false;
    m_focused = pane;
    return true;
}

PaneId SplitLayout::next_pane(int direction) const
{
    auto all = panes();
    auto it = std::find(all.begin(), all.end(), m_focused);
    auto index = static_cast<int64_t>(it == all.end() ? 0 : it - all.begin());
    auto count = static_cast<int64_t>(all.size());
    auto next = ((index + (direction >= 0 ? 1 : -1)) % count + count) % count;
    return all[static_cast<size_t>(next)];
}

bool SplitLayout::add_pane(PaneId pane, PaneId beside, Orientation orientation)
{
    if (contains(pane) || pane_count() >= MAXIMUM_PANES)
        return false;
    auto* leaf = find_leaf(beside);
    if (!leaf)
        return false;

    auto* parent = parent_of(*leaf);
    if (parent && parent->orientation == orientation) {
        auto it = std::find_if(parent->children.begin(), parent->children.end(), [&](auto const& child) { return child.get() == leaf; });
        auto index = static_cast<size_t>(it - parent->children.begin());
        auto share = parent->ratios[index] / 2;
        parent->ratios[index] = share;
        parent->children.insert(parent->children.begin() + static_cast<std::ptrdiff_t>(index + 1), make_leaf(pane));
        parent->ratios.insert(parent->ratios.begin() + static_cast<std::ptrdiff_t>(index + 1), share);
        parent->ratios = normalize_ratios(parent->ratios);
    } else {
        // The pane's leaf becomes a split of the old pane and the new one.
        auto old_pane = *leaf->pane;
        leaf->pane.reset();
        leaf->orientation = orientation;
        leaf->children.clear();
        leaf->children.push_back(make_leaf(old_pane));
        leaf->children.push_back(make_leaf(pane));
        leaf->ratios = { 0.5, 0.5 };
    }
    m_focused = pane;
    return true;
}

bool SplitLayout::remove_pane(PaneId pane)
{
    auto* leaf = find_leaf(pane);
    if (!leaf || leaf == m_root.get())
        return false;
    auto* parent = parent_of(*leaf);
    auto it = std::find_if(parent->children.begin(), parent->children.end(), [&](auto const& child) { return child.get() == leaf; });
    auto index = static_cast<size_t>(it - parent->children.begin());
    parent->children.erase(it);
    parent->ratios.erase(parent->ratios.begin() + static_cast<std::ptrdiff_t>(index));
    parent->ratios = normalize_ratios(parent->ratios);

    Node* took_place = nullptr;
    if (parent->children.size() == 1) {
        // A split of one is just that one.
        auto only = std::move(parent->children.front());
        parent->pane = only->pane;
        parent->orientation = only->orientation;
        parent->ratios = std::move(only->ratios);
        parent->children = std::move(only->children);
        took_place = parent;
    } else {
        took_place = parent->children[std::min(index, parent->children.size() - 1)].get();
    }

    if (m_focused == pane)
        m_focused = first_pane(*took_place);
    return true;
}

bool SplitLayout::swap(PaneId a, PaneId b)
{
    auto* first = find_leaf(a);
    auto* second = find_leaf(b);
    if (!first || !second || first == second)
        return false;
    std::swap(first->pane, second->pane);
    return true;
}

bool SplitLayout::replace_pane(PaneId old_pane, PaneId new_pane)
{
    auto* leaf = find_leaf(old_pane);
    if (!leaf || (old_pane != new_pane && contains(new_pane)))
        return false;
    leaf->pane = new_pane;
    if (m_focused == old_pane)
        m_focused = new_pane;
    return true;
}

void SplitLayout::toggle_orientation()
{
    if (m_root->is_leaf())
        return;
    m_root->orientation = m_root->orientation == Orientation::Horizontal ? Orientation::Vertical : Orientation::Horizontal;
}

bool SplitLayout::set_ratios_around(PaneId pane, std::vector<double> ratios)
{
    auto* leaf = find_leaf(pane);
    auto* parent = leaf ? parent_of(*leaf) : nullptr;
    if (!parent || ratios.size() != parent->children.size())
        return false;
    parent->ratios = normalize_ratios(std::move(ratios));
    return true;
}

std::vector<double> SplitLayout::ratios_around(PaneId pane) const
{
    auto* leaf = find_leaf(pane);
    auto* parent = leaf ? parent_of(*leaf) : nullptr;
    if (!parent)
        return { 1.0 };
    return parent->ratios;
}

void SplitLayout::equalize()
{
    equalize_node(*m_root);
}


namespace {

constexpr double RATIO_SCALE = 10000;

bool write_node(SplitLayout::Node const& node, std::function<std::optional<int64_t>(PaneId)> const& save_id, std::string& out)
{
    if (node.is_leaf()) {
        auto id = save_id(*node.pane);
        if (!id.has_value())
            return false;
        out += std::to_string(*id);
        return true;
    }
    out += node.orientation == Orientation::Horizontal ? "h(" : "v(";
    for (size_t index = 0; index < node.children.size(); ++index) {
        if (index > 0)
            out += ',';
        // NB: Ratios are written in ten-thousandths, as whole numbers, so the text doesn't depend on the locale's decimal point.
        auto ratio = index < node.ratios.size() ? node.ratios[index] : 0.0;
        out += std::to_string(std::llround(ratio * RATIO_SCALE));
        out += ':';
        if (!write_node(*node.children[index], save_id, out))
            return false;
    }
    out += ')';
    return true;
}

class Reader {
public:
    explicit Reader(std::string_view text)
        : m_text(text)
    {
    }

    bool at_end() const { return m_position >= m_text.size(); }

    bool consume(char c)
    {
        if (at_end() || m_text[m_position] != c)
            return false;
        ++m_position;
        return true;
    }

    std::optional<int64_t> read_integer()
    {
        int64_t value = 0;
        auto const* begin = m_text.data() + m_position;
        auto [end, error] = std::from_chars(begin, m_text.data() + m_text.size(), value);
        if (error != std::errc {} || end == begin)
            return {};
        m_position += static_cast<size_t>(end - begin);
        return value;
    }

    std::optional<double> read_ratio()
    {
        auto value = read_integer();
        if (!value.has_value() || *value < 0)
            return {};
        return static_cast<double>(*value) / RATIO_SCALE;
    }

    std::unique_ptr<SplitLayout::Node> read_node(int depth)
    {
        if (depth > 4 || at_end())
            return nullptr;
        auto kind = m_text[m_position];
        if (kind != 'h' && kind != 'v') {
            auto id = read_integer();
            if (!id.has_value())
                return nullptr;
            return make_leaf(*id);
        }
        ++m_position;
        if (!consume('('))
            return nullptr;
        std::vector<std::unique_ptr<SplitLayout::Node>> children;
        std::vector<double> ratios;
        do {
            auto ratio = read_ratio();
            if (!ratio.has_value() || !consume(':'))
                return nullptr;
            auto child = read_node(depth + 1);
            if (!child)
                return nullptr;
            ratios.push_back(*ratio);
            children.push_back(std::move(child));
            if (children.size() > MAXIMUM_PANES)
                return nullptr;
        } while (consume(','));
        if (!consume(')') || children.size() < 2)
            return nullptr;
        auto node = make_split(kind == 'h' ? Orientation::Horizontal : Orientation::Vertical, std::move(children));
        node->ratios = normalize_ratios(std::move(ratios));
        return node;
    }

private:
    std::string_view m_text;
    size_t m_position { 0 };
};

}

std::optional<std::string> SplitLayout::serialize(std::function<std::optional<int64_t>(PaneId)> const& save_id) const
{
    auto focused = save_id(m_focused);
    if (!focused.has_value())
        return {};
    std::string out = std::to_string(*focused);
    out += '|';
    if (!write_node(*m_root, save_id, out))
        return {};
    return out;
}

std::optional<SplitLayout> SplitLayout::deserialize(std::string_view text, std::function<std::optional<PaneId>(int64_t)> const& pane_for)
{
    Reader reader(text);
    auto saved_focus = reader.read_integer();
    if (!saved_focus.has_value() || !reader.consume('|'))
        return {};
    auto root = reader.read_node(0);
    if (!root || !reader.at_end() || root->is_leaf())
        return {};

    SplitLayout layout;
    layout.m_root = std::move(root);
    auto saved_ids = layout.panes();
    if (saved_ids.size() > MAXIMUM_PANES || std::set<PaneId>(saved_ids.begin(), saved_ids.end()).size() != saved_ids.size())
        return {};
    layout.m_focused = std::find(saved_ids.begin(), saved_ids.end(), *saved_focus) != saved_ids.end() ? *saved_focus : saved_ids.front();

    // NB: Saved ids and pane ids are both small numbers, so the panes first take ids no pane has (negative ones), and
    //     only then the panes found for them, so no replacement lands on a pane that's still to be replaced.
    std::optional<PaneId> focused_pane;
    for (size_t index = 0; index < saved_ids.size(); ++index)
        layout.replace_pane(saved_ids[index], -static_cast<PaneId>(index) - 1);
    std::set<PaneId> found;
    for (size_t index = 0; index < saved_ids.size(); ++index) {
        auto placeholder = -static_cast<PaneId>(index) - 1;
        auto pane = pane_for(saved_ids[index]);
        if (!pane.has_value() || *pane < 0 || found.contains(*pane)) {
            if (layout.pane_count() <= 2) {
                // Fewer than two panes would be left, which is no split.
                return {};
            }
            layout.remove_pane(placeholder);
            continue;
        }
        found.insert(*pane);
        layout.replace_pane(placeholder, *pane);
        if (saved_ids[index] == *saved_focus)
            focused_pane = *pane;
    }
    if (layout.pane_count() < 2)
        return {};
    layout.m_focused = focused_pane.value_or(first_pane(*layout.m_root));
    return layout;
}

}
