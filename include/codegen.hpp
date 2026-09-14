#pragma once

#include "ast.hpp"

#include <memory>
#include <string>

namespace lumen {

class CodeGenerator {
public:
    explicit CodeGenerator(std::string moduleName);
    ~CodeGenerator();

    CodeGenerator(const CodeGenerator&) = delete;
    CodeGenerator& operator=(const CodeGenerator&) = delete;

    bool generate(const Program& program);
    bool writeIR(const std::string& outputPath) const;
    void printIR() const;
    const std::string& error() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace lumen
