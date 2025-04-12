#pragma once
#include "ast.hpp"
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include "exp_t.hpp"
#include "tribool.hpp"

namespace std {
    template <>
    struct hash<pair<string, string>> {
        size_t operator()(const pair<string, string>& p) const {
            return hash<string>()(p.first) ^ hash<string>()(p.second);
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
    uset<pair<string, string>> reliable_links;
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
    // (name, params, stmts)
    vector<tuple<string, vector<string>, vector<StmtAST*>*>> macros;

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
    void analyzeThread(const string& type, const string& name,
        vector<StmtAST*>& stmts);
    
    using MacroArgMap = umap<string, vector<string*>>;
    // Outside macro.
    vector<StmtAST*> expandMacro(const string& type, const vector<StmtAST*>& stmts);
    // Inside macro.
    vector<StmtAST*> expandMacro(const string& type, const string& name,
        const vector<StmtAST*>& stmts, const MacroArgMap& args);
    void substituteMacroParam(ExpAST& exp, const MacroArgMap& args);
    void substituteMacroParam(AssignAST& assign, const MacroArgMap& args);

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

    bool configContains(const string& name) const;
    bool configEnables(const string& name) const;

    void addOurConstants();
    void addOurVariables();
    void addOurFns();
    void addOurProperties();

    bool isIdent(const string& s);
    bool isKey(const vector<string*>& tla, size_t i);
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
