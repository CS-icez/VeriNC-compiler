%option noyywrap
%option nounput
%option noinput

%{

#include <cstdlib>
#include <string>

#include "parser.hpp"

using namespace std;

%}

TLA           \$[^\$]*\$

WhiteSpace    [ \t\n\r]*
LineComment   "//".*

Identifier    [a-zA-Z_][a-zA-Z0-9_]*

%%

{TLA}           {
    auto s = new string(yytext);
    *s = s->substr(1, s->length() - 2);
    yylval.p_string = s;
    return TLA;
}

{WhiteSpace}    { }
{LineComment}   { }


"configuration" { return CONFIGURATION; }
"topology"      { return TOPOLOGY; }
"protocol"      { return PROTOCOL; }
"property"      { return PROPERTY; }
"nodetype"      { return NODETYPE; }
"node"          { return NODE; }
"link"          { return LINK; }
"--"            { return DoubleMinus; }
"route"         { return ROUTE; }
"var"           { return VAR; }
"const"         { return CONST; }
"fn"            { return FN; }
"thread"        { return THREAD; }
"temp"          { return TEMP; }
"in"            { return IN; }

"if"            { return IF; }
"elif"          { return ELIF; }
"else"          { return ELSE; }
"while"         { return WHILE; }
"break"         { return BREAK; }
"continue"      { return CONTINUE; }

{Identifier}    { yylval.p_string = new string(yytext); return IDENT; }

.               { yylval.p_string = new string(yytext); return yytext[0]; }

%%