#include "tla_builder.hpp"
#include <algorithm>
#include <cassert>
#include <format>
#include <ranges>
#include <regex>
#include "debug.hpp"
using std::format;
using namespace std::string_literals;
namespace rg = std::ranges;

bool TLABuilder::configContains(const string& name) const {
    auto proj = &decltype(configs)::value_type::first;
    return rg::find(configs, name, proj) != configs.end();
}

bool TLABuilder::configEnables(const string& name) const {
    auto proj = &decltype(configs)::value_type::first;
    auto it = rg::find(configs, name, proj);
    if (it != configs.end()) {
        return it->second == "TRUE" || it->second == "1";
    }
    return false;
}

void TLABuilder::addOurConstants() {
    // `null = null`
    configs.emplace_back(null, null);

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
    std::remove_reference_t<decltype(type2constDecls[all])> vec;

    for (const auto& type : nodetypes) {
        // `TYPE_SET = {node1, node2, ...}`
        auto type_set = toUpper(type) + "_SET";
        addNewName(type_set, false);
        t = "{"s + join(type2nodes[type], ", ") + "}";
        vec.emplace_back(type_set, t);

        // `TYPE_NUM = Cardinality(TYPE_SET)`
        auto type_num = toUpper(type) + "_NUM";
        addNewName(type_num, false);
        t = format("Cardinality({})", type_set);
        vec.emplace_back(type_num, t);
    }

    // `NODE_SET = {node1, node2, ...}`
    auto node_set = "NODE_SET";
    addNewName(node_set, false);
    t = string("{") + join(nodes_in_order, ", ") + "}";
    vec.emplace_back(node_set, t);

    // `MAX_LOSS = 0`
    auto max_loss = "MAX_LOSS";
    if (!configContains(max_loss)) {
        addNewName(max_loss, false);
        configs.emplace_back(max_loss, "0");
    }
    // `MAX_OUT_OF_ORDER = 0`
    auto max_out_of_order = "MAX_OUT_OF_ORDER";
    if (!configContains(max_out_of_order)) {
        addNewName(max_out_of_order, false);
        configs.emplace_back(max_out_of_order, "0");
    }
    // `MAX_DUPLICATION = 0`
    auto max_duplication = "MAX_DUPLICATION";
    if (!configContains(max_duplication)) {
        addNewName(max_duplication, false);
        configs.emplace_back(max_duplication, "0");
    }

    // `__links = src1 :> {dst1, ...} @@ ...`
    vector<string> link_entries;
    for (const auto& src : nodes_in_order) {
        link_entries.push_back(format("{} :> {{{}}}", src, join(links[src], ", ")));
    }
    vec.emplace_back("__links", join(link_entries, " @@ "));

    // `__reliable_links = {<<src1, dst1>>, ...}`
    vector<string> reliable_link_entries;
    for (const auto& [src, dst] : reliable_links) {
        reliable_link_entries.push_back(format("<<{}, {}>>", src, dst));
    }
    t = "{"s + join(reliable_link_entries, ", ") + "}";
    vec.emplace_back("__reliable_links", t);

    // `__next_hop = <<src1, dst1>> :> next1 @@ ...`
    t = "\n          ";
    auto cnt = 1;
    for (const auto& src : nodes_in_order) {
        for (const auto& dst : nodes_in_order) {
            if (cnt > 1) {
                t += " @@ ";
            }
            t += format("<<{}, {}>> :> {}", src, dst, nexts[src][dst]);
            if (cnt % 3 == 0) {
                t += "\n      ";
            }
            ++cnt;
        }
    }
    vec.emplace_back("__next_hop", t);

    auto symmetry = "SYMMETRY_REDUCTION";
    if (configEnables(symmetry)) {
        for (const auto& [type, nodes] : type2nodes) {
            if (nodes.size() > 1) {
                auto type_sym = toUpper(type) + "_SYMMETRY";
                auto type_set = toUpper(type) + "_SET";
                addNewName(type_sym, false);
                vec.emplace_back(type_sym, format("Permutations({})", type_set));
            }
        }
    }

    // Wait for `vector::insert_range` in C++23.
    type2constDecls[all].insert(type2constDecls[all].begin(), vec.begin(), vec.end());
}

void TLABuilder::addOurVariables() {
    // `__net_buf = [__n \in NODE_SET |-> <<>>]`
    type2varDecls[all].emplace_back(
        "__net_buf",
        R"!!([__n \in NODE_SET |-> <<>>])!!",
        false
    );

    // `__max_loss = MAX_LOSS`
    type2varDecls[all].emplace_back("__max_loss", "MAX_LOSS", false);

    // `__max_out_of_order = MAX_OUT_OF_ORDER`
    type2varDecls[all].emplace_back("__max_out_of_order", "MAX_OUT_OF_ORDER", false);

    // `__max_duplication = MAX_DUPLICATION`
    type2varDecls[all].emplace_back("__max_duplication", "MAX_DUPLICATION", false);

    // __flying = {}
    // __num = 0
    // __reads = {}
    // __values = {0}
    // TODO: assumptions.
    if (configEnables("CHECK_CACHE_CONSISTENCY")) {
        type2varDecls[all].emplace_back("__flying", "{}", false);
        type2varDecls[all].emplace_back("__num", "0", false);
        type2varDecls[all].emplace_back("__reads", "{}", false);
        type2varDecls[all].emplace_back("__values", "{0}", false);
    }
}

