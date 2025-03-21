#include "tla_builder.hpp"
#include <algorithm>
#include <cassert>
#include <format>
#include <ranges>
#include "debug.hpp"
using std::format;
using namespace std::string_literals;
namespace rg = std::ranges;

std::string TLABuilder::buildTLA() {
    DEBUG("Enter {}", __func__);
    string res;
    res += format("---- MODULE {} ----\n\n", module_name);
    res += "EXTENDS Integers, Sequences, FiniteSets, "
        "TLC, Bitwise, FiniteSetsExt, SequencesExt, Functions\n\n";

    DEBUG("{}: finish module header", __func__);

    res += "CONSTANTS\n";
    for (const auto& [name, tla] : configs) {
        res += format("  {},\n", name);
    }
    res[res.length() - 2] = '\n';

    DEBUG("{}: finish constants", __func__);

    res += "(* --fair algorithm main {\n";

    res += "  variables\n";
    for (const auto& [type, vec] : type2varDecls) {
        for (const auto& [name, exp, is_choice] : vec) {
            auto op = is_choice ? "\\in" : "=";
            auto type_set = toUpper(type) + "_SET";
            auto exp_s = exp.to_string();
            if (type == all) {
                res += format("    {} {} {};\n", name, op, exp_s);
            } else {
                if (is_choice) {
                    res += format("    {} \\in [{} -> ({})];\n", name, type_set, exp_s);
                } else {
                    res += format("    {} = [__n \\in {} |-> ({})];\n", name, type_set, exp_s);
                }
            }
        }
    }
    res += "\n";

    DEBUG("{}: finish variables", __func__);

    res += "  define {\n";

    for (const auto& [name, exp] : type2constDecls[all]) {
        res += format("    {} == {}\n", name, exp.to_string());
    }

    for (const auto& [type, vec] : type2constDecls) {
        if (type == all) {
            continue;
        }
        for (const auto& [name, exp] : vec) {
            auto type_set = toUpper(type) + "_SET";
            auto exp_s = exp.to_string();
            res += format(
                "    {} == [__n \\in {} |-> ({})]\n",
                name, type_set, exp_s
            );
        }
    }
    res += "\n";
    for (const auto& [name, params, exp] : fns) {
        res += format("    {}({}) == {}\n", name, join(params, ", "), exp.to_string());
    }
    res += "  }\n\n\n";

    DEBUG("{}: finish define block", __func__);

    res += buildMacros();
    res += "\n";

    DEBUG("{}: finish macros", __func__);

    for (const auto& [type, name, labels] : threads) {
        res += "\n";
        res += buildProcess(type, name, labels);
    }
    res += "} *)\n\n";

    for (const auto& [name, exp] : invariants) {
        res += format("\\* {} == {}\n", name, exp.to_string());
    }
    if (!invariants.empty()) {
        res += "\n";
    }
    for (const auto& [name, exp] : properties) {
        res += format("\\* {} == {}\n", name, exp.to_string());
    }
    if (!properties.empty()) {
        res += "\n";
    }

    res += "====\n";

    DEBUG("Exit {}", __func__);
    return res;
}

