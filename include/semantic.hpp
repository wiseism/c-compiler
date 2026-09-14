#pragma once

#include "ast.hpp"

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

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace lumen
