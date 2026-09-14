#include "semantic.hpp"

#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace lumen {

namespace {

struct FunctionInfo {
    TypePtr returnType;
    std::vector<TypePtr> parameterTypes;
    bool hasDefinition = false;
};

} // namespace

struct SemanticAnalyzer::Impl {
    std::unordered_map<std::string, FunctionInfo> functions;
    std::unordered_map<std::string, TypePtr> globals;
    std::unordered_map<std::string, bool> topLevelIsFunction;
    std::unordered_map<std::string, bool> globalDefinitions;
    std::vector<std::unordered_map<std::string, TypePtr>> scopes;
    TypePtr currentReturnType;
    std::string errorMessage;

    bool fail(int line, const std::string& message) {
        if (errorMessage.empty()) {
            errorMessage = "line " + std::to_string(line) + ": " + message;
        }
        return false;
    }

    TypePtr failExpr(int line, const std::string& message) {
        fail(line, message);
        return nullptr;
    }

    TypePtr lookup(const std::string& name) const {
        for (auto scope = scopes.rbegin(); scope != scopes.rend(); ++scope) {
            const auto found = scope->find(name);
            if (found != scope->end()) return found->second;
        }
        const auto global = globals.find(name);
        return global == globals.end() ? nullptr : global->second;
    }

