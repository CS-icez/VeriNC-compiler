#include "tla_builder.hpp"
#include <format>
#include <stdexcept>
using std::format;
using std::pair;
using std::string;
using std::vector;

namespace std {
    template <>
    struct hash<pair<string, string>> {
        size_t operator()(const pair<string, string>& p) const {
            return hash<string>()(p.first) ^ hash<string>()(p.second);
        }
    };
}

static void check(bool cond, const string& msg) {
    if (!cond) {
        throw std::runtime_error(msg);
    }
}

auto TLABuilder::build() -> pair<string, string> {
    analyze(spec);
    return {buildTLA(), buildCFG()};
}

void TLABuilder::analyze(SpecAST* spec) {
    for (auto section : *spec->sections) {
        analyze(section);
    }
}

void TLABuilder::analyze(SectionAST* section) {
    vector<ProtocolAST*> protocols;

    // `protocol` sections will be analyzed at last.
    switch (section->rule) {
        case SectionAST::Configuration:
            for (auto config : *section->configs) {
                analyze(config);
            }
            break;
        case SectionAST::Topology:
            for (auto topology : *section->topologies) {
                analyze(topology);
            }
            break;
        case SectionAST::Protocol:
            for (auto protocol : *section->protocols) {
                protocols.push_back(protocol);
            }
            break;
        case SectionAST::Property:
            for (auto property : *section->properties) {
                analyze(property);
            }
            break;
    }

    // Complete routing tables.
    completeNexts();

    // Analyze protocols.
    for (auto protocol : protocols) {
        analyze(protocol);
    }
}

void TLABuilder::analyze(ConfigAST* config) {
    // Collect configuration.
    auto assign = config->assign;
    auto name = *assign->ident;
    check(
        !globalNames.contains(name),
        format("Declare name {} twice in global scope", name));
    globalNames.insert(name);
    check(
        assign->keys == nullptr,
        format("LHS of configuration should be an identifier")
    );
    auto exp = assign->exp;
    check(
        exp->rule == ExpAST::TLA,
        format("RHS of configuration {} should not involve primitive calls", name)
    );
    configs.emplace_back(name, exp->tla);
}

void TLABuilder::analyze(TopologyAST* topology) {
    switch (topology->rule) {
        case TopologyAST::NodeType:
            // Collect node types.
            for (auto type : *topology->types) {
                // TODO: check not reserved words.
                nodetypes.insert(*type->ident);
            }
            break;
        case TopologyAST::Node: {
            // Collect nodes.
            auto type = *topology->type->ident;
            check(nodetypes.contains(type), format("Declare nodes of unknown node type {}", type));
            for (auto node : *topology->nodes) {
                check(!nodes.contains(*node), format("Declare node {} twice", *node));
                // TODO: check not reserved words.
                nodes.insert(*node);
                type2nodes[type].insert(*node);

                // Initialize nexts.
                for (auto s : nodes) {
                    nexts[*node][s] = null;
                    nexts[s][*node] = null;
                }
                nexts[*node][*node] = *node;
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
                        check(nodes.contains(*src), format("Declare a link with unknown node {}", *src));
                        check(nodes.contains(*dst), format("Declare a link with unknown node {}", *dst));
                        check(*src != *dst, format("Declare a self-link of {}", *src));
                        links[*src].insert(*dst);
                        links[*dst].insert(*src);
                    }
                }
            }
            break;
        case TopologyAST::Route:
            // Check the existence of sources.
            for (auto src : *topology->srcs) {
                check(nodes.contains(*src), format("Declare a route with unknown source {}", *src));
            }
            // Collect routes.
            for (auto entry : *topology->entries) {
                check(nodes.contains(*entry->next), format("Declare a route with unknown next-hop {}", *entry->next));
                for (auto dst : *entry->dsts) {
                    check(nodes.contains(*dst), format("Declare a route with unknown destination {}", *dst));
                    for (auto src : *topology->srcs) {
                        check(*src != *dst, format("Declare a route with the same source and destination {}", *src));
                        check(nexts[*src][*dst] == null, format("Declare the route from {} to {} twice", *src, *dst));
                        nexts[*src][*dst] = *entry->next;
                    }
                }
            }
            break;
    }
}

