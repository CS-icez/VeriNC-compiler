//! Raw pointers rather than smart pointers are used throughout the AST
//! to get rid of the complex C++ type system.
//! I believe I have perfectly managed these pointers, but even if I didn't,
//! all heap memory will be released after the program terminates, and AST
//! does not involve other resources such as files or network connections.

#pragma once
#include <algorithm>
#include <ranges>
#include <string>
#include <vector>
#include "debug.hpp"

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
static void delete_if(const std::vector<T*>* vec) {
    if (vec == nullptr) {
        return;
    }
    std::ranges::for_each(*vec, [](const T* elem) { delete_if(elem); });
    delete vec;
}

template <typename T>
static T* deep_copy(const T* ptr) {
    if (ptr == nullptr) {
        return nullptr;
    }
    return new T(*ptr);
}

template <typename T>
static std::vector<T*>* deep_copy(const std::vector<T*>* vec) {
    if (vec == nullptr) {
        return nullptr;
    }
    auto res = new std::vector<T*>();
    res->reserve(vec->size());
    std::ranges::transform(*vec, std::back_inserter(*res),
        [](const T* elem) { return deep_copy(elem); });
    return res;
}

struct SpecAST {
    std::vector<SectionAST*>* sections;

    explicit SpecAST(std::vector<SectionAST*>* _sections) : sections(_sections) { }
    ~SpecAST() { delete_if(sections); }
    SpecAST(const SpecAST&) = delete;
    SpecAST& operator=(const SpecAST&) = delete;
};

struct SectionAST {
    enum Rule {
        Configuration,
        Topology,
        Protocol,
        Property,
    } rule;
    std::vector<ConfigAST*>*   configs;
    std::vector<TopologyAST*>* topologies;
    std::vector<ProtocolAST*>* protocols;
    std::vector<PropertyAST*>* properties;

    SectionAST(Rule _rule, std::vector<ConfigAST*>* _configs,
        std::vector<TopologyAST*>* _topologies, std::vector<ProtocolAST*>* _protocols,
        std::vector<PropertyAST*>* _properties) :
        rule(_rule), configs(_configs), topologies(_topologies),
            protocols(_protocols), properties(_properties) { }
    ~SectionAST() {
        delete_if(configs);
        delete_if(topologies);
        delete_if(protocols);
        delete_if(properties);
    }
    SectionAST(const SectionAST&) = delete;
    SectionAST& operator=(const SectionAST&) = delete;
};

struct ConfigAST {
    AssignAST* assign;

    explicit ConfigAST(AssignAST* _assign) : assign(_assign) { }
    ~ConfigAST() { delete_if(assign); }
    ConfigAST(const ConfigAST&) = delete;
    ConfigAST& operator=(const ConfigAST&) = delete;
};

// TODO: support ident[key1][key2] = exp
struct AssignAST {
    std::string* ident;
    std::vector<ExpAST*>* keys;
    ExpAST* exp;
    bool is_choice;

    AssignAST(std::string* _ident, std::vector<ExpAST*>* _keys, ExpAST* _exp, bool _is_choice) :
        ident(_ident), keys(_keys), exp(_exp), is_choice(_is_choice) { }
    ~AssignAST() {
        delete_if(ident);
        delete_if(keys);
        delete_if(exp);
    }

    AssignAST(const AssignAST& other) {
        ident = deep_copy(other.ident);
        keys = deep_copy(other.keys);
        exp = deep_copy(other.exp);
        is_choice = other.is_choice;
    }
    // Copy-and-swap idiom.
    AssignAST& operator=(AssignAST other) {
        swap(*this, other);
        return *this;
    }
    AssignAST(AssignAST&&) = default;
    AssignAST& operator=(AssignAST&&) = default;

    friend void swap(AssignAST& a, AssignAST& b) {
        using std::swap;
        swap(a.ident, b.ident);
        swap(a.keys, b.keys);
        swap(a.exp, b.exp);
        swap(a.is_choice, b.is_choice);
    }
};

struct TopologyAST {
    enum Rule {
        NodeType,
        Node,
        Link,
        Route,
    } rule;
    std::vector<TypeAST*>* types;
    TypeAST* type;
    std::vector<AssignAST*>* nodes;
    std::vector<std::vector<std::string*>*>* vec_nodes;
    std::vector<std::string*>* srcs;
    std::vector<RouteEntryAST*>* entries;

    TopologyAST(Rule _rule, std::vector<TypeAST*>* _types, TypeAST* _type,
        std::vector<AssignAST*>* _nodes, std::vector<std::vector<std::string*>*>* _vec_nodes,
        std::vector<std::string*>* _srcs, std::vector<RouteEntryAST*>* _entries) :
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
    TopologyAST(const TopologyAST&) = delete;
    TopologyAST& operator=(const TopologyAST&) = delete;
};

struct TypeAST {
    std::string* ident;

    explicit TypeAST(std::string* _ident) : ident(_ident) { }
    ~TypeAST() { delete_if(ident); }
    TypeAST(const TypeAST&) = delete;
    TypeAST& operator=(const TypeAST&) = delete;
};

struct RouteEntryAST {
    std::vector<std::string*>* dsts;
    std::string* next;

