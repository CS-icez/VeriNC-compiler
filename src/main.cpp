#include <cassert>
#include <cstdlib>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <format>
#include <iostream>
#include <string>
#include "ast.hpp"
#include "debug.hpp"
#include "tla_builder.hpp"

using std::string;
using std::format;
using namespace std::string_literals;
namespace fs = std::filesystem;

extern int yydebug;
extern FILE* yyin;
extern int yyparse(SpecAST*& ast);

static void check(bool cond, const string& msg) {
	if (!cond) {
		throw std::runtime_error(msg);
	}
}

int main(int argc, char* argv[]) {
	check(
		argc == 4 && string(argv[2]) == "-o",
		"Usage: ./verinc <spec-file> -o <out-dir>"
	);

	#ifdef DEBUG_ON
	yydebug = 1; 
	#endif
	
	yyin = fopen(argv[1], "r");
	check(yyin, format("Cannot open spec file {}", argv[1]));

	SpecAST* ast = nullptr;
	auto ret = yyparse(ast);
	check(!ret, "Parsing failed");
	fclose(yyin);

	DEBUG("Parsed successfully");

	auto module = fs::path(argv[1]).stem().string();
	DEBUG_EXP(module); 
	TLABuilder builder(ast, module);
	auto [tla, cfg] = builder.build();

	DEBUG("Built successfully");

	auto out_dir = fs::path(argv[3]);
	fs::create_directory(out_dir);

	auto tla_out = (out_dir / module).concat(".tla");
	//! This is graceful, but didn't check open status. The same for the following.
	std::ofstream(tla_out) << tla;

	auto cfg_out = (out_dir / module).concat(".cfg");
	std::ofstream(cfg_out) << cfg;

	auto flags = ""s;
	#ifndef DEBUG_ON
	flags += "> /dev/null 2>&1";
	#endif
	auto cmd = format("java -cp lib/tla2tools.jar pcal.trans {} {}", tla_out.string(), flags);
	std::system(cmd.c_str());
	auto tla_is = std::ifstream(tla_out);
	std::string program{
		std::istreambuf_iterator<char>(tla_is),
		std::istreambuf_iterator<char>()
	};

	auto final_program = TLABuilder::uncommentProperties(program);
	std::ofstream(tla_out) << final_program;
}
