#pragma once
#include "ast.hpp"
#include <algorithm>
#include <cassert>
#include <iostream>
#include <ranges>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include "debug.hpp"

struct TriBool {
    enum TriBoolValue { True, False, Both } value;
    
    TriBool() : value(False) { }
    explicit TriBool(TriBoolValue _value) : value(_value) { }
    TriBool(bool _value) : value(_value ? True : False) { }
    TriBool& operator=(bool _value) {
        value = _value ? True : False;
        return *this;
    }

    bool operator==(TriBool _value) const {
        return value == _value.value;
    }
    bool operator==(bool _value) const {
        return *this == TriBool(_value);
    }
    operator bool() const = delete;

    bool is_both() const {
        return value == Both;
    }

    TriBool operator&(const TriBool& other) const {
        if (value == True || other.value == True) {
            return TriBool(True);
        } else if (value == False && other.value == False) {
            return TriBool(False);
        } else {
            return TriBool(Both);
        }
    }
    TriBool& operator&=(const TriBool& other) {
        return *this = *this & other;
    }
    TriBool operator|(const TriBool& other) const {
        return value == other.value ? TriBool(value) : TriBool(Both);
    }
    TriBool& operator|=(const TriBool& other) {
        return *this = *this | other;
    }

    friend std::ostream& operator<<(std::ostream& os, const TriBool& obj) {
        switch (obj.value) {
            case True: os << "true"; break;
            case False: os << "false"; break;
            case Both: os << "both"; break;
        }
        return os;
    }
};

template <typename R>
concept StringRange = std::ranges::input_range<R>
    && (std::same_as<std::ranges::range_value_t<R>, std::string*>
        || std::same_as<std::ranges::range_value_t<R>, std::string>);

template <StringRange R>
string join(R&& range, const string& sep) {
    using elem_t = std::ranges::range_value_t<R>;
    string res;
    bool is_first = true;
    for (const auto& s : range) {
        if (!is_first) {
            res += sep;
        }
        is_first = false;
        if constexpr (std::same_as<elem_t, std::string*>) {
            res += *s;
        } else {
            res += s;
        }
    }
    return res;
}

class exp_t {
    using tla_t = const std::vector<std::string*>*;
public:
    exp_t(const std::string& _value) : value(_value) { }
    exp_t(tla_t _value) : value(_value) { }

    bool operator==(const string& s) const {
        if (std::holds_alternative<std::string>(value)) {
            return std::get<std::string>(value) == s;
        } else {
            const auto& vec = *std::get<tla_t>(value);
            return vec.size() == 1 && *vec[0] == s;
        }
    }

    std::string to_string() const {
        // DEBUG("Enter {}", __func__);
        if (std::holds_alternative<std::string>(value)) {
            // DEBUG("{}: is string", __func__);
            // DEBUG("Exit {}", __func__);
            return std::get<std::string>(value);
        }
        // DEBUG("{}: is TLA", __func__);
        const auto& vec = *std::get<tla_t>(value);
        string res;
        auto is_ident = [](const string& s) {
            return s.size() > 0 && !std::isdigit(s[0])
                && std::ranges::all_of(s, [](char c) {
                    return std::isalnum(c) || c == '_';
                });
        };

        for (size_t i = 0; i < vec.size(); ++i) {
            // DEBUG_EXP(i);
            const auto& curr = *vec[i];
            // DEBUG_EXP(curr);
            const auto& prev = i > 0 ? *vec[i - 1] : "";
            // DEBUG_EXP(prev);
            const auto& next = i + 1 == vec.size() ? "" : *vec[i + 1];
            // Exception rules:
            //   1. ident.field
            //   2. (exp)
            //   3. [exp]
            //   4. {exp}
            //   5. exp, exp
            //   6. ~exp
            //   7. ident[exp] or ident(exp)
            //   8. <= or >= or ==
            //   9. :>
            //  10. =>
            //  11. [exp][exp]
            bool add_space = (i > 0)
                && prev != "." && curr != "."
                && prev != "(" && curr != ")"
                && prev != "[" && curr != "]"
                && prev != "{" && curr != "}"
                && curr != ","
                && prev != "~"
                && !(is_ident(prev) && (curr == "[" || curr == "(") && next != "]")
                && !((prev == "<" || prev == ">" || prev == "=") && curr == "=")
                && !(prev == ":" && curr == ">")
                && !(prev == "=" && curr == ">")
                && !(prev.ends_with("]") && curr == "[" && next != "]");
            if (add_space) {
                res += " ";
            }
            res += curr;
        }
        // DEBUG_EXP(vec);
        // DEBUG_EXP(res);
        // DEBUG("Exit {}", __func__);
        return res;
    }

    static std::string to_string(const ExpAST& exp) {
        assert(exp.rule == ExpAST::TLA
            && "Internal error: calling exp_t::to_string on non-TLA expression");
        return exp_t(exp.tla).to_string();
    }

    static std::string to_string(const vector<ExpAST*>& exps) {
        auto f = [](const ExpAST* exp) { return to_string(*exp); };
        return join(exps | std::views::transform(f), ", ");
    }

private:
    std::variant<std::string, tla_t> value;
};

namespace std {
    template <>
    struct formatter<TriBool> {
        constexpr auto parse(format_parse_context& ctx) {
            return ctx.begin();
        }
        
        auto format(const TriBool& tb, format_context& ctx) const {
            auto out = ctx.out();
            switch (tb.value) {
                case TriBool::True: out = format_to(out, "true"); break;
                case TriBool::False: out = format_to(out, "false"); break;
                case TriBool::Both: out = format_to(out, "both"); break;
            }
            return out;
        }
    };
}

class TLABuilder {
    // `using std::pair` is not allowed in class scope,
    // but use it in namespace scope of a header file
    // will expose names to files that `include` it,
    // which I try to avoid.
    template <typename T1, typename T2>
    using pair = std::pair<T1, T2>;
    using string = std::basic_string<char>;
    template <typename... Ts>
    using tuple = std::tuple<Ts...>;
    template <typename T>
    using vector = std::vector<T>;
    template <typename K, typename V>
    using umap = std::unordered_map<K, V>;
    template <typename T>
    using uset = std::unordered_set<T>;

public:
    TLABuilder(SpecAST* _spec, const string& _module_name) :
        spec(_spec), module_name(_module_name) { }
    ~TLABuilder() { delete spec; }

    auto build() -> pair<string, string>;
    static string uncommentProperties(const string& program);

private:
    SpecAST* spec;
    string module_name;

    static inline const string null = "null";
    static inline const string all = "all";
    static inline const string fake_label = "__$fake_label";

    // https://github.com/DistributedPlusCal/DistributedPlusCal/blob/master/tlatools/TLA%2B%20Tools/pcal/PlusCal.tla
    // This list is not actually complete. There are other reserved words like `Init`.
    static inline const uset<string> tla_reserved{
        "ASSUME", "ASSUMPTION", "AXIOM", "CASE", "CHOOSE",
        "CONSTANT", "CONSTANTS", "DOMAIN", "ELSE", "ENABLED",
        "EXCEPT", "EXTENDS", "IF", "IN", "INSTANCE", "LET",
        "LOCAL", "MODULE", "OTHER", "UNION", "SUBSET", "THEN",
        "THEOREM", "UNCHANGED", "VARIABLE", "VARIABLES", 
        "WITH", "WF_", "SF_",
        "assert", "begin", "call", "do", "either", "else",
        "elsif", "end", "goto", "if", "macro", "or",
        "print", "procedure", "process", "fair", "return", "skip",
        "then", "variable", "variables", "while", "with",
        "Done", "Error"
    };
    static inline const uset<string> our_reserved{
        "all", "null",
        "configuration", "topology", "protocol", "property",
        "nodetype", "node", "link", "route",
        "var", "const", "fn", "thread",
        "temp", "if", "elif", "else", "while", "break", "continue",
        "send", "unicast", "multicast", "receive",
        "wait", "exit", "assert", "print",
        "forall", "exists", "in", "let",
        "self"
    };

    uset<string> names;
    uset<string> localNames;
    umap<string, uset<string>> type2localNames;
    // (name, exp)
    vector<pair<string, exp_t>> configs;
    // (name, exp)
    vector<pair<string, exp_t>> invariants;
    // (name, exp)
    vector<pair<string, exp_t>> properties;

