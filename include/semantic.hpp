#pragma once

#include "ast.hpp"
#include "typed_ir.hpp"

#include <memory>
#include <string>

namespace lumen {

class SemanticAnalyzer {
public:
    SemanticAnalyzer();
    ~SemanticAnalyzer();

    SemanticAnalyzer(const SemanticAnalyzer&) = delete;
    SemanticAnalyzer& operator=(const SemanticAnalyzer&) = delete;

    bool analyze(Program& program);
    const std::string& error() const;
    const TypedProgram& typedProgram() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace lumen
