#include "ast.hpp"
#include "codegen.hpp"
#include "parser.hpp"
#include "semantic.hpp"

#include <cstdio>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>

extern FILE* yyin;
extern int yylineno;
extern void yyrestart(FILE*);
extern std::unique_ptr<lumen::Program> parsedProgram;

namespace {
void usage(const char* argv0) {
    std::cerr << "usage: " << argv0 << " <input.lm> [-o <output.ll>]\n";
}
} // namespace

int main(int argc, char** argv) {
    std::string inputPath;
    std::string outputPath;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "-h" || arg == "--help") {
            usage(argv[0]);
            return 0;
        }
        if (arg == "--version") {
            std::cout << "lumenc 0.1.0\n";
            return 0;
        }
        if (arg == "-o") {
            if (++i == argc) {
                std::cerr << "error: -o requires a path\n";
                return 2;
            }
            outputPath = argv[i];
            continue;
        }
        if (!arg.empty() && arg.front() == '-') {
            std::cerr << "error: unknown option '" << arg << "'\n";
            return 2;
        }
        if (!inputPath.empty()) {
            std::cerr << "error: only one input file is supported\n";
            return 2;
        }
        inputPath = arg;
    }

    if (inputPath.empty()) {
        usage(argv[0]);
        return 2;
    }

    FILE* input = std::fopen(inputPath.c_str(), "r");
    if (!input) {
        std::perror(("error: cannot open " + inputPath).c_str());
        return 1;
    }

    yyin = input;
    yylineno = 1;
    yyrestart(input);
    const int parseResult = yyparse();
    std::fclose(input);
    if (parseResult != 0 || !parsedProgram) {
        return 1;
    }

    lumen::SemanticAnalyzer semanticAnalyzer;
    if (!semanticAnalyzer.analyze(*parsedProgram)) {
        std::cerr << "error: " << semanticAnalyzer.error() << '\n';
        return 1;
    }

    lumen::CodeGenerator generator(std::filesystem::path(inputPath).filename().string());
    if (!generator.generate(*parsedProgram)) {
        std::cerr << "error: " << generator.error() << '\n';
        return 1;
    }

    if (outputPath.empty()) {
        generator.printIR();
    } else if (!generator.writeIR(outputPath)) {
        std::cerr << "error: " << generator.error() << '\n';
        return 1;
    }
    return 0;
}
