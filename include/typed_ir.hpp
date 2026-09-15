#pragma once

#include "ast.hpp"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace lumen {

using SymbolId = std::uint32_t;
using FunctionId = std::uint32_t;

enum class SymbolKind {
    Global,
    Parameter,
    Local,
};

struct TypedSymbol {
    SymbolId id;
    SymbolKind kind;
    std::string name;
    TypePtr type;
};

struct TypedGlobal {
    SymbolId symbol;
    std::string name;
    TypePtr type;
    const GlobalVar* definition = nullptr;
};

struct TypedFunction {
    FunctionId id;
    std::string name;
    TypePtr returnType;
    std::vector<TypePtr> parameterTypes;
    const Function* definition = nullptr;
    std::vector<SymbolId> parameterSymbols;
};

// A zero-copy, typed high-level IR. Structured statements remain in the AST, while all
// semantic facts needed by a backend are frozen here as stable IDs and resolved bindings.
struct TypedProgram {
    const Program* source = nullptr;
    std::vector<TypedSymbol> symbols;
    std::vector<TypedGlobal> globals;
    std::vector<TypedFunction> functions;

    std::unordered_map<const VarDeclStmt*, SymbolId> localDeclarations;
    std::unordered_map<const VariableExpr*, SymbolId> variableBindings;
    std::unordered_map<const CallExpr*, FunctionId> callBindings;

    const TypedSymbol& symbol(SymbolId id) const { return symbols.at(id); }
    const TypedFunction& function(FunctionId id) const { return functions.at(id); }
};

} // namespace lumen
