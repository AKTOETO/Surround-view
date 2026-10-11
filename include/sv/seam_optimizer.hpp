#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace sv::seam
{
// Integer weighted Potts energy. Negative unary costs mark unavailable labels.
struct Edge
{
    unsigned first, second;
    int64_t weight;
};

struct Problem
{
    std::vector<std::array<int64_t, 4>> unary;
    std::vector<Edge> edges;
};

struct Result
{
    std::vector<unsigned> labels;
    std::vector<int64_t> accepted_energies;
    unsigned sweeps = 0;
    bool converged = false;
};

struct Summary
{
    int64_t initial_energy = 0, final_energy = 0;
    size_t nodes = 0, edges = 0;
    unsigned sweeps = 0, accepted_moves = 0;
    bool converged = false;
};

int64_t energy(const Problem &, const std::vector<unsigned> &);
std::vector<unsigned> expand(const Problem &, const std::vector<unsigned> &, unsigned alpha);
Result optimize(const Problem &, unsigned max_sweeps = 8);
} // namespace sv::seam
