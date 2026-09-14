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
        return nullptr;
    }

    bool expectType(const TypePtr& actual, const TypePtr& expected,
                    int line, const std::string& context) {
        if (sameType(actual, expected)) return true;
        return fail(line, context + " requires '" + expected->toString() +
                          "', got '" + (actual ? actual->toString() : "<unknown>") + "'");
    }

    TypePtr analyzeExpr(Expr& expr) {
        TypePtr result;
        if (dynamic_cast<NumberExpr*>(&expr)) {
            result = Type::int32();
        } else if (auto* variable = dynamic_cast<VariableExpr*>(&expr)) {
            result = lookup(variable->name);
            if (!result) return failExpr(expr.line, "unknown variable '" + variable->name + "'");
        } else if (auto* assign = dynamic_cast<AssignExpr*>(&expr)) {
            TypePtr target = lookup(assign->name);
            if (!target) {
                return failExpr(expr.line, "assignment to unknown variable '" + assign->name + "'");
            }
            TypePtr value = analyzeExpr(*assign->value);
            if (!value) return nullptr;
            if (!sameType(target, value)) {
                return failExpr(expr.line, "cannot assign '" + value->toString() + "' to '" +
                                           target->toString() + "'");
            }
            result = target;
        } else if (auto* unary = dynamic_cast<UnaryExpr*>(&expr)) {
            TypePtr operand = analyzeExpr(*unary->operand);
            if (!operand) return nullptr;
            if (!expectType(operand, Type::int32(), expr.line,
                            "operator '" + unary->op + "'")) return nullptr;
            result = Type::int32();
        } else if (auto* binary = dynamic_cast<BinaryExpr*>(&expr)) {
            TypePtr left = analyzeExpr(*binary->lhs);
            if (!left) return nullptr;
            TypePtr right = analyzeExpr(*binary->rhs);
            if (!right) return nullptr;
            if (!expectType(left, Type::int32(), expr.line,
                            "left operand of '" + binary->op + "'")) return nullptr;
            if (!expectType(right, Type::int32(), expr.line,
                            "right operand of '" + binary->op + "'")) return nullptr;
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
                if (!sameType(argument, found->second.parameterTypes[i])) {
                    return failExpr(call->args[i]->line, "argument " + std::to_string(i + 1) +
                               " of '" + call->callee + "' requires '" +
                               found->second.parameterTypes[i]->toString() + "', got '" +
                               argument->toString() + "'");
                }
            }
            result = found->second.returnType;
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
            return analyzeExpr(*expression->expr) != nullptr;
        }
        if (auto* declaration = dynamic_cast<VarDeclStmt*>(&stmt)) {
            auto& currentScope = scopes.back();
            if (currentScope.count(declaration->name)) {
                return fail(stmt.line, "redeclaration of variable '" + declaration->name + "'");
            }
            if (declaration->init) {
                TypePtr initializer = analyzeExpr(*declaration->init);
                if (!initializer) return false;
                if (!sameType(declaration->declaredType, initializer)) {
                    return fail(stmt.line, "cannot initialize '" + declaration->name +
                                           "' of type '" + declaration->declaredType->toString() +
                                           "' with '" + initializer->toString() + "'");
                }
            }
            currentScope.emplace(declaration->name, declaration->declaredType);
            return true;
        }
        if (auto* returnStmt = dynamic_cast<ReturnStmt*>(&stmt)) {
            TypePtr value = analyzeExpr(*returnStmt->value);
            if (!value) return false;
            if (!sameType(currentReturnType, value)) {
                return fail(stmt.line, "return requires '" + currentReturnType->toString() +
                                       "', got '" + value->toString() + "'");
            }
            definitelyReturns = true;
            return true;
        }
        if (auto* ifStmt = dynamic_cast<IfStmt*>(&stmt)) {
            TypePtr condition = analyzeExpr(*ifStmt->cond);
            if (!condition || !expectType(condition, Type::int32(), ifStmt->cond->line,
                                          "if condition")) return false;
            bool thenReturns = false;
            if (!analyzeScoped(*ifStmt->thenBranch, thenReturns)) return false;
            bool elseReturns = false;
            if (ifStmt->elseBranch && !analyzeScoped(*ifStmt->elseBranch, elseReturns)) return false;
            definitelyReturns = ifStmt->elseBranch && thenReturns && elseReturns;
            return true;
        }
        if (auto* whileStmt = dynamic_cast<WhileStmt*>(&stmt)) {
            TypePtr condition = analyzeExpr(*whileStmt->cond);
            if (!condition || !expectType(condition, Type::int32(), whileStmt->cond->line,
                                          "while condition")) return false;
            bool ignored = false;
            return analyzeScoped(*whileStmt->body, ignored);
        }
        return fail(stmt.line, "internal error: unknown statement node");
    }

    bool registerFunction(const std::string& name, const TypePtr& returnType,
                          const std::vector<Parameter>& params, bool definition, int line) {
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

    bool analyzeFunction(Function& function) {
        scopes.clear();
        scopes.emplace_back();
        currentReturnType = function.returnType;
        for (const auto& parameter : function.params) {
            if (scopes.back().count(parameter.name)) {
                return fail(parameter.line, "duplicate parameter '" + parameter.name + "'");
            }
            scopes.back().emplace(parameter.name, parameter.type);
        }

        bool definitelyReturns = false;
        if (!analyzeBlock(*function.body, false, definitelyReturns)) return false;
        if (!definitelyReturns) {
            return fail(function.line, "function '" + function.name +
                                       "' may reach the end without returning");
        }
        return true;
    }

    bool analyze(Program& program) {
        errorMessage.clear();
        functions.clear();
        for (auto& declaration : program.declarations) {
            if (auto* prototype = dynamic_cast<Prototype*>(declaration.get())) {
                if (!registerFunction(prototype->name, prototype->returnType, prototype->params,
                                      false, prototype->line)) return false;
            } else if (auto* function = dynamic_cast<Function*>(declaration.get())) {
                if (!registerFunction(function->name, function->returnType, function->params,
                                      true, function->line)) return false;
            } else {
                return fail(declaration->line, "internal error: unknown top-level node");
            }
        }

        for (auto& declaration : program.declarations) {
            if (auto* function = dynamic_cast<Function*>(declaration.get())) {
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
