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
#include "debug.hpp"
using std::cout;
using std::endl;
using std::format;
using namespace std::string_literals;
namespace rg = std::ranges;

void TLABuilder::analyzeThread(const string& type, const string& name,
    vector<StmtAST*>& stmts) {
    DEBUG("Analyzing thread {}...", name);
    check(
        nodetypes.contains(type),
        format("Declare thread of unknown node type {}", type)
    );
    addNewName(name);

    // Expand macros.
    stmts = expandMacro(type, stmts);

    // Add a mandatory constraint for retx. Recognition is based on thread name.
    if (toUpper(name).ends_with("RETX")) {
        check(
            stmts.size() >= 2 && stmts[0]->rule == StmtAST::Breakpoint
                && stmts[1]->rule == StmtAST::While,
            format("Retransmission thread {} should start with a breakpoint followed by a while loop", name)
        );
        auto tla = make_vec(make_str("__net_buf[__Node(self)] = <<>>"));
        auto exp = make_ast<ExpAST>(ExpAST::TLA, n2, tla);
        auto exps = make_vec(exp);
        auto wait_stmt = make_ast<StmtAST>(StmtAST::PrimCall, make_str("wait"), exps, n6);
        auto while_branch = stmts[1]->stmts;
        while_branch->insert(while_branch->begin(), wait_stmt);
    }

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
        // Note that
        //   `if (cond) {} else { receive(); send(pkt); }`
        // is allowed.

        // DEBUG_EXP(path.has_recv);
        // DEBUG_EXP(path.has_sendlike);
        // check(
        //     (path.has_recv == true && path.has_sendlike == true)
        //     || path.has_recv == false
        //     || path.has_sendlike == false,
        //     "The following patterns are not allowed. "
        //     "This is a limitation of the current implementation. "
        //     "(1) `if (cond) { receive(); } send(pkt);` "
        //     "(2) receive(); if (cond) { send(); } "
        //     "(3) if (cond1) { receive(); } if (cond2) { send(); } "
        //     "Try redesigning execution logic or setting a breakpoint."
        // );
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
                DEBUG("Encounter breakpoint \033[32m{}\033[0m...", label.name);
                addNewName(label.name);
                path = PathMeta();
                break;
            case StmtAST::Assign:
                check_after_exit();
                analyzeAssignStmt(type, *stmt->assigns, path);
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
            case StmtAST::MacroCall:
                DEBUG("Internal error: macro call {} should have been expanded", *stmt->name);
                assert(false && "Internal error: macro call should have been expanded");
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
                check(
                    it != stmts.begin() && (*(it - 1))->rule == StmtAST::Breakpoint,
                    "`while` statement must be preceded immediately by a breakpoint"
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
    return labels;
}

TLABuilder::PathMeta TLABuilder::analyzeIfStmt(const string& type, StmtAST& stmt,
    PathMeta path, LabelMeta& label_meta) {
    DEBUG("Enter {}", __func__);
    assert(stmt.rule == StmtAST::If && "Internal error: not an if statement");
    DEBUG_EXP(path.has_recv);

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

    vector<TriBool> branch_has_sendlike;
    auto has_temp = !label_meta.temps.empty();
    auto [res_path, if_label_meta] = analyzeBranch(type, *stmt.stmts, path, has_temp);
    label_meta.branches.push_back(std::move(if_label_meta));
    branch_has_sendlike.push_back(res_path.has_sendlike);

    if (stmt.vec_elif_stmts != nullptr) {
        assert(stmt.vec_elif_stmts->size() == stmt.vec_elif_exp->size()
            && "Internal error: elif condition and statement list size mismatch");
        for (auto elif_stmts : *stmt.vec_elif_stmts) {
            auto [elif_path, elif_label_meta] = analyzeBranch(type, *elif_stmts, path, has_temp);
            res_path |= elif_path;
            label_meta.branches.push_back(std::move(elif_label_meta));
            branch_has_sendlike.push_back(elif_path.has_sendlike);
        }
    }
    
    if (stmt.else_stmts != nullptr) {
        auto [else_path, else_label_meta] = analyzeBranch(type, *stmt.else_stmts, path, has_temp);
        res_path |= else_path;
        label_meta.branches.push_back(std::move(else_label_meta));
        branch_has_sendlike.push_back(else_path.has_sendlike);
    } else {
        res_path |= path;
    }

    DEBUG_EXP(path.has_recv);
    DEBUG_EXP(path.has_sendlike);
    DEBUG_EXP(branch_has_sendlike);
    assert((path.has_recv != true || !path.has_sendlike.is_both())
        && "Internal error: such condition should have been recursively fixed");
    bool cond = path.has_recv == true && path.has_sendlike == false
        && rg::any_of(branch_has_sendlike, [](TriBool b) { return b != false; });
    if (cond) {
        if (stmt.else_stmts == nullptr) {
            stmt.else_stmts = make_vec<StmtAST>();
            auto else_label_meta = LabelMeta();
            else_label_meta.name = fake_label + "_" + std::to_string(__LINE__);
            label_meta.branches.push_back({else_label_meta});
            branch_has_sendlike.push_back(false);
        }
        auto branch_num = branch_has_sendlike.size();
        for (size_t i = 0; i < branch_num; ++i) {
            DEBUG_EXP(branch_has_sendlike);
            assert(!branch_has_sendlike[i].is_both()
                && "Internal error: such condition should have been recursively fixed");
            auto& branch_labels = label_meta.branches[label_meta.branches.size() - branch_num + i];
            assert(!branch_labels.empty() && "Internal error: empty branch labels");
            auto& branch_last_label = branch_labels.back();
            if (branch_has_sendlike[i] == false) {
                auto drop = make_ast<StmtAST>(StmtAST::PrimCall, make_str("__Drop"), make_vec<ExpAST>(), n6);
                branch_last_label.stmts.push_back(drop);
                branch_last_label.has_sendlike = true;
            }
        }
        label_meta.has_sendlike = true;
        res_path.has_sendlike = true;
    }

    DEBUG("Exit {}", __func__);
    return res_path;
}

TLABuilder::PathMeta TLABuilder::analyzeWhileStmt(const string& type, StmtAST& stmt,
    PathMeta path, LabelMeta& label_meta) {
    DEBUG("Enter {}", __func__);
    assert(stmt.rule == StmtAST::While && "Internal error: not a while statement");
    
    check(
        stmt.exp->rule == ExpAST::TLA,
        "While condition should not involve primitive calls"
    );
    mangleTLA(type, *stmt.exp->tla);

    auto has_temp = !label_meta.temps.empty();
    [[maybe_unused]] auto [while_path, while_label_meta] =
        analyzeBranch(type, *stmt.stmts, path, has_temp);
    label_meta.branches.push_back(std::move(while_label_meta));

    DEBUG("Exit {}", __func__);
    return path;
}

auto TLABuilder::analyzeBranch(const string& type, vector<StmtAST*>& stmts,
    PathMeta path, bool has_temp) -> pair<PathMeta, vector<LabelMeta>> {
    DEBUG("Enter {}", __func__);
    DEBUG_EXP(path.has_recv);
    DEBUG_EXP(path.has_sendlike);
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
    auto collect_last_label = [this, &path, &label, &labels]() {
        check(
            !label.stmts.empty(),
            format("No statement follows label {}", label.name)
        );
        // TODO: correct?
        // DEBUG_EXP(path.has_recv);
        // DEBUG_EXP(path.has_sendlike);
        // check(
        //     (path.has_recv == true && path.has_sendlike == true)
        //         || path.has_recv == false
        //         || path.has_sendlike == false,
        //     "The following patterns are not allowed. "
        //     "This is a limitation of the current implementation. "
        //     "(1) `if (cond) { receive(); } send(pkt);` "
        //     "(2) receive(); if (cond) { send(); } "
        //     "(3) if (cond1) { receive(); } if (cond2) { send(); } "
        //     "Try redesigning execution logic or setting a breakpoint."
        // );
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
        auto fake = fake_label + "_" + std::to_string(__LINE__);
        stmts.insert(
            stmts.begin(),
            make_ast<StmtAST>(StmtAST::Breakpoint, make_str(fake), n7)
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
                }
                label = LabelMeta();
                label.name = *stmt->name;
                DEBUG("Encounter breakpoint \033[32m{}\033[0m...", label.name);
                addNewName(label.name, !label.name.starts_with(fake_label));
                if (!label.name.starts_with(fake_label)) {
                    path = PathMeta();
                    path.branch_has_label = true;
                }
                break;
            case StmtAST::Assign:
                check_after_exit();
                analyzeAssignStmt(type, *stmt->assigns, path);
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
            case StmtAST::MacroCall:
                DEBUG("Internal error: macro call {} should have been expanded", *stmt->name);
                assert(false && "Internal error: macro call should have been expanded");
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
                // check(
                //     !has_temp,
                //     "While loops are not allowed after declaring temporary values. "
                //     "Try setting a breakpoint before the while loop, "
                //     "or declaring temporary values inside the while loop."
                // );
                check(
                    it != stmts.begin() && (*(it - 1))->rule == StmtAST::Breakpoint,
                    "`while` statement must be preceded immediately by a breakpoint"
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
    DEBUG_EXP(path.has_recv);
    DEBUG_EXP(path.has_sendlike);
    DEBUG("Exit {}", __func__);
    return {path, labels};
}

void TLABuilder::analyzeAssignStmt(const string& type, vector<AssignAST*>& assigns,
    PathMeta& path) {
    DEBUG("Enter {}", __func__);
    DEBUG_EXP(path.has_recv);
    assert(!assigns.empty() && "Ill-formed AST: empty assignment list");

    // Check that elements of `assigns` have the same `ident` field.
    auto ident = *assigns[0]->ident;
    check(
        rg::all_of(assigns, [ident](const auto& assign) { return *assign->ident == ident; }),
        format("An assignment statement can only operate on one variable")
    );

    check(
        type2varNames[all].contains(ident) || type2varNames[type].contains(ident),
        format("{} cannot be assigned by node type {}", ident, type)
    );

    for (auto assign : assigns) {
        auto exp = assign->exp;
        if (assign->keys != nullptr) {
            for (auto key : *assign->keys) {
                check(
                    key->rule == ExpAST::TLA,
                    format("Keys of assignment to {} should not involve primitive calls", ident)
                );
                mangleTLA(type, *key->tla);
            }
        }
        check(
            !assign->is_choice,
            format("Assignment to {} should not involve nondeterminism", ident)
        );
        switch (exp->rule) {
            case ExpAST::TLA:
                mangleTLA(type, *exp->tla);
                break;
            case ExpAST::PrimCall:
                assert(!exp->fn_name->starts_with("__")
                    && "Internal error: analyzing an assignment statement twice");
                check(
                    *exp->fn_name == "receive",
                    format(
                        "RHS of an assignment to {} "
                        "should not involve primitive calls other than `receive`", 
                        *assign->ident
                    )
                );
                analyzeReceiveCall(type, *exp->fn_name, *exp->args, path);
                break;
            default:
                assert(false && "Internal error: unknown expression type");
        }
    }
    DEBUG_EXP(path.has_recv);
    DEBUG("Exit {}", __func__);
}

void TLABuilder::analyzePrimCallStmt(const string& type, string& name,
    vector<ExpAST*>& args, PathMeta& path) {
    DEBUG("Enter {}", __func__);
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
        if (name == "__CheckCacheConsistency") {
            check(
                args.size() == 2,
                "`__CheckCacheConsistency` should have exactly two arguments, "
                "i.e. the packet and whether it is the start of an event"
            );
        }
        else {
            assert(false && "Internal error: analyzing a primitive call twice");
        }
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
    DEBUG("Exit {}", __func__);
}

void TLABuilder::analyzeReceiveCall([[maybe_unused]] const string& type, string& name,
    vector<ExpAST*>& args, PathMeta& path) {
    DEBUG("Enter {}", __func__);
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
    auto tla = make_vec(make_str("__Node(self)"));
    args.push_back(make_ast<ExpAST>(ExpAST::TLA, n2, tla));
    DEBUG("Exit {}", __func__);
}

bool TLABuilder::analyzeTempStmt(const string& type, vector<AssignAST*>& assigns,
    decltype(LabelMeta::temps)& temps, PathMeta& path) {
    DEBUG("Enter {}", __func__);
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
                temps.emplace_back(*assign->ident, exp->tla, assign->is_choice);
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
                check(
                    !assign->is_choice,
                    format(
                        "Nondeterminism is only allowed on sets, "
                        "but receive() returns a packet (a dictionary)"
                    )
                );
                analyzeReceiveCall(type, *exp->fn_name, *exp->args, path);
                temps.emplace_back(*assign->ident, "__Receive(__Node(self))", false);
                has_effect = true;
                break;
            default:
                assert(false && "Internal error: unknown expression type");
        }
    }

    DEBUG("Exit {}", __func__);
    return has_effect;
}
