#include "codegen.hpp"

#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Type.h>
#include <llvm/IR/Verifier.h>
#include <llvm/Support/FileSystem.h>
#include <llvm/Support/raw_ostream.h>

#include <cstdint>
#include <string>
#include <system_error>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace lumen {

struct CodeGenerator::Impl {
    explicit Impl(std::string moduleName)
        : module(std::move(moduleName), context), builder(context) {}

    llvm::LLVMContext context;
    llvm::Module module;
    llvm::IRBuilder<> builder;
    std::vector<std::unordered_map<std::string, llvm::AllocaInst*>> scopes;
    std::unordered_set<std::string> definitions;
    std::string errorMessage;

    llvm::Type* intType() { return llvm::Type::getInt32Ty(context); }

    bool fail(int line, const std::string& message) {
        if (errorMessage.empty()) {
            errorMessage = "line " + std::to_string(line) + ": " + message;
        }
        return false;
    }

    llvm::Value* failExpr(int line, const std::string& message) {
        fail(line, message);
        return nullptr;
    }

    llvm::AllocaInst* createAlloca(llvm::Function* function, const std::string& name) {
        llvm::IRBuilder<> entryBuilder(&function->getEntryBlock(),
                                       function->getEntryBlock().begin());
        return entryBuilder.CreateAlloca(intType(), nullptr, name);
    }

    llvm::AllocaInst* lookup(const std::string& name) {
        for (auto scope = scopes.rbegin(); scope != scopes.rend(); ++scope) {
            const auto found = scope->find(name);
            if (found != scope->end()) return found->second;
        }
        return nullptr;
    }

    llvm::Value* asBool(llvm::Value* value) {
        return builder.CreateICmpNE(value, llvm::ConstantInt::get(intType(), 0), "truth");
    }

    llvm::Function* declareFunction(const std::string& name,
                                    const std::vector<std::string>& params, int line) {
        if (llvm::Function* existing = module.getFunction(name)) {
            if (existing->arg_size() != params.size()) {
                fail(line, "conflicting declaration of function '" + name + "'");
                return nullptr;
            }
            return existing;
        }

        std::vector<llvm::Type*> parameterTypes(params.size(), intType());
        auto* type = llvm::FunctionType::get(intType(), parameterTypes, false);
        auto* function = llvm::Function::Create(type, llvm::Function::ExternalLinkage,
                                                name, module);
        std::size_t index = 0;
        for (auto& argument : function->args()) argument.setName(params[index++]);
        return function;
    }

    llvm::Value* emitShortCircuit(const BinaryExpr& expr) {
        llvm::Value* left = emitExpr(*expr.lhs);
        if (!left) return nullptr;
        llvm::Value* leftBool = asBool(left);
        llvm::BasicBlock* leftBlock = builder.GetInsertBlock();
        llvm::Function* function = leftBlock->getParent();
        llvm::BasicBlock* rhsBlock = llvm::BasicBlock::Create(context, "logic.rhs", function);
        llvm::BasicBlock* mergeBlock = llvm::BasicBlock::Create(context, "logic.end", function);

        const bool isAnd = expr.op == "&&";
        if (isAnd) builder.CreateCondBr(leftBool, rhsBlock, mergeBlock);
        else builder.CreateCondBr(leftBool, mergeBlock, rhsBlock);

        builder.SetInsertPoint(rhsBlock);
        llvm::Value* right = emitExpr(*expr.rhs);
        if (!right) return nullptr;
        llvm::Value* rightBool = asBool(right);
        llvm::BasicBlock* completedRhs = builder.GetInsertBlock();
        builder.CreateBr(mergeBlock);

        builder.SetInsertPoint(mergeBlock);
        auto* phi = builder.CreatePHI(llvm::Type::getInt1Ty(context), 2, "logic");
        phi->addIncoming(llvm::ConstantInt::get(llvm::Type::getInt1Ty(context), !isAnd),
                         leftBlock);
        phi->addIncoming(rightBool, completedRhs);
        return builder.CreateZExt(phi, intType(), "logic.i32");
    }

