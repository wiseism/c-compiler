#include "ast.hpp"
#include "constant_fold.hpp"
#include "semantic.hpp"

#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <limits>
#include <memory>
#include <string>
#include <vector>

struct yy_buffer_state;
using YY_BUFFER_STATE = yy_buffer_state*;

extern YY_BUFFER_STATE yy_scan_bytes(const char*, int);
extern void yy_delete_buffer(YY_BUFFER_STATE);
extern int yylex_destroy();
extern int yyparse();
extern int yylineno;
extern int yycolumn;
extern std::unique_ptr<lumen::Program> parsedProgram;
extern std::vector<std::string> parseDiagnostics;
extern bool suppressParserDiagnostics;

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    if (size > 65536 || size > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        return 0;
    }

    parsedProgram.reset();
    parseDiagnostics.clear();
    suppressParserDiagnostics = true;
    yylineno = 1;
    yycolumn = 1;
    YY_BUFFER_STATE buffer =
        yy_scan_bytes(reinterpret_cast<const char*>(data), static_cast<int>(size));
    const int parseResult = yyparse();
    yy_delete_buffer(buffer);
    yylex_destroy();

    if (parseResult == 0 && parseDiagnostics.empty() && parsedProgram) {
        lumen::SemanticAnalyzer analyzer;
        if (analyzer.analyze(*parsedProgram)) {
            lumen::ConstantFolder folder;
            folder.run(*parsedProgram);
        }
    }
    parsedProgram.reset();
    return 0;
}

#ifdef LUMEN_FUZZER_STANDALONE
int main(int argc, char** argv) {
    for (int index = 1; index < argc; ++index) {
        std::ifstream input(argv[index], std::ios::binary);
        const std::string contents((std::istreambuf_iterator<char>(input)),
                                   std::istreambuf_iterator<char>());
        LLVMFuzzerTestOneInput(reinterpret_cast<const std::uint8_t*>(contents.data()),
                               contents.size());
    }
    return 0;
}
#endif
