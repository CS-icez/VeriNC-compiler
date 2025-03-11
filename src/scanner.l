%option noyywrap
%option nounput
%option noinput

%{

#include <cstdlib>
#include <string>

#include "parser.hpp"

using namespace std;

%}

/* Four rules: */
/*   1. Double-quoted string. Do not allow escaped double quote in string. */
/*   2. Escaped operator (\alphas). */
/*   3. Non-escaped operator (non-alnum chars). */
/*   4. Integer. */
/* Note that `\w` and `\d` are not supported in flex. */
OTHER_TOKEN \"[^\"]*\"|\\[[:alpha:]]+|[^\"[:space:][:alnum:]_\(\)\[\]\{\},;=:]+|[[:digit:]]+

WhiteSpace  [ \t\n\r]+
LineComment "//".*
Identifier  [a-zA-Z_][a-zA-Z0-9_]*

%%

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

"send"          { yylval.p_string = new string(yytext); return PRIMITIVE; }
"unicast"       { yylval.p_string = new string(yytext); return PRIMITIVE; }
"multicast"     { yylval.p_string = new string(yytext); return PRIMITIVE; }
"receive"       { yylval.p_string = new string(yytext); return PRIMITIVE; }
"wait"          { yylval.p_string = new string(yytext); return PRIMITIVE; }
"exit"          { yylval.p_string = new string(yytext); return PRIMITIVE; }
"assert"        { yylval.p_string = new string(yytext); return PRIMITIVE; }
"print"         { yylval.p_string = new string(yytext); return PRIMITIVE; }

{Identifier}    { yylval.p_string = new string(yytext); return IDENT; }
{OTHER_TOKEN}   { yylval.p_string = new string(yytext); return OTHER_TOKEN; }

.               { return yytext[0]; }

%%