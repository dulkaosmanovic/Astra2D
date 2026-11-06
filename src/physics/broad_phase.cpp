#include "astra2d/physics/broad_phase.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>

namespace astra2d {

namespace {

// A box covering more cells than this is tested against every other box instead (e.g. a long
// ground), so it does not fill thousands of cells.
constexpr std::int64_t kMaxCellsPerProxy = 256;

std::int32_t cellCoordinate(float value, float inverseCellSize)
{
    const float cell = std::floor(value * inverseCellSize);
    constexpr float kLimit = static_cast<float>(std::numeric_limits<std::int32_t>::max()) / 2.0F;
    return static_cast<std::int32_t>(std::clamp(cell, -kLimit, kLimit));
}

std::uint64_t cellKey(std::int32_t x, std::int32_t y)
{
    return (static_cast<std::uint64_t>(static_cast<std::uint32_t>(x)) << 32U) |
           static_cast<std::uint32_t>(y);
}

} // namespace

BroadPhase::BroadPhase(BroadPhaseType type, float cellSize)
    : m_type(type)
    , m_cellSize(cellSize)
{
}

void BroadPhase::setType(BroadPhaseType type)
{
    m_type = type;
}

void BroadPhase::setCellSize(float cellSize)
{
    m_cellSize = cellSize;
}

void BroadPhase::findPairs(const std::vector<BroadPhaseProxy>& proxies,
                           std::vector<ProxyPair>& pairs)
{
    pairs.clear();
    m_pairTests = 0;
    switch (m_type) {
    case BroadPhaseType::BruteForce:
        bruteForce(proxies, pairs);
        break;
    case BroadPhaseType::SortAndSweep:
        sortAndSweep(proxies, pairs);
        break;
    case BroadPhaseType::UniformGrid:
        uniformGrid(proxies, pairs);
        break;
    }
}

void BroadPhase::test(const BroadPhaseProxy& a, const BroadPhaseProxy& b,
                      std::vector<ProxyPair>& pairs)
{
    ++m_pairTests;
    if (a.aabb.overlaps(b.aabb)) {
        pairs.emplace_back(std::min(a.id, b.id), std::max(a.id, b.id));
    }
}

void BroadPhase::bruteForce(const std::vector<BroadPhaseProxy>& proxies,
                            std::vector<ProxyPair>& pairs)
{
    for (std::size_t i = 0; i < proxies.size(); ++i) {
        for (std::size_t j = i + 1; j < proxies.size(); ++j) {
            if (proxies[i].movable || proxies[j].movable) {
                test(proxies[i], proxies[j], pairs);
            }
        }
    }
}

void BroadPhase::sortAndSweep(const std::vector<BroadPhaseProxy>& proxies,
                              std::vector<ProxyPair>& pairs)
{
    // Boxes sorted by their left edge; a box can only overlap the boxes after it that start
    // before it ends. The order from the last step is kept: it changes little, which suits the
    // sort.
    if (m_order.size() != proxies.size()) {
        m_order.resize(proxies.size());
        std::iota(m_order.begin(), m_order.end(), 0U);
    }
    std::ranges::sort(m_order, [&](std::uint32_t a, std::uint32_t b) {
        return proxies[a].aabb.min.x < proxies[b].aabb.min.x;
    });
    for (std::size_t i = 0; i < m_order.size(); ++i) {
        const BroadPhaseProxy& a = proxies[m_order[i]];
        for (std::size_t j = i + 1; j < m_order.size(); ++j) {
            const BroadPhaseProxy& b = proxies[m_order[j]];
            if (b.aabb.min.x > a.aabb.max.x) {
                break;
            }
            if (a.movable || b.movable) {
                test(a, b, pairs);
            }
        }
    }
}

void BroadPhase::uniformGrid(const std::vector<BroadPhaseProxy>& proxies,
                             std::vector<ProxyPair>& pairs)
{
    m_usedCellSize = m_cellSize > 0.0F ? m_cellSize : automaticCellSize(proxies);
    fillCells(proxies);
    groupCells();
    testCells(proxies, pairs);
    testLargeProxies(proxies, pairs);
}

float BroadPhase::automaticCellSize(const std::vector<BroadPhaseProxy>& proxies)
{
    // About twice the typical size of a moving box: each covers 1 to 4 cells.
    double total = 0.0;
    int count = 0;
    for (const BroadPhaseProxy& proxy : proxies) {
        if (proxy.movable) {
            const glm::vec2 size = proxy.aabb.max - proxy.aabb.min;
            total += std::max(size.x, size.y);
            ++count;
        }
    }
    const float size = count > 0 ? static_cast<float>(2.0 * total / count) : 1.0F;
    return size > 0.0F && std::isfinite(size) ? size : 1.0F;
}

void BroadPhase::fillCells(const std::vector<BroadPhaseProxy>& proxies)
{
    const float inverse = 1.0F / m_usedCellSize;
    m_cells.clear();
    m_large.clear();
    m_firstCell.resize(proxies.size());
    for (std::size_t i = 0; i < proxies.size(); ++i) {
        const Aabb& box = proxies[i].aabb;
        const std::int32_t x0 = cellCoordinate(box.min.x, inverse);
        const std::int32_t y0 = cellCoordinate(box.min.y, inverse);
        const std::int32_t x1 = cellCoordinate(box.max.x, inverse);
        const std::int32_t y1 = cellCoordinate(box.max.y, inverse);
        m_firstCell[i] = {x0, y0};
        const std::int64_t cells =
            (static_cast<std::int64_t>(x1) - x0 + 1) * (static_cast<std::int64_t>(y1) - y0 + 1);
        if (cells > kMaxCellsPerProxy) {
            m_large.push_back(static_cast<std::uint32_t>(i));
            continue;
        }
        for (std::int32_t x = x0; x <= x1; ++x) {
            for (std::int32_t y = y0; y <= y1; ++y) {
                m_cells.push_back({.cell = cellKey(x, y), .proxy = static_cast<std::uint32_t>(i)});
            }
        }
    }
}

void BroadPhase::groupCells()
{
    // Group the entries by cell without a comparison sort: counting sort into hash buckets, O(n).
    // A bucket can hold several cells (hash collisions); entries are compared by cell later.
    std::size_t buckets = 16;
    while (buckets < 2 * m_cells.size()) {
        buckets *= 2;
    }
    const auto bucketOf = [&](std::uint64_t cell) {
        return static_cast<std::size_t>((cell * 0x9E3779B97F4A7C15ULL) >> 32U) & (buckets - 1);
    };
    m_bucketStart.assign(buckets + 1, 0);
    for (const CellEntry& entry : m_cells) {
        ++m_bucketStart[bucketOf(entry.cell) + 1];
    }
    for (std::size_t b = 0; b < buckets; ++b) {
        m_bucketStart[b + 1] += m_bucketStart[b];
    }
    m_sorted.resize(m_cells.size());
    m_bucketFill.assign(m_bucketStart.begin(), m_bucketStart.end() - 1);
    for (const CellEntry& entry : m_cells) {
        m_sorted[m_bucketFill[bucketOf(entry.cell)]++] = entry;
    }
}

void BroadPhase::testCells(const std::vector<BroadPhaseProxy>& proxies,
                           std::vector<ProxyPair>& pairs)
{
    // Boxes in the same cell are tested once: in the first cell they share (the one at the lower
    // left of the overlap of their cell ranges), not again in the others.
    const auto firstSharedCell = [&](std::uint32_t a, std::uint32_t b) {
        return cellKey(std::max(m_firstCell[a].first, m_firstCell[b].first),
                       std::max(m_firstCell[a].second, m_firstCell[b].second));
    };
    for (std::size_t bucket = 0; bucket + 1 < m_bucketStart.size(); ++bucket) {
        const std::size_t end = m_bucketStart[bucket + 1];
        for (std::size_t i = m_bucketStart[bucket]; i < end; ++i) {
            const CellEntry& first = m_sorted[i];
            for (std::size_t j = i + 1; j < end; ++j) {
                const CellEntry& second = m_sorted[j];
                // Another cell in the same bucket, two unmovable boxes, or not their first cell.
                if (second.cell != first.cell ||
                    (!proxies[first.proxy].movable && !proxies[second.proxy].movable) ||
                    firstSharedCell(first.proxy, second.proxy) != first.cell) {
                    continue;
                }
                test(proxies[first.proxy], proxies[second.proxy], pairs);
            }
        }
    }
}

void BroadPhase::testLargeProxies(const std::vector<BroadPhaseProxy>& proxies,
                                  std::vector<ProxyPair>& pairs)
{
    for (const std::uint32_t index : m_large) {
        const BroadPhaseProxy& large = proxies[index];
        for (std::uint32_t j = 0; j < proxies.size(); ++j) {
            // Pairs of two large boxes once, from the one with the lower index.
            if (j == index || (j < index && std::ranges::binary_search(m_large, j))) {
                continue;
            }
            if (large.movable || proxies[j].movable) {
                test(large, proxies[j], pairs);
            }
        }
    }
}

} // namespace astra2d
