#include "tla_builder.hpp"
#include <algorithm>
#include <cassert>
#include <format>
#include <ranges>
#include <queue>
#include "debug.hpp"
using std::format;
using namespace std::string_literals;
namespace rg = std::ranges;

void TLABuilder::analyze(TopologyAST* topology) {
    switch (topology->rule) {
        case TopologyAST::NodeType:
            // Collect node types.
            for (auto type : *topology->types) {
                addNewName(*type->ident);
                nodetypes.insert(*type->ident);
            }
            break;
        case TopologyAST::Node: {
            // Collect nodes.
            auto type = *topology->type->ident;
            check(
                nodetypes.contains(type),
                format("Declare nodes of unknown node type {}", type)
            );
            for (auto node : *topology->nodes) {
                auto& ident = *node->ident;
                addNewName(ident);
                nodes.insert(ident);
                nodes_in_order.push_back(ident);
                type2nodes[type].push_back(ident);

                auto exp = node->exp;
                if (exp != nullptr) {
                    check(
                        exp->rule == ExpAST::TLA,
                        format("The numeric value of node {} should not involve primitive calls", ident)
                    );
                    configs.emplace_back(ident, exp->tla);
                } else {
                    configs.emplace_back(ident, ident);
                }

                // Self-link is reliable.
                reliable_links.insert({ident, ident});
                // Initialize nexts.
                for (auto s : nodes) {
                    nexts[ident][s] = null;
                    nexts[s][ident] = null;
                }
                nexts[ident][ident] = ident;
            }
            break;
        }
        case TopologyAST::Link: {
            // Collect links.
            bool is_reliable = topology->is_reliable;
            for (size_t i = 0; i + 1 < topology->vec_nodes->size(); ++i) {
                auto srcs = (*topology->vec_nodes)[i];
                auto dsts = (*topology->vec_nodes)[i + 1];
                for (auto src : *srcs) {
                    for (auto dst : *dsts) {
                        check(
                            nodes.contains(*src),
                            format("Declare a link with unknown node {}", *src)
                        );
                        check(
                            nodes.contains(*dst),
                            format("Declare a link with unknown node {}", *dst)
                        );
                        check(*src != *dst, format("Declare a self-link of {}", *src));
                        links[*src].insert(*dst);
                        links[*dst].insert(*src);
                        if (is_reliable) {
                            reliable_links.insert({*src, *dst});
                            reliable_links.insert({*dst, *src});
                        }
                        // Neighbors are naturally next hops.
                        nexts[*src][*dst] = *dst;
                        nexts[*dst][*src] = *src;
                    }
                }
            }
            break;
        }
        case TopologyAST::Route:
            // Check the existence of sources.
            for (auto src : *topology->srcs) {
                check(
                    nodes.contains(*src),
                    format("Declare a route with unknown source {}", *src)
                );
            }
            // Collect routes.
            for (auto entry : *topology->entries) {
                check(
                    nodes.contains(*entry->next),
                    format("Declare a route with unknown next-hop {}", *entry->next)
                );
                for (auto dst : *entry->dsts) {
                    check(
                        nodes.contains(*dst),
                        format("Declare a route with unknown destination {}", *dst)
                    );
                    for (auto src : *topology->srcs) {
                        check(
                            *src != *dst,
                            format("Declare a route with the same source and destination {}", *src)
                        );
                        check(
                            !links[*src].contains(*dst),
                            format("Declare a route from {} to {} but they are directly connected", *src, *dst)
                        );
                        check(
                            nexts[*src][*dst] == null,
                            format("Declare the route from {} to {} twice", *src, *dst)
                        );
                        nexts[*src][*dst] = *entry->next;
                    }
                }
            }
            break;
        default:
            assert(false && "Internal error: unknown topology type");
    }
}

// TODO: ensure routing calculation is strictly correct.

void TLABuilder::completeNexts() {
    DEBUG("Completing routing tables...");
    bool complete = rg::all_of(nodes, [this](const auto& src) {
        return rg::all_of(nodes, [this, &src](const auto& dst) {
            return nexts[src][dst] != null;
        });
    });
    if (complete) {
        DEBUG("Routing table is already complete, skipping inference");
    } else {
        check(
            !topoHasCircle(),
            "The topology contains circles and the routing table is incomplete. "
            "Please provide a complete routing table via `route` declarations"
        );
        for (auto src : nodes) {
            for (auto next : links[src]) {
                fillNexts(src, next);
            }
        }
    }
    DEBUG("Routing tables completed");
}

bool TLABuilder::topoHasCircle() {
    uset<string> visited;
    auto start = *nodes.begin();
    std::queue<pair<string, string>> q; // (current, parent)
    visited.insert(start);
    q.push({start, null});
    while (!q.empty()) {
        auto [curr, parent] = q.front();
        q.pop();
        for (const auto& neighbor : links[curr]) {
            if (neighbor == parent) {
                continue;
            }
            if (visited.contains(neighbor)) {
                return true;
            }
            visited.insert(neighbor);
            q.push({neighbor, curr});
        }
    }
    check(
        visited.size() == nodes.size(),
        "The topology is disconnected, which is not supported for now"
    );
    return false;
}

void TLABuilder::fillNexts(const string& src, const string& next) {
    std::queue<pair<string, string>> q; // (current, parent)
    q.push({next, src});
    while (!q.empty()) {
        auto curr = q.front();
        q.pop();
        if (nexts[src][curr.first] == null) {
            nexts[src][curr.first] = next;
        }
        check(
            nexts[src][curr.first] == next,
            format("The route declared from {} to {} conflicts with the topology", src, curr.first)
        );
        for (const auto& neighbor : links[curr.first]) {
            if (neighbor != curr.second) {
                q.push({neighbor, curr.first});
            }
        }
    }
}
