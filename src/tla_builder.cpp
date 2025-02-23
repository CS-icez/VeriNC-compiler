#include "tla_builder.hpp"
#include <algorithm>
#include <cctype>
#include <format>
#include <iostream>
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

void TLABuilder::check(bool cond, const string& msg) {
    if (!cond) {
        throw std::runtime_error(msg);
    }
}

void TLABuilder::addNewName(const string& name, bool is_user_defined) {
    check(
        !tla_reserved.contains(name) && !our_reserved.contains(name),
        format("Name {} is a reserved word", name)
    );
    if (is_user_defined) {
        check(
            !globalNames.contains(name),
            format("Name {} has been declared", name)
        );
        check(
            !name.starts_with("__") && !name.starts_with("WF_") && !name.starts_with("SF_"),
            format("Name {} starts with reserved prefix __, WF_, or SF_", name)
        );
    } else {
        check(
            !globalNames.contains(name),
            format("Name {} has been declared for special use", name)
        );
    }
    globalNames.insert(name);
}

auto TLABuilder::build() -> pair<string, string> {
    analyze(spec);
    std::cout << "Analysis completed" << std::endl;
    return {buildTLA(), buildCFG()};
}

void TLABuilder::analyze(SpecAST* spec) {
    vector<ProtocolAST*> protocols;

    for (auto section : *spec->sections) {
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
                    // `protocol` sections will be analyzed at last.
                    protocols.push_back(protocol);
                }
                break;
            case SectionAST::Property:
                for (auto property : *section->properties) {
                    analyze(property);
                }
                break;
        }
    }

    // Complete routing tables.
    completeNexts();

    // Add pre-defined symbols.
    //! It is dangerous to store pointers to container elements.
    strPool.reserve(100);
    vecStrPool.reserve(100);
    addConstants();
    addVariables();
    addFns();

    // Analyze protocol sections.
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
    addNewName(name);
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
                addNewName(*node);
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
    }
}

void TLABuilder::completeNexts() {
    uset<pair<string, string>> visited;
    std::cout << "Completing routing tables..." << std::endl;
    for (auto src : nodes) {
        for (auto dst : nodes) {
            findNext(src, dst, visited);
        }
    }
    std::cout << "Routing tables completed" << std::endl;
}

string TLABuilder::findNext(const string& src, const string& dst,
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
            res = nexts[next][dst];
            continue;
        }
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

void TLABuilder::addConstants() {
    // `null = null`
    strPool.emplace_back("null");
    configs.emplace_back(null, &strPool.back());

    // Auxiliary functions.
    auto to_upper = [](const string& s) {
        auto res = s;
        std::transform(s.begin(), s.end(), res.begin(),
            [](unsigned char c) { return std::toupper(c); });
        return res;
    };
    auto join = [](const uset<string>& v, const string& sep) {
        string res;
        bool is_first = true;
        for (const auto& s : v) {
            if (is_first) {
                is_first = false;
            } else {
                res += sep;
            }
            res += s;
        }
        return res;
    };
    auto first_eq = [](const string& s) {
        return [s](const pair<string, string*>& p) { return p.first == s; };
    };

    // Check that there is no duplicate if converting `nodetypes` to capitals.
    uset<string> capitals;
    for (const auto& type : nodetypes) {
        capitals.insert(to_upper(type));
    }
    check(
        capitals.size() == nodetypes.size(),
        "Node types that differ only in capitalization are not allowed"
    );

    string t = null;

    for (const auto& type : nodetypes) {
        // `TYPE_SET = {node1, node2, ...}`
        auto type_set = to_upper(type) + "_SET";
        addNewName(type_set, false);
        t = string("{") + join(type2nodes[type], ", ") + "}";
        strPool.push_back(t);
        configs.emplace_back(type_set, &strPool.back());

        // `TYPE_NUM = Cardinality(TYPE_SET)`
        auto type_num = to_upper(type) + "_NUM";
        addNewName(type_num, false);
        t = format("Cardinality({})", type_set);
        strPool.push_back(t);
        type2cvDecls[all].emplace_back(type_num, true, &strPool.back());
    }

    // `NODE_SET = {node1, node2, ...}`
    auto node_set = "NODE_SET";
    addNewName(node_set, false);
    t = string("{") + join(nodes, ", ") + "}";
    strPool.push_back(t);
    type2cvDecls[all].emplace_back(node_set, true, &strPool.back());

    // `MAX_LOSS = 0`
    auto max_loss = "MAX_LOSS";
    bool not_defined = (std::find_if(configs.begin(), configs.end(),
        first_eq(max_loss)) == configs.end());
    if (not_defined) {
        strPool.push_back("0");
        configs.emplace_back(max_loss, &strPool.back());
    }
    // `MAX_OUT_OF_ORDER = 0`
    auto max_out_of_order = "MAX_OUT_OF_ORDER";
    not_defined = (std::find_if(configs.begin(), configs.end(),
        first_eq(max_out_of_order)) == configs.end());
    if (not_defined) {
        strPool.push_back("0");
        configs.emplace_back(max_out_of_order, &strPool.back());
    }

    // TODO: symmetry.
}

