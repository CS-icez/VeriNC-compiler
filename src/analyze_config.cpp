#include "tla_builder.hpp"
#include <algorithm>
#include <cassert>
#include <format>
#include <ranges>
#include "debug.hpp"
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
        assign->vec_keys == nullptr,
        format("LHS of configuration {} should be an identifier", name)
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
