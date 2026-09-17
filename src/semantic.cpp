#include "semantic.hpp"

#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace lumen {

namespace {

struct FunctionInfo {
    TypePtr returnType;
    std::vector<TypePtr> parameterTypes;
    bool hasDefinition = false;
    FunctionId id = 0;
};

struct Binding {
    TypePtr type;
    SymbolId symbol;
};

} // namespace

struct SemanticAnalyzer::Impl {
    std::unordered_map<std::string, FunctionInfo> functions;
    std::unordered_map<std::string, Binding> globals;
    std::unordered_map<std::string, bool> topLevelIsFunction;
    std::unordered_map<std::string, bool> globalDefinitions;
    std::vector<std::unordered_map<std::string, Binding>> scopes;
    TypePtr currentReturnType;
    std::string errorMessage;
    std::vector<std::string> diagnosticMessages;
    std::unordered_set<const TopLevel*> invalidDeclarations;
    int loopDepth = 0;
    TypedProgram typed;

    SymbolId addSymbol(SymbolKind kind, const std::string& name, const TypePtr& type) {
        const SymbolId id = static_cast<SymbolId>(typed.symbols.size());
        typed.symbols.push_back(TypedSymbol{id, kind, name, type});
        return id;
    }

    bool fail(int line, const std::string& message, int column = 1) {
        const std::string diagnostic = "line " + std::to_string(line) + ":" +
                                       std::to_string(column) + ": " + message;
        diagnosticMessages.push_back(diagnostic);
        if (!errorMessage.empty()) errorMessage += '\n';
        errorMessage += diagnostic;
        return false;
    }

    bool fail(const Node& node, const std::string& message) {
        return fail(node.line, message, node.column);
    }

    TypePtr failExpr(int line, const std::string& message, int column = 1) {
        fail(line, message, column);
        return nullptr;
    }

    TypePtr failExpr(const Node& node, const std::string& message) {
        return failExpr(node.line, message, node.column);
    }

    const Binding* lookup(const std::string& name) const {
        for (auto scope = scopes.rbegin(); scope != scopes.rend(); ++scope) {
            const auto found = scope->find(name);
            if (found != scope->end()) return &found->second;
        }
        const auto global = globals.find(name);
        return global == globals.end() ? nullptr : &global->second;
    }

    bool expectType(const TypePtr& actual, const TypePtr& expected,
                    int line, const std::string& context) {
        if (sameType(actual, expected)) return true;
        return fail(line, context + " requires '" + expected->toString() +
                          "', got '" + (actual ? actual->toString() : "<unknown>") + "'");
    }

    bool expectType(const TypePtr& actual, const TypePtr& expected,
                    const Node& node, const std::string& context) {
        if (sameType(actual, expected)) return true;
        return fail(node, context + " requires '" + expected->toString() +
                          "', got '" + (actual ? actual->toString() : "<unknown>") + "'");
    }

    static bool isScalar(const TypePtr& type) {
        return type && (type->kind() == TypeKind::Int32 || type->kind() == TypeKind::Pointer ||
                        type->kind() == TypeKind::NullPointer);
    }

    static bool isLValue(const Expr& expr) {
        if (dynamic_cast<const VariableExpr*>(&expr) || dynamic_cast<const IndexExpr*>(&expr)) {
            return true;
        }
        const auto* unary = dynamic_cast<const UnaryExpr*>(&expr);
        return unary && unary->op == "*";
    }

    static void resolveNull(Expr& expr, const TypePtr& expected) {
        if (expr.inferredType && expr.inferredType->kind() == TypeKind::NullPointer &&
            isPointerType(expected)) {
            expr.inferredType = expected;
        }
    }

    static bool assignable(const TypePtr& destination, const TypePtr& source) {
        return sameType(destination, source) ||
               (isPointerType(destination) && source &&
                source->kind() == TypeKind::NullPointer);
    }