void TLABuilder::addVariables() {
    // `__net_buf = [ip \in IP_SET |-> <<>>]`
    strPool.push_back(R"([ip \in IP_SET |-> <<>>])");
    type2cvDecls[all].emplace_back("__net_buf", false, &strPool.back());

    // `__max_loss = MAX_LOSS`
    strPool.push_back("MAX_LOSS");
    type2cvDecls[all].emplace_back("__max_loss", false, &strPool.back());

    // `__max_out_of_order = MAX_OUT_OF_ORDER`
    strPool.push_back("MAX_OUT_OF_ORDER");
    type2cvDecls[all].emplace_back("__max_out_of_order", false, &strPool.back());
}

void TLABuilder::addFns() {
    string* exp = nullptr;
    string* arg1 = nullptr;
    string* arg2 = nullptr;
    string* arg3 = nullptr;
    vector<string*>* args = nullptr;

    // `__MinPsnElem(S) == CHOOSE x \in S : \A y \in S : x.psn <= y.psn`
    strPool.push_back(R"(CHOOSE x \in S : \A y \in S : x.psn <= y.psn)");
    exp = &strPool.back();
    strPool.push_back("S");
    arg1 = &strPool.back();
    vecStrPool.push_back({arg1});
    args = &vecStrPool.back();
    fns.emplace_back("__MinPsnElem", args, exp);

    // ```
    // __Set2Seq(S) == LET
    //   RECURSIVE F(_, _)
    //   F(res_, S_) == IF S_ = {}
    //     THEN res_
    //     ELSE LET x == MinSeqElem(S_) IN F(Append(res_, x), S_ \ {x})
    // IN F(<<>>, S)
    // ```
    strPool.push_back(
        "LET\n"
        "  RECURSIVE F(_, _)\n"
        "  F(res_, S_) == IF S_ = {}\n"
        "    THEN res_\n"
        "    ELSE LET x == MinSeqElem(S_) IN F(Append(res_, x), S_ \\ {x})\n"
        "IN F(<<>>, S)"
    );
    exp = &strPool.back();
    strPool.push_back("S");
    arg1 = &strPool.back();
    vecStrPool.push_back({arg1});
    args = &vecStrPool.back();
    fns.emplace_back("__Set2Seq", args, exp);

    // `__OutOfOrderRange(seq) == 1..Len(seq)`
    strPool.push_back("1..Len(seq)");
    exp = &strPool.back();
    strPool.push_back("seq");
    arg1 = &strPool.back();
    vecStrPool.push_back({arg1});
    args = &vecStrPool.back();
    fns.emplace_back("__OutOfOrderRange", args, exp);

    // `__InsertAtEnd(seq, i, elem) == InsertAt(seq, Len(seq) - i + 1, elem)`
    strPool.push_back("InsertAt(seq, Len(seq) - i + 1, elem)");
    exp = &strPool.back();
    strPool.push_back("seq");
    arg1 = &strPool.back();
    strPool.push_back("i");
    arg2 = &strPool.back();
    strPool.push_back("elem");
    arg3 = &strPool.back();
    vecStrPool.push_back({arg1, arg2, arg3});
    args = &vecStrPool.back();
    fns.emplace_back("__InsertAtEnd", args, exp);
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
            addNewName(name);
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
    addNewName(name);

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
    addNewName(name);
    type2threads[type][name] = stmts;

    // TODO: implement.
}

void TLABuilder::analyze(PropertyAST* property) {
    // Collect properties.
    auto name = *property->ident;
    addNewName(name);

    auto exp = property->ctl->exp;
    check(
        exp->rule == ExpAST::TLA,
        format("RHS of property {} should not involve primitive calls", name)
    );

    auto tla = exp->tla;
    bool is_temporal = (tla->starts_with("[]") || tla->starts_with("<>"));
    if (is_temporal) {
        properties.emplace_back(name, tla);
    } else {
        invariants.emplace_back(name, tla);
    }
}

string TLABuilder::buildTLA() {
    // TODO: implement.
    return null;
}

string TLABuilder::buildCFG() {
    string res;
    res += "SPECIFICATION Spec\n";
    res += "CONSTANTS\n";
    for (const auto& [name, tla] : configs) {
        res += format("  {} = {}\n", name, *tla);
    }
    if (!invariants.empty()) {
        res += "INVARIANTS\n";
        for (const auto& [name, tla] : invariants) {
            res += format("  {}\n", name);
        }
    }
    if (!properties.empty()) {
        res += "PROPERTIES\n";
        for (const auto& [name, tla] : properties) {
            res += format("  {}\n", name);
        }
    }
    // TODO: symmetry.
    return res;
}