    llvm::Value* emitExpr(const Expr& expr) {
        if (const auto* number = dynamic_cast<const NumberExpr*>(&expr)) {
            return llvm::ConstantInt::get(intType(), static_cast<std::uint64_t>(number->value), true);
        }
        if (const auto* variable = dynamic_cast<const VariableExpr*>(&expr)) {
            llvm::AllocaInst* slot = lookup(variable->name);
            if (!slot) return failExpr(expr.line, "unknown variable '" + variable->name + "'");
            return builder.CreateLoad(intType(), slot, variable->name);
        }
        if (const auto* assign = dynamic_cast<const AssignExpr*>(&expr)) {
            llvm::AllocaInst* slot = lookup(assign->name);
            if (!slot) return failExpr(expr.line, "assignment to unknown variable '" + assign->name + "'");
            llvm::Value* value = emitExpr(*assign->value);
            if (!value) return nullptr;
            builder.CreateStore(value, slot);
            return value;
        }
        if (const auto* unary = dynamic_cast<const UnaryExpr*>(&expr)) {
            llvm::Value* operand = emitExpr(*unary->operand);
            if (!operand) return nullptr;
            if (unary->op == "-") return builder.CreateNeg(operand, "neg");
            if (unary->op == "!") {
                llvm::Value* result = builder.CreateNot(asBool(operand), "not");
                return builder.CreateZExt(result, intType(), "not.i32");
            }
            return failExpr(expr.line, "unsupported unary operator '" + unary->op + "'");
        }
        if (const auto* binary = dynamic_cast<const BinaryExpr*>(&expr)) {
            if (binary->op == "&&" || binary->op == "||") return emitShortCircuit(*binary);
            llvm::Value* left = emitExpr(*binary->lhs);
            llvm::Value* right = left ? emitExpr(*binary->rhs) : nullptr;
            if (!left || !right) return nullptr;

            llvm::Value* result = nullptr;
            if (binary->op == "+") result = builder.CreateAdd(left, right, "add");
            else if (binary->op == "-") result = builder.CreateSub(left, right, "sub");
            else if (binary->op == "*") result = builder.CreateMul(left, right, "mul");
            else if (binary->op == "/") result = builder.CreateSDiv(left, right, "div");
            else if (binary->op == "%") result = builder.CreateSRem(left, right, "rem");
            else if (binary->op == "==") result = builder.CreateICmpEQ(left, right, "eq");
            else if (binary->op == "!=") result = builder.CreateICmpNE(left, right, "ne");
            else if (binary->op == "<") result = builder.CreateICmpSLT(left, right, "lt");
            else if (binary->op == ">") result = builder.CreateICmpSGT(left, right, "gt");
            else if (binary->op == "<=") result = builder.CreateICmpSLE(left, right, "le");
            else if (binary->op == ">=") result = builder.CreateICmpSGE(left, right, "ge");
            else return failExpr(expr.line, "unsupported binary operator '" + binary->op + "'");

            if (result->getType()->isIntegerTy(1)) {
                result = builder.CreateZExt(result, intType(), "cmp.i32");
            }
            return result;
        }
        if (const auto* call = dynamic_cast<const CallExpr*>(&expr)) {
            llvm::Function* callee = module.getFunction(call->callee);
            if (!callee) return failExpr(expr.line, "unknown function '" + call->callee + "'");
            if (callee->arg_size() != call->args.size()) {
                return failExpr(expr.line, "function '" + call->callee + "' expects " +
                                 std::to_string(callee->arg_size()) + " argument(s), got " +
                                 std::to_string(call->args.size()));
            }
            std::vector<llvm::Value*> arguments;
            arguments.reserve(call->args.size());
            for (const auto& argument : call->args) {
                llvm::Value* value = emitExpr(*argument);
                if (!value) return nullptr;
                arguments.push_back(value);
            }
            return builder.CreateCall(callee, arguments, "call");
        }
        return failExpr(expr.line, "internal error: unknown expression node");
    }

