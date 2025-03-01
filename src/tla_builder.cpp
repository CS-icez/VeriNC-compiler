#include "tla_builder.hpp"
#include <algorithm>
#include <cassert>
#include <cctype>
#include <format>
#include <iostream>
#include <ranges>
#include <regex>
#include <stdexcept>
#include "make_ast.hpp"
#include "utils.hpp"
using std::cout;
using std::endl;
using std::format;
using std::pair;
using std::string;
using std::vector;
using namespace std::string_literals;
namespace rg = std::ranges;

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
            !names.contains(name),
            format("Name {} has been declared", name)
        );
        check(
            !name.starts_with("__") && !name.starts_with("WF_") && !name.starts_with("SF_"),
            format("Name {} starts with reserved prefix __, WF_, or SF_", name)
        );
    } else {
        check(
            !names.contains(name) || name.find('$') != string::npos,
            format("Name {} has been declared for special use", name)
        );
    }
    names.insert(name);
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
            default:
                assert(false && "Internal error: unknown section type");
        }
    }

    // Complete routing tables.
    completeNexts();

    check(
        !nodetypes.empty(),
        "No node type is declared"
    );
    for (const auto& type : nodetypes) {
        check(
            !type2nodes.empty(),
            format("No node declared for node type {}", type)
        );
    }

    // Add pre-defined symbols.
    //! It is dangerous to store pointers to container elements.
    strPool.reserve(200);
    vecStrPool.reserve(100);
    addOurConstants();
    addOurVariables();
    addOurFns();

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
        !names.contains(name),
        format("Declare name {} twice in global scope", name));
    addNewName(name);
    names.insert(name);
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
                nodes_in_order.push_back(*node);
                type2nodes[type].push_back(*node);

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
        default:
            assert(false && "Internal error: unknown topology type");
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

string TLABuilder::toUpper(const string& str) {
    auto res = str;
    rg::transform(str, res.begin(),
        [](unsigned char c) { return std::toupper(c); });
    return res;
}

void TLABuilder::addOurConstants() {
    // `null = null`
    strPool.emplace_back("null");
    configs.emplace_back(null, &strPool.back());

    // Auxiliary functions.
    auto first_eq = [](const string& s) {
        return [s](const pair<string, string*>& p) { return p.first == s; };
    };

    // Check that there is no duplicate if converting `nodetypes` to capitals.
    uset<string> capitals;
    for (const auto& type : nodetypes) {
        capitals.insert(toUpper(type));
    }
    check(
        capitals.size() == nodetypes.size(),
        "Node types that differ only in capitalization are not allowed"
    );

    string t = null;

    for (const auto& type : nodetypes) {
        // `TYPE_SET = {node1, node2, ...}`
        auto type_set = toUpper(type) + "_SET";
        addNewName(type_set, false);
        t = "{"s + join(type2nodes[type], ", ") + "}";
        strPool.push_back(t);
        configs.emplace_back(type_set, &strPool.back());

        // `TYPE_NUM = Cardinality(TYPE_SET)`
        auto type_num = toUpper(type) + "_NUM";
        addNewName(type_num, false);
        t = format("Cardinality({})", type_set);
        strPool.push_back(t);
        type2constDecls[all].emplace_back(type_num, &strPool.back());
    }

    // `NODE_SET = {node1, node2, ...}`
    auto node_set = "NODE_SET";
    addNewName(node_set, false);
    t = string("{") + join(nodes_in_order, ", ") + "}";
    strPool.push_back(t);
    type2constDecls[all].emplace_back(node_set, &strPool.back());

    // `MAX_LOSS = 0`
    auto max_loss = "MAX_LOSS";
    bool not_defined = (std::find_if(configs.begin(), configs.end(),
        first_eq(max_loss)) == configs.end());
    if (not_defined) {
        addNewName(max_loss, false);
        strPool.push_back("0");
        configs.emplace_back(max_loss, &strPool.back());
    }
    // `MAX_OUT_OF_ORDER = 0`
    auto max_out_of_order = "MAX_OUT_OF_ORDER";
    not_defined = (std::find_if(configs.begin(), configs.end(),
        first_eq(max_out_of_order)) == configs.end());
    if (not_defined) {
        addNewName(max_out_of_order, false);
        strPool.push_back("0");
        configs.emplace_back(max_out_of_order, &strPool.back());
    }

    // `__links = src1 :> {dst1, ...} @@ ...`
    vector<string> link_entries;
    for (const auto& src : nodes_in_order) {
        link_entries.push_back(format("{} :> {{{}}}", src, join(links[src], ", ")));
    }
    strPool.push_back(join(link_entries, " @@ "));
    type2constDecls[all].emplace_back("__links", &strPool.back());

    // `__next_hop = <<src1, dst1>> :> next1 @@ ...`
    vector<string> route_entries;
    for (const auto& src : nodes_in_order) {
        for (const auto& dst : nodes_in_order) {
            route_entries.push_back(format("<<{}, {}>> :> {}", src, dst, nexts[src][dst]));
        }
    }
    strPool.push_back(join(route_entries, " @@ "));
    type2constDecls[all].emplace_back("__next_hop", &strPool.back());

    // TODO: symmetry.
}

void TLABuilder::addOurVariables() {
    // `__net_buf = [__n \in NODE_SET |-> <<>>]`
    strPool.push_back(R"!!([__n \in NODE_SET |-> <<>>])!!");
    type2varDecls[all].emplace_back("__net_buf", &strPool.back());

    // `__max_loss = MAX_LOSS`
    strPool.push_back("MAX_LOSS");
    type2varDecls[all].emplace_back("__max_loss", &strPool.back());

    // `__max_out_of_order = MAX_OUT_OF_ORDER`
    strPool.push_back("MAX_OUT_OF_ORDER");
    type2varDecls[all].emplace_back("__max_out_of_order", &strPool.back());

    // '__node_terminated = [__n \in NODE_SET |-> FALSE])'
    strPool.push_back(R"!!([__n \in NODE_SET |-> FALSE])!!");
    type2varDecls[all].emplace_back("__node_terminated", &strPool.back());
}

