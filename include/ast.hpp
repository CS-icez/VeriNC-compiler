#pragma once
#include <string>
#include <vector>
#include "debug.hpp"
using std::string;
using std::vector;

struct SpecAST;
struct SectionAST;
struct ConfigAST;
struct AssignAST;
struct TopologyAST;
struct TypeAST;
struct RouteEntryAST;
struct ProtocolAST;
struct StmtAST;
struct ExpAST;
struct PropertyAST;
struct CtlAST;

template <typename T>
static void delete_if(const T* ptr) {
    if (ptr != nullptr) {
        delete ptr;
    }
}

template <typename T>
static void delete_if(const vector<T>* vec) {
    if (vec == nullptr) {
        return;
    }
    for (auto elem : *vec) {
        delete_if(elem);
    }
    delete vec;
}

struct SpecAST {
    vector<SectionAST*>* sections;

    explicit SpecAST(vector<SectionAST*>* _sections) : sections(_sections) { }
    ~SpecAST() { delete_if(sections); }
};

struct SectionAST {
    enum Rule {
        Configuration,
        Topology,
        Protocol,
        Property,
    } rule;
    vector<ConfigAST*>*   configs;
    vector<TopologyAST*>* topologies;
    vector<ProtocolAST*>* protocols;
    vector<PropertyAST*>* properties;

    SectionAST(Rule _rule, vector<ConfigAST*>* _configs,
        vector<TopologyAST*>* _topologies, vector<ProtocolAST*>* _protocols,
        vector<PropertyAST*>* _properties) :
        rule(_rule), configs(_configs), topologies(_topologies),
            protocols(_protocols), properties(_properties) { }
    ~SectionAST() {
        delete_if(configs);
        delete_if(topologies);
        delete_if(protocols);
        delete_if(properties);
    }
};

struct ConfigAST {
    AssignAST* assign;

    explicit ConfigAST(AssignAST* _assign) : assign(_assign) { }
    ~ConfigAST() { delete_if(assign); }
};

// TODO: support ident[key1][key2] = exp
struct AssignAST {
    string* ident;
    vector<ExpAST*>* keys;
    ExpAST* exp;
    bool is_choice;

    AssignAST(string* _ident, vector<ExpAST*>* _keys, ExpAST* _exp, bool _is_choice) :
        ident(_ident), keys(_keys), exp(_exp), is_choice(_is_choice) { }
    ~AssignAST() {
        delete_if(ident);
        delete_if(keys);
        delete_if(exp);
    }
};

struct TopologyAST {
    enum Rule {
        NodeType,
        Node,
        Link,
        Route,
    } rule;
    vector<TypeAST*>* types;
    TypeAST* type;
    vector<AssignAST*>* nodes;
    vector<vector<string*>*>* vec_nodes;
    vector<string*>* srcs;
    vector<RouteEntryAST*>* entries;

    TopologyAST(Rule _rule, vector<TypeAST*>* _types, TypeAST* _type,
        vector<AssignAST*>* _nodes, vector<vector<string*>*>* _vec_nodes,
        vector<string*>* _srcs, vector<RouteEntryAST*>* _entries) :
        rule(_rule), types(_types), type(_type), nodes(_nodes),
            vec_nodes(_vec_nodes), srcs(_srcs), entries(_entries) { }

    ~TopologyAST() {
        delete_if(types);
        delete_if(type);
        delete_if(nodes);
        delete_if(vec_nodes);
        delete_if(srcs);
        delete_if(entries);
    }
};

struct TypeAST {
    string* ident;

    explicit TypeAST(string* _ident) : ident(_ident) { }
    ~TypeAST() { delete_if(ident); }
};

struct RouteEntryAST {
    vector<string*>* dsts;
    string* next;

    RouteEntryAST(vector<string*>* _dsts, string* _next) : dsts(_dsts), next(_next) { }
    ~RouteEntryAST() { delete_if(dsts); delete_if(next); }
};

struct ProtocolAST {
    enum Rule { Var, Const, Fn, Thread } rule;
    TypeAST* type;
    vector<AssignAST*>* assigns;
    string* name;
    vector<string*>* params;
    ExpAST* exp;
    vector<StmtAST*>* stmts;

    ProtocolAST(Rule _rule, TypeAST* _type, vector<AssignAST*>* _assigns,
        string* _name, vector<string*>* _params, ExpAST* _exp, vector<StmtAST*>* _stmts) :
        rule(_rule), type(_type), assigns(_assigns), name(_name),
            params(_params), exp(_exp), stmts(_stmts) { }
    ~ProtocolAST() {
        delete_if(type);
        delete_if(assigns);
        delete_if(name);
        delete_if(params);
        delete_if(exp);
        delete_if(stmts);
    }
};

struct StmtAST {
    enum Rule {
        Breakpoint,
        Assign,
        Null,
        PrimCall,
        Temp,
        If,
        While, Break, Continue,
    } rule;
    string* name;
    vector<ExpAST*>* exps;
    vector<AssignAST*>* assigns;
    ExpAST* exp;
    vector<StmtAST*>* stmts;
    vector<ExpAST*>* vec_elif_exp;
    vector<vector<StmtAST*>*>* vec_elif_stmts;
    vector<StmtAST*>* else_stmts;

    StmtAST(Rule _rule, string* _name, vector<ExpAST*>* _exps,
        vector<AssignAST*>* _assigns, ExpAST* _exp, vector<StmtAST*>* _stmts,
        vector<ExpAST*>* _vec_elif_exp, vector<vector<StmtAST*>*>* _vec_elif_stmts,
        vector<StmtAST*>* _else_stmts) :
        rule(_rule), name(_name), exps(_exps), assigns(_assigns), exp(_exp),
            stmts(_stmts), vec_elif_exp(_vec_elif_exp), vec_elif_stmts(_vec_elif_stmts),
            else_stmts(_else_stmts) { }

    ~StmtAST() {
        delete_if(name);
        delete_if(exps);
        delete_if(assigns);
        delete_if(exp);
        delete_if(stmts);
        delete_if(vec_elif_exp);
        delete_if(vec_elif_stmts);
        delete_if(else_stmts);
    }
};

struct ExpAST {
    enum Rule { PrimCall, TLA } rule;
    string* fn_name;
    vector<ExpAST*>* args;
    string* tla;

    ExpAST(Rule _rule, string* _fn_name, vector<ExpAST*>* _args, string* _tla) :
        rule(_rule), fn_name(_fn_name), args(_args), tla(_tla) { }

    ~ExpAST() {
        delete_if(fn_name);
        delete_if(args);
        delete_if(tla);
    }
};

struct PropertyAST {
    string* ident;
    CtlAST* ctl;

    PropertyAST(string* _ident, CtlAST* _ctl) : ident(_ident), ctl(_ctl) { }
    ~PropertyAST() { delete_if(ident); delete_if(ctl); }
};

struct CtlAST {
    ExpAST* exp;

    explicit CtlAST(ExpAST* _exp) : exp(_exp) { }
    ~CtlAST() { delete_if(exp); }
};
