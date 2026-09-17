#include "constant_fold.hpp"

#include <cstdint>
#include <memory>
#include <string>

namespace lumen {

struct ConstantFolder::Impl {
    std::string errorMessage;

    bool fail(int line, const std::string& message) {
        if (errorMessage.empty()) {
            errorMessage = "line " + std::to_string(line) + ": " + message;
        }
        return false;
    }

    static NumberExpr* number(Expr* expr) { return dynamic_cast<NumberExpr*>(expr); }

    static std::int32_t valueOf(const NumberExpr& expr) {
        return static_cast<std::int32_t>(static_cast<std::uint32_t>(expr.value));
    }

    static void replaceWithNumber(std::unique_ptr<Expr>& expr, std::int32_t value) {
        const int line = expr->line;
        auto replacement = std::make_unique<NumberExpr>(value, line);
        replacement->inferredType = Type::int32();
        expr = std::move(replacement);
    }

    void foldExpr(std::unique_ptr<Expr>& expr) {
        if (!expr || number(expr.get()) || dynamic_cast<NullExpr*>(expr.get()) ||
            dynamic_cast<VariableExpr*>(expr.get())) return;

        if (auto* assign = dynamic_cast<AssignExpr*>(expr.get())) {
            foldExpr(assign->target);
            foldExpr(assign->value);
            return;
        }
        if (auto* call = dynamic_cast<CallExpr*>(expr.get())) {
            for (auto& argument : call->args) foldExpr(argument);
            return;
        }
        if (auto* unary = dynamic_cast<UnaryExpr*>(expr.get())) {
            foldExpr(unary->operand);
            NumberExpr* operand = number(unary->operand.get());
            if (!operand) return;
            const std::int32_t value = valueOf(*operand);
            if (unary->op == "-") {
                const auto bits = std::uint32_t{0} - static_cast<std::uint32_t>(value);
                replaceWithNumber(expr, static_cast<std::int32_t>(bits));
            } else if (unary->op == "!") {
                replaceWithNumber(expr, value == 0 ? 1 : 0);
            }
            return;
        }
        if (auto* index = dynamic_cast<IndexExpr*>(expr.get())) {
            foldExpr(index->base);
            foldExpr(index->index);
            const auto* foldedIndex = number(index->index.get());
            if (foldedIndex && index->base->inferredType &&
                isArrayType(index->base->inferredType)) {
                const std::int32_t value = valueOf(*foldedIndex);
                if (value < 0 || static_cast<std::uint32_t>(value) >=
                                     index->base->inferredType->arrayLength()) {
                    fail(index->index->line, "array index " + std::to_string(value) +
                         " is outside [0, " +
                         std::to_string(index->base->inferredType->arrayLength()) + ")");
                }
            }
            return;
        }
        auto* binary = dynamic_cast<BinaryExpr*>(expr.get());
        if (!binary) return;

        foldExpr(binary->lhs);
        NumberExpr* leftNumber = number(binary->lhs.get());
        if (leftNumber && binary->op == "&&" && valueOf(*leftNumber) == 0) {
            replaceWithNumber(expr, 0);
            return;
        }
        if (leftNumber && binary->op == "||" && valueOf(*leftNumber) != 0) {
            replaceWithNumber(expr, 1);
            return;
        }

        foldExpr(binary->rhs);
        leftNumber = number(binary->lhs.get());
        NumberExpr* rightNumber = number(binary->rhs.get());
        if (!leftNumber || !rightNumber) return;
        const std::int32_t left = valueOf(*leftNumber);
        const std::int32_t right = valueOf(*rightNumber);
        std::int32_t result = 0;

        if (binary->op == "+") {
            result = static_cast<std::int32_t>(static_cast<std::uint32_t>(left) +
                                               static_cast<std::uint32_t>(right));
        } else if (binary->op == "-") {
            result = static_cast<std::int32_t>(static_cast<std::uint32_t>(left) -
                                               static_cast<std::uint32_t>(right));
        } else if (binary->op == "*") {
            result = static_cast<std::int32_t>(static_cast<std::uint32_t>(left) *
                                               static_cast<std::uint32_t>(right));
        } else if (binary->op == "/") {
            if (right == 0 || (left == INT32_MIN && right == -1)) return;
            result = left / right;
        } else if (binary->op == "%") {
            if (right == 0 || (left == INT32_MIN && right == -1)) return;
            result = left % right;
        } else if (binary->op == "^") {
            result = static_cast<std::int32_t>(static_cast<std::uint32_t>(left) ^
                                               static_cast<std::uint32_t>(right));
        } else if (binary->op == "==") result = left == right;
        else if (binary->op == "!=") result = left != right;
        else if (binary->op == "<") result = left < right;
        else if (binary->op == ">") result = left > right;
        else if (binary->op == "<=") result = left <= right;
        else if (binary->op == ">=") result = left >= right;
        else if (binary->op == "&&") result = left != 0 && right != 0;
        else if (binary->op == "||") result = left != 0 || right != 0;
        else return;

        replaceWithNumber(expr, result);
    }

    void foldStmt(Stmt& stmt) {
        if (auto* block = dynamic_cast<BlockStmt*>(&stmt)) {
            for (auto& child : block->body) foldStmt(*child);
        } else if (auto* expression = dynamic_cast<ExprStmt*>(&stmt)) {
            foldExpr(expression->expr);
        } else if (auto* declaration = dynamic_cast<VarDeclStmt*>(&stmt)) {
            foldExpr(declaration->init);
        } else if (auto* returnStmt = dynamic_cast<ReturnStmt*>(&stmt)) {
            foldExpr(returnStmt->value);
        } else if (auto* ifStmt = dynamic_cast<IfStmt*>(&stmt)) {
            foldExpr(ifStmt->cond);
            foldStmt(*ifStmt->thenBranch);
            if (ifStmt->elseBranch) foldStmt(*ifStmt->elseBranch);
        } else if (auto* whileStmt = dynamic_cast<WhileStmt*>(&stmt)) {
            foldExpr(whileStmt->cond);
            foldStmt(*whileStmt->body);
        }
    }

    bool run(Program& program) {
        errorMessage.clear();
        for (auto& declaration : program.declarations) {
            if (auto* global = dynamic_cast<GlobalVar*>(declaration.get())) {
                foldExpr(global->init);
                if (global->init && !number(global->init.get()) &&
                    !dynamic_cast<NullExpr*>(global->init.get())) {
                    return fail(global->line, "initializer for global '" + global->name +
                                              "' is not a constant expression");
                }
            } else if (auto* function = dynamic_cast<Function*>(declaration.get())) {
                foldStmt(*function->body);
            }
            if (!errorMessage.empty()) return false;
        }
        return true;
    }
};

ConstantFolder::ConstantFolder() : impl_(std::make_unique<Impl>()) {}
ConstantFolder::~ConstantFolder() = default;

bool ConstantFolder::run(Program& program) { return impl_->run(program); }
const std::string& ConstantFolder::error() const { return impl_->errorMessage; }

} // namespace lumen
