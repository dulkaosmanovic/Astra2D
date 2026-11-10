#include "astra2d/core/profiler.hpp"

#include "astra2d/core/assert.hpp"

#include <algorithm>
#include <chrono>
#include <stdexcept>

namespace astra2d {

namespace {

double steadySeconds()
{
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

} // namespace

Profiler::Profiler(Clock clock, double smoothing)
    : m_clock(clock != nullptr ? clock : steadySeconds)
    , m_smoothing(smoothing)
{
    if (!(smoothing > 0.0) || smoothing > 1.0) { // NaN fails the first test
        throw std::invalid_argument("Profiler smoothing must be in (0, 1]");
    }
}

Profiler& Profiler::global()
{
    static Profiler profiler;
    return profiler;
}

void Profiler::setEnabled(bool enabled)
{
    if (enabled && !m_enabled) {
        m_frames = 0;
        m_measured.assign(m_measured.size(), false);
    }
    m_enabled = enabled;
}

void Profiler::beginFrame()
{
    ASTRA_ASSERT(m_open.size() <= 1, "Profiler::beginFrame() while scope '{}' is open",
                 m_nodes[static_cast<std::size_t>(m_open.back().node)].name);
    m_open.clear();
    m_recording = m_enabled;
    if (!m_recording) {
        return;
    }
    if (m_nodes.empty()) {
        m_nodes.push_back({.name = "frame"});
        m_current.emplace_back();
        m_measured.push_back(false);
    }
    clearAccumulators();
    m_open.push_back({.node = 0, .start = m_clock()});
}

void Profiler::endFrame()
{
    if (!m_recording) {
        return;
    }
    ASTRA_ASSERT(m_open.size() == 1, "Profiler::endFrame() while scope '{}' is open",
                 m_nodes[static_cast<std::size_t>(m_open.back().node)].name);
    m_current[0] = {.calls = 1, .ms = 1000.0 * (m_clock() - m_open[0].start)};
    for (std::size_t i = 0; i < m_nodes.size(); ++i) {
        ProfileNode& node = m_nodes[i];
        node.calls = m_current[i].calls;
        node.ms = m_current[i].ms;
        if (m_measured[i]) {
            node.averageMs += m_smoothing * (node.ms - node.averageMs);
        } else {
            node.averageMs = node.ms; // first frame of this node: nothing to smooth with
            m_measured[i] = true;
        }
    }
    ++m_frames;
    m_open.clear();
    m_recording = false;
}

void Profiler::beginScope(std::string_view name)
{
    if (!m_recording) {
        return;
    }
    const int node = childNamed(m_open.back().node, name);
    m_open.push_back({.node = node, .start = m_clock()});
}

void Profiler::endScope()
{
    if (!m_recording) {
        return;
    }
    ASTRA_ASSERT(m_open.size() > 1, "Profiler::endScope() without an open scope");
    const OpenScope scope = m_open.back();
    m_open.pop_back();
    Accumulator& current = m_current[static_cast<std::size_t>(scope.node)];
    ++current.calls;
    current.ms += 1000.0 * (m_clock() - scope.start);
}

void Profiler::reset()
{
    ASTRA_ASSERT(m_open.size() <= 1, "Profiler::reset() while a scope is open");
    m_nodes.clear();
    m_current.clear();
    m_measured.clear();
    m_open.clear();
    m_recording = false;
    m_frames = 0;
}

int Profiler::childNamed(int parent, std::string_view name)
{
    for (const int child : m_nodes[static_cast<std::size_t>(parent)].children) {
        if (m_nodes[static_cast<std::size_t>(child)].name == name) {
            return child;
        }
    }
    const int child = static_cast<int>(m_nodes.size());
    const int depth = m_nodes[static_cast<std::size_t>(parent)].depth + 1;
    // push_back may move the nodes, so the parent is looked up again afterwards.
    m_nodes.push_back({.name = std::string(name), .parent = parent, .depth = depth});
    m_nodes[static_cast<std::size_t>(parent)].children.push_back(child);
    m_current.emplace_back();
    m_measured.push_back(false);
    return child;
}

void Profiler::clearAccumulators()
{
    std::ranges::fill(m_current, Accumulator{});
}

} // namespace astra2d
