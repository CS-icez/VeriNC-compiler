#include "tla_builder.hpp"
#include <algorithm>
#include <cassert>
#include <format>
#include <ranges>
#include "make_ast.hpp"
#include "debug.hpp"
using std::format;
using namespace std::string_literals;
namespace rg = std::ranges;

auto TLABuilder::expandMacro(const string& type, const vector<StmtAST*>& stmts)
    -> vector<StmtAST*> {
    vector<StmtAST*> res;
    res.reserve(stmts.size());
    auto proj = [](const auto& macro) { return std::get<0>(macro); };
    auto l = "("s;
    auto r = ")"s;

    for (auto stmt : stmts) {
        switch (stmt->rule) {
            case StmtAST::Breakpoint:
                [[fallthrough]];
            case StmtAST::Assign:
                [[fallthrough]];
            case StmtAST::Null:
                [[fallthrough]];
            case StmtAST::PrimCall:
                [[fallthrough]];
            case StmtAST::Temp:
                res.push_back(stmt);
                break;
            case StmtAST::MacroCall: {
                auto macro = rg::find(macros, *stmt->name, proj);
                check(
                    macro != macros.end(),
                    format("Macro {} is not defined but called", *stmt->name)
                );
                const auto& params = std::get<1>(*macro);
                check(
                    stmt->exps->size() == params.size(),
                    format("Macro {} expects {} arguments but {} are provided",
                        *stmt->name, params.size(), stmt->exps->size())
                );
                MacroArgMap args;
                for (size_t i = 0; i < stmt->exps->size(); ++i) {
                    auto exp = stmt->exps->at(i);
                    auto param = params[i];
                    check(
                        exp->rule == ExpAST::TLA,
                        "Arguments of macro calls should not involve primitive calls"
                    );
                    auto arg = *exp->tla;
                    arg.insert(arg.begin(), &l);
                    arg.push_back(&r);
                    args[param] = arg;
                }
                auto expanded = expandMacro(type, *stmt->name, *std::get<2>(*macro), args);
                // Wait for `vector::append_range` in C++23.
                res.insert(res.end(), expanded.begin(), expanded.end());
                break;
            }
            case StmtAST::If:
                res.push_back(stmt);
                *res.back()->stmts = expandMacro(type, *stmt->stmts);
                if (stmt->vec_elif_stmts != nullptr) {
                    rg::for_each(*stmt->vec_elif_stmts, [&](const auto& stmts) {
                        *stmts = expandMacro(type, *stmts);
                    });
                }
                if (stmt->else_stmts != nullptr) {
                    *stmt->else_stmts = expandMacro(type, *stmt->else_stmts);
                }
                break;
            case StmtAST::While:
                res.push_back(stmt);
                *res.back()->stmts = expandMacro(type, *stmt->stmts);
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

    return res;
}

auto TLABuilder::expandMacro(const string& type, const string& name,
    const vector<StmtAST*>& stmts, const MacroArgMap& args) -> vector<StmtAST*> {
    DEBUG("Enter {} with\n  name={}\n  args={}", __func__, name, args);
    vector<StmtAST*> res;
    res.reserve(stmts.size());
    auto substitute = [&](const auto& ast) {
        substituteMacroParam(*ast, args);
    };
    auto l = "("s;
    auto r = ")"s;

    for (auto stmt : stmts) {
        switch (stmt->rule) {
            case StmtAST::Breakpoint:
                check(false, "Breakpoint is not allowed inside a macro");
                break;
            case StmtAST::Assign:
                res.push_back(stmt->clone());
                rg::for_each(*res.back()->assigns, substitute);
                break;
            case StmtAST::Null:
                res.push_back(stmt->clone());
                break;
            case StmtAST::PrimCall:
                res.push_back(stmt->clone());
                rg::for_each(*res.back()->exps, substitute);
                break;
            case StmtAST::MacroCall: {
                auto proj = [](const auto& macro) { return std::get<0>(macro); };
                auto macro = rg::find(macros, *stmt->name, proj);
                check(
                    macro != macros.end(),
                    format("Macro {} is not defined but called in macro {}", *stmt->name, name)
                );
                const auto& params = std::get<1>(*macro);
                check(
                    stmt->exps->size() == params.size(),
                    format("Macro {} expects {} arguments but {} are provided in macro {}",
                        *stmt->name, params.size(), stmt->exps->size(), name)
                );
                MacroArgMap new_args = args;
                for (size_t i = 0; i < stmt->exps->size(); ++i) {
                    auto exp = stmt->exps->at(i);
                    substitute(exp);
                    auto param = params[i];
                    check(
                        exp->rule == ExpAST::TLA,
                        "Arguments of macro calls should not involve primitive calls"
                    );
                    auto arg = *exp->tla;
                    arg.insert(arg.begin(), &l);
                    arg.push_back(&r);
                    new_args[param] = arg;
                }
                auto expanded = expandMacro(type, *stmt->name, *std::get<2>(*macro), new_args);
                res.insert(res.end(), expanded.begin(), expanded.end());
                break;
            }
            case StmtAST::Temp:
                // TODO: values declared in macro should be put in a separate `with`.
                res.push_back(stmt->clone());
                rg::for_each(*res.back()->assigns, substitute);
                break;
            case StmtAST::If: {
                auto new_stmt = make_ast<StmtAST>(
                    StmtAST::If,
                    n3,
                    stmt->exp->clone(),
                    make_vec<StmtAST>(),
                    deep_copy(stmt->vec_elif_exp),
                    nullptr,
                    nullptr
                );
                res.push_back(new_stmt);
                substitute(new_stmt->exp);
                *new_stmt->stmts = expandMacro(type, name, *stmt->stmts, args);
                if (stmt->vec_elif_exp != nullptr) {
                    rg::for_each(*new_stmt->vec_elif_exp, substitute);
                    new_stmt->vec_elif_stmts = make_vec<vector<StmtAST*>>();
                    rg::for_each(*stmt->vec_elif_stmts, [&](const auto& stmts) {
                        auto& dst = *new_stmt->vec_elif_stmts;
                        dst.push_back(make_vec<StmtAST>());
                        *dst.back() = expandMacro(type, name, *stmts, args);
                    });
                }
                if (stmt->else_stmts != nullptr) {
                    new_stmt->else_stmts = make_vec<StmtAST>();
                    *new_stmt->else_stmts = expandMacro(type, name, *stmt->else_stmts, args);
                }
                break;
            }
            case StmtAST::While: {
                auto new_stmt = make_ast<StmtAST>(
                    StmtAST::While,
                    n3,
                    stmt->exp->clone(),
                    make_vec<StmtAST>(),
                    n3
                );
                res.push_back(new_stmt);
                substitute(new_stmt->exp);
                *new_stmt->stmts = expandMacro(type, name, *stmt->stmts, args);
                break;
            }
            case StmtAST::Break:
                [[fallthrough]];
            case StmtAST::Continue:
                check(false, "Break and continue are not allowed inside a macro");
                break;
            default:
                assert(false && "Internal error: unknown statement type");
        }
    }

    DEBUG("Exit {} with\n  name={}\n  args={}", __func__, name, args);
    return res;
}

void TLABuilder::substituteMacroParam(ExpAST& exp, const MacroArgMap& args) {
    switch (exp.rule) {
        case ExpAST::TLA: {
            auto& tla = *exp.tla;
            vector<string*> res;
            for (size_t i = 0; i < tla.size(); ++i) {
                if (!isIdent(*tla[i]) || isKey(tla, i) || !args.contains(*tla[i])) {
                    res.push_back(tla[i]);
                    continue;
                }
                // Wait for `vector::append_range` in C++23.
                auto vec = deep_copy(&args.at(*tla[i]));
                res.insert(res.end(), vec->begin(), vec->end());
                delete vec;
            }
            tla = res;
            break;
        }
        case ExpAST::PrimCall:
            rg::for_each(*exp.args, [&](const auto& arg) {
                substituteMacroParam(*arg, args);
            });
            break;
        default:
            assert(false && "Internal error: unknown expression type");
            break;
    }
}

void TLABuilder::substituteMacroParam(AssignAST& assign, const MacroArgMap& args) {
    if (auto& ident = *assign.ident; args.contains(ident)) {
        auto& arg = args.at(ident);
        check(
            arg.size() == 3,
            format(
                "Parameter {} appears in macro as an assigned variable, "
                "thus can only be substituted by another identifier. "
                "Assignment like `(IF TRUE THEN x ELSE y) = 1` is not allowed",
                ident
            )
        );
        ident = *arg[1];
    }
    if (assign.vec_keys != nullptr) {
        rg::for_each(*assign.vec_keys, [&](const auto& keys) {
            rg::for_each(*keys, [&](const auto& key) {
                substituteMacroParam(*key, args);
            });
        });
    }
    substituteMacroParam(*assign.exp, args);
}
