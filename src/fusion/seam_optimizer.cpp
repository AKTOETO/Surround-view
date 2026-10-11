#include "sv/seam_optimizer.hpp"
#include <algorithm>
#include <boost/graph/adjacency_list.hpp>
#include <boost/graph/push_relabel_max_flow.hpp>
#include <limits>
#include <queue>
#include <stdexcept>

namespace sv::seam
{
namespace
{
int64_t validate(const Problem &problem)
{
    if (problem.unary.size() > 262144 || problem.edges.size() > 524288)
    {
        throw std::invalid_argument("seam graph budget exceeded");
    }
    int64_t bound = 1;
    for (const auto &costs : problem.unary)
    {
        const auto maximum = *std::max_element(costs.begin(), costs.end());
        if (maximum < 0 || maximum > 1000000000 ||
            *std::min_element(costs.begin(), costs.end()) < -1)
        {
            throw std::invalid_argument("seam unary requires available nonnegative costs <= 1e9");
        }
        bound += maximum;
    }
    for (const auto &edge : problem.edges)
    {
        if (edge.first >= problem.unary.size() || edge.second >= problem.unary.size() ||
            edge.first == edge.second || edge.weight < 0 || edge.weight > 1000000000)
        {
            throw std::invalid_argument("invalid seam edge");
        }
        bound += edge.weight;
    }
    // Hard constraints use a graph-wide bound at each unavailable label.
    // Bound the sum of capacities, not only each individual int64 capacity:
    // push_relabel initially saturates all source arcs.
    const auto factor = 4 * static_cast<int64_t>(problem.unary.size()) + 16;
    if (bound > std::numeric_limits<int64_t>::max() / factor)
    {
        throw std::invalid_argument("seam total capacity budget exceeded");
    }
    return bound;
}

int64_t evaluate(const Problem &problem, const std::vector<unsigned> &labels)
{
    if (labels.size() != problem.unary.size())
    {
        throw std::invalid_argument("seam labels size mismatch");
    }
    int64_t total = 0;
    for (size_t n = 0; n < labels.size(); ++n)
    {
        if (labels[n] >= 4 || problem.unary[n][labels[n]] < 0)
        {
            throw std::invalid_argument("unavailable seam label");
        }
        total += problem.unary[n][labels[n]];
    }
    for (const auto &edge : problem.edges)
    {
        total += edge.weight * (labels[edge.first] != labels[edge.second]);
    }
    return total;
}

using Traits = boost::adjacency_list_traits<boost::vecS, boost::vecS, boost::directedS>;
using Graph = boost::adjacency_list<
    boost::vecS, boost::vecS, boost::directedS, boost::no_property,
    boost::property<
        boost::edge_capacity_t, int64_t,
        boost::property<boost::edge_residual_capacity_t, int64_t,
                        boost::property<boost::edge_reverse_t, Traits::edge_descriptor>>>>;

std::vector<unsigned> move(const Problem &problem, const std::vector<unsigned> &labels,
                           unsigned alpha, int64_t forbidden)
{
    const auto count = labels.size();
    Graph graph(count + 2);
    auto capacity = get(boost::edge_capacity, graph);
    auto reverse = get(boost::edge_reverse, graph);
    auto edge = [&](size_t u, size_t v, int64_t cost)
    {
        const auto e = add_edge(u, v, graph).first, r = add_edge(v, u, graph).first;
        capacity[e] = cost;
        capacity[r] = 0;
        reverse[e] = r;
        reverse[r] = e;
    };
    std::vector<int64_t> keep(count), change(count);
    for (size_t n = 0; n < count; ++n)
    {
        keep[n] = 2 * problem.unary[n][labels[n]];
        change[n] = 2 * (problem.unary[n][alpha] < 0 ? forbidden : problem.unary[n][alpha]);
    }
    for (const auto &link : problem.edges)
    {
        const auto p = link.first, q = link.second;
        const int64_t e00 = link.weight * (labels[p] != labels[q]);
        const int64_t e01 = link.weight * (labels[p] != alpha);
        const int64_t e10 = link.weight * (alpha != labels[q]);
        const int64_t cross = e01 + e10 - e00; // >= 0 for weighted Potts.
        change[p] += 2 * (e10 - e00) - cross;
        change[q] += 2 * (e01 - e00) - cross;
        edge(p, q, cross);
        edge(q, p, cross);
    }
    for (size_t n = 0; n < count; ++n)
    {
        const auto minimum = std::min(keep[n], change[n]);
        edge(count, n, change[n] - minimum);
        edge(n, count + 1, keep[n] - minimum);
    }
    boost::push_relabel_max_flow(graph, count, count + 1);
    const auto residual = get(boost::edge_residual_capacity, graph);
    std::vector<bool> reachable(count + 2, false);
    std::queue<size_t> queue;
    queue.push(count);
    reachable[count] = true;
    while (!queue.empty())
    {
        const auto u = queue.front();
        queue.pop();
        for (auto [it, end] = out_edges(u, graph); it != end; ++it)
        {
            const auto v = target(*it, graph);
            if (residual[*it] > 0 && !reachable[v])
            {
                reachable[v] = true;
                queue.push(v);
            }
        }
    }
    auto result = labels;
    for (size_t n = 0; n < count; ++n)
    {
        if (!reachable[n])
        {
            result[n] = alpha;
        }
    }
    return result;
}
} // namespace

int64_t energy(const Problem &problem, const std::vector<unsigned> &labels)
{
    validate(problem);
    return evaluate(problem, labels);
}

std::vector<unsigned> expand(const Problem &problem, const std::vector<unsigned> &labels,
                             unsigned alpha)
{
    const auto forbidden = validate(problem);
    evaluate(problem, labels);
    if (alpha >= 4)
    {
        throw std::invalid_argument("invalid expansion label");
    }
    return move(problem, labels, alpha, forbidden);
}

Result optimize(const Problem &problem, unsigned max_sweeps)
{
    const auto forbidden = validate(problem);
    if (!max_sweeps || max_sweeps > 32)
    {
        throw std::invalid_argument("seam sweep budget must be 1..32");
    }
    Result result;
    for (const auto &costs : problem.unary)
    {
        unsigned best = 0;
        int64_t minimum = std::numeric_limits<int64_t>::max();
        for (unsigned label = 0; label < 4; ++label)
        {
            if (costs[label] >= 0 && costs[label] < minimum)
            {
                best = label;
                minimum = costs[label];
            }
        }
        result.labels.push_back(best);
    }
    auto current = evaluate(problem, result.labels);
    result.accepted_energies.push_back(current);
    for (unsigned sweep = 0; sweep < max_sweeps; ++sweep)
    {
        bool improved = false;
        for (unsigned alpha = 0; alpha < 4; ++alpha)
        {
            auto candidate = move(problem, result.labels, alpha, forbidden);
            const auto next = evaluate(problem, candidate);
            if (next < current)
            {
                result.labels = std::move(candidate);
                current = next;
                result.accepted_energies.push_back(current);
                improved = true;
            }
        }
        ++result.sweeps;
        if (!improved)
        {
            result.converged = true;
            break;
        }
    }
    return result;
}
} // namespace sv::seam
