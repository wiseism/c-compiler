#pragma once

#include "typed_ir.hpp"

#include <memory>
#include <string>

namespace lumen {

struct CodegenOptions {
    unsigned optimizationLevel = 0;
    bool emitObject = false;
    bool debugInfo = false;
    std::string sourcePath;
};

class CodeGenerator {
public:
    explicit CodeGenerator(std::string moduleName, CodegenOptions options = {});
    ~CodeGenerator();

    CodeGenerator(const CodeGenerator&) = delete;
    CodeGenerator& operator=(const CodeGenerator&) = delete;

    bool generate(const TypedProgram& program);
    bool writeIR(const std::string& outputPath) const;
    bool writeObject(const std::string& outputPath);
    void printIR() const;
    const std::string& error() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace lumen
