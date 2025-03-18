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
using std::endl;
using std::format;
using namespace std::string_literals;
namespace rg = std::ranges;

void TLABuilder::expandMacro(const string& type, vector<StmtAST*>& stmts) {
    vector<StmtAST*> res;
    res.reserve(stmts.size());
    auto proj = [](const auto& macro) { return std::get<0>(macro); };
    auto l = "("s;
    auto r = ")"s;

    for (auto stmt : stmts) {
        if (stmt->rule != StmtAST::MacroCall) {
            res.push_back(stmt);
            continue;
        }
        auto macro = rg::find(macros, *stmt->name, proj);
        check(
            macro != macros.end(),
            format("Macro {} is not defined but called", *stmt->name)
        );
        const auto& params = std::get<1>(*macro);
        check(
            stmt->exps->size() == std::get<1>(*macro).size(),
            format("Macro {} expects {} arguments but {} are provided",
                *stmt->name, params.size(), stmt->exps->size())
        );
        umap<string, vector<string*>> args;
        for (size_t i = 0; i < stmt->exps->size(); ++i) {
            auto exp = stmt->exps->at(i);
            auto param = params[i];
            check(
                exp->rule == ExpAST::TLA,
                "Arguments of macro calls should not involve primitive calls"
            );
            mangleTLA(type, *exp->tla);
            auto arg = *exp->tla;
            arg.insert(arg.begin(), &l);
            arg.push_back(&r);
            args[param] = arg;
            auto expanded = expandMacro(type, *stmt->name, args);
            // Wait for `vector::append_range` in C++23.
            res.insert(res.end(), expanded.begin(), expanded.end());
        }
    }

    stmts = std::move(res);
}

auto TLABuilder::expandMacro(const string& type, const string& name, 
    const umap<string, vector<string*>>& args) -> vector<StmtAST*> {
    return {};
}