    static bool validObjectType(const TypePtr& type) {
        if (!type || type->kind() == TypeKind::Void) return false;
        if (type->kind() == TypeKind::Array) {
            return type->arrayLength() > 0 && validObjectType(type->elementType());
        }
        return type->kind() != TypeKind::NullPointer;
    }

    TypePtr analyzeExpr(Expr& expr) {
        TypePtr result;
        if (dynamic_cast<NumberExpr*>(&expr)) {
            result = Type::int32();
        } else if (dynamic_cast<NullExpr*>(&expr)) {
            result = Type::nullPointer();
        } else if (auto* variable = dynamic_cast<VariableExpr*>(&expr)) {
            const Binding* binding = lookup(variable->name);
            if (!binding) return failExpr(expr, "unknown variable '" + variable->name + "'");
            result = binding->type;
            typed.variableBindings[variable] = binding->symbol;
        } else if (auto* assign = dynamic_cast<AssignExpr*>(&expr)) {
            if (!isLValue(*assign->target)) {
                return failExpr(expr, "left side of assignment is not assignable");
            }
            TypePtr target = analyzeExpr(*assign->target);
            if (!target) return nullptr;
            if (isArrayType(target)) {
                return failExpr(expr, "arrays are not assignable");
            }
            TypePtr value = analyzeExpr(*assign->value);
            if (!value) return nullptr;
            resolveNull(*assign->value, target);
            value = assign->value->inferredType;
            if (!assignable(target, value)) {
                return failExpr(expr, "cannot assign '" + value->toString() + "' to '" +
                                           target->toString() + "'");
            }
            result = target;
        } else if (auto* unary = dynamic_cast<UnaryExpr*>(&expr)) {
            TypePtr operand = analyzeExpr(*unary->operand);
            if (!operand) return nullptr;
            if (unary->op == "-") {
                if (!expectType(operand, Type::int32(), expr, "operator '-'")) return nullptr;
                result = Type::int32();
            } else if (unary->op == "!") {
                if (!isScalar(operand)) {
                    return failExpr(expr, "operator '!' requires a scalar operand, got '" +
                                               operand->toString() + "'");
                }
                if (operand->kind() == TypeKind::NullPointer) {
                    unary->operand->inferredType = Type::pointerTo(Type::voidType());
                }
                result = Type::int32();
            } else if (unary->op == "&") {
                if (!isLValue(*unary->operand)) {
                    return failExpr(expr, "operator '&' requires an lvalue");
                }
                if (isArrayType(operand)) {
                    return failExpr(expr, "take the address of an array element, not the array");
                }
                result = Type::pointerTo(operand);
            } else if (unary->op == "*") {
                if (!isPointerType(operand)) {
                    return failExpr(expr, "operator '*' requires a pointer, got '" +
                                               operand->toString() + "'");
                }
                if (operand->elementType()->kind() == TypeKind::Void) {
                    return failExpr(expr, "cannot dereference a void pointer");
                }
                result = operand->elementType();
            } else {
                return failExpr(expr, "unsupported unary operator '" + unary->op + "'");
            }
        } else if (auto* binary = dynamic_cast<BinaryExpr*>(&expr)) {
            TypePtr left = analyzeExpr(*binary->lhs);
            if (!left) return nullptr;
            TypePtr right = analyzeExpr(*binary->rhs);
            if (!right) return nullptr;
            if (binary->op == "==" || binary->op == "!=") {
                if (left->kind() == TypeKind::NullPointer &&
                    right->kind() == TypeKind::NullPointer) {
                    const TypePtr voidPointer = Type::pointerTo(Type::voidType());
                    binary->lhs->inferredType = voidPointer;
                    binary->rhs->inferredType = voidPointer;
                }
                resolveNull(*binary->lhs, right);
                resolveNull(*binary->rhs, left);
                left = binary->lhs->inferredType;
                right = binary->rhs->inferredType;
                if (!sameType(left, right)) {
                    return failExpr(expr, "operands of '" + binary->op +
                                               "' have incompatible types '" + left->toString() +
                                               "' and '" + right->toString() + "'");
                }
            } else if (binary->op == "&&" || binary->op == "||") {
                if (!isScalar(left) || !isScalar(right)) {
                    return failExpr(expr, "operator '" + binary->op +
                                               "' requires scalar operands");
                }
                if (left->kind() == TypeKind::NullPointer) {
                    binary->lhs->inferredType = Type::pointerTo(Type::voidType());
                }
                if (right->kind() == TypeKind::NullPointer) {
                    binary->rhs->inferredType = Type::pointerTo(Type::voidType());
                }
            } else {
                if (!expectType(left, Type::int32(), expr,
                                "left operand of '" + binary->op + "'")) return nullptr;
                if (!expectType(right, Type::int32(), expr,
                                "right operand of '" + binary->op + "'")) return nullptr;
            }
            result = Type::int32();
        } else if (auto* call = dynamic_cast<CallExpr*>(&expr)) {
            const auto found = functions.find(call->callee);
            if (found == functions.end()) {
                return failExpr(expr, "unknown function '" + call->callee + "'");
            }
            if (found->second.parameterTypes.size() != call->args.size()) {
                return failExpr(expr, "function '" + call->callee + "' expects " +
                                           std::to_string(found->second.parameterTypes.size()) +
                                           " argument(s), got " + std::to_string(call->args.size()));
            }
            for (std::size_t i = 0; i < call->args.size(); ++i) {
                TypePtr argument = analyzeExpr(*call->args[i]);
                if (!argument) return nullptr;
                resolveNull(*call->args[i], found->second.parameterTypes[i]);
                argument = call->args[i]->inferredType;
                if (!assignable(found->second.parameterTypes[i], argument)) {
                    return failExpr(*call->args[i], "argument " + std::to_string(i + 1) +
                               " of '" + call->callee + "' requires '" +
                               found->second.parameterTypes[i]->toString() + "', got '" +
                               argument->toString() + "'");
                }
            }
            typed.callBindings[call] = found->second.id;
            result = found->second.returnType;
        } else if (auto* index = dynamic_cast<IndexExpr*>(&expr)) {
            TypePtr base = analyzeExpr(*index->base);
            if (!base) return nullptr;
            TypePtr indexType = analyzeExpr(*index->index);
            if (!indexType || !expectType(indexType, Type::int32(), *index->index,
                                          "array index")) return nullptr;
            if (!isArrayType(base) && !isPointerType(base)) {
                return failExpr(expr, "indexing requires an array or pointer, got '" +
                                           base->toString() + "'");
            }
            if (isArrayType(base)) {
                if (const auto* literal = dynamic_cast<const NumberExpr*>(index->index.get())) {
                    const std::int64_t value = literal->value;
                    if (value < 0 || static_cast<std::uint64_t>(value) >= base->arrayLength()) {
                        return failExpr(*index->index, "array index " +
                            std::to_string(value) + " is outside [0, " +
                            std::to_string(base->arrayLength()) + ")");
                    }
                }
                result = base->elementType();
            } else {
                if (base->elementType()->kind() == TypeKind::Void) {
                    return failExpr(expr, "cannot index a void pointer");
                }
                result = base->elementType();
            }
        } else {
            return failExpr(expr, "internal error: unknown expression node");
        }

        expr.inferredType = result;
        return result;
    }