    bool emitBlock(const BlockStmt& block, bool introduceScope) {
        if (introduceScope) scopes.emplace_back();
        bool ok = true;
        for (const auto& statement : block.body) {
            if (builder.GetInsertBlock()->getTerminator()) break;
            if (!emitStmt(*statement)) {
                ok = false;
                break;
            }
        }
        if (introduceScope) scopes.pop_back();
        return ok;
    }

    bool emitScoped(const Stmt& statement) {
        scopes.emplace_back();
        const bool ok = emitStmt(statement);
        scopes.pop_back();
        return ok;
    }

    bool emitStmt(const Stmt& stmt) {
        if (const auto* block = dynamic_cast<const BlockStmt*>(&stmt)) {
            return emitBlock(*block, true);
        }
        if (const auto* expression = dynamic_cast<const ExprStmt*>(&stmt)) {
            return emitExpr(*expression->expr) != nullptr;
        }
        if (const auto* declaration = dynamic_cast<const VarDeclStmt*>(&stmt)) {
            auto& currentScope = scopes.back();
            if (currentScope.count(declaration->name)) {
                return fail(stmt.line, "redeclaration of variable '" + declaration->name + "'");
            }
            llvm::Function* function = builder.GetInsertBlock()->getParent();
            llvm::AllocaInst* slot = createAlloca(function, declaration->name);
            llvm::Value* initialValue = declaration->init
                ? emitExpr(*declaration->init)
                : llvm::ConstantInt::get(intType(), 0);
            if (!initialValue) return false;
            builder.CreateStore(initialValue, slot);
            currentScope.emplace(declaration->name, slot);
            return true;
        }
        if (const auto* returnStmt = dynamic_cast<const ReturnStmt*>(&stmt)) {
            llvm::Value* value = emitExpr(*returnStmt->value);
            if (!value) return false;
            builder.CreateRet(value);
            return true;
        }
        if (const auto* ifStmt = dynamic_cast<const IfStmt*>(&stmt)) {
            llvm::Value* condition = emitExpr(*ifStmt->cond);
            if (!condition) return false;
            llvm::Function* function = builder.GetInsertBlock()->getParent();
            auto* thenBlock = llvm::BasicBlock::Create(context, "if.then", function);
            auto* elseBlock = ifStmt->elseBranch
                ? llvm::BasicBlock::Create(context, "if.else", function) : nullptr;
            auto* mergeBlock = llvm::BasicBlock::Create(context, "if.end", function);
            builder.CreateCondBr(asBool(condition), thenBlock, elseBlock ? elseBlock : mergeBlock);

            builder.SetInsertPoint(thenBlock);
            if (!emitScoped(*ifStmt->thenBranch)) return false;
            bool thenFallsThrough = !builder.GetInsertBlock()->getTerminator();
            if (thenFallsThrough) builder.CreateBr(mergeBlock);

            bool elseFallsThrough = true;
            if (elseBlock) {
                builder.SetInsertPoint(elseBlock);
                if (!emitScoped(*ifStmt->elseBranch)) return false;
                elseFallsThrough = !builder.GetInsertBlock()->getTerminator();
                if (elseFallsThrough) builder.CreateBr(mergeBlock);
            }

            builder.SetInsertPoint(mergeBlock);
            if (elseBlock && !thenFallsThrough && !elseFallsThrough) builder.CreateUnreachable();
            return true;
        }
        if (const auto* whileStmt = dynamic_cast<const WhileStmt*>(&stmt)) {
            llvm::Function* function = builder.GetInsertBlock()->getParent();
            auto* conditionBlock = llvm::BasicBlock::Create(context, "while.cond", function);
            auto* bodyBlock = llvm::BasicBlock::Create(context, "while.body", function);
            auto* endBlock = llvm::BasicBlock::Create(context, "while.end", function);
            builder.CreateBr(conditionBlock);

            builder.SetInsertPoint(conditionBlock);
            llvm::Value* condition = emitExpr(*whileStmt->cond);
            if (!condition) return false;
            builder.CreateCondBr(asBool(condition), bodyBlock, endBlock);

            builder.SetInsertPoint(bodyBlock);
            if (!emitScoped(*whileStmt->body)) return false;
            if (!builder.GetInsertBlock()->getTerminator()) builder.CreateBr(conditionBlock);
            builder.SetInsertPoint(endBlock);
            return true;
        }
        return fail(stmt.line, "internal error: unknown statement node");
    }

