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
    check(
        !assign->is_choice,
        "Configuration should not involve nondeterminism"
    );
    auto exp = assign->exp;
    check(
        exp->rule == ExpAST::TLA,
        format("RHS of configuration {} should not involve primitive calls", name)
    );
    configs.emplace_back(name, exp->tla);
}
