#include "ast.hpp"
#include "codegen.hpp"
#include "constant_fold.hpp"
#include "parser.hpp"
#include "semantic.hpp"

#include <cstdio>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

extern FILE* yyin;
extern int yylineno;
extern int yycolumn;
extern void yyrestart(FILE*);
extern std::unique_ptr<lumen::Program> parsedProgram;
extern std::vector<std::string> parseDiagnostics;

namespace {
void usage(const char* argv0) {
    std::cerr << "usage: " << argv0
              << " [--emit-llvm | -c] [-g] [-O0|-O1|-O2|-O3] <input.lm> [-o output]\n";
}
} // namespace

int main(int argc, char** argv) {
    std::string inputPath;
    std::string outputPath;
    lumen::CodegenOptions codegenOptions;

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
        if (arg == "--emit-llvm") {
            codegenOptions.emitObject = false;
            continue;
        }
        if (arg == "-c" || arg == "--emit-object") {
            codegenOptions.emitObject = true;
            continue;
        }
        if (arg == "-g") {
            codegenOptions.debugInfo = true;
            continue;
        }
        if (arg.size() == 3 && arg[0] == '-' && arg[1] == 'O' &&
            arg[2] >= '0' && arg[2] <= '3') {
            codegenOptions.optimizationLevel = static_cast<unsigned>(arg[2] - '0');
            continue;
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
    yycolumn = 1;
    parseDiagnostics.clear();
    parsedProgram.reset();
    yyrestart(input);
    const int parseResult = yyparse();
    std::fclose(input);
    if (parseResult != 0 || !parseDiagnostics.empty() || !parsedProgram) {
        return 1;
    }

    lumen::SemanticAnalyzer semanticAnalyzer;
    if (!semanticAnalyzer.analyze(*parsedProgram)) {
        for (const std::string& diagnostic : semanticAnalyzer.diagnostics()) {
            std::cerr << "error: " << diagnostic << '\n';
        }
        return 1;
    }

    lumen::ConstantFolder constantFolder;
    if (!constantFolder.run(*parsedProgram)) {
        std::cerr << "error: " << constantFolder.error() << '\n';
        return 1;
    }

    codegenOptions.sourcePath = std::filesystem::absolute(inputPath).string();
    lumen::CodeGenerator generator(std::filesystem::path(inputPath).filename().string(),
                                   codegenOptions);
    if (!generator.generate(semanticAnalyzer.typedProgram())) {
        std::cerr << "error: " << generator.error() << '\n';
        return 1;
    }

    if (codegenOptions.emitObject) {
        if (outputPath.empty()) {
            outputPath = std::filesystem::path(inputPath).filename().replace_extension(".o").string();
        }
        if (!generator.writeObject(outputPath)) {
            std::cerr << "error: " << generator.error() << '\n';
            return 1;
        }
    } else if (outputPath.empty()) {
        generator.printIR();
    } else if (!generator.writeIR(outputPath)) {
        std::cerr << "error: " << generator.error() << '\n';
        return 1;
    }
    return 0;
}