    bool emitFunction(const Function& astFunction) {
        llvm::Function* function = module.getFunction(astFunction.name);
        if (!function) return fail(astFunction.line, "internal error: missing function declaration");
        if (!function->empty() || !definitions.insert(astFunction.name).second) {
            return fail(astFunction.line, "redefinition of function '" + astFunction.name + "'");
        }

        auto* entry = llvm::BasicBlock::Create(context, "entry", function);
        builder.SetInsertPoint(entry);
        scopes.clear();
        scopes.emplace_back();

        std::size_t index = 0;
        for (auto& argument : function->args()) {
            const std::string& parameterName = astFunction.params[index++];
            if (scopes.back().count(parameterName)) {
                return fail(astFunction.line, "duplicate parameter '" + parameterName + "'");
            }
            argument.setName(parameterName);
            llvm::AllocaInst* slot = createAlloca(function, parameterName);
            builder.CreateStore(&argument, slot);
            scopes.back().emplace(parameterName, slot);
        }

        if (!emitBlock(*astFunction.body, false)) return false;
        if (!builder.GetInsertBlock()->getTerminator()) {
            return fail(astFunction.line, "function '" + astFunction.name + "' may reach the end without returning");
        }

        std::string verificationError;
        llvm::raw_string_ostream stream(verificationError);
        if (llvm::verifyFunction(*function, &stream)) {
            stream.flush();
            return fail(astFunction.line, "LLVM verification failed for '" +
                                         astFunction.name + "': " + verificationError);
        }
        return true;
    }

    bool generate(const Program& program) {
        for (const auto& declaration : program.declarations) {
            if (const auto* prototype = dynamic_cast<const Prototype*>(declaration.get())) {
                if (!declareFunction(prototype->name, prototype->params, prototype->line)) return false;
            } else if (const auto* function = dynamic_cast<const Function*>(declaration.get())) {
                if (!declareFunction(function->name, function->params, function->line)) return false;
            }
        }

        for (const auto& declaration : program.declarations) {
            if (const auto* function = dynamic_cast<const Function*>(declaration.get())) {
                if (!emitFunction(*function)) return false;
            }
        }

        std::string verificationError;
        llvm::raw_string_ostream stream(verificationError);
        if (llvm::verifyModule(module, &stream)) {
            stream.flush();
            errorMessage = "LLVM module verification failed: " + verificationError;
            return false;
        }
        return true;
    }
};

CodeGenerator::CodeGenerator(std::string moduleName)
    : impl_(std::make_unique<Impl>(std::move(moduleName))) {}

CodeGenerator::~CodeGenerator() = default;

bool CodeGenerator::generate(const Program& program) {
    return impl_->generate(program);
}

bool CodeGenerator::writeIR(const std::string& outputPath) const {
    std::error_code ec;
    llvm::raw_fd_ostream output(outputPath, ec, llvm::sys::fs::OF_Text);
    if (ec) {
        impl_->errorMessage = "cannot write '" + outputPath + "': " + ec.message();
        return false;
    }
    impl_->module.print(output, nullptr);
    return true;
}

void CodeGenerator::printIR() const {
    impl_->module.print(llvm::outs(), nullptr);
}

const std::string& CodeGenerator::error() const {
    return impl_->errorMessage;
}

} // namespace lumen
