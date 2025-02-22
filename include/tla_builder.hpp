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

    auto build() -> pair<string, string>;

private:
    SpecAST* spec;
    string module_name;

    const string null = "null";
    const string all = "all";

    uset<string> globalNames;
    vector<pair<string, string*>> configs;
    vector<PropertyAST*> properties;

    uset<string> nodetypes;
    uset<string> nodes;
    umap<string, uset<string>> type2nodes;
    umap<string, uset<string>> links;
    umap<string, umap<string, string>> nexts;

    umap<string, vector<tuple<string, bool, string*>>> type2cvDecls; // type -> (name, is_const, init)
    umap<string, uset<string>> type2consts;
    umap<string, uset<string>> type2vars;
    vector<tuple<string, vector<string*>*, string*>> fns;
    umap<string, umap<string, vector<StmtAST*>*>> type2threads;


    void analyze(SpecAST* spec);
    void analyze(SectionAST* section);
    void analyze(ConfigAST* config);
    void analyze(TopologyAST* topology);
    void analyze(TypeAST* type);
    void analyze(RouteEntryAST* entry);
    void analyze(ProtocolAST* protocol);
    void analyze(PropertyAST* property);

    // Analyze constant/variable declaration.
    void analyzeCV(const string& type, bool is_const, AssignAST* assign);
    void analyzeThread(const string& type, const string& name, vector<StmtAST*>* stmts);

    void completeNexts();
    string findNext(const string& src, const string& dst,
        uset<pair<string, string>>& visited);

    string buildTLA();
    string buildCFG();
};
