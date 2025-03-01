#pragma once
#include "ast.hpp"
#include <iostream>
#include <ranges>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
using std::pair;
using std::string;
using std::tuple;
using std::vector;
template <typename K, typename V>
using umap = std::unordered_map<K, V>;
template <typename T>
using uset = std::unordered_set<T>;

class TLABuilder {
public:
    TLABuilder(SpecAST* _spec, const string& _module_name) :
        spec(_spec), module_name(_module_name) { }
    ~TLABuilder() { delete spec; }

    auto build() -> pair<string, string>;

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
        "then", "variable", "variables", "while", "with"
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

    vector<string> strPool;
    vector<vector<string*>> vecStrPool;

    uset<string> names;
    uset<string> localNames;
    umap<string, uset<string>> type2localNames;
    // (name, exp)
    vector<pair<string, string*>> configs;
    // (name, exp)
    vector<pair<string, string*>> invariants;
    // (name, exp)
    vector<pair<string, string*>> properties;

    uset<string> nodetypes;
    uset<string> nodes;
    vector<string> nodes_in_order;
    umap<string, vector<string>> type2nodes;
    umap<string, uset<string>> links;
    // src -> (dst -> next)
    umap<string, umap<string, string>> nexts;

    // type -> (name, init)
    umap<string, vector<tuple<string, string*>>> type2constDecls;
    // type -> (name, init)
    umap<string, vector<tuple<string, string*>>> type2varDecls;
    umap<string, uset<string>> type2constNames;
    umap<string, uset<string>> type2varNames;
    // (name, params, exp)
    vector<tuple<string, vector<string*>*, string*>> fns;

    struct LabelMeta {
        string name;
        vector<StmtAST*> stmts;
        vector<vector<LabelMeta>> branches;
        vector<pair<string, ExpAST*>> temps;
        TriBool has_recv;
        TriBool has_sendlike;
    };
    // (type, name, labels)
    vector<tuple<string, string, vector<LabelMeta>>> threads;


    void check(bool cond, const string& msg);
    void addNewName(const string& name, bool is_user_defined = true);

    void analyze(SpecAST* spec);
    void analyze(ConfigAST* config);
    void analyze(TopologyAST* topology);
    void analyze(ProtocolAST* protocol);
    void analyze(PropertyAST* property);

    // Analyze constant/variable declaration.
    void analyzeCV(const string& type, bool is_const, AssignAST* assign);
    void analyzeThread(const string& type, const string& name, vector<StmtAST*>& stmts);
    
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
    void analyzeAssignStmt(const string& type, vector<AssignAST*>& assigns);
    void analyzePrimCallStmt(const string& type, string& name,
        vector<ExpAST*>& args, PathMeta& path);
    void analyzeReceiveCall(const string& type, string& name,
        vector<ExpAST*>& args, PathMeta& path);
    bool analyzeTempStmt(const string& type, vector<AssignAST*>& assigns,
        vector<pair<string, ExpAST*>>& temps, PathMeta& path);

    void completeNexts();
    string findNext(const string& src, const string& dst,
        uset<pair<string, string>>& visited);

    void addOurConstants();
    void addOurVariables();
    void addOurFns();

    void mangleTLA(const string& type, string& tla);

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

    template <typename T>
    requires std::ranges::range<T>
        && (std::same_as<typename T::value_type, std::string*>
            || std::same_as<typename T::value_type, std::string>)
    string join(const T& container, const string& sep) {
        string res;
        bool is_first = true;
        for (const auto& s : container) {
            if (is_first) {
                is_first = false;
            } else {
                res += sep;
            }
            if constexpr (std::same_as<typename T::value_type, std::string*>) {
                res += *s;
            } else {
                res += s;
            }
        }
        return res;
    }

    string exp2str(const ExpAST& exp);
    vector<string> exps2strs(const vector<ExpAST*>& exps);
};