    bool analyzeScoped(Stmt& stmt, bool& definitelyReturns) {
        scopes.emplace_back();
        const bool ok = analyzeStmt(stmt, definitelyReturns);
        scopes.pop_back();
        return ok;
    }

    bool analyzeBlock(BlockStmt& block, bool introduceScope, bool& definitelyReturns) {
        if (introduceScope) scopes.emplace_back();
        definitelyReturns = false;
        bool ok = true;
        for (auto& statement : block.body) {
            bool statementReturns = false;
            if (!analyzeStmt(*statement, statementReturns)) ok = false;
            definitelyReturns = definitelyReturns || statementReturns;
        }
        if (introduceScope) scopes.pop_back();
        return ok;
    }

    bool analyzeStmt(Stmt& stmt, bool& definitelyReturns) {
        definitelyReturns = false;
        if (auto* block = dynamic_cast<BlockStmt*>(&stmt)) {
            return analyzeBlock(*block, true, definitelyReturns);
        }
        if (auto* expression = dynamic_cast<ExprStmt*>(&stmt)) {
            TypePtr type = analyzeExpr(*expression->expr);
            if (!type) return false;
            if (type->kind() == TypeKind::NullPointer) {
                return fail(stmt, "null literal requires a pointer context");
            }
            if (isArrayType(type)) {
                return fail(stmt, "array value requires an index");
            }
            return true;
        }
        if (auto* declaration = dynamic_cast<VarDeclStmt*>(&stmt)) {
            auto& currentScope = scopes.back();
            if (!validObjectType(declaration->declaredType)) {
                return fail(stmt, "variable '" + declaration->name +
                                       "' has invalid object type '" +
                                       declaration->declaredType->toString() + "'");
            }
            if (currentScope.count(declaration->name)) {
                return fail(stmt, "redeclaration of variable '" + declaration->name + "'");
            }
            if (declaration->init) {
                TypePtr initializer = analyzeExpr(*declaration->init);
                if (!initializer) return false;
                resolveNull(*declaration->init, declaration->declaredType);
                initializer = declaration->init->inferredType;
                if (!assignable(declaration->declaredType, initializer)) {
                    return fail(stmt, "cannot initialize '" + declaration->name +
                                           "' of type '" + declaration->declaredType->toString() +
                                           "' with '" + initializer->toString() + "'");
                }
            }
            const SymbolId symbol = addSymbol(SymbolKind::Local, declaration->name,
                                              declaration->declaredType);
            currentScope.emplace(declaration->name,
                                 Binding{declaration->declaredType, symbol});
            typed.localDeclarations[declaration] = symbol;
            return true;
        }
        if (auto* returnStmt = dynamic_cast<ReturnStmt*>(&stmt)) {
            if (!returnStmt->value) {
                if (currentReturnType->kind() != TypeKind::Void) {
                    return fail(stmt, "non-void function must return a value");
                }
            } else {
                TypePtr value = analyzeExpr(*returnStmt->value);
                if (!value) return false;
                resolveNull(*returnStmt->value, currentReturnType);
                value = returnStmt->value->inferredType;
                if (!assignable(currentReturnType, value)) {
                    return fail(stmt, "return requires '" + currentReturnType->toString() +
                                           "', got '" + value->toString() + "'");
                }
            }
            definitelyReturns = true;
            return true;
        }
        if (dynamic_cast<BreakStmt*>(&stmt)) {
            if (loopDepth == 0) return fail(stmt, "'break' is only valid inside a loop");
            return true;
        }
        if (auto* ifStmt = dynamic_cast<IfStmt*>(&stmt)) {
            TypePtr condition = analyzeExpr(*ifStmt->cond);
            if (!condition || !isScalar(condition)) {
                return fail(*ifStmt->cond, "if condition requires a scalar value");
            }
            if (condition->kind() == TypeKind::NullPointer) {
                ifStmt->cond->inferredType = Type::pointerTo(Type::voidType());
            }
            bool thenReturns = false;
            if (!analyzeScoped(*ifStmt->thenBranch, thenReturns)) return false;
            bool elseReturns = false;
            if (ifStmt->elseBranch && !analyzeScoped(*ifStmt->elseBranch, elseReturns)) return false;
            definitelyReturns = ifStmt->elseBranch && thenReturns && elseReturns;
            return true;
        }
        if (auto* whileStmt = dynamic_cast<WhileStmt*>(&stmt)) {
            TypePtr condition = analyzeExpr(*whileStmt->cond);
            if (!condition || !isScalar(condition)) {
                return fail(*whileStmt->cond, "while condition requires a scalar value");
            }
            if (condition->kind() == TypeKind::NullPointer) {
                whileStmt->cond->inferredType = Type::pointerTo(Type::voidType());
            }
            bool ignored = false;
            ++loopDepth;
            const bool ok = analyzeScoped(*whileStmt->body, ignored);
            --loopDepth;
            return ok;
        }
        return fail(stmt, "internal error: unknown statement node");
    }

    bool registerFunction(const std::string& name, const TypePtr& returnType,
                          const std::vector<Parameter>& params, bool definition,
                          int line, int column) {
        const auto kind = topLevelIsFunction.find(name);
        if (kind != topLevelIsFunction.end() && !kind->second) {
            return fail(line, "'" + name + "' is already declared as a global variable",
                        column);
        }
        topLevelIsFunction[name] = true;
        std::vector<TypePtr> parameterTypes;
        parameterTypes.reserve(params.size());
        for (const auto& parameter : params) {
            if (parameter.type->kind() == TypeKind::Void) {
                return fail(parameter.line, "parameter '" + parameter.name +
                                            "' cannot have type void", parameter.column);
            }
            parameterTypes.push_back(parameter.type);
        }

        const FunctionId id = static_cast<FunctionId>(typed.functions.size());
        auto [position, inserted] = functions.emplace(
            name, FunctionInfo{returnType, parameterTypes, false, id});
        if (inserted) {
            typed.functions.push_back(TypedFunction{id, name, returnType, parameterTypes,
                                                    nullptr, {}});
        }
        if (!inserted) {
            const FunctionInfo& existing = position->second;
            bool compatible = sameType(existing.returnType, returnType) &&
                              existing.parameterTypes.size() == parameterTypes.size();
            for (std::size_t i = 0; compatible && i < parameterTypes.size(); ++i) {
                compatible = sameType(existing.parameterTypes[i], parameterTypes[i]);
            }
            if (!compatible) return fail(line, "conflicting declaration of function '" + name +
                                               "'", column);
        }
        if (definition) {
            if (position->second.hasDefinition) {
                return fail(line, "redefinition of function '" + name + "'", column);
            }
            position->second.hasDefinition = true;
        }
        return true;
    }