void TLABuilder::addOurFns() {
    string* exp = nullptr;
    string* arg1 = nullptr;
    string* arg2 = nullptr;
    string* arg3 = nullptr;
    vector<string*>* args = nullptr;

    auto add_indent = [](const string& s) {
        const string ident = "      ";
        return "\n" + ident + std::regex_replace(s, std::regex("\n"), "\n" + ident);
    };

    // `__MinPsnElem(S) == CHOOSE x \in S : \A y \in S : x.psn <= y.psn`
    strPool.push_back(R"!!(CHOOSE __x \in __S : \A __y \in __S : __x.psn <= __y.psn)!!");
    exp = &strPool.back();
    strPool.push_back("__S");
    arg1 = &strPool.back();
    vecStrPool.push_back({arg1});
    args = &vecStrPool.back();
    fns.emplace_back("__MinPsnElem", args, exp);

    // ```
    // __Set2OrderedSeq(S) ==
    //   LET
    //     RECURSIVE F(_, _)
    //     F(res, SS) == IF SS = {}
    //       THEN res
    //       ELSE LET x == MinPsnElem(SS) IN F(Append(res, x), S \ {x})
    //   IN F(<<>>, S)
    // ```
    strPool.push_back(add_indent(
        "LET\n"
        "  RECURSIVE F(_, _)\n"
        "  F(__res, __SS) == IF __SS = {}\n"
        "    THEN __res\n"
        "    ELSE LET __x == MinPsnElem(__SS) IN F(Append(__res, __x), __SS \\ {__x})\n"
        "IN F(<<>>, __S)"
    ));
    exp = &strPool.back();
    strPool.push_back("__S");
    arg1 = &strPool.back();
    vecStrPool.push_back({arg1});
    args = &vecStrPool.back();
    fns.emplace_back("__Set2OrderedSeq", args, exp);

    // `__OutOfOrderRange(seq) == 1..Len(seq)`
    strPool.push_back("1..Len(__seq)");
    exp = &strPool.back();
    strPool.push_back("__seq");
    arg1 = &strPool.back();
    vecStrPool.push_back({arg1});
    args = &vecStrPool.back();
    fns.emplace_back("__OutOfOrderRange", args, exp);

    // `__InsertAtEnd(seq, i, elem) == InsertAt(seq, Len(seq) - i + 1, elem)`
    strPool.push_back("InsertAt(__seq, Len(__seq) - __i + 1, __elem)");
    exp = &strPool.back();
    strPool.push_back("__seq");
    arg1 = &strPool.back();
    strPool.push_back("__i");
    arg2 = &strPool.back();
    strPool.push_back("__elem");
    arg3 = &strPool.back();
    vecStrPool.push_back({arg1, arg2, arg3});
    args = &vecStrPool.back();
    fns.emplace_back("__InsertAtEnd", args, exp);

    // `__Receive(n) == Head(__net_buf[n])`
    strPool.push_back("Head(__net_buf[__n])");
    exp = &strPool.back();
    strPool.push_back("__n");
    arg1 = &strPool.back();
    vecStrPool.push_back({arg1});
    args = &vecStrPool.back();
    fns.emplace_back("__Receive", args, exp);

    // `__Node(__thread) == Head(__thread)`
    strPool.push_back("Head(__thread)");
    exp = &strPool.back();
    strPool.push_back("__thread");
    arg1 = &strPool.back();
    vecStrPool.push_back({arg1});
    args = &vecStrPool.back();
    fns.emplace_back("__Node", args, exp);

    // `__PosCount(f) == Cardinality({x \in DOMAIN f : f[x] > 0})`
    strPool.push_back("Cardinality({__x \\in DOMAIN __f : __f[__x] > 0})");
    exp = &strPool.back();
    strPool.push_back("__f");
    arg1 = &strPool.back();
    vecStrPool.push_back({arg1});
    args = &vecStrPool.back();
    fns.emplace_back("__PosCount", args, exp);

    // `__AllPossibleLoss(S) == {s \in SUBSET S : Cardinality(s) <= __max_loss}`
    strPool.push_back("{__s \\in SUBSET __S : Cardinality(__s) <= __max_loss}");
    exp = &strPool.back();
    strPool.push_back("__s");
    arg1 = &strPool.back();
    vecStrPool.push_back({arg1});
    args = &vecStrPool.back();
    fns.emplace_back("__AllPossibleLoss", args, exp);

    // TODO: construct rather than filter
    // ```
    // __AllPossibleOutOfOrder(S) ==
    //   LET
    //     max_range == 0..Max({Len(__net_buf[i]) : i \in S})
    //     ooo_set == [S -> max_range]
    //     ooo_set_possible == {i \in ooo_set : 
    //       /\ (\A j \in DOMAIN i : i[j] \in __OutOfOrderRange(__net_buf[j]) \cup {0})
    //       /\ __PosCount(i) <= __max_out_of_order
    //     }
    //   IN ooo_set_possible
    // ```
    strPool.push_back(add_indent(
        "LET\n"
        "  __max_range == 0..Max({Len(__net_buf[__i]) : __i \\in __S})\n"
        "  __ooo_set == [__S -> __max_range]\n"
        "  __ooo_set_possible == {__i \\in __ooo_set : \n"
        "    /\\ (\\A __j \\in DOMAIN __i : __i[__j] \\in __OutOfOrderRange(__net_buf[__j]) \\cup {0})\n"
        "    /\\ __PosCount(__i) <= __max_out_of_order\n"
        "  }\n"
        "IN __ooo_set_possible"
    ));
    exp = &strPool.back();
    strPool.push_back("__S");
    arg1 = &strPool.back();
    vecStrPool.push_back({arg1});
    args = &vecStrPool.back();
    fns.emplace_back("__AllPossibleOutOfOrder", args, exp);

    // `__AllPossibleSeq(S) == {seq \in [1..Cardinality(S) -> S] : IsInjective(seq)}`
    strPool.push_back("{__seq \\in [1..Cardinality(__S) -> __S] : IsInjective(__seq)}");
    exp = &strPool.back();
    strPool.push_back("__S");
    arg1 = &strPool.back();
    vecStrPool.push_back({arg1});
    args = &vecStrPool.back();
    fns.emplace_back("__AllPossibleSeq", args, exp);
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
            check(
                !protocol->params->empty(),
                format(
                    "Function {} should have at least one parameter, "
                    "otherwise it is effectively a constant",
                    name
                )
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
            assert(protocol->stmts != nullptr&& "Internal error: thread should have statements");
            analyzeThread(*protocol->type->ident, *protocol->name, *protocol->stmts);
            break;
        default:
            assert(false && "Internal error: unknown protocol type");
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
    if (!is_global) {
        localNames.insert(name);
        type2localNames[type].insert(name);
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

    if (is_const) {
        type2constDecls[type].emplace_back(name, exp->tla);
        type2constNames[type].insert(name);
    } else {
        type2varDecls[type].emplace_back(name, exp->tla);
        type2varNames[type].insert(name);
    }
}

void TLABuilder::analyzeThread(const string& type, const string& name,
    vector<StmtAST*>& stmts) {
    DEBUG("Analyzing thread {}...", name);
    check(
        nodetypes.contains(type),
        format("Declare thread of unknown node type {}", type)
    );
    addNewName(name);

    threads.emplace_back(type, name, analyzeThreadStmts(type, stmts));
    // analyzeThreadStmts(type, stmts);
    DEBUG("Thread {} analyzed", name);
}

auto TLABuilder::analyzeThreadStmts(const string& type, vector<StmtAST*>& stmts)
    -> vector<LabelMeta> {
    PathMeta path;
    LabelMeta label;
    vector<LabelMeta> labels;
    const string first = "__first_label";
    label.name = first;

    auto check_after_exit = [&path, this]() {
        check(
            path.has_exit != true,
            "Statements after exit are unreachable"
        );
    };
    auto collect_last_label = [this, &path, &label, &labels]() {
        check(
            !label.stmts.empty(),
            format("No statement follows label {}", label.name)
        );
        check(
            (path.has_recv == true && path.has_sendlike == true)
            || path.has_recv == false
            || path.has_sendlike == false,
            "The following patterns are not allowed. "
            "This is a limitation of the current implementation. "
            "(1) `if (cond) { receive(); } send(pkt);` "
            "(2) receive(); if (cond) { send(); } "
            "(3) if (cond1) { receive(); } if (cond2) { send(); } "
            "Try redesigning execution logic or setting a breakpoint."
        );
        label.has_recv = path.has_recv;
        label.has_sendlike = path.has_sendlike;
        labels.push_back(label);
        DEBUG("Breakpoint {} collected", label.name);
    };
    
    check(!stmts.empty(), "Thread should not be empty");

    for (auto it = stmts.begin(); it != stmts.end(); ++it) {
        StmtAST* stmt = *it;
        switch (stmt->rule) {
            case StmtAST::Breakpoint:
                if (label.name != first) {
                    collect_last_label();
                }
                label = LabelMeta();
                label.name = *stmt->name;
                DEBUG("Encounter breakpoint {}...", label.name);
                addNewName(label.name);
                path = PathMeta();
                break;
            case StmtAST::Assign:
                check_after_exit();
                analyzeAssignStmt(type, *stmt->assigns);
                path.has_effect = true;
                label.stmts.push_back(stmt);
                break;
            case StmtAST::Null:
                label.stmts.push_back(stmt);
                break;
            case StmtAST::PrimCall:
                check_after_exit();
                analyzePrimCallStmt(type, *stmt->name, *stmt->exps, path);
                label.stmts.push_back(stmt);
                break;
            case StmtAST::Temp:
                check_after_exit();
                path.has_effect &= analyzeTempStmt(type, *stmt->assigns, label.temps, path);
                label.stmts.push_back(stmt);
                break;
            case StmtAST::If:
                check_after_exit();
                path = analyzeIfStmt(type, *stmt, path, label);
                check(
                    path.branch_has_label == false
                        || it + 1 == stmts.end()
                        || (*(it + 1))->rule == StmtAST::Breakpoint,
                    "`if` statement must be followed by a breakpoint "
                    "if any of its branches has a breakpoint"   
                );
                label.stmts.push_back(stmt);
                break;
            case StmtAST::While:
                check_after_exit();
                path = analyzeWhileStmt(type, *stmt, path, label);
                label.stmts.push_back(stmt);
                break;
            case StmtAST::Break:
                [[fallthrough]];
            case StmtAST::Continue:
                check(
                    false,
                    format("Break and continue statements are not supported for now")
                );
                break;
            default:
                assert(false && "Internal error: unknown statement type");
        }
    }

    collect_last_label();
    return labels;
}

TLABuilder::PathMeta TLABuilder::analyzeIfStmt(const string& type, StmtAST& stmt,
    PathMeta path, LabelMeta& label_meta) {
    assert(stmt.rule == StmtAST::If && "Internal error: not an if statement");
    
    check(
        stmt.exp->rule == ExpAST::TLA,
        "If condition should not involve primitive calls"
    );
    mangleTLA(type, *stmt.exp->tla);
    if (stmt.vec_elif_exp != nullptr) {
        assert(!stmt.vec_elif_exp->empty()
            && "Internal error: empty elif condition list");
        for (auto exp : *stmt.vec_elif_exp) {
            check(
                exp->rule == ExpAST::TLA,
                "Elif condition should not involve primitive calls"
            );
            mangleTLA(type, *exp->tla);
        }
    }

    auto has_temp = !label_meta.temps.empty();
    auto [res_path, if_label_meta] = analyzeBranch(type, *stmt.stmts, path, has_temp);
    label_meta.branches.push_back(std::move(if_label_meta));
    
    if (stmt.vec_elif_stmts != nullptr) {
        assert(stmt.vec_elif_stmts->size() == stmt.vec_elif_exp->size()
            && "Internal error: elif condition and statement list size mismatch");
        for (auto elif_stmts : *stmt.vec_elif_stmts) {
            auto [elif_path, elif_label_meta] = analyzeBranch(type, *elif_stmts, path, has_temp);
            res_path |= elif_path;
            label_meta.branches.push_back(std::move(elif_label_meta));
        }
    }
    
    if (stmt.else_stmts != nullptr) {
        auto [else_path, else_label_meta] = analyzeBranch(type, *stmt.else_stmts, path, has_temp);
        res_path |= else_path;
        label_meta.branches.push_back(std::move(else_label_meta));
    } else {
        res_path |= path;
    }

    return res_path;
}

TLABuilder::PathMeta TLABuilder::analyzeWhileStmt(const string& type, StmtAST& stmt,
    PathMeta path, LabelMeta& label_meta) {
    assert(stmt.rule == StmtAST::While && "Internal error: not a while statement");
    
    check(
        stmt.exp->rule == ExpAST::TLA,
        "While condition should not involve primitive calls"
    );
    mangleTLA(type, *stmt.exp->tla);

    auto has_temp = !label_meta.temps.empty();
    auto [res_path, while_label_meta] = analyzeBranch(type, *stmt.stmts, path, has_temp);
    res_path |= path;
    label_meta.branches.push_back(std::move(while_label_meta));

    return res_path;
}

// TODO: merge with `analyzeThreadStmts`.
auto TLABuilder::analyzeBranch(const string& type, vector<StmtAST*>& stmts,
    PathMeta path, bool has_temp) -> pair<PathMeta, vector<LabelMeta>> {
    LabelMeta label;
    vector<LabelMeta> labels;
    const string first = "__first_label";
    label.name = first;
    
    auto check_after_exit = [&path, this]() {
        check(
            path.has_exit != true,
            "Statements after exit are unreachable"
        );
    };
    assert(path.has_exit == false && "Internal error: exit before branch");
    auto collect_last_label = [this, &path, &label, &labels, has_temp]() {
        check(
            !label.stmts.empty(),
            format("No statement follows label {}", label.name)
        );
        // TODO: correct?
        check(
            (path.has_recv == true && path.has_sendlike == true)
                || path.has_recv == false
                || path.has_sendlike == false,
            "The following patterns are not allowed. "
            "This is a limitation of the current implementation. "
            "(1) `if (cond) { receive(); } send(pkt);` "
            "(2) receive(); if (cond) { send(); } "
            "(3) if (cond1) { receive(); } if (cond2) { send(); } "
            "Try redesigning execution logic or setting a breakpoint."
        );
        label.has_recv = path.has_recv;
        label.has_sendlike = path.has_sendlike;
        labels.push_back(label);
    };
    
    if (stmts.empty()) {
        stmts.push_back(make_ast<StmtAST>(StmtAST::Null, n8));
        return {path, labels};
    }

    // Prepend a fake label to keep things consistent.
    if (stmts.front()->rule != StmtAST::Breakpoint) {
        stmts.insert(
            stmts.begin(),
            make_ast<StmtAST>(StmtAST::Breakpoint, make_str(fake_label), n7)
        );
    }

    for (auto it = stmts.begin(); it != stmts.end(); ++it) {
        StmtAST* stmt = *it;
        switch (stmt->rule) {
            case StmtAST::Breakpoint:
                if (label.name != first) {
                    check(
                        !has_temp,
                        "After declaring temporary values, "
                        "breakpoints are not allowed in following branches"
                    );
                    collect_last_label();
                    path.branch_has_label = true;
                }
                label = LabelMeta();
                label.name = *stmt->name;
                addNewName(label.name, label.name != fake_label);
                path = PathMeta();
                break;
            case StmtAST::Assign:
                check_after_exit();
                analyzeAssignStmt(type, *stmt->assigns);
                path.has_effect = true;
                label.stmts.push_back(stmt);
                break;
            case StmtAST::Null:
                label.stmts.push_back(stmt);
                break;
            case StmtAST::PrimCall:
                check_after_exit();
                analyzePrimCallStmt(type, *stmt->name, *stmt->exps, path);
                label.stmts.push_back(stmt);
                break;
            case StmtAST::Temp:
                check_after_exit();
                path.has_effect &= analyzeTempStmt(type, *stmt->assigns, label.temps, path);
                label.stmts.push_back(stmt);
                break;
            case StmtAST::If:
                // TODO: test no problem with nested branches.
                check_after_exit();
                path = analyzeIfStmt(type, *stmt, path, label);
                check(
                    path.branch_has_label == false
                        || it + 1 == stmts.end()
                        || (*(it + 1))->rule == StmtAST::Breakpoint,
                    "`if` statement must be followed by a breakpoint "
                    "if any of its branches has a breakpoint"
                );
                label.stmts.push_back(stmt);
                break;
            case StmtAST::While:
                check_after_exit();
                check(
                    !has_temp,
                    "While loops are not allowed after declaring temporary values. "
                    "Try setting a breakpoint before the while loop, "
                    "or declaring temporary values inside the while loop."
                );
                path = analyzeWhileStmt(type, *stmt, path, label);
                label.stmts.push_back(stmt);
                break;
            case StmtAST::Break:
                [[fallthrough]];
            case StmtAST::Continue:
                check(
                    false,
                    format("Break and continue statements are not supported for now")
                );
                break;
            default:
                assert(false && "Internal error: unknown statement type");
        }
    }

    collect_last_label();
    return {path, labels};
}

void TLABuilder::analyzeAssignStmt(const string& type, vector<AssignAST*>& assigns) {
    assert(!assigns.empty() && "Ill-formed AST: empty assignment list");
    
    // Check that elements of `assigns` have the same `ident` field.
    auto ident = *assigns[0]->ident;
    check(
        std::all_of(assigns.begin(), assigns.end(),
            [ident](AssignAST* assign) { return *assign->ident == ident; }),
        format("An assignment statement can only operate on one variable")
    );

    check(
        type2varNames[all].contains(ident) || type2varNames[type].contains(ident),
        format("{} cannot be assigned by node type {}", ident, type)
    );

    for (auto assign : assigns) {
        auto exp = assign->exp;
        check(
            exp->rule == ExpAST::TLA,
            format("RHS of assignment to {} should not involve primitive calls", ident)
        );
        mangleTLA(type, *exp->tla);
        if (assign->keys != nullptr) {
            for (auto key : *assign->keys) {
                check(
                    key->rule == ExpAST::TLA,
                    format("Keys of assignment to {} should not involve primitive calls", ident)
                );
                mangleTLA(type, *key->tla);
            }
        }
    }
}

void TLABuilder::analyzePrimCallStmt(const string& type, string& name,
    vector<ExpAST*>& args, PathMeta& path) {
    if (name == "send" || name == "unicast" || name == "multicast") {
        path.has_effect = true;
        check(
            path.has_sendlike == false,
            "Two send-like operations within one atomic path"
        );
        path.has_sendlike = true;

        check(
            !path.has_recv.is_both(),
            "When one branch calls `receive` but the other not, "
            "send-like operations are not allowed after they merge. "
            "Try setting a breakpoint before the send-like operation. "
            "This is a limitation of the current implementation."
        );
        if (name == "send") {
            check(
                args.size() == 1,
                "`send` should have exactly one argument, "
                "i.e. the packet to be sent"
            );
            name = path.has_recv == true ? "__DropSend" : "__Send";
        } else if (name == "unicast") {
            check(
                args.size() == 1,
                "`unicast` should have exactly one argument, "
                "i.e. a dictionary mapping arbitrary keys to packets"
            );
            name = path.has_recv == true ? "__DropUnicast" : "__Unicast";
        } else if (name == "multicast") {
            check(
                args.size() == 2,
                "`multicast` should have exactly two arguments, "
                "i.e. the packet to be sent and the set of destinations"
            );
            name = path.has_recv == true ? "__DropMulticast" : "__Multicast";
        } else {
            assert(false && "Internal error: unknown send-like operation");
        }
    }
    else if (name == "receive") {
        analyzeReceiveCall(type, name, args, path);
    }
    else if (name == "wait") {
        check(
            path.has_effect == false,
            "`wait` is only allowed at the beginning of an atomic path"
        );
        check(
            args.size() == 1,
            "`wait` should have exactly one argument, "
            "i.e. the condition to wait for"
        );
        name = "__Wait";

    }
    else if (name == "exit") {
        check(
            path.has_exit == false,
            "Two exit operations within one atomic path"
        );
        path.has_exit = true;
        check(
            args.size() == 0,
            "`exit` should not have any arguments"
        );
        name = "__Exit";
    }
    else if (name == "assert") {
        check(
            args.size() == 2,
            "`assert` should have exactly two argument, "
            "i.e. the condition and the error message"
        );
        name = "__Assert";
    }
    else if (name == "print") {
        check(
            args.size() == 1,
            "`print` should have exactly one argument, "
            "i.e. the expression to print"
        );
        name = "__Print";
    }
    else if (name.starts_with("__")) {
        assert(false && "Internal error: analyzing a primitive call twice");
    }
    else {
        check(
            false,
            format("Unknown primitive call {}", name)
        );
    }

    for (auto exp : args) {
        check(
            exp->rule == ExpAST::TLA,
            "Arguments of primitive calls should not involve primitive calls"
        );
        mangleTLA(type, *exp->tla);
    }
}

void TLABuilder::analyzeReceiveCall([[maybe_unused]] const string& type, string& name,
    vector<ExpAST*>& args, PathMeta& path) {
    assert(name == "receive" && "Internal error: not a receive call");
    path.has_effect = true;
    check(
        path.has_recv == false,
        "Two receive operations within one atomic path"
    );
    path.has_recv = true;
    check(
        path.has_sendlike == false,
        "Calling `receive` after send-like operations is not allowed within one atomic path"
    );
    check(
        args.size() == 0,
        "`receive` should not have any arguments"
    );
    name = "__Receive";
    args.push_back(make_ast<ExpAST>(ExpAST::TLA, n2, make_str("self")));
}

bool TLABuilder::analyzeTempStmt(const string& type, vector<AssignAST*>& assigns,
    vector<pair<string, ExpAST*>>& temps, PathMeta& path) {
    assert(!assigns.empty() && "Ill-formed AST: empty assignment list");
    
    bool has_effect = false;
    for (auto assign : assigns) {
        // TODO: record names in a stack and check comprehensively.
        check(
            !names.contains(*assign->ident),
            format("Declare temporary value with existing name {}", *assign->ident)
        );
        check(
            assign->keys == nullptr,
            format("LHS of temporary value declaration "
                "should be an identifier", *assign->ident)
        );

        auto exp = assign->exp;
        switch (exp->rule) {
            case ExpAST::TLA:
                mangleTLA(type, *exp->tla);
                break;
            case ExpAST::PrimCall:
                assert(!exp->fn_name->starts_with("__")
                    && "Internal error: analyzing a temporary value declaration twice");
                check(
                    *exp->fn_name == "receive",
                    format(
                        "RHS of temporary value declaration "
                        "should not involve primitive calls other than `receive`", 
                        *assign->ident
                    )
                );
                analyzeReceiveCall(type, *exp->fn_name, *exp->args, path);
                has_effect = true;
                break;
            default:
                assert(false && "Internal error: unknown expression type");
        }

        temps.emplace_back(*assign->ident, exp);
    }

    return has_effect;
}

void TLABuilder::mangleTLA(const string& type, string& tla) {
    string res;
    size_t i = 0;

    // Auxiliary functions.
    auto is_ident_start = [](char c) {
        return ('a' <= c && c <= 'z') || ('A' <= c && c <= 'Z') || c == '_';
    };
    auto is_ident_char = [&is_ident_start](char c) {
        return is_ident_start(c) || ('0' <= c && c <= '9');
    };
    auto get_ident = [&i, tla, &is_ident_char]() {
        size_t start = i;
        ++i;
        while (i < tla.size() && is_ident_char(tla[i])) {
            ++i;
        }
        return tla.substr(start, i - start);
    };

    while (i < tla.size()) {
        if (is_ident_start(tla[i])) {
            auto ident = get_ident();
            if (localNames.contains(ident)) {
                check(
                    type == all || type2localNames[type].contains(ident),
                    format(
                        "Identifier {} in expression {} "
                        "cannot be accessed by node type {}",
                        ident, tla, type
                    )
                );
                res += ident + "[__Node(self)]";
            }
            else {
                // TODO: check if `ident` is undeclared.
                if (ident == "self") {
                    res += "__Node(self)";
                } else {
                    res += ident;
                }
            }
        }
        else {
            res += tla[i];
            ++i;
        }
    }
    tla = res;
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
    mangleTLA(all, *tla);
    bool is_temporal = (tla->starts_with("[]") || tla->starts_with("<>"));
    if (is_temporal) {
        properties.emplace_back(name, tla);
    } else {
        invariants.emplace_back(name, tla);
    }
}

string TLABuilder::exp2str(const ExpAST& exp) {
    switch (exp.rule) {
        case ExpAST::TLA:
            return *exp.tla;
        case ExpAST::PrimCall:
            return format(
                "{}({})",
                *exp.fn_name,
                join(exps2strs(*exp.args), ", ")
            );
        default:
            assert(false && "Internal error: unknown expression type");
    }
}

vector<string> TLABuilder::exps2strs(const vector<ExpAST*>& exps) {
    vector<string> res;
    res.reserve(exps.size());
    rg::transform(exps, std::back_inserter(res),
        [this](const ExpAST* exp) { return exp2str(*exp); });
    return res;
}

string TLABuilder::buildTLA() {
    string res;
    res += format("---- MODULE {} ----\n\n", module_name);
    res += "EXTENDS Integers, Sequences, FiniteSets, "
        "TLC, Bitwise, FiniteSetsExt, SequencesExt, Functions\n\n";

    res += "CONSTANTS\n";
    for (const auto& [name, tla] : configs) {
        res += format("  {},\n", name);
    }
    res[res.length() - 2] = '\n';

    res += "(* --fair algorithm main {\n";

    res += "  variables\n";
    for (const auto& [type, vec] : type2varDecls) {
        for (const auto& [name, tla] : vec) {
            if (type == all) {
                res += format("    {} = {};\n", name, *tla);
            } else {
                res += format(
                    "    {} = [__n \\in {} |-> ({})];\n",
                    name, toUpper(type) + "_SET", *tla
                );
            }
        }
    }
    res += "\n";

    res += "  define {\n";
    for (const auto& [type, vec] : type2constDecls) {
        for (const auto& [name, tla] : vec) {
            if (type == all) {
                res += format("    {} == {}\n", name, *tla);
            } else {
                res += format(
                    "    {} == [__n \\in {} |-> ({})]\n",
                    name, toUpper(type) + "_SET", *tla
                );
            }
        }
    }
    res += "\n";
    for (const auto& [name, params, tla] : fns) {
        res += format("    {}({}) == {}\n", name, join(*params, ", "), *tla);
    }
    res += "  }\n\n\n";

    res += buildMacros();
    res += "\n";

    for (const auto& [type, name, labels] : threads) {
        res += "\n";
        res += buildProcess(type, name, labels);
    }
    res += "} *)\n\n";

    for (const auto& [name, tla] : invariants) {
        res += format("{} == {}\n", name, *tla);
    }
    res += "\n";
    for (const auto& [name, tla] : properties) {
        res += format("{} == {}\n", name, *tla);
    }

    return res;
}

string TLABuilder::buildMacros() {
    const string ident = "  ";
    auto add_indent = [&ident](const std::string& s) {
        auto res = ident;
        for (auto it = s.begin(); it != s.end(); ++it) {
            res += *it;
            if (*it == '\n' && (it + 1) != s.end()) {
                res += ident;
            }
        }
        return res;
    };

    string res = "";
    string m = "";

    // TODO: supporting more transmission primitives: an unsafe but general sending, 
    // TODO: a reliable sending.

    // __Assert
    m = "macro __Assert(__cond, __msg) {\n"
    "  with (__b = Assert(__cond, __msg)) {\n"
    "    assert __b;\n"
    "  }\n"
    "}\n";
    res += add_indent(m) + "\n";

    // __Drop
    m = "macro __Drop() {\n"
        "  __Assert(__net_buf[__Node(self)] # {}, \"Drop: empty buffer\");\n"
        "  net_buf[__Node(self)] := Tail(@);\n"
        "}\n";
    res += add_indent(m) + "\n";

    // TODO: a unified way to implement these sending primitives?

    // __Send
    m = "macro __Send(__pkt) {\n"
        "  with (__h = __next_hop[__Node(self), __pkt.dst]) {\n"
        "    either {\n"
        "      await __max_out_of_order > 0;\n"
        "      await __OutOfOrderRange(__net_buf[__h]) # {};\n"
        "      with (__pos \\in __OutOfOrderRange(__net_buf[__h])) {\n"
        "        __net_buf[__h] := __InsertAtEnd(@, __pos, pkt);\n"
        "      };\n"
        "      __max_out_of_order := __max_out_of_order - 1;\n"
        "    }\n"
        "    or {\n"
        "      await __max_loss > 0;\n"
        "      __max_loss := __max_loss - 1;\n"
        "    }\n"
        "    or {\n"
        "      __net_buf[__h] := Append(@, __pkt);\n"
        "    }\n"
        "  }\n"
        "}\n";
    res += add_indent(m) + "\n";

    // __DropSend
    m = "macro __DropSend(__pkt) {\n"
        "  __Assert(__net_buf[__Node(self)] # {}, \"DropSend: empty buffer\");\n"
        "  with (__s = __Node(self), __h = __next_hop[__s, __pkt.dst]) {\n"
        "    either {\n"
        "      await __max_out_of_order > 0;\n"
        "      await __OutOfOrderRange(__net_buf[__h]) # {};\n"
        "      with (__pos \\in __OutOfOrderRange(__net_buf[__h])) {\n"
        "        if (__s = __h) {\n"
        "          __net_buf[__h] := Tail(__InsertAtEnd(@, __pos, pkt));\n"
        "        } else {\n"
        "          __net_buf[__h] := __InsertAtEnd(@, __pos, pkt) ||\n"
        "          __net_buf[__s] := Tail(@);\n"
        "        }\n"
        "      };\n"
        "      __max_out_of_order := __max_out_of_order - 1;\n"
        "    }\n"
        "    or {\n"
        "      await __max_loss > 0;\n"
        "      __max_loss := __max_loss - 1;\n"
        "      __net_buf[__s] := Tail(@);\n"
        "    }\n"
        "    or {\n"
        "      if (__s = __h) {\n"
        "        __net_buf[__h] := Tail(Append(@, __pkt));\n"
        "      } else {\n"
        "        __net_buf[__h] := Append(@, __pkt) ||\n"
        "        __net_buf[__s] := Tail(@);\n"
        "      }\n"
        "    }\n"
        "  }\n"
        "}\n";
    res += add_indent(m) + "\n";

    // TODO: allowing arbitrary out-of-order?
    // __Unicast
    m = "macro __Unicast(__pkts) {\n"
        "  with (\n"
        "    __keys = DOMAIN __pkts,\n"
        "    __dst = __pkts[CHOOSE __i \\in __keys : TRUE].dst,\n"
        "    __h = __next_hop[__Node(self), __dst],\n"
        "    __loss \\in __AllPossibleLoss(__keys),\n"
        "    __unlost_pkts = {__pkts[__i] : __i \\in __keys \\ __loss},\n"
        "    __ordered = __Set2OrderedSeq(__unlost_pkts)\n"
        "  ) {\n"
        "    __Assert(Cardinality(__keys) > 0, \"Unicast: empty packets\");\n"
        "    __Assert(\n"
        "      \\A __i \\in __keys : __pkts[__i].dst = __dst,\n"
        "      \"Unicast: different destinations\"\n"
        "    );\n"
        "    __max_loss := __max_loss - Cardinality(__loss);\n"
        "    either {\n"
        "      await __max_out_of_order > 0;\n"
        "      await Cardinality(__unlost_pkts) >= 2;\n"
        "      with (__ooo \\in __AllPossibleSeq(__unlost_pkts) \\ {__ordered}) {\n"
        "        __max_out_of_order := __max_out_of_order - 1;\n"
        "        __net_buf[__h] := Append(@, __ooo);\n"
        "      }\n"
        "    }\n"
        "    or {\n"
        "      __net_buf[__h] := Append(@, __ordered);\n"
        "    }\n"
        "  }\n"
        "}\n";
    res += add_indent(m) + "\n";

    // __DropUnicast
    m = "macro __DropUnicast(__pkts) {\n"
        "  with (\n"
        "    __keys = DOMAIN __pkts,\n"
        "    __dst = __pkts[CHOOSE __i \\in __keys : TRUE].dst,\n"
        "    __s = __Node(self),\n"
        "    __h = __next_hop[__s, __dst],\n"
        "    __loss \\in __AllPossibleLoss(__keys),\n"
        "    __unlost_pkts = {__pkts[__i] : __i \\in __keys \\ __loss},\n"
        "    __ordered = __Set2OrderedSeq(__unlost_pkts)\n"
        "  ) {\n"
        "    __Assert(Cardinality(__keys) > 0, \"DropUnicast: empty packets\");\n"
        "    __Assert(\n"
        "      \\A __i \\in __keys : __pkts[__i].dst = __dst,\n"
        "      \"DropUnicast: different destinations\"\n"
        "    );\n"
        "    __max_loss := __max_loss - Cardinality(__loss);\n"
        "    either {\n"
        "      await __max_out_of_order > 0;\n"
        "      await Cardinality(__unlost_pkts) >= 2;\n"
        "      with (__ooo \\in __AllPossibleSeq(__unlost_pkts) \\ {__ordered}) {\n"
        "        __max_out_of_order := __max_out_of_order - 1;\n"
        "        if (__s = __h) {\n"
        "          __net_buf[__h] := Tail(Append(@, __ooo));\n"
        "        } else {\n"
        "          __net_buf[__h] := Append(@, __ooo) ||\n"
        "          __net_buf[__s] := Tail(@);\n"
        "        }\n"
        "      }\n"
        "    }\n"
        "    or {\n"
        "      if (__s = __h) {\n"
        "        __net_buf[__h] := Tail(Append(@, __ordered));\n"
        "      } else {\n"
        "        __net_buf[__h] := Append(@, __ordered) ||\n"
        "        __net_buf[__s] := Tail(@);\n"
        "      }\n"
        "    }\n"
        "  }\n"
        "}\n";
    res += add_indent(m) + "\n";

    // TODO: allowing arbitrary destinations?
    // __Multicast
    m = "macro __Multicast(__pkt, __dsts) {\n"
        "  __Assert(__dsts \\subseteq __links[__Node(self)], \"Multicast: invalid destinations\");\n"
        "  __Assert(__Node(self) \\notin __dsts, \"DropMulticast: self in destinations\");\n"
        "  with (\n"
        "    __pkts = [__dst \\in __dsts |-> \"dst\" :> __dst @@ __pkt],\n"
        "    __loss \\in __AllPossibleLoss(__dsts),\n"
        "    __unlost_dsts = __dsts \\ __loss,\n"
        "    __ooo \\in __AllPossibleOutOfOrder(__unlost_dsts)\n"
        "  ) {\n"
        "    __net_buf := [__n \\in NODE_SET |->\n"
        "      CASE __n \\in __unlost_dsts -> InsertAtEnd(__net_buf[__n], __ooo[__n], __pkts[__n])\n"
        "      [] OTHER -> __net_buf[__n]\n"
        "    ];\n"
        "    __max_loss := __max_loss - Cardinality(__loss);\n"
        "    __max_out_of_order := __max_out_of_order - PosCount(__ooo);\n"
        "  }\n"
        "}\n";
    res += add_indent(m) + "\n";

    // __DropMulticast
    m = "macro __DropMulticast(__pkt, __dsts) {\n"
        "  __Assert(__net_buf[__Node(self)] # {}, \"DropMulticast: empty buffer\");\n"
        "  __Assert(__dsts \\subseteq __links[__Node(self)], \"DropMulticast: invalid destinations\");\n"
        "  __Assert(__Node(self) \\notin __dsts, \"DropMulticast: self in destinations\");\n"
        "  with (\n"
        "    __pkts = [__dst \\in __dsts |-> \"dst\" :> __dst @@ __pkt],\n"
        "    __loss \\in __AllPossibleLoss(__dsts),\n"
        "    __unlost_dsts = __dsts \\ __loss,\n"
        "    __ooo \\in __AllPossibleOutOfOrder(__unlost_dsts)\n"
        "  ) {\n"
        "    __net_buf := [__n \\in NODE_SET |->\n"
        "      CASE __n \\in __unlost_dsts -> InsertAtEnd(__net_buf[__n], __ooo[__n], __pkts[__n])\n"
        "      [] __n = __Node(self) -> Tail(__net_buf[__n])\n"
        "      [] OTHER -> __net_buf[__n]\n"
        "    ];\n"
        "    __max_loss := __max_loss - Cardinality(__loss);\n"
        "    __max_out_of_order := __max_out_of_order - PosCount(__ooo);\n"
        "  }\n"
        "}\n";
    res += add_indent(m) + "\n";

    // __Wait
    m = "macro __Wait(__cond) {\n"
        "  await __cond;\n"
        "}\n";
    res += add_indent(m) + "\n";

    // __Exit
    m = "macro __Exit() {\n"
        "  __node_terminated[__Node(self)] := TRUE;\n"
        "}\n";
    res += add_indent(m) + "\n";

    // __Print
    m = "macro __Print(__x) {\n"
        "  print __x;\n"
        "}\n";
    res += add_indent(m) + "\n";

    res.pop_back();
    return res;
}

string TLABuilder::buildProcess(const string& type, const string& name,
    const vector<LabelMeta>& label_metas) {
    string res;
    res += format(
        R"!!(  fair+ process ({} \in ({} \X {{"{}"}})) {{)!!" "\n",
        name, toUpper(type) + "_SET", name
    );
    auto end_label = format("__L_{}_End", name);
    res += buildLabels(label_metas, 4, end_label);
    res += format("  {}:\n", end_label);
    res += "    skip;\n";
    res += "  }\n";
    return res;
}

string TLABuilder::buildLabels(const vector<LabelMeta>& label_metas, int indent,
    const string& end_label) {
    string res;
    for (const auto& label_meta : label_metas) {
        res += buildLabel(label_meta, indent, end_label);
    }
    return res;
}

string TLABuilder::buildLabel(const LabelMeta& label_meta, int indent,
    const string& end_label) {
    assert(indent >= 4 && "Internal error: invalid indentation number");

    auto spaces = string(indent, ' ');
    auto branch_it = label_meta.branches.begin();
    auto branch_end = label_meta.branches.end();
    string res = "";

    if (label_meta.name != fake_label) {
        res += format("{}{}:\n", string(indent - 2, ' '), label_meta.name);
        res += format(
            "{}if (__node_terminated[__Node(self)]) {{ goto {}; }};\n",
            spaces, end_label
        );
    }

    if (!label_meta.temps.empty()) {
        res += format("{}with (\n", spaces);
        for (const auto& [name, exp] : label_meta.temps) {
            res += format("{}  {} = {},\n", spaces, name, exp2str(*exp));
        }
        res += spaces + ") {\n";
        indent += 2;
        spaces = string(indent, ' ');
    }

    for (const auto& stmt : label_meta.stmts) {
        switch (stmt->rule) {
            case StmtAST::Breakpoint:
                assert(*stmt->name == label_meta.name && "Internal error: label name mismatch");
                break;
            case StmtAST::Assign: {
                vector<string> assigns;
                bool has_recv = false;
                for (const auto& assign : *stmt->assigns) {
                    auto lhs = *assign->ident;
                    if (assign->keys != nullptr) {
                        lhs += format("[{}]", join(exps2strs(*assign->keys), ", "));
                    }
                    auto rhs = exp2str(*assign->exp);
                    assigns.push_back(format("{} := {}", lhs, rhs));
                    auto exp = assign->exp;
                    if (exp->rule == ExpAST::PrimCall && *exp->fn_name == "__Receive") {
                        has_recv = true;
                    }
                }
                res += spaces + join(assigns, " || ") + ";\n";
                if (label_meta.has_sendlike == false && has_recv) {
                    res += format("{}__Drop();\n", spaces);
                }
                break;
            }
            case StmtAST::Null:
                res += format("{}skip;\n", spaces);
                break;
            case StmtAST::PrimCall: {
                auto name = *stmt->name;
                auto args = exps2strs(*stmt->exps);
                res += format("{}{}({});\n", spaces, name, join(args, ", "));
                if (label_meta.has_sendlike == false && name == "__Receive") {
                    res += format("{}__Drop();\n", spaces);
                }
                break;
            }
            case StmtAST::Temp:
                break;
            case StmtAST::If:
                res += format("{}if({}) {{\n", spaces, exp2str(*stmt->exp));
                res += buildLabels(*branch_it++, indent + 2, end_label);
                res += format("{}}};\n", spaces);
                if (stmt->vec_elif_exp != nullptr) {
                    for (size_t i = 0; i < stmt->vec_elif_exp->size(); ++i) {
                        auto elif_exp = (*stmt->vec_elif_exp)[i];
                        res += format("{}else if({}) {{\n", spaces, exp2str(*elif_exp));
                        res += buildLabels(*branch_it++, indent + 2, end_label);
                        res += format("{}}};\n", spaces);
                    }
                }
                if (stmt->else_stmts != nullptr) {
                    res += format("{}else {{\n", spaces);
                    res += buildLabels(*branch_it++, indent + 2, end_label);
                    res += format("{}}};\n", spaces);
                }
                break;
            case StmtAST::While:
                res += format("{}while({}) {{\n", spaces, exp2str(*stmt->exp));
                res += buildLabels(*branch_it++, indent + 2, end_label);
                res += format("{}}};\n", spaces);
                break;
            case StmtAST::Break:
                [[fallthrough]];
            case StmtAST::Continue:
                assert(false && "Internal error: break and continue statements not supported");
                break;
            default:
                assert(false && "Internal error: unknown statement type");
        }
    }

    assert(branch_it == branch_end && "Internal error: branch iterator not at end");

    if (!label_meta.temps.empty()) {
        res += string(indent - 2, ' ') + "}\n";
    }

    return res;
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
