#include <cassert>
#include <cstdlib>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <format>
#include <iostream>
#include <string>
#include "ast.hpp"
#include "tla_builder.hpp"

using std::string;
using std::format;
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
	check(argc == 2, "Usage: ./bin/main <spec-file>");

	yydebug = 1; 
	
	yyin = fopen(argv[1], "r");
	check(yyin, format("Cannot open spec file {}", argv[1]));

	SpecAST* ast = nullptr;
	auto ret = yyparse(ast);
	check(!ret, "Parsing failed");
	fclose(yyin);

	std::cout << "Parsed successfully" << std::endl;

	auto file = std::string(argv[1]);
	auto module = file.substr(0, file.find_last_of('.'));
	// string::npos == -1, so works fine without '/' in `module`.
	module = module.substr(module.find_last_of('/') + 1);
	TLABuilder builder(ast, module);
	auto [tla, cfg] = builder.build();

	std::cout << "Built successfully" << std::endl;

	fs::create_directory("tla");
	auto tla_out = std::string("tla/") + module + ".tla";
	std::ofstream tla_os(tla_out);
	check(tla_os.is_open(), format("Cannot open {}", tla_out));
	tla_os << tla;
	tla_os.close();

	auto cfg_out = std::string("tla/") + module + ".cfg";
	std::ofstream cfg_os(cfg_out);
	check(cfg_os.is_open(), format("Cannot open {}", cfg_out));
	cfg_os << cfg;
	cfg_os.close();

	std::system(format("java -cp lib/tla2tools.jar pcal.trans {}", tla_out).c_str());
	std::ifstream tla_is(tla_out);
	check(tla_is.is_open(), format("Cannot open {}", tla_out));
	std::string program((std::istreambuf_iterator<char>(tla_is)),
		std::istreambuf_iterator<char>());
	tla_is.close();

	auto final_program = TLABuilder::uncommentProperties(program);
	tla_os.open(tla_out);
	check(tla_os.is_open(), format("Cannot open {}", tla_out));
	tla_os << final_program;
	tla_os.close();
}