    bool registerGlobal(const GlobalVar& global) {
        if (!validObjectType(global.declaredType)) {
            return fail(global, "variable '" + global.name +
                                     "' has invalid object type '" +
                                     global.declaredType->toString() + "'");
        }
        const auto kind = topLevelIsFunction.find(global.name);
        if (kind != topLevelIsFunction.end() && kind->second) {
            return fail(global, "'" + global.name + "' is already declared as a function");
        }
        topLevelIsFunction[global.name] = false;

        const auto existing = globals.find(global.name);
        if (existing != globals.end() && !sameType(existing->second.type, global.declaredType)) {
            return fail(global, "conflicting declaration of global variable '" +
                                     global.name + "'");
        }
        if (existing == globals.end()) {
            const SymbolId symbol = addSymbol(SymbolKind::Global, global.name,
                                              global.declaredType);
            globals.emplace(global.name, Binding{global.declaredType, symbol});
            typed.globals.push_back(TypedGlobal{symbol, global.name, global.declaredType, nullptr});
        }
        if (!global.isExtern) {
            if (globalDefinitions[global.name]) {
                return fail(global, "redefinition of global variable '" + global.name + "'");
            }
            globalDefinitions[global.name] = true;
            const SymbolId symbol = globals.at(global.name).symbol;
            for (auto& typedGlobal : typed.globals) {
                if (typedGlobal.symbol == symbol) {
                    typedGlobal.definition = &global;
                    break;
                }
            }
        }
        return true;
    }