std::string TLABuilder::buildMacros() {
    DEBUG("Enter {}", __func__);
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
        "  __Assert(__net_buf[__Node(self)] # <<>>, \"Drop: empty buffer\");\n"
        "  __net_buf[__Node(self)] := Tail(@);\n"
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
        "        __net_buf[__h] := __InsertAtEnd(@, __pos, __pkt);\n"
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
        "  __Assert(__net_buf[__Node(self)] # <<>>, \"DropSend: empty buffer\");\n"
        "  with (__s = __Node(self), __h = __next_hop[__s, __pkt.dst]) {\n"
        "    either {\n"
        "      await __max_out_of_order > 0;\n"
        "      await __OutOfOrderRange(__net_buf[__h]) # {};\n"
        "      with (__pos \\in __OutOfOrderRange(__net_buf[__h])) {\n"
        "        if (__s = __h) {\n"
        "          __net_buf[__h] := Tail(__InsertAtEnd(@, __pos, __pkt));\n"
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
        "        __net_buf[__h] := @ \\o __ooo;\n"
        "      }\n"
        "    }\n"
        "    or {\n"
        "      __net_buf[__h] := @ \\o __ordered;\n"
        "    }\n"
        "  }\n"
        "}\n";
    res += add_indent(m) + "\n";

    // __DropUnicast
    m = "macro __DropUnicast(__pkts) {\n"
        "  __Assert(__net_buf[__Node(self)] # <<>>, \"DropUnicast: empty buffer\");\n"
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
        "          __net_buf[__h] := Tail(@ \\o __ooo);\n"
        "        } else {\n"
        "          __net_buf[__h] := @ \\o __ooo ||\n"
        "          __net_buf[__s] := Tail(@);\n"
        "        }\n"
        "      }\n"
        "    }\n"
        "    or {\n"
        "      if (__s = __h) {\n"
        "        __net_buf[__h] := Tail(@ \\o __ordered);\n"
        "      } else {\n"
        "        __net_buf[__h] := @ \\o __ordered ||\n"
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
        "      CASE __n \\in __unlost_dsts -> __InsertAtEnd(__net_buf[__n], __ooo[__n], __pkts[__n])\n"
        "      [] OTHER -> __net_buf[__n]\n"
        "    ];\n"
        "    __max_loss := __max_loss - Cardinality(__loss);\n"
        "    __max_out_of_order := __max_out_of_order - __PosCount(__ooo);\n"
        "  }\n"
        "}\n";
    res += add_indent(m) + "\n";

    // __DropMulticast
    m = "macro __DropMulticast(__pkt, __dsts) {\n"
        "  __Assert(__net_buf[__Node(self)] # <<>>, \"DropMulticast: empty buffer\");\n"
        "  __Assert(__dsts \\subseteq __links[__Node(self)], \"DropMulticast: invalid destinations\");\n"
        "  __Assert(__Node(self) \\notin __dsts, \"DropMulticast: self in destinations\");\n"
        "  with (\n"
        "    __pkts = [__dst \\in __dsts |-> \"dst\" :> __dst @@ __pkt],\n"
        "    __loss \\in __AllPossibleLoss(__dsts),\n"
        "    __unlost_dsts = __dsts \\ __loss,\n"
        "    __ooo \\in __AllPossibleOutOfOrder(__unlost_dsts)\n"
        "  ) {\n"
        "    __net_buf := [__n \\in NODE_SET |->\n"
        "      CASE __n \\in __unlost_dsts -> __InsertAtEnd(__net_buf[__n], __ooo[__n], __pkts[__n])\n"
        "      [] __n = __Node(self) -> Tail(__net_buf[__n])\n"
        "      [] OTHER -> __net_buf[__n]\n"
        "    ];\n"
        "    __max_loss := __max_loss - Cardinality(__loss);\n"
        "    __max_out_of_order := __max_out_of_order - __PosCount(__ooo);\n"
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
        "  __active_threads[__Node(self)] := 0;\n"
        "}\n";
    res += add_indent(m) + "\n";

    // __Print
    m = "macro __Print(__x) {\n"
        "  print __x;\n"
        "}\n";
    res += add_indent(m) + "\n";

    // __CheckCacheConsistency
    if (auto proj = &decltype(configs)::value_type::first;
        rg::find(configs, "CHECK_CACHE_CONSISTENCY", proj) != configs.end()) {
        // TODO: assumptions.
        m = "macro __CheckCacheConsistency(__pkt, __is_start) {\n"
            "  if (__pkt.op = WRITE /\\ __is_start) {\n"
            "    __num := __num + 1;\n"
            "    __reads := {};\n"
            "    __values := {};\n"
            "  }\n"
            "  else if (__pkt.op = WRITE /\\ ~__is_start) {\n"
            "    __num := __num - 1;\n"
            "    __values := __values \\cup {__pkt.value};\n"
            "  }\n"
            "  else if (__pkt.op = READ /\\ __is_start) {\n"
            "    if (__num = 0) {\n"
            "      __reads := __reads \\cup {__pkt.id};\n"
            "    }\n"
            "  }\n"
            "  else if (__pkt.op = READ /\\ ~__is_start) {\n"
            "    if (__pkt.id \\in __reads) {\n"
            "      __Assert(__pkt.value \\in __values, \"CheckCacheConsistency: consistency violation\");\n"
            "      __reads := __reads \\ {__pkt.id};\n"
            "    }\n"
            "  }\n"
            "  else {\n"
            "    __Assert(FALSE, \"CheckConsistency: unexpected packet type\");\n"
            "  }\n"
            "}\n";
        res += add_indent(m) + "\n";
    } else {
        m = "macro __CheckCacheConsistency(__pkt, __is_start) {\n"
            "  skip;\n"
            "}\n";
        res += add_indent(m) + "\n";
    }

    res.pop_back();
    DEBUG("Exit {}", __func__);
    return res;
}

