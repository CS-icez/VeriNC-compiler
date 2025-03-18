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
            vector<string> params;
            // Wait for `rg::to` in C++23.
            rg::transform(*protocol->params, std::back_inserter(params),
                [](const auto& s) { return *s; });
            fns.emplace_back(name, std::move(params), exp->tla);
            break;
        }
        case ProtocolAST::Macro: {
            // Collect macro.
            vector<string> params;
            // Wait for `rg::to` in C++23.
            rg::transform(*protocol->params, std::back_inserter(params),
                [](const auto& s) { return *s; });
            macros.emplace_back(*protocol->name, std::move(params), protocol->stmts);
            break;
        }
        case ProtocolAST::Thread:
            assert(protocol->stmts != nullptr && "Internal error: thread should have statements");
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
    // Replace `self` with `__n`.
    // *exp->tla = std::regex_replace(*exp->tla, std::regex("self"), "__n");
    rg::for_each(*exp->tla, [](const auto& s) {
        if (*s == "self") {
            *s = "__n";
        }
    });
    mangleTLA(type, *exp->tla, true);

    if (is_const) {
        type2constNames[type].insert(name);
    } else {
        type2varNames[type].insert(name);
    }

    if (is_const && !assign->is_choice) {
        type2constDecls[type].emplace_back(name, exp->tla);
    } else {
        type2varDecls[type].emplace_back(name, exp->tla, assign->is_choice);
    }
}