void TLABuilder::completeNexts() {
    uset<pair<string, string>> visited;
    for (auto src : nodes) {
        for (auto dst : nodes) {
            findNext(src, dst, visited);
        }
    }
}

string TLABuilder::findNext(const string& src, const string& dst,
    uset<pair<string, string>>& visited) {
    if (nexts[src][dst] != null) {
        return nexts[src][dst];
    }

    auto success_cnt = 0;
    auto res = null;
    // Enumerate all possible next hops.
    for (const auto& next : links[src]) {
        if (visited.contains({next, dst})) {
            continue;
        }
        visited.insert({next, dst});
        auto t = findNext(next, dst, visited);
        if (t != null) {
            ++success_cnt;
            res = t;
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

void TLABuilder::analyze(ProtocolAST* protocol) {
    switch (protocol->rule) {
        case ProtocolAST::Var:
            [[fallthrough]];
        case ProtocolAST::Const: {
            // Collect constants/variables.
            auto type = *protocol->type->ident;
            for (auto assign : *protocol->assigns) {
                analyzeCV(type, protocol->rule == ProtocolAST::Const, assign);
            }
            break;
        }
        case ProtocolAST::Fn: {
            // Collect function.
            auto name = *protocol->name;
            check(
                !globalNames.contains(name),
                format("Declare name {} twice in global scope", name)
            );
            auto exp = protocol->exp;
            check(
                exp->rule == ExpAST::TLA,
                format("RHS of function {} should not involve primitive calls", name)
            );
            fns.emplace_back(name, protocol->params, exp->tla);
            break;
        }
        case ProtocolAST::Thread:
            analyzeThread(*protocol->type->ident, *protocol->name, protocol->stmts);
            break;
    }
}

void TLABuilder::analyzeCV(const string& type, bool is_const, AssignAST* assign) {
    string cv = (is_const ? "constant" : "variable");
    bool is_global = (type == all);
    if (!is_global) {
        check(
            nodetypes.contains(type),
            format("Declare {} of unknown node type {}", cv, type)
        );
    }

    auto name = *assign->ident;
    if (is_global) {
        check(
            !type2vars[type].contains(name),
            format("Declare name {} twice in global scope", name)
        );
        globalNames.insert(name);
    }
    else {
        check(
            !type2consts[type].contains(name) && !type2vars[type].contains(name),
            format("Declare name {} twice for node type {}", name, type)
        );
        if (is_const) {
            type2consts[type].insert(name);
        } else {
            type2vars[type].insert(name);
        }
    }

    check(
        assign->keys == nullptr,
        format("LHS of {} declaration should be an identifier", cv)
    );
    auto exp = assign->exp;
    check(
        exp->rule == ExpAST::TLA,
        format("RHS of {} declaration {} should not involve primitive calls", cv, name)
    );
    type2cvDecls[type].emplace_back(name, is_const, exp->tla);
}

void TLABuilder::analyzeThread(const string& type, const string& name,
    vector<StmtAST*>* stmts) {
    check(
        nodetypes.contains(type),
        format("Declare thread of unknown node type {}", type)
    );
    check(
        !globalNames.contains(name),
        format("Declare name {} twice in global scope", name)
    );
    globalNames.insert(name);
    type2threads[type][name] = stmts;

    // TODO: implement.
}

void TLABuilder::analyze(PropertyAST* property) {
    // Collect properties.
    properties.push_back(property);
}

string TLABuilder::buildTLA() {
    // TODO: implement.
    return null;
}

string TLABuilder::buildCFG() {
    // TODO: implement.
    return null;
}