void TLABuilder::addOurFns() {
    string exp = null;

    auto add_indent = [](const string& s) {
        const string ident = "      ";
        return "\n" + ident + std::regex_replace(s, std::regex("\n"), "\n" + ident);
    };

    // `__IsReliableLink(src, dst) == <<src, dst>> \in __reliable_links`
    fns.emplace_back(
        "__IsReliableLink",
        vector<string>{"__src", "__dst"},
        "<<__src, __dst>> \\in __reliable_links"
    );

    // `__IsUnreliableLink(src, dst) == <<src, dst>> \notin __reliable_links`
    fns.emplace_back(
        "__IsUnreliableLink",
        vector<string>{"__src", "__dst"},
        "<<__src, __dst>> \\notin __reliable_links"
    );

    // `__MinPsnElem(S) == CHOOSE x \in S : \A y \in S : x.psn <= y.psn`
    fns.emplace_back(
        "__MinPsnElem",
        vector<string>{"__S"},
        R"!!(CHOOSE __x \in __S : \A __y \in __S : __x.psn <= __y.psn)!!"
    );

    // ```
    // __Set2OrderedSeq(S) ==
    //   LET
    //     RECURSIVE F(_, _)
    //     F(res, SS) == IF SS = {}
    //       THEN res
    //       ELSE LET x == __MinPsnElem(SS) IN F(Append(res, x), S \ {x})
    //   IN F(<<>>, S)
    // ```
    exp = add_indent(
        "LET\n"
        "  RECURSIVE F(_, _)\n"
        "  F(__res, __SS) == IF __SS = {}\n"
        "    THEN __res\n"
        "    ELSE LET __x == __MinPsnElem(__SS) IN F(Append(__res, __x), __SS \\ {__x})\n"
        "IN F(<<>>, __S)"
    );
    fns.emplace_back("__Set2OrderedSeq", vector<string>{"__S"}, exp);

    // `__OutOfOrderRange(seq) == 1..Len(seq)`
    fns.emplace_back(
        "__OutOfOrderRange",
        vector<string>{"__seq"},
        "1..Len(__seq)"
    );

    // `__InsertAtEnd(seq, i, elem) == InsertAt(seq, Len(seq) - i + 1, elem)`
    fns.emplace_back(
        "__InsertAtEnd",
        vector<string>{"__seq", "__i", "__elem"},
        "InsertAt(__seq, Len(__seq) - __i + 1, __elem)"
    );

    // `__Receive(n) == Head(__net_buf[n])`
    fns.emplace_back(
        "__Receive",
        vector<string>{"__n"},
        "Head(__net_buf[__n])"
    );

    // `__Node(__thread) == Head(__thread)`
    fns.emplace_back(
        "__Node",
        vector<string>{"__thread"},
        "Head(__thread)"
    );

    // `__PosCount(f) == Cardinality({x \in DOMAIN f : f[x] > 0})`
    fns.emplace_back(
        "__PosCount",
        vector<string>{"__f"},
        "Cardinality({__x \\in DOMAIN __f : __f[__x] > 0})"
    );

    // `__AllPossibleLoss(S) == {s \in SUBSET S : Cardinality(s) <= __max_loss}`
    fns.emplace_back(
        "__AllPossibleLoss",
        vector<string>{"__S"},
        "{__s \\in SUBSET __S : Cardinality(__s) <= __max_loss}"
    );

    // TODO: construct rather than filter
    // ```
    // __AllPossibleOutOfOrder(S) ==
    //   LET
    //     max_range == IF S = {} THEN {} ELSE 0..Max({Len(__net_buf[i]) : i \in S})
    //     ooo_set == [S -> max_range]
    //     ooo_set_possible == {i \in ooo_set : 
    //       /\ (\A j \in DOMAIN i : i[j] \in __OutOfOrderRange(__net_buf[j]) \cup {0})
    //       /\ __PosCount(i) <= __max_out_of_order
    //     }
    //   IN ooo_set_possible
    // ```
    exp = add_indent(
        "LET\n"
        "  __max_range == IF __S = {} THEN {} ELSE 0..Max({Len(__net_buf[__i]) : __i \\in __S})\n"
        "  __ooo_set == [__S -> __max_range]\n"
        "  __ooo_set_possible == {__i \\in __ooo_set : \n"
        "    /\\ (\\A __j \\in DOMAIN __i : __i[__j] \\in __OutOfOrderRange(__net_buf[__j]) \\cup {0})\n"
        "    /\\ __PosCount(__i) <= __max_out_of_order\n"
        "  }\n"
        "IN __ooo_set_possible"
    );
    fns.emplace_back("__AllPossibleOutOfOrder", vector<string>{"__S"}, exp);

    // `__AllPossibleSeq(S) == {seq \in [1..Cardinality(S) -> S] : IsInjective(seq)}`
    fns.emplace_back(
        "__AllPossibleSeq",
        vector<string>{"__S"},
        "{__seq \\in [1..Cardinality(__S) -> __S] : IsInjective(__seq)}"
    );

    // `__NodeTerminated(n) == __active_threads[n] <= 0`
    fns.emplace_back(
        "__NodeTerminated",
        vector<string>{"__n"},
        "__active_threads[__n] <= 0"
    );
}

void TLABuilder::addOurProperties() {
    if (configEnables("TERMINATION_CHECK")) {
        properties.emplace_back(
            "__Termination",
            R"!!(<>(\A __n \in NODE_SET : __active_threads[__n] <= 0))!!"
        );
    }
}
