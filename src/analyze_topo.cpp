#include "tla_builder.hpp"
#include <algorithm>
#include <cassert>
#include <format>
#include <ranges>
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

                // Initialize nexts.
                for (auto s : nodes) {
                    nexts[ident][s] = null;
                    nexts[s][ident] = null;
                }
                nexts[ident][ident] = ident;
            }
            break;
        }
        case TopologyAST::Link:
            // Collect links.
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
                        // Neighbors are naturally next hops.
                        nexts[*src][*dst] = *dst;
                        nexts[*dst][*src] = *src;
                    }
                }
            }
            break;
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

namespace std {
    template <>
    struct hash<pair<string, string>> {
        size_t operator()(const pair<string, string>& p) const {
            return hash<string>()(p.first) ^ hash<string>()(p.second);
        }
    };
}

void TLABuilder::completeNexts() {
    uset<pair<string, string>> visited;
    DEBUG("Completing routing tables...");
    for (auto src : nodes) {
        for (auto dst : nodes) {
            visited.insert({src, dst});
            findNext(src, dst, visited);
        }
    }
    DEBUG("Routing tables completed");
}

std::string TLABuilder::findNext(const string& src, const string& dst,
    uset<pair<string, string>>& visited) {
    // TODO: Guarantee correctness in theory.
    if (nexts[src][dst] != null) {
        return nexts[src][dst];
    }

    auto success_cnt = 0;
    auto res = null;
    // Enumerate all possible next hops.
    for (const auto& next : links[src]) {
        if (nexts[next][dst] != null) {
            ++success_cnt;
            res = next;
            continue;
        }
        if (visited.contains({next, dst})) {
            continue;
        }
        visited.insert({next, dst});
        auto t = findNext(next, dst, visited);
        if (t != null) {
            ++success_cnt;
            res = next;
        }
    }
    check(success_cnt > 0, format("{} and {} is not connected", src, dst));
    check(success_cnt == 1, format(
        "{} and {} is connected by multiple paths but not explicitly specified",
        src, dst
    ));
    nexts[src][dst] = res;
    return res;
}