    RouteEntryAST(std::vector<std::string*>* _dsts, std::string* _next) : dsts(_dsts), next(_next) { }
    ~RouteEntryAST() { delete_if(dsts); delete_if(next); }
    RouteEntryAST(const RouteEntryAST&) = delete;
    RouteEntryAST& operator=(const RouteEntryAST&) = delete;
};

struct ProtocolAST {
    enum Rule { Var, Const, Fn, Macro, Thread } rule;
    TypeAST* type;
    std::vector<AssignAST*>* assigns;
    std::string* name;
    std::vector<std::string*>* params;
    ExpAST* exp;
    std::vector<StmtAST*>* stmts;

    ProtocolAST(Rule _rule, TypeAST* _type, std::vector<AssignAST*>* _assigns,
        std::string* _name, std::vector<std::string*>* _params, ExpAST* _exp, std::vector<StmtAST*>* _stmts) :
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
    ProtocolAST(const ProtocolAST&) = delete;
    ProtocolAST& operator=(const ProtocolAST&) = delete;
};

struct StmtAST {
    enum Rule {
        Breakpoint,
        Assign,
        Null,
        PrimCall,
        MacroCall,
        Temp,
        If,
        While, Break, Continue,
    } rule;
    std::string* name;
    std::vector<ExpAST*>* exps;
    std::vector<AssignAST*>* assigns;
    ExpAST* exp;
    std::vector<StmtAST*>* stmts;
    std::vector<ExpAST*>* vec_elif_exp;
    std::vector<std::vector<StmtAST*>*>* vec_elif_stmts;
    std::vector<StmtAST*>* else_stmts;

    StmtAST(Rule _rule, std::string* _name, std::vector<ExpAST*>* _exps,
        std::vector<AssignAST*>* _assigns, ExpAST* _exp, std::vector<StmtAST*>* _stmts,
        std::vector<ExpAST*>* _vec_elif_exp, std::vector<std::vector<StmtAST*>*>* _vec_elif_stmts,
        std::vector<StmtAST*>* _else_stmts) :
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

    StmtAST(const StmtAST& other) : rule(other.rule) {
        name = deep_copy(other.name);
        exps = deep_copy(other.exps);
        assigns = deep_copy(other.assigns);
        exp = deep_copy(other.exp);
        stmts = deep_copy(other.stmts);
        vec_elif_exp = deep_copy(other.vec_elif_exp);
        vec_elif_stmts = deep_copy(other.vec_elif_stmts);
        else_stmts = deep_copy(other.else_stmts);
    }
    // Copy-and-swap idiom.
    StmtAST& operator=(StmtAST other) {
        swap(*this, other);
        return *this;
    }
    StmtAST(StmtAST&&) = default;
    StmtAST& operator=(StmtAST&&) = default;

    friend void swap(StmtAST& a, StmtAST& b) {
        using std::swap;
        swap(a.rule, b.rule);
        swap(a.name, b.name);
        swap(a.exps, b.exps);
        swap(a.assigns, b.assigns);
        swap(a.exp, b.exp);
        swap(a.stmts, b.stmts);
        swap(a.vec_elif_exp, b.vec_elif_exp);
        swap(a.vec_elif_stmts, b.vec_elif_stmts);
        swap(a.else_stmts, b.else_stmts);
    }

    StmtAST* clone() const {
        return deep_copy(this);
    }
};

struct ExpAST {
    enum Rule { PrimCall, TLA } rule;
    std::string* fn_name;
    std::vector<ExpAST*>* args;
    std::vector<std::string*>* tla;

    ExpAST(Rule _rule, std::string* _fn_name, std::vector<ExpAST*>* _args, std::vector<std::string*>* _tla) :
        rule(_rule), fn_name(_fn_name), args(_args), tla(_tla) { }

    ~ExpAST() {
        delete_if(fn_name);
        delete_if(args);
        delete_if(tla);
    }

    ExpAST(const ExpAST& other) : rule(other.rule) {
        fn_name = deep_copy(other.fn_name);
        args = deep_copy(other.args);
        tla = deep_copy(other.tla);
    }
    // Copy-and-swap idiom.
    ExpAST& operator=(ExpAST other) {
        swap(*this, other);
        return *this;
    }
    ExpAST(ExpAST&&) = default;
    ExpAST& operator=(ExpAST&&) = default;

    friend void swap(ExpAST& a, ExpAST& b) {
        using std::swap;
        swap(a.rule, b.rule);
        swap(a.fn_name, b.fn_name);
        swap(a.args, b.args);
        swap(a.tla, b.tla);
    }

    ExpAST* clone() const {
        return deep_copy(this);
    }
};

struct PropertyAST {
    std::string* ident;
    CtlAST* ctl;

    PropertyAST(std::string* _ident, CtlAST* _ctl) : ident(_ident), ctl(_ctl) { }
    ~PropertyAST() { delete_if(ident); delete_if(ctl); }
};

struct CtlAST {
    ExpAST* exp;

    explicit CtlAST(ExpAST* _exp) : exp(_exp) { }
    ~CtlAST() { delete_if(exp); }
};