    uset<string> nodetypes;
    uset<string> nodes;
    vector<string> nodes_in_order;
    umap<string, vector<string>> type2nodes;
    umap<string, uset<string>> links;
    // src -> (dst -> next)
    umap<string, umap<string, string>> nexts;

    // type -> (name, init)
    umap<string, vector<tuple<string, exp_t>>> type2constDecls;
    // type -> (name, init, is_choice)
    umap<string, vector<tuple<string, exp_t, bool>>> type2varDecls;
    umap<string, uset<string>> type2constNames;
    umap<string, uset<string>> type2varNames;
    // (name, params, exp)
    vector<tuple<string, vector<string>, exp_t>> fns;

    struct LabelMeta {
        string name;
        vector<StmtAST*> stmts;
        vector<vector<LabelMeta>> branches;
        vector<tuple<string, exp_t, bool>> temps; // is_choice
        TriBool has_recv;
        TriBool has_sendlike;
    };
    // (type, name, labels)
    vector<tuple<string, string, vector<LabelMeta>>> threads;


    static void check(bool cond, const string& msg);
    void addNewName(const string& name, bool is_user_defined = true);

    void analyze(SpecAST* spec);
    void analyze(ConfigAST* config);
    void analyze(TopologyAST* topology);
    void analyze(ProtocolAST* protocol);
    void analyze(PropertyAST* property);

    // Analyze constant/variable declaration.
    void analyzeCV(const string& type, bool is_const, AssignAST* assign);
    // void analyzeMacro(const string& name, const vector<string>& params,
    //     vector<StmtAST*>& stmts);
    void analyzeThread(const string& type, const string& name,
        vector<StmtAST*>& stmts);
    
    struct PathMeta {
        TriBool has_recv;
        TriBool has_sendlike;
        TriBool has_exit;
        TriBool has_effect;
        TriBool branch_has_label;

        PathMeta& operator&=(const PathMeta& other) {
            has_recv &= other.has_recv;
            has_sendlike &= other.has_sendlike;
            has_exit &= other.has_exit;
            has_effect &= other.has_effect;
            branch_has_label &= other.branch_has_label;
            return *this;
        }

        PathMeta& operator|=(const PathMeta& other) {
            has_recv |= other.has_recv;
            has_sendlike |= other.has_sendlike;
            has_exit |= other.has_exit;
            has_effect |= other.has_effect;
            branch_has_label |= other.branch_has_label;
            return *this;
        }
    };
    auto analyzeThreadStmts(const string& type, vector<StmtAST*>& stmts)
        -> vector<LabelMeta>;
    PathMeta analyzeIfStmt(const string& type, StmtAST& stmt, PathMeta path,
        LabelMeta& label_meta);
    PathMeta analyzeWhileStmt(const string& type, StmtAST& stmt, PathMeta path,
        LabelMeta& label_meta);
    auto analyzeBranch(const string& type, vector<StmtAST*>& stmts, 
        PathMeta path, bool has_temp) -> pair<PathMeta, vector<LabelMeta>>;
    void analyzeAssignStmt(const string& type, vector<AssignAST*>& assigns,
        PathMeta& path);
    void analyzePrimCallStmt(const string& type, string& name,
        vector<ExpAST*>& args, PathMeta& path);
    void analyzeReceiveCall(const string& type, string& name,
        vector<ExpAST*>& args, PathMeta& path);
    bool analyzeTempStmt(const string& type, vector<AssignAST*>& assigns,
        decltype(LabelMeta::temps)& temps, PathMeta& path);

    void completeNexts();
    string findNext(const string& src, const string& dst,
        uset<pair<string, string>>& visited);

    void addOurConstants();
    void addOurVariables();
    void addOurFns();
    // void addOurMacros();
    void addOurProperties();

    void mangleTLA(const string& type, const vector<string*>& tla,
        bool is_cv_decl = false);

    string buildTLA();
    string buildCFG();

    string buildMacros();
    string buildProcess(const string& type, const string& name,
        const vector<LabelMeta>& label_metas);
    string buildLabels(const vector<LabelMeta>& label_metas, int indent,
        const string& end_label);
    string buildLabel(const LabelMeta& label_meta, int indent,
        const string& end_label);

    string toUpper(const string& str);
};
