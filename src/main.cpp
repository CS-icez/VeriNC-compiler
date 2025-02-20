#include <cassert>
#include <cstdio>
#include <iostream>
#include <fstream>
#include <string>
#include "ast.hpp"

using namespace std;

extern FILE* yyin;
extern int yyparse(SpecAST*& ast);
extern int yydebug;

int main(int argc, char* argv[]) {
	yydebug = 1; 

	assert(argc == 2);

	yyin = fopen(argv[1], "r");
	assert(yyin);

	SpecAST* ast = nullptr;
	auto ret = yyparse(ast);
	assert(!ret);
}
