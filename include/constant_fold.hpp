#pragma once

#include "ast.hpp"

#include <memory>
#include <string>

namespace lumen {

class ConstantFolder {
public:
    ConstantFolder();
    ~ConstantFolder();

    ConstantFolder(const ConstantFolder&) = delete;
    ConstantFolder& operator=(const ConstantFolder&) = delete;

    bool run(Program& program);
    const std::string& error() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace lumen
