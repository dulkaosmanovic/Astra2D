#pragma once

#include "astra2d/physics/geometry.hpp"

#include <cstdint>
#include <utility>
#include <vector>

/// @addtogroup physics
/// @{

namespace astra2d {

/// How the broad phase finds the pairs of shapes whose bounding boxes overlap.
enum class BroadPhaseType {
    /// Tests every pair: O(n²). The baseline for evaluation E3.
    BruteForce,
    /// Sorts the boxes along x and tests only boxes whose x intervals overlap: O(n log n + pairs).
    SortAndSweep,
    /// Puts every box into the cells of a uniform grid it covers and tests boxes that share a
    /// cell: about O(n) when the cell size suits the shapes.
    UniformGrid,
};

/// A bounding box handed to the broad phase.
struct BroadPhaseProxy {
    Aabb aabb;            ///< World bounding box.
    std::uint32_t id = 0; ///< Returned in pairs (the world uses the shape index).
    /// Pairs of two proxies that both cannot move (static or kinematic bodies) are never
    /// reported, nor tested.
    bool movable = true;
};

/// A pair of overlapping proxies, by id, with `first < second`.
using ProxyPair = std::pair<std::uint32_t, std::uint32_t>;

/// Finds overlapping bounding boxes. Each call is independent: proxies are passed in again every
/// step, so it holds only reusable buffers (and the sort order, which changes little between
/// steps). All three methods report the same pairs; only the order and the cost differ.
class BroadPhase {
public:
    /// A broad phase of @p type; @p cellSize is the grid cell size (0 = pick from the proxies).
    explicit BroadPhase(BroadPhaseType type = BroadPhaseType::UniformGrid, float cellSize = 0.0F);

    /// Changes the method.
    void setType(BroadPhaseType type);
    /// The method.
    [[nodiscard]] BroadPhaseType type() const { return m_type; }
    /// Changes the grid cell size (0 = pick from the proxies at every call).
    void setCellSize(float cellSize);
    /// The grid cell size used by the last call.
    [[nodiscard]] float cellSize() const { return m_usedCellSize; }

    /// Replaces @p pairs with the overlapping pairs of @p proxies, in no particular order (each
    /// once). Ids must be unique.
    void findPairs(const std::vector<BroadPhaseProxy>& proxies, std::vector<ProxyPair>& pairs);

    /// Bounding box tests done by the last findPairs() (the cost measured in evaluation E3).
    [[nodiscard]] std::int64_t pairTests() const { return m_pairTests; }

private:
    void bruteForce(const std::vector<BroadPhaseProxy>& proxies, std::vector<ProxyPair>& pairs);
    void sortAndSweep(const std::vector<BroadPhaseProxy>& proxies, std::vector<ProxyPair>& pairs);
    void uniformGrid(const std::vector<BroadPhaseProxy>& proxies, std::vector<ProxyPair>& pairs);
    void test(const BroadPhaseProxy& a, const BroadPhaseProxy& b, std::vector<ProxyPair>& pairs);
    [[nodiscard]] static float automaticCellSize(const std::vector<BroadPhaseProxy>& proxies);
    void fillCells(const std::vector<BroadPhaseProxy>& proxies);
    void groupCells();
    void testCells(const std::vector<BroadPhaseProxy>& proxies, std::vector<ProxyPair>& pairs);
    void testLargeProxies(const std::vector<BroadPhaseProxy>& proxies,
                          std::vector<ProxyPair>& pairs);

    struct CellEntry {
        std::uint64_t cell = 0;
        std::uint32_t proxy = 0;
    };

    BroadPhaseType m_type;
    float m_cellSize;
    float m_usedCellSize = 0.0F;
    std::int64_t m_pairTests = 0;
    std::vector<std::uint32_t> m_order; // sort and sweep: proxy indices sorted by min.x
    std::vector<CellEntry> m_cells;     // grid: (cell, proxy) for every cell a box covers
    std::vector<CellEntry> m_sorted;    // grid: the same, grouped by hash bucket
    std::vector<std::size_t> m_bucketStart;
    std::vector<std::size_t> m_bucketFill;
    std::vector<std::uint32_t> m_large; // grid: proxies that cover too many cells
    std::vector<std::pair<std::int32_t, std::int32_t>> m_firstCell; // grid: lowest cell per proxy
};

} // namespace astra2d

/// @}
