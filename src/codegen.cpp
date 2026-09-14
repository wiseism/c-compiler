#include "codegen.hpp"

#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/GlobalVariable.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Intrinsics.h>
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
    std::vector<std::unordered_map<std::string, llvm::Value*>> scopes;
    std::unordered_set<std::string> definitions;
    std::string errorMessage;

    llvm::Type* intType() { return llvm::Type::getInt32Ty(context); }

    llvm::Type* llvmType(const TypePtr& type) {
        switch (type->kind()) {
        case TypeKind::Int32:
            return intType();
        case TypeKind::Void:
            return llvm::Type::getVoidTy(context);
        case TypeKind::Pointer:
            if (type->elementType()->kind() == TypeKind::Void) {
                return llvm::PointerType::getUnqual(llvm::Type::getInt8Ty(context));
            }
            return llvm::PointerType::getUnqual(llvmType(type->elementType()));
        case TypeKind::Array:
            return llvm::ArrayType::get(llvmType(type->elementType()), type->arrayLength());
        case TypeKind::NullPointer:
            break;
        }
        return nullptr;
    }

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

    llvm::AllocaInst* createAlloca(llvm::Function* function, const TypePtr& type,
                                   const std::string& name) {
        llvm::IRBuilder<> entryBuilder(&function->getEntryBlock(),
                                       function->getEntryBlock().begin());
        return entryBuilder.CreateAlloca(llvmType(type), nullptr, name);
    }

    llvm::Value* lookup(const std::string& name) {
        for (auto scope = scopes.rbegin(); scope != scopes.rend(); ++scope) {
            const auto found = scope->find(name);
            if (found != scope->end()) return found->second;
        }
        return module.getNamedGlobal(name);
    }

    llvm::Value* asBool(llvm::Value* value) {
        if (value->getType()->isPointerTy()) {
            auto* pointerType = llvm::cast<llvm::PointerType>(value->getType());
            return builder.CreateICmpNE(value, llvm::ConstantPointerNull::get(pointerType),
                                        "ptr.truth");
        }
        return builder.CreateICmpNE(value, llvm::ConstantInt::get(intType(), 0), "truth");
    }

    void emitTrapUnless(llvm::Value* condition, const std::string& name) {
        llvm::Function* function = builder.GetInsertBlock()->getParent();
        auto* validBlock = llvm::BasicBlock::Create(context, name + ".ok", function);
        auto* trapBlock = llvm::BasicBlock::Create(context, name + ".trap", function);
        builder.CreateCondBr(condition, validBlock, trapBlock);
        builder.SetInsertPoint(trapBlock);
        llvm::Function* trap = llvm::Intrinsic::getDeclaration(&module, llvm::Intrinsic::trap);
        builder.CreateCall(trap);
        builder.CreateUnreachable();
        builder.SetInsertPoint(validBlock);
    }

    void emitNullCheck(llvm::Value* pointer) {
        auto* pointerType = llvm::cast<llvm::PointerType>(pointer->getType());
        llvm::Value* notNull = builder.CreateICmpNE(
            pointer, llvm::ConstantPointerNull::get(pointerType), "nonnull");
        emitTrapUnless(notNull, "nullcheck");
    }

    void emitBoundsCheck(llvm::Value* index, std::size_t length) {
        llvm::Value* nonNegative = builder.CreateICmpSGE(
            index, llvm::ConstantInt::get(intType(), 0), "index.nonnegative");
        llvm::Value* belowLength = builder.CreateICmpSLT(
            index, llvm::ConstantInt::get(intType(), length), "index.inrange");
        emitTrapUnless(builder.CreateAnd(nonNegative, belowLength, "index.valid"),
                       "boundscheck");
    }

    llvm::Function* declareFunction(const TypePtr& returnType, const std::string& name,
                                    const std::vector<Parameter>& params, int line) {
        if (llvm::Function* existing = module.getFunction(name)) {
            if (existing->arg_size() != params.size()) {
                fail(line, "conflicting declaration of function '" + name + "'");
                return nullptr;
            }
            return existing;
        }

        std::vector<llvm::Type*> parameterTypes;
        parameterTypes.reserve(params.size());
        for (const auto& parameter : params) parameterTypes.push_back(llvmType(parameter.type));
        auto* type = llvm::FunctionType::get(llvmType(returnType), parameterTypes, false);
        auto* function = llvm::Function::Create(type, llvm::Function::ExternalLinkage,
                                                name, module);
        std::size_t index = 0;
        for (auto& argument : function->args()) argument.setName(params[index++].name);
        return function;
    }

    bool declareGlobal(const GlobalVar& global) {
        llvm::GlobalVariable* variable = module.getNamedGlobal(global.name);
        if (!variable) {
            llvm::Type* type = llvmType(global.declaredType);
            llvm::Constant* initializer = nullptr;
            if (!global.isExtern) {
                const auto* number = dynamic_cast<const NumberExpr*>(global.init.get());
                if (number) {
                    initializer = llvm::ConstantInt::get(intType(),
                        static_cast<std::uint64_t>(number->value), true);
                } else {
                    initializer = llvm::Constant::getNullValue(type);
                }
            }
            new llvm::GlobalVariable(module, type, false,
                                     llvm::GlobalValue::ExternalLinkage,
                                     initializer, global.name);
            return true;
        }
        if (!global.isExtern) {
            if (variable->hasInitializer()) {
                return fail(global.line, "redefinition of global variable '" + global.name + "'");
            }
            const auto* number = dynamic_cast<const NumberExpr*>(global.init.get());
            llvm::Constant* initializer = number
                ? static_cast<llvm::Constant*>(llvm::ConstantInt::get(intType(),
                      static_cast<std::uint64_t>(number->value), true))
                : llvm::Constant::getNullValue(llvmType(global.declaredType));
            variable->setInitializer(initializer);
        }
        return true;
    }

    llvm::Value* emitAddress(const Expr& expr) {
        if (const auto* variable = dynamic_cast<const VariableExpr*>(&expr)) {
            llvm::Value* slot = lookup(variable->name);
            if (!slot) return failExpr(expr.line, "unknown variable '" + variable->name + "'");
            return slot;
        }
        if (const auto* unary = dynamic_cast<const UnaryExpr*>(&expr)) {
            if (unary->op != "*") {
                return failExpr(expr.line, "expression is not addressable");
            }
            llvm::Value* pointer = emitExpr(*unary->operand);
            if (!pointer) return nullptr;
            emitNullCheck(pointer);
            return pointer;
        }
        if (const auto* index = dynamic_cast<const IndexExpr*>(&expr)) {
            llvm::Value* indexValue = emitExpr(*index->index);
            if (!indexValue) return nullptr;
            if (isArrayType(index->base->inferredType)) {
                llvm::Value* arrayAddress = emitAddress(*index->base);
                if (!arrayAddress) return nullptr;
                emitBoundsCheck(indexValue, index->base->inferredType->arrayLength());
                llvm::Value* zero = llvm::ConstantInt::get(intType(), 0);
                return builder.CreateInBoundsGEP(
                    llvmType(index->base->inferredType), arrayAddress,
                    {zero, indexValue}, "array.element");
            }
            llvm::Value* pointer = emitExpr(*index->base);
            if (!pointer) return nullptr;
            emitNullCheck(pointer);
            return builder.CreateInBoundsGEP(
                llvmType(index->base->inferredType->elementType()), pointer,
                indexValue, "pointer.element");
        }
        return failExpr(expr.line, "expression is not addressable");
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
        if (dynamic_cast<const NullExpr*>(&expr)) {
            auto* type = llvm::dyn_cast_or_null<llvm::PointerType>(llvmType(expr.inferredType));
            if (!type) return failExpr(expr.line, "null literal has no pointer context");
            return llvm::ConstantPointerNull::get(type);
        }
        if (const auto* variable = dynamic_cast<const VariableExpr*>(&expr)) {
            if (isArrayType(expr.inferredType)) {
                return failExpr(expr.line, "array value requires an index");
            }
            llvm::Value* slot = emitAddress(expr);
            if (!slot) return nullptr;
            return builder.CreateLoad(llvmType(expr.inferredType), slot, variable->name);
        }
        if (const auto* assign = dynamic_cast<const AssignExpr*>(&expr)) {
            llvm::Value* slot = emitAddress(*assign->target);
            if (!slot) return nullptr;
            llvm::Value* value = emitExpr(*assign->value);
            if (!value) return nullptr;
            builder.CreateStore(value, slot);
            return value;
        }
        if (const auto* unary = dynamic_cast<const UnaryExpr*>(&expr)) {
            if (unary->op == "&") return emitAddress(*unary->operand);
            if (unary->op == "*") {
                llvm::Value* address = emitAddress(expr);
                if (!address) return nullptr;
                return builder.CreateLoad(llvmType(expr.inferredType), address, "deref");
            }
            llvm::Value* operand = emitExpr(*unary->operand);
            if (!operand) return nullptr;
            if (unary->op == "-") return builder.CreateNeg(operand, "neg");
            if (unary->op == "!") {
                llvm::Value* result = builder.CreateNot(asBool(operand), "not");
                return builder.CreateZExt(result, intType(), "not.i32");
            }
            return failExpr(expr.line, "unsupported unary operator '" + unary->op + "'");
        }
        if (dynamic_cast<const IndexExpr*>(&expr)) {
            llvm::Value* address = emitAddress(expr);
            if (!address) return nullptr;
            return builder.CreateLoad(llvmType(expr.inferredType), address, "element");
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
            return builder.CreateCall(callee, arguments,
                                      callee->getReturnType()->isVoidTy() ? "" : "call");
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
            llvm::AllocaInst* slot = createAlloca(function, declaration->declaredType,
                                                  declaration->name);
            llvm::Value* initialValue = declaration->init
                ? emitExpr(*declaration->init)
                : llvm::Constant::getNullValue(llvmType(declaration->declaredType));
            if (!initialValue) return false;
            builder.CreateStore(initialValue, slot);
            currentScope.emplace(declaration->name, slot);
            return true;
        }
        if (const auto* returnStmt = dynamic_cast<const ReturnStmt*>(&stmt)) {
            if (!returnStmt->value) {
                builder.CreateRetVoid();
                return true;
            }
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
            const std::string& parameterName = astFunction.params[index++].name;
            if (scopes.back().count(parameterName)) {
                return fail(astFunction.line, "duplicate parameter '" + parameterName + "'");
            }
            argument.setName(parameterName);
            llvm::AllocaInst* slot = createAlloca(
                function, astFunction.params[index - 1].type, parameterName);
            builder.CreateStore(&argument, slot);
            scopes.back().emplace(parameterName, slot);
        }

        if (!emitBlock(*astFunction.body, false)) return false;
        if (!builder.GetInsertBlock()->getTerminator()) {
            if (astFunction.returnType->kind() == TypeKind::Void) builder.CreateRetVoid();
            else return fail(astFunction.line, "function '" + astFunction.name +
                                               "' may reach the end without returning");
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
                if (!declareFunction(prototype->returnType, prototype->name,
                                     prototype->params, prototype->line)) return false;
            } else if (const auto* function = dynamic_cast<const Function*>(declaration.get())) {
                if (!declareFunction(function->returnType, function->name,
                                     function->params, function->line)) return false;
            } else if (const auto* global = dynamic_cast<const GlobalVar*>(declaration.get())) {
                if (!declareGlobal(*global)) return false;
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
