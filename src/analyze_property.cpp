#include "tla_builder.hpp"
#include <algorithm>
#include <cassert>
#include <format>
#include <ranges>
#include "debug.hpp"
using std::format;
using namespace std::string_literals;
namespace rg = std::ranges;

void TLABuilder::analyze(PropertyAST* property) {
    // Collect properties.
    auto name = *property->ident;
    addNewName(name);

    auto exp = property->ctl->exp;
    check(
        exp->rule == ExpAST::TLA,
        format("RHS of property {} should not involve primitive calls", name)
    );

    const auto& tla = *exp->tla;
    mangleTLA(all, tla);
    bool is_temporal = tla.size() > 2 && (
        (*tla[0] == "<" && *tla[1] == ">") || (*tla[0] == "[" && *tla[1] == "]")
    );
    if (is_temporal) {
        properties.emplace_back(name, &tla);
    } else {
        invariants.emplace_back(name, &tla);
    }
}