std::string TLABuilder::buildProcess(const string& type, const string& name,
    const vector<LabelMeta>& label_metas) {
    DEBUG("Enter {} with name={}", __func__, name);
    string res;
    res += format(
        R"!!(  fair+ process ({} \in ({} \X {{"{}"}})) {{)!!" "\n",
        name, toUpper(type) + "_SET", name
    );
    auto end_label = format("__L_{}_End", name);
    res += buildLabels(label_metas, 4, end_label);
    res += format("  {}:\n", end_label) +
        "    if (__active_threads[__Node(self)] > 0) {\n"
        "      __active_threads[__Node(self)] := @ - 1;\n"
        "    };\n"
        "  }\n";
    DEBUG("Exit {} with name={}", __func__, name);
    return res;
}

std::string TLABuilder::buildLabels(const vector<LabelMeta>& label_metas, int indent,
    const string& end_label) {
    string res;
    for (const auto& label_meta : label_metas) {
        res += buildLabel(label_meta, indent, end_label);
    }
    return res;
}

std::string TLABuilder::buildLabel(const LabelMeta& label_meta, int indent,
    const string& end_label) {
    DEBUG("Enter {} with name={}", __func__, label_meta.name);
    assert(indent >= 4 && "Internal error: invalid indentation number");

    auto spaces = string(indent, ' ');
    auto branch_it = label_meta.branches.begin();
    auto branch_end = label_meta.branches.end();
    string res = "";

    if (!label_meta.name.starts_with(fake_label)) {
        res += format("{}{}:\n", string(indent - 2, ' '), label_meta.name);
    }

    if (!label_meta.name.starts_with(fake_label) && label_meta.stmts.front()->rule != StmtAST::While) {
        res += format(
            "{}if (__active_threads[__Node(self)] <= 0) {{ goto {}; }};\n",
            spaces, end_label
        );
        res += format("{}else {{\n", spaces);
        indent += 2;
        spaces = string(indent, ' ');
    }

    // TODO: This is wrong.
    // Introduce a `with` block to declare temporary values.
    if (!label_meta.temps.empty()) {
        res += format("{}with (\n", spaces);
        for (const auto& [name, exp, is_choice] : label_meta.temps) {
            res += format(
                "{}  {} {} {},\n",
                spaces, name, is_choice ? "\\in" : "=", exp.to_string()
            );
        }
        res += spaces + ") {\n";
        indent += 2;
        spaces = string(indent, ' ');
    }

    auto assigns_involve_recv = [](const auto& assigns) {
        return rg::any_of(assigns, [](const auto& ptr) {
            auto exp = ptr->exp;
            return exp->rule == ExpAST::PrimCall && *exp->fn_name == "__Receive";
        });
    };
    auto wait_net_buf = "__Wait(__net_buf[__Node(self)] # <<>>);\n"s;

    for (const auto& stmt : label_meta.stmts) {
        switch (stmt->rule) {
            case StmtAST::Breakpoint:
                assert(*stmt->name == label_meta.name && "Internal error: label name mismatch");
                break;
            case StmtAST::Assign: {
                vector<string> assigns;
                auto has_recv = assigns_involve_recv(*stmt->assigns);
                if (has_recv) {
                    res += spaces + wait_net_buf;
                }
                for (const auto& assign : *stmt->assigns) {
                    auto lhs = *assign->ident;
                    if (localNames.contains(lhs)) {
                        lhs += "[__Node(self)]";
                    }
                    if (assign->vec_keys != nullptr) {
                        for (const auto& keys : *assign->vec_keys) {
                            lhs += format("[{}]", exp_t::to_string(*keys));
                        }
                    }
                    auto rhs = exp_t::to_string(*assign->exp);
                    assigns.push_back(format("{} := {}", lhs, rhs));
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
                auto args = exp_t::to_string(*stmt->exps);
                if (name == "__Receive") {
                    res += spaces + wait_net_buf;
                }
                res += format("{}{}({});\n", spaces, name, args);
                if (label_meta.has_sendlike == false && name == "__Receive") {
                    res += format("{}__Drop();\n", spaces);
                }
                break;
            }
            case StmtAST::Temp: {
                if (assigns_involve_recv(*stmt->assigns)) {
                    res += spaces + wait_net_buf;
                    if (label_meta.has_sendlike == false) {
                        res += format("{}__Drop();\n", spaces);
                    }
                }
                break;
            }
            case StmtAST::If:
                res += format("{}if ({}) {{\n", spaces, exp_t::to_string(*stmt->exp));
                res += buildLabels(*branch_it++, indent + 2, end_label);
                res += format("{}}};\n", spaces);
                if (stmt->vec_elif_exp != nullptr) {
                    for (size_t i = 0; i < stmt->vec_elif_exp->size(); ++i) {
                        auto elif_exp = (*stmt->vec_elif_exp)[i];
                        res += format(
                            "{}else if ({}) {{\n",
                            spaces, exp_t::to_string(*elif_exp)
                        );
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
                res += format(
                    "{}while({}) {{\n",
                    spaces, exp_t::to_string(*stmt->exp)
                );
                res += format(
                    "{}  if (__active_threads[__Node(self)] <= 0) {{ goto {}; }};\n",
                    spaces, end_label
                );
                res += format("{}  else {{\n", spaces);
                res += buildLabels(*branch_it++, indent + 4, end_label);
                res += format("{}  }};\n", spaces);
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

    // Close the `with` block.
    if (!label_meta.temps.empty()) {
        indent -= 2;
        spaces = string(indent, ' ');
        res += spaces + "}\n";
    }

    if (!label_meta.name.starts_with(fake_label) && label_meta.stmts.front()->rule != StmtAST::While) {
        indent -= 2;
        spaces = string(indent, ' ');
        res += spaces + "};\n";
    }

    DEBUG("Exit {} with name={}", __func__, label_meta.name);
    return res;
}

std::string TLABuilder::buildCFG() {
    DEBUG("Enter {}", __func__);
    string res;
    res += "SPECIFICATION Spec\n";
    res += "CONSTANTS\n";
    for (const auto& [name, exp] : configs) {
        res += format("  {} = {}\n", name, exp.to_string());
    }
    if (!invariants.empty()) {
        res += "INVARIANTS\n";
        for (const auto& [name, exp] : invariants) {
            res += format("  {}\n", name);
        }
    }
    if (!properties.empty()) {
        res += "PROPERTIES\n";
        for (const auto& [name, exp] : properties) {
            res += format("  {}\n", name);
        }
    }
    // TODO: symmetry.
    DEBUG("Exit {}", __func__);
    return res;
}