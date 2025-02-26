#pragma once
#include "ast.hpp"
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
        "send", "send_m", "multicast", "receive",
        "wait", "exit", "assert", "print",
        "forall", "exists", "in", "let",
        "self"
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
    umap<string, uset<string>> type2nodes;
    umap<string, uset<string>> links;
    // src -> (dst -> next)
    umap<string, umap<string, string>> nexts;

    // type -> (name, is_const, init)
    umap<string, vector<tuple<string, bool, string*>>> type2cvDecls;
    umap<string, uset<string>> type2consts;
    umap<string, uset<string>> type2vars;
    // (name, args, exp)
    vector<tuple<string, vector<string*>*, string*>> fns;

    struct LabelMeta {
        string name;
        vector<StmtAST*> stmts;
        vector<vector<LabelMeta>> branches;
        vector<pair<string, string*>> temps;
        bool has_recv;
    };
    // type -> (name -> labels)
    umap<string, umap<string, vector<LabelMeta>>> type2threads;


    void check(bool cond, const string& msg);
    void addNewName(const string& name, bool is_user_defined = true);

    void analyze(SpecAST* spec);
    void analyze(ConfigAST* config);
    void analyze(TopologyAST* topology);
    void analyze(ProtocolAST* protocol);
    void analyze(PropertyAST* property);

    // Analyze constant/variable declaration.
    void analyzeCV(const string& type, bool is_const, AssignAST* assign);
    void analyzeThread(const string& type, const string& name, vector<StmtAST*>* stmts);
    
    struct TriBool {
        enum TriBoolValue { True, False, Both } value;
        
        TriBool() : value(False) { }
        explicit TriBool(TriBoolValue _value) : value(_value) { }
        explicit TriBool(bool _value) : value(_value ? True : False) { }
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

        static inline TriBool merge(const TriBool& a, const TriBool& b) {
            return a.value == b.value ? a : TriBool(Both);
        }
        TriBool operator&(const TriBool& other) const {
            return merge(*this, other);
        }
        TriBool& operator&=(const TriBool& other) {
            *this = merge(*this, other);
            return *this;
        }
    };

    struct PathMeta {
        TriBool has_recv;
        TriBool has_sendlike;
        TriBool has_exit;
        TriBool has_effect;
    };
    auto analyzeStmts(const string& type, vector<StmtAST*>* stmts)
        -> vector<LabelMeta>;
    auto analyzeBranch(const string& type, vector<StmtAST*>* stmts, 
        PathMeta meta) -> pair<PathMeta, vector<LabelMeta>>;
    void analyzeAssignStmt(const string& type, vector<AssignAST*>& assigns);
    void analyzePrimCallStmt(const string& type, string& name,
        vector<ExpAST*>& args, PathMeta& path);

    void completeNexts();
    string findNext(const string& src, const string& dst,
        uset<pair<string, string>>& visited);

    void addOurConstants();
    void addOurVariables();
    void addOurFns();

    void mangleTLA(const string& type, string& tla);

    string buildTLA();
    string buildCFG();
};