    bool expectType(const TypePtr& actual, const TypePtr& expected,
                    int line, const std::string& context) {
        if (sameType(actual, expected)) return true;
        return fail(line, context + " requires '" + expected->toString() +
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
            result = lookup(variable->name);
            if (!result) return failExpr(expr.line, "unknown variable '" + variable->name + "'");
        } else if (auto* assign = dynamic_cast<AssignExpr*>(&expr)) {
            if (!isLValue(*assign->target)) {
                return failExpr(expr.line, "left side of assignment is not assignable");
            }
            TypePtr target = analyzeExpr(*assign->target);
            if (!target) return nullptr;
            if (isArrayType(target)) {
                return failExpr(expr.line, "arrays are not assignable");
            }
            TypePtr value = analyzeExpr(*assign->value);
            if (!value) return nullptr;
            resolveNull(*assign->value, target);
            value = assign->value->inferredType;
            if (!assignable(target, value)) {
                return failExpr(expr.line, "cannot assign '" + value->toString() + "' to '" +
                                           target->toString() + "'");
            }
            result = target;
        } else if (auto* unary = dynamic_cast<UnaryExpr*>(&expr)) {
            TypePtr operand = analyzeExpr(*unary->operand);
            if (!operand) return nullptr;
            if (unary->op == "-") {
                if (!expectType(operand, Type::int32(), expr.line, "operator '-'")) return nullptr;
                result = Type::int32();
            } else if (unary->op == "!") {
                if (!isScalar(operand)) {
                    return failExpr(expr.line, "operator '!' requires a scalar operand, got '" +
                                               operand->toString() + "'");
                }
                if (operand->kind() == TypeKind::NullPointer) {
                    unary->operand->inferredType = Type::pointerTo(Type::voidType());
                }
                result = Type::int32();
            } else if (unary->op == "&") {
                if (!isLValue(*unary->operand)) {
                    return failExpr(expr.line, "operator '&' requires an lvalue");
                }
                if (isArrayType(operand)) {
                    return failExpr(expr.line, "take the address of an array element, not the array");
                }
                result = Type::pointerTo(operand);
            } else if (unary->op == "*") {
                if (!isPointerType(operand)) {
                    return failExpr(expr.line, "operator '*' requires a pointer, got '" +
                                               operand->toString() + "'");
                }
                if (operand->elementType()->kind() == TypeKind::Void) {
                    return failExpr(expr.line, "cannot dereference a void pointer");
                }
                result = operand->elementType();
            } else {
                return failExpr(expr.line, "unsupported unary operator '" + unary->op + "'");
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
                    return failExpr(expr.line, "operands of '" + binary->op +
                                               "' have incompatible types '" + left->toString() +
                                               "' and '" + right->toString() + "'");
                }
            } else if (binary->op == "&&" || binary->op == "||") {
                if (!isScalar(left) || !isScalar(right)) {
                    return failExpr(expr.line, "operator '" + binary->op +
                                               "' requires scalar operands");
                }
                if (left->kind() == TypeKind::NullPointer) {
                    binary->lhs->inferredType = Type::pointerTo(Type::voidType());
                }
                if (right->kind() == TypeKind::NullPointer) {
                    binary->rhs->inferredType = Type::pointerTo(Type::voidType());
                }
            } else {
                if (!expectType(left, Type::int32(), expr.line,
                                "left operand of '" + binary->op + "'")) return nullptr;
                if (!expectType(right, Type::int32(), expr.line,
                                "right operand of '" + binary->op + "'")) return nullptr;
            }
            result = Type::int32();
        } else if (auto* call = dynamic_cast<CallExpr*>(&expr)) {
            const auto found = functions.find(call->callee);
            if (found == functions.end()) {
                return failExpr(expr.line, "unknown function '" + call->callee + "'");
            }
            if (found->second.parameterTypes.size() != call->args.size()) {
                return failExpr(expr.line, "function '" + call->callee + "' expects " +
                                           std::to_string(found->second.parameterTypes.size()) +
                                           " argument(s), got " + std::to_string(call->args.size()));
            }
            for (std::size_t i = 0; i < call->args.size(); ++i) {
                TypePtr argument = analyzeExpr(*call->args[i]);
                if (!argument) return nullptr;
                resolveNull(*call->args[i], found->second.parameterTypes[i]);
                argument = call->args[i]->inferredType;
                if (!assignable(found->second.parameterTypes[i], argument)) {
                    return failExpr(call->args[i]->line, "argument " + std::to_string(i + 1) +
                               " of '" + call->callee + "' requires '" +
                               found->second.parameterTypes[i]->toString() + "', got '" +
                               argument->toString() + "'");
                }
            }
            result = found->second.returnType;
        } else if (auto* index = dynamic_cast<IndexExpr*>(&expr)) {
            TypePtr base = analyzeExpr(*index->base);
            if (!base) return nullptr;
            TypePtr indexType = analyzeExpr(*index->index);
            if (!indexType || !expectType(indexType, Type::int32(), index->index->line,
                                          "array index")) return nullptr;
            if (!isArrayType(base) && !isPointerType(base)) {
                return failExpr(expr.line, "indexing requires an array or pointer, got '" +
                                           base->toString() + "'");
            }
            if (isArrayType(base)) {
                if (const auto* literal = dynamic_cast<const NumberExpr*>(index->index.get())) {
                    const std::int64_t value = literal->value;
                    if (value < 0 || static_cast<std::uint64_t>(value) >= base->arrayLength()) {
                        return failExpr(index->index->line, "array index " +
                            std::to_string(value) + " is outside [0, " +
                            std::to_string(base->arrayLength()) + ")");
                    }
                }
                result = base->elementType();
            } else {
                if (base->elementType()->kind() == TypeKind::Void) {
                    return failExpr(expr.line, "cannot index a void pointer");
                }
                result = base->elementType();
            }
        } else {
            return failExpr(expr.line, "internal error: unknown expression node");
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
            if (!analyzeStmt(*statement, statementReturns)) {
                ok = false;
                break;
            }
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
                return fail(stmt.line, "null literal requires a pointer context");
            }
            if (isArrayType(type)) {
                return fail(stmt.line, "array value requires an index");
            }
            return true;
        }
        if (auto* declaration = dynamic_cast<VarDeclStmt*>(&stmt)) {
            auto& currentScope = scopes.back();
            if (!validObjectType(declaration->declaredType)) {
                return fail(stmt.line, "variable '" + declaration->name +
                                       "' has invalid object type '" +
                                       declaration->declaredType->toString() + "'");
            }
            if (currentScope.count(declaration->name)) {
                return fail(stmt.line, "redeclaration of variable '" + declaration->name + "'");
            }
            if (declaration->init) {
                TypePtr initializer = analyzeExpr(*declaration->init);
                if (!initializer) return false;
                resolveNull(*declaration->init, declaration->declaredType);
                initializer = declaration->init->inferredType;
                if (!assignable(declaration->declaredType, initializer)) {
                    return fail(stmt.line, "cannot initialize '" + declaration->name +
                                           "' of type '" + declaration->declaredType->toString() +
                                           "' with '" + initializer->toString() + "'");
                }
            }
            currentScope.emplace(declaration->name, declaration->declaredType);
            return true;
        }
        if (auto* returnStmt = dynamic_cast<ReturnStmt*>(&stmt)) {
            if (!returnStmt->value) {
                if (currentReturnType->kind() != TypeKind::Void) {
                    return fail(stmt.line, "non-void function must return a value");
                }
            } else {
                TypePtr value = analyzeExpr(*returnStmt->value);
                if (!value) return false;
                resolveNull(*returnStmt->value, currentReturnType);
                value = returnStmt->value->inferredType;
                if (!assignable(currentReturnType, value)) {
                    return fail(stmt.line, "return requires '" + currentReturnType->toString() +
                                           "', got '" + value->toString() + "'");
                }
            }
            definitelyReturns = true;
            return true;
        }
        if (auto* ifStmt = dynamic_cast<IfStmt*>(&stmt)) {
            TypePtr condition = analyzeExpr(*ifStmt->cond);
            if (!condition || !isScalar(condition)) {
                return fail(ifStmt->cond->line, "if condition requires a scalar value");
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
                return fail(whileStmt->cond->line, "while condition requires a scalar value");
            }
            if (condition->kind() == TypeKind::NullPointer) {
                whileStmt->cond->inferredType = Type::pointerTo(Type::voidType());
            }
            bool ignored = false;
            return analyzeScoped(*whileStmt->body, ignored);
        }
        return fail(stmt.line, "internal error: unknown statement node");
    }

    bool registerFunction(const std::string& name, const TypePtr& returnType,
                          const std::vector<Parameter>& params, bool definition, int line) {
        const auto kind = topLevelIsFunction.find(name);
        if (kind != topLevelIsFunction.end() && !kind->second) {
            return fail(line, "'" + name + "' is already declared as a global variable");
        }
        topLevelIsFunction[name] = true;
        std::vector<TypePtr> parameterTypes;
        parameterTypes.reserve(params.size());
        for (const auto& parameter : params) parameterTypes.push_back(parameter.type);

        auto [position, inserted] = functions.emplace(
            name, FunctionInfo{returnType, parameterTypes, false});
        if (!inserted) {
            const FunctionInfo& existing = position->second;
            bool compatible = sameType(existing.returnType, returnType) &&
                              existing.parameterTypes.size() == parameterTypes.size();
            for (std::size_t i = 0; compatible && i < parameterTypes.size(); ++i) {
                compatible = sameType(existing.parameterTypes[i], parameterTypes[i]);
            }
            if (!compatible) return fail(line, "conflicting declaration of function '" + name + "'");
        }
        if (definition) {
            if (position->second.hasDefinition) {
                return fail(line, "redefinition of function '" + name + "'");
            }
            position->second.hasDefinition = true;
        }
        return true;
    }

    bool registerGlobal(const GlobalVar& global) {
        if (!validObjectType(global.declaredType)) {
            return fail(global.line, "variable '" + global.name +
                                     "' has invalid object type '" +
                                     global.declaredType->toString() + "'");
        }
        const auto kind = topLevelIsFunction.find(global.name);
        if (kind != topLevelIsFunction.end() && kind->second) {
            return fail(global.line, "'" + global.name + "' is already declared as a function");
        }
        topLevelIsFunction[global.name] = false;

        const auto existing = globals.find(global.name);
        if (existing != globals.end() && !sameType(existing->second, global.declaredType)) {
            return fail(global.line, "conflicting declaration of global variable '" +
                                     global.name + "'");
        }
        globals[global.name] = global.declaredType;
        if (!global.isExtern) {
            if (globalDefinitions[global.name]) {
                return fail(global.line, "redefinition of global variable '" + global.name + "'");
            }
            globalDefinitions[global.name] = true;
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
            return fail(global.line, "cannot initialize global '" + global.name +
                                     "' of type '" + global.declaredType->toString() +
                                     "' with '" + initializer->toString() + "'");
        }
        return true;
    }

    bool analyzeFunction(Function& function) {
        scopes.clear();
        scopes.emplace_back();
        currentReturnType = function.returnType;
        for (const auto& parameter : function.params) {
            if (parameter.type->kind() == TypeKind::Void) {
                return fail(parameter.line, "parameter '" + parameter.name +
                                            "' cannot have type void");
            }
            if (scopes.back().count(parameter.name)) {
                return fail(parameter.line, "duplicate parameter '" + parameter.name + "'");
            }
            scopes.back().emplace(parameter.name, parameter.type);
        }

        bool definitelyReturns = false;
        if (!analyzeBlock(*function.body, false, definitelyReturns)) return false;
        if (function.returnType->kind() != TypeKind::Void && !definitelyReturns) {
            return fail(function.line, "function '" + function.name +
                                       "' may reach the end without returning");
        }
        return true;
    }

    bool analyze(Program& program) {
        errorMessage.clear();
        functions.clear();
        globals.clear();
        topLevelIsFunction.clear();
        globalDefinitions.clear();
        for (auto& declaration : program.declarations) {
            if (auto* prototype = dynamic_cast<Prototype*>(declaration.get())) {
                if (!registerFunction(prototype->name, prototype->returnType, prototype->params,
                                      false, prototype->line)) return false;
            } else if (auto* function = dynamic_cast<Function*>(declaration.get())) {
                if (!registerFunction(function->name, function->returnType, function->params,
                                      true, function->line)) return false;
            } else if (auto* global = dynamic_cast<GlobalVar*>(declaration.get())) {
                if (!registerGlobal(*global)) return false;
            } else {
                return fail(declaration->line, "internal error: unknown top-level node");
            }
        }

        for (auto& declaration : program.declarations) {
            if (auto* global = dynamic_cast<GlobalVar*>(declaration.get())) {
                if (!analyzeGlobal(*global)) return false;
            } else if (auto* function = dynamic_cast<Function*>(declaration.get())) {
                if (!analyzeFunction(*function)) return false;
            }
        }
        return true;
    }
};

SemanticAnalyzer::SemanticAnalyzer() : impl_(std::make_unique<Impl>()) {}
SemanticAnalyzer::~SemanticAnalyzer() = default;

bool SemanticAnalyzer::analyze(Program& program) { return impl_->analyze(program); }
const std::string& SemanticAnalyzer::error() const { return impl_->errorMessage; }

} // namespace lumen
