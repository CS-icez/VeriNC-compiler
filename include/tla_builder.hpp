#pragma once
#include "ast.hpp"
#include <unordered_map>
#include <unordered_set>
using std::pair;
using std::string;
using std::vector;
template <typename K, typename V>
using umap = std::unordered_map<K, V>;
template <typename T>
using uset = std::unordered_set<T>;

class TLABuilder {
public:
    TLABuilder(SpecAST* _spec, const string& _module_name) :
        spec(_spec), module_name(_module_name) { }

    auto build() -> pair<string, string>;

private:
    SpecAST* spec;
    string module_name;

    const string null = "null";

    vector<AssignAST*> constants;
    uset<string> nodetypes;
    uset<string> nodes;
    umap<string, uset<string>> type2nodes;
    umap<string, uset<string>> links;
    umap<string, umap<string, string>> nexts;

    void analyze(SpecAST* spec);
    void analyze(SectionAST* section);
    void analyze(ConfigAST* config);
    void analyze(TopologyAST* topology);
    void analyze(TypeAST* type);
    void analyze(RouteEntryAST* entry);
    void analyze(ProtocolAST* protocol);
    void analyze(StmtAST* stmt);
    void analyze(ExpAST* exp);
    void analyze(PropertyAST* property);
    void analyze(CtlAST* ctl);

    void completeNexts();
    string findNext(const string& src, const string& dst,
        uset<pair<string, string>>& visited);

    string buildTLA(string module_name);
    string buildCFG();
};