    bool analyzeGlobal(GlobalVar& global) {
        if (!global.init) return true;
        TypePtr initializer = analyzeExpr(*global.init);
        if (!initializer) return false;
        resolveNull(*global.init, global.declaredType);
        initializer = global.init->inferredType;
        if (!assignable(global.declaredType, initializer)) {
            return fail(global, "cannot initialize global '" + global.name +
                                     "' of type '" + global.declaredType->toString() +
                                     "' with '" + initializer->toString() + "'");
        }
        return true;
    }

    bool analyzeFunction(Function& function) {
        scopes.clear();
        scopes.emplace_back();
        currentReturnType = function.returnType;
        TypedFunction& typedFunction = typed.functions.at(functions.at(function.name).id);
        typedFunction.definition = &function;
        typedFunction.parameterSymbols.clear();
        for (const auto& parameter : function.params) {
            if (parameter.type->kind() == TypeKind::Void) {
                return fail(parameter.line, "parameter '" + parameter.name +
                                            "' cannot have type void", parameter.column);
            }
            if (scopes.back().count(parameter.name)) {
                return fail(parameter.line, "duplicate parameter '" + parameter.name + "'",
                            parameter.column);
            }
            const SymbolId symbol = addSymbol(SymbolKind::Parameter, parameter.name,
                                              parameter.type);
            scopes.back().emplace(parameter.name, Binding{parameter.type, symbol});
            typedFunction.parameterSymbols.push_back(symbol);
        }

        bool definitelyReturns = false;
        if (!analyzeBlock(*function.body, false, definitelyReturns)) return false;
        if (function.returnType->kind() != TypeKind::Void && !definitelyReturns) {
            return fail(function, "function '" + function.name +
                                       "' may reach the end without returning");
        }
        return true;
    }

    bool analyze(Program& program) {
        errorMessage.clear();
        diagnosticMessages.clear();
        invalidDeclarations.clear();
        functions.clear();
        globals.clear();
        topLevelIsFunction.clear();
        globalDefinitions.clear();
        typed = TypedProgram{};
        typed.source = &program;
        bool ok = true;
        for (auto& declaration : program.declarations) {
            bool registered = false;
            if (auto* prototype = dynamic_cast<Prototype*>(declaration.get())) {
                registered = registerFunction(prototype->name, prototype->returnType,
                                              prototype->params, false, prototype->line,
                                              prototype->column);
            } else if (auto* function = dynamic_cast<Function*>(declaration.get())) {
                registered = registerFunction(function->name, function->returnType,
                                              function->params, true, function->line,
                                              function->column);
            } else if (auto* global = dynamic_cast<GlobalVar*>(declaration.get())) {
                registered = registerGlobal(*global);
            } else {
                registered = fail(declaration->line,
                                  "internal error: unknown top-level node",
                                  declaration->column);
            }
            if (!registered) {
                invalidDeclarations.insert(declaration.get());
                ok = false;
            }
        }

        for (auto& declaration : program.declarations) {
            if (invalidDeclarations.count(declaration.get())) continue;
            if (auto* global = dynamic_cast<GlobalVar*>(declaration.get())) {
                if (!analyzeGlobal(*global)) ok = false;
            } else if (auto* function = dynamic_cast<Function*>(declaration.get())) {
                if (!analyzeFunction(*function)) ok = false;
            }
        }
        return ok;
    }
};

SemanticAnalyzer::SemanticAnalyzer() : impl_(std::make_unique<Impl>()) {}
SemanticAnalyzer::~SemanticAnalyzer() = default;

bool SemanticAnalyzer::analyze(Program& program) { return impl_->analyze(program); }
const std::string& SemanticAnalyzer::error() const { return impl_->errorMessage; }
const std::vector<std::string>& SemanticAnalyzer::diagnostics() const {
    return impl_->diagnosticMessages;
}
const TypedProgram& SemanticAnalyzer::typedProgram() const { return impl_->typed; }

} // namespace lumen
