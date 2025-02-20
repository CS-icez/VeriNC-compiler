%glr-parser
%define parse.error detailed

%code requires {

#include <cstdio>
#include <iostream>
#include <string>
#include <vector>
#include <algorithm>
#include <utility>
#include "ast.hpp"
#include "make_ast.hpp"

using namespace std;

using PA_E_VS = pair<ExpAST*, vector<StmtAST*>*>;
using PA_VE_VVS = pair<vector<ExpAST*>*, vector<vector<StmtAST*>*>*>;

int yylex();
void yyerror(SpecAST*&, const char* s);

}

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
    vector<ExpAST*>*        vec_Exp;
    vector<PropertyAST*>*   vec_Property;
    vector<TypeAST*>*       vec_Type;
    vector<AssignAST*>*     vec_Assign;
    vector<string*>*        vec_ident;

    vector<vector<string*>*>* vv_ident;

    PA_E_VS*   pa_E_vS;
    PA_VE_VVS* pa_vE_vvS;
}

%token <p_string> IDENT TLA
%token <p_string> CONFIGURATION TOPOLOGY PROTOCOL PROPERTY NODETYPE LINK DoubleMinus
    ROUTE VAR CONST FN THREAD TEMP
%token <p_string> IF ELIF ELSE WHILE BREAK CONTINUE

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
%type <vec_Exp>        Exps OptExps
%type <vec_Property>   Properties
%type <vec_Type>       Types
%type <vec_Assign>     Assigns
%type <vec_ident>      Idents
%type <vv_ident>       Links

%type <pa_vE_vvS> OptElifs Elifs
%type <pa_E_vS>   Elif
%type <vec_Stmt>  OptElse

%%

Spec
    : Sections { $$ = make_ast<SpecAST>($1); }
    ;

Sections
    : Sections Section { $1->push_back($2); $$ = $1; }
    | Section { $$ = make_vec($1); }
    ;

Section
    : CONFIGURATION '{' Configs '}' { $$ = make_ast<SectionAST>(SectionAST::Configuration, $3, n3); }
    | TOPOLOGY '{' Topologies '}' { $$ = make_ast<SectionAST>(SectionAST::Topology, n1, $3, n2); }
    | PROTOCOL '{' Protocols '}' { $$ = make_ast<SectionAST>(SectionAST::Protocol, n2, $3, n1); }
    | PROPERTY '{' Properties '}' { $$ = make_ast<SectionAST>(SectionAST::Property, n3, $3); }
    ;

Configs
    : Configs Config { $1->push_back($2); $$ = $1; }
    | Config { $$ = make_vec($1); }

Config
    : Assign ';' { $$ = make_ast<ConfigAST>($1); }
    ;

Assign
    : IDENT '=' Exp { $$ = make_ast<AssignAST>($1, n1, $3); }
    | IDENT '[' Exps ']' '=' Exp { $$ = make_ast<AssignAST>($1, $3, $6); }
    ;

Topologies
    : Topologies Topology { $1->push_back($2); $$ = $1; }
    | Topology { $$ = make_vec($1); }
    ;

Topology
    : NODETYPE Types ';' { $$ = make_ast<TopologyAST>(TopologyAST::NodeType, $2, n5); }
    | Type Idents ';' { $$ = make_ast<TopologyAST>(TopologyAST::Node, n1, $1, $2, n3); }
    | LINK Links ';' { $$ = make_ast<TopologyAST>(TopologyAST::Link, n3, $2, n2); }
    | ROUTE '(' Idents ')' '{' RouteEntries '}' { $$ = make_ast<TopologyAST>(TopologyAST::Route, n4, $3, $6); }
    ;

Types 
    : Types ',' Type { $1->push_back($3); $$ = $1; }
    | Type { $$ = make_vec($1); }
    ;

Idents
    : Idents ',' IDENT { $1->push_back($3); $$ = $1; }
    | IDENT { $$ = make_vec($1); }
    ;

Type
    : IDENT { $$ = make_ast<TypeAST>($1); }
    ;

Links
    : Links DoubleMinus Idents { $1->push_back($3); $$ = $1; }
    | Idents { $$ = make_vec($1); }

RouteEntries
    : RouteEntries RouteEntry { $1->push_back($2); $$ = $1; }
    | RouteEntry { $$ = make_vec($1); }
    ;

RouteEntry
    : Idents ':' IDENT ';' { $$ = make_ast<RouteEntryAST>($1, $3); }
    ;

