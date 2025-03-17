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
    addOurConstants();
    addOurVariables();
    addOurFns();
    addOurProperties();
    
    // Analyze protocol sections.
    for (auto protocol : protocols) {
        analyze(protocol);
    }

    // `__active_threads = [__n \in TYPE1_SET |-> num1] @@ ...`
    vector<string> vec_exp;
    for (const auto& type : nodetypes) {
        auto s = format(
            "[__n \\in {}_SET |-> {}]",
            toUpper(type),
            rg::count(threads, type, [&](const auto& t) { return std::get<0>(t); })
        );
        vec_exp.push_back(std::move(s));
    }
    type2varDecls[all].emplace_back("__active_threads", join(vec_exp, " @@ "), false);
}

std::string TLABuilder::toUpper(const string& str) {
    auto res = str;
    // Wait for `rg::to` in C++23.
    rg::transform(str, res.begin(),
        // Here directly use &std::toupper will cause a compilation error.
        [](unsigned char c) { return std::toupper(c); });
    return res;
}

void TLABuilder::mangleTLA(const string& type, const vector<string*>& tla,
    bool is_cv_decl) {
    auto is_ident = [](const string& s) {
        return s.size() > 0 && !std::isdigit(s[0])
            && std::ranges::all_of(s, [](char c) {
                return std::isalnum(c) || c == '_';
            });
    };

    for (size_t i = 0; i < tla.size(); ++i) {
        if (!is_ident(*tla[i])) {
            continue;
        }
        // Line 1070 of https://github.com/tlaplus/tlaplus/blob/master/tlatools/org.lamport.tlatools/src/pcal/PlusCal2.tla#L284
        bool is_last = (i == tla.size() - 1);
        bool is_first = (i == 0);
        auto prefix = uset({"["s, ","s});
        auto suffix = uset({":"s, "|->"s});
        bool is_key = !is_first && ( *tla[i - 1] == "."
            || (!is_last && prefix.contains(*tla[i - 1]) && suffix.contains(*tla[i + 1]))
        );
        if (is_key) {
            continue;
        }
        // Replace `self` with `__Node(self)`.
        if (!localNames.contains(*tla[i])) {
            if (*tla[i] == "self") {
                *tla[i] = "__Node(self)";
            }
            continue;
        }
        check(
            type2localNames[type].contains(*tla[i]),
            format(
                "Identifier {} cannot be accessed by node type {}",
                *tla[i], type
            )
        );
        *tla[i] += is_cv_decl ? "[__n]" : "[__Node(self)]";
    }
}

std::string TLABuilder::uncommentProperties(const string& program) {
    auto marker = "\\* END TRANSLATION"s;
    auto end_translation = program.find(marker);
    check(
        end_translation != string::npos,
        "Cannot find the end of the translation"
    );

    auto property_pos = end_translation + marker.length();
    auto first_half = program.substr(0, property_pos);
    auto second_half = program.substr(property_pos);

    second_half = std::regex_replace(second_half, std::regex(R"!!(\\\* )!!"), "");

    return first_half + second_half;
}
