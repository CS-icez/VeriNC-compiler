#include <cassert>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <string>
#include "ast.hpp"
#include "tla_builder.hpp"

using namespace std;

extern int yydebug;
extern FILE* yyin;
extern int yyparse(SpecAST*& ast);

int main(int argc, char* argv[]) {
	yydebug = 1; 

	assert(argc == 2);

	yyin = fopen(argv[1], "r");
	assert(yyin);

	SpecAST* ast = nullptr;
	auto ret = yyparse(ast);
	assert(!ret);
	fclose(yyin);

	std::cout << "Parsed successfully" << std::endl;

	auto file = string(argv[1]);
	auto module = file.substr(0, file.find_last_of('.'));
	TLABuilder builder(ast, module);
	auto [tla, cfg] = builder.build();

	std::cout << "Built successfully" << std::endl;

	ofstream tla_out(string("tla/") + module + ".tla");
	tla_out << tla << std::endl;
	ofstream cfg_out(string("cfg/") + module + ".cfg");
	cfg_out << cfg << std::endl;
}