Protocols
    : Protocols Protocol { $1->push_back($2); $$ = $1; }
    | Protocol { $$ = make_vec($1); }
    ;

Protocol
    : VAR '(' Type ')' Assigns ';' { $$ = make_ast<ProtocolAST>(ProtocolAST::Var, $3, $5, n4); }
    | CONST '(' Type ')' Assigns ';' { $$ = make_ast<ProtocolAST>(ProtocolAST::Const, $3, $5, n4); }
    | FN IDENT '(' Idents ')' '=' Exp ';' { $$ = make_ast<ProtocolAST>(ProtocolAST::Fn, n2, $2, $4, $7, n1); }
    | THREAD '(' Type ')' IDENT '{' Stmts '}' { $$ = make_ast<ProtocolAST>(ProtocolAST::Thread, $3, n1, $5, n2, $7); }
    ;

Stmts
    : Stmts Stmt { $1->push_back($2); $$ = $1; }
    | Stmt { $$ = make_vec($1); }
    ;

Stmt
    : IDENT ':' { $$ = make_ast<StmtAST>(StmtAST::Breakpoint, $1, n7); }
    | Assigns ';' { $$ = make_ast<StmtAST>(StmtAST::Assign, n2, $1, n5); }
    | ';' { $$ = make_ast<StmtAST>(StmtAST::Null, n8); }
    | IDENT '(' OptExps ')' ';' { $$ = make_ast<StmtAST>(StmtAST::PrimCall, $1, $3, n6); }
    | TEMP Assigns ';' { $$ = make_ast<StmtAST>(StmtAST::Temp, n2, $2, n5); }
    | IF '(' Exp ')' '{' Stmts '}' OptElifs OptElse {
        if ($8 != nullptr) {
            $$ = make_ast<StmtAST>(StmtAST::If, n3, $3, $6, $8->first, $8->second, $9);
            delete $8;
        } else {
            $$ = make_ast<StmtAST>(StmtAST::If, n3, $3, $6, n2, $9);
        }
    }
    | WHILE '(' Exp ')' '{' Stmts '}' { $$ = make_ast<StmtAST>(StmtAST::While, n3, $3, $6, n3); }
    | BREAK ';' { $$ = make_ast<StmtAST>(StmtAST::Break, n8); }
    | CONTINUE ';' { $$ = make_ast<StmtAST>(StmtAST::Continue, n8); }
    ;

OptElifs
    : Elifs { $$ = $1; }
    | %empty { $$ = nullptr; }
    ;

Elifs
    : Elifs Elif {
        $1->first->push_back($2->first);
        $1->second->push_back($2->second);
        $$ = $1;
        delete $2;
    }
    | Elif {
        $$ = make_ast<PA_VE_VVS>(make_vec($1->first), make_vec($1->second));
        delete $1;
    }
    ;

Elif
    : ELIF '(' Exp ')' '{' Stmts '}' { $$ = make_ast<PA_E_VS>($3, $6); }
    ;

OptElse
    : ELSE '{' Stmts '}' { $$ = $3; }
    | %empty { $$ = nullptr; }
    ;

Assigns 
    : Assigns ',' Assign { $1->push_back($3); $$ = $1; }
    | Assign { $$ = make_vec($1); }
    ;

OptExps
    : Exps { $$ = $1; }
    | %empty { $$ = nullptr; }
    ;

Exps
    : Exps ',' Exp { $1->push_back($3); $$ = $1; }
    | Exp { $$ = make_vec($1); }
    ;

Exp
    : IDENT '(' OptExps ')' { $$ = make_ast<ExpAST>(ExpAST::PrimCall, $1, $3, n1); }
    | TLA { $$ = make_ast<ExpAST>(ExpAST::TLA, n2, $1); }
    ;

Properties
    : Properties Property { $1->push_back($2); $$ = $1; }
    | Property { $$ = make_vec($1); }
    ;

Property
    : IDENT '=' Ctl ';' { $$ = make_ast<PropertyAST>($1, $3); }
    ;

Ctl 
    : Exp { $$ = make_ast<CtlAST>($1); }
    ;

%%

void yyerror(SpecAST*&, const char* s) {
    extern int yylineno;
    extern char *yytext;

    fprintf(stderr, "Error at line %d: %s\n", yylineno, s);
    fprintf(stderr, "Near token: '%s'\n", yytext);
    fprintf(stderr, "error: %s\n", s);
}
