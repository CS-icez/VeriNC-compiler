%glr-parser

%{

#include <string>
#include <vector>
#include <algorithm>
#include "ast.hpp"

int yylex();

using namespace std;

%}

%parse-param { SpecAST*& ast }

%union {
    string*        p_string;
    SpecAST*       p_Spec;
    SectionAST*    p_Section;
    ConfigAST*     p_Config;
    AssignAST*     p_Assign;
    TopologyAST*   p_Topology;
    TypeAST*       p_Type;
    RouteEntryAST* p_RouteEntry;
    ProtocolAST*   p_Protocol;
    StmtAST*       p_Stmt;
    ExpAST*        p_Exp;
    PropertyAST*   p_Property;
    CtlAST*        p_Ctl;
    
    vector<SectionAST*>*    vec_Section;
    vector<ConfigAST*>*     vec_Config;
    vector<TopologyAST*>*   vec_Topology;
    vector<RouteEntryAST*>* vec_RouteEntry;
    vector<ProtocolAST*>*   vec_Protocol;
    vector<StmtAST*>*       vec_Stmt;
    vector<PropertyAST*>*   vec_Property;
    vector<TypeAST*>*       vec_Type;
    vector<AssignAST*>*     vec_Assign;
    vector<string*>*        vec_ident;
}

%token <p_string> IDENT

%type <p_Spec>       Spec
%type <p_Section>    Section
%type <p_Config>     Config
%type <p_Assign>     Assign
%type <p_Topology>   Topology
%type <p_Type>       Type
%type <p_RouteEntry> RouteEntry
%type <p_Protocol>   Protocol
%type <p_Stmt>       Stmt
%type <p_Exp>        Exp
%type <p_Property>   Property
%type <p_Ctl>        Ctl

%type <vec_Section>    Sections
%type <vec_Config>     Configs
%type <vec_Topology>   Topologies
%type <vec_RouteEntry> RouteEntries
%type <vec_Protocol>   Protocols
%type <vec_Stmt>       Stmts
%type <vec_Property>   Properties
%type <vec_Type>       Types
%type <vec_Assign>     Assigns
%type <vec_ident>      Idents
%type <p_Topology>     Links

%%

Spec
    : Sections { ast = new SpecAST($1); }
    ;

Sections
    : Sections Section { $1->push_back($2); $$ = $1; }
    | Section { $$ = new vector<SectionAST*>{$1}; }
    ;

Section
    : CONFIGURATION '{' Configs '}' { $$ = new SectionAST(SectionAST::Configuration,
        $1, nullptr, nullptr, nullptr); }
    | TOPOLOGY '{' Topologies '}' { $$ = new SectionAST(SectionAST::Topology,
        nullptr, $1, nullptr, nullptr); }
    | PROTOCOL '{' Protocols '}' { $$ = new SectionAST(SectionAST::Protocol,
        nullptr, nullptr, $1, nullptr); }
    | PROPERTY '{' Properties '}' { $$ = new SectionAST(SectionAST::Property,
        nullptr, nullptr, nullptr, $1); }
    ;

Configs
    : Configs Config { $1->push_back($2); $$ = $1; }
    | Config { $$ = new vector<ConfigAST*>{$1}; }

Config
    : Assign ';' { $$ = new ConfigAST($1); }
    ;

Assign
    : IDENT '=' Exp { $$ = new AssignAST($1, $3); }
    ;

Topologies
    : Topologies Topology ';' { $1->push_back($2); $$ = $1; }
    | Topology ';' { $$ = new vector<TopologyAST*>{$1}; }
    ;

Topology
    : NODETYPE Types ';' { $$ = new TopologyAST(TopologyAST::NodeType,
        $2, nullptr, nullptr, nullptr, nullptr, nullptr); }
    | Type Idents ';' { $$ = new TopologyAST(TopologyAST::Type,
        nullptr, $1, $2, nullptr, nullptr, nullptr); }
    | LINK Links ';' { $$ = new TopologyAST(TopologyAST::Link,
        nullptr, nullptr, nullptr, $2, nullptr, nullptr); }
    | ROUTE '(' Idents ')' '{' RouteEntries '}' { $$ = new (TopologyAST::Route,
        nullptr, nullptr, nullptr, nullptr, $3, $6); }
    ;

Types 
    : Types ',' Type { $1->push_back($3); $$ = $1; }
    | Type { $$ = new vector<TypeAST*>{$1}; }
    ;

Idents
    : Idents ',' IDENT { $1->push_back($3); $$ = $1; }
    | IDENT { $$ = new vector<string*>{$1}; }
    ;

Type
    : IDENT { $$ = new TypeAST($1); }
    ;

Links
    : Links DoubleMinus Idents { $1->push_back($3); $$ = $1; }
    | Idents { $$ = new vector<vector<string*>>{$1}; }

RouteEntries
    : RouteEntries RouteEntry { $1->push_back($2); $$ = $1; }
    | RouteEntry { $$ = new vector<RouteEntryAST*>{$1}; }
    ;

RouteEntry
    : Idents ':' IDENT ';' { $$ = new RouteEntryAST($1, $3); }
    ;

Protocol
    : VAR '(' Type ')' Assigns ';' { $$ = new ProtocolAST(ProtocolAST::Var,
        $3, $5, nullptr, nullptr); }
    | THREAD '(' Type ')' IDENT '{' Stmts '}' { $$ = new ProtocolAST(ProtocolAST::Thread,
        $3, nullptr, $5, $7); }
    ;

Stmt
    : IDENT ':' { $$ = new StmtAST(StmtAST::Breakpoint,
        $1, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr); }
    |

%%
