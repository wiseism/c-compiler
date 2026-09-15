#include "codegen.hpp"

#include <llvm/Analysis/CGSCCPassManager.h>
#include <llvm/Analysis/LoopAnalysisManager.h>
#include <llvm/Config/llvm-config.h>
#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/DIBuilder.h>
#include <llvm/IR/DebugInfoMetadata.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/GlobalVariable.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Intrinsics.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/LegacyPassManager.h>
#include <llvm/IR/PassManager.h>
#include <llvm/IR/PassInstrumentation.h>
#include <llvm/IR/Type.h>
#include <llvm/IR/Verifier.h>
#if LLVM_VERSION_MAJOR >= 12
#include <llvm/Passes/OptimizationLevel.h>
#endif
#include <llvm/Passes/PassBuilder.h>
#include <llvm/Support/CodeGen.h>
#include <llvm/BinaryFormat/Dwarf.h>
#include <llvm/Support/FileSystem.h>
#if __has_include(<llvm/TargetParser/Host.h>)
#include <llvm/TargetParser/Host.h>
#else
#include <llvm/Support/Host.h>
#endif
#if __has_include(<llvm/MC/TargetRegistry.h>)
#include <llvm/MC/TargetRegistry.h>
#else
#include <llvm/Support/TargetRegistry.h>
#endif
#include <llvm/Support/TargetSelect.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Target/TargetMachine.h>
#include <llvm/Target/TargetOptions.h>

#include <cctype>
#include <cstdint>
#include <filesystem>
#include <iomanip>
#include <optional>
#include <sstream>
#include <string>
#include <system_error>
#include <unordered_map>
#include <utility>
#include <vector>

namespace lumen {

struct CodeGenerator::Impl {
    Impl(std::string moduleName, CodegenOptions requestedOptions)
        : module(std::move(moduleName), context), builder(context),
          options(requestedOptions) {}

    llvm::LLVMContext context;
    llvm::Module module;
    llvm::IRBuilder<> builder;
    CodegenOptions options;
    std::unique_ptr<llvm::TargetMachine> targetMachine;
    std::unique_ptr<llvm::DIBuilder> debugBuilder;
    llvm::DIFile* debugFile = nullptr;
    llvm::DISubprogram* debugScope = nullptr;
    const TypedProgram* typedProgram = nullptr;
    std::unordered_map<SymbolId, llvm::Value*> storage;
    std::string errorMessage;
    std::size_t snapshotIndex = 0;
    std::string lastSnapshotIR;

    bool writeSnapshot(const std::string& label, bool force = false) {
        if (options.passSnapshotDirectory.empty()) return true;
        std::string currentIR;
        llvm::raw_string_ostream stream(currentIR);
        module.print(stream, nullptr);
        stream.flush();
        if (!force && currentIR == lastSnapshotIR) return true;
        lastSnapshotIR = currentIR;
        std::error_code ec;
        std::filesystem::create_directories(options.passSnapshotDirectory, ec);
        if (ec) {
            errorMessage = "cannot create pass snapshot directory '" +
                           options.passSnapshotDirectory + "': " + ec.message();
            return false;
        }
        std::string safeLabel;
        safeLabel.reserve(label.size());
        for (unsigned char character : label) {
            safeLabel.push_back(std::isalnum(character) ? static_cast<char>(character) : '_');
        }
        if (safeLabel.size() > 80) safeLabel.resize(80);
        std::ostringstream filename;
        filename << std::setfill('0') << std::setw(4) << snapshotIndex++ << '-'
                 << safeLabel << ".ll";
        const std::filesystem::path path =
            std::filesystem::path(options.passSnapshotDirectory) / filename.str();
        llvm::raw_fd_ostream output(path.string(), ec, llvm::sys::fs::OF_Text);
        if (ec) {
            errorMessage = "cannot write pass snapshot '" + path.string() + "': " +
                           ec.message();
            return false;
        }
        output << currentIR;
        return true;
    }

    void initializeDebugInfo() {
        if (!options.debugInfo) return;
        debugBuilder = std::make_unique<llvm::DIBuilder>(module);
        const std::filesystem::path path = options.sourcePath.empty()
            ? std::filesystem::path(module.getName().str())
            : std::filesystem::path(options.sourcePath);
        debugFile = debugBuilder->createFile(path.filename().string(),
                                             path.parent_path().string());
        debugBuilder->createCompileUnit(llvm::dwarf::DW_LANG_C, debugFile, "lumenc",
                                        options.optimizationLevel != 0, "", 0);
        module.addModuleFlag(llvm::Module::Warning, "Debug Info Version",
                             llvm::DEBUG_METADATA_VERSION);
        module.addModuleFlag(llvm::Module::Warning, "Dwarf Version", 4);
    }

    llvm::DIType* debugType(const TypePtr& type) {
        if (!debugBuilder || type->kind() == TypeKind::Void) return nullptr;
        if (type->kind() == TypeKind::Int32) {
            return debugBuilder->createBasicType("int", 32, llvm::dwarf::DW_ATE_signed);
        }
        if (type->kind() == TypeKind::Pointer) {
            return debugBuilder->createPointerType(debugType(type->elementType()), 64);
        }
        if (type->kind() == TypeKind::Array) {
            llvm::DIType* element = debugType(type->elementType());
            llvm::Metadata* subrange = debugBuilder->getOrCreateSubrange(0, type->arrayLength());
            return debugBuilder->createArrayType(
                32 * type->arrayLength(), 32, element,
                debugBuilder->getOrCreateArray({subrange}));
        }
        return nullptr;
    }

    void setDebugLocation(int line, int column) {
        if (!debugScope) return;
        builder.SetCurrentDebugLocation(
            llvm::DebugLoc(llvm::DILocation::get(context, line, column, debugScope)));
    }

    void declareDebugVariable(llvm::Value* storageValue, const std::string& name,
                              const TypePtr& type, int line, int column,
                              bool parameter = false, unsigned argument = 0) {
        if (!debugBuilder || !debugScope) return;
        llvm::DILocalVariable* variable = parameter
            ? debugBuilder->createParameterVariable(debugScope, name, argument, debugFile,
                                                    line, debugType(type), true)
            : debugBuilder->createAutoVariable(debugScope, name, debugFile, line,
                                               debugType(type), true);
        debugBuilder->insertDeclare(storageValue, variable, debugBuilder->createExpression(),
                                    llvm::DILocation::get(context, line, column, debugScope),
                                    builder.GetInsertBlock());
    }

    bool prepareTargetMachine() {
        if (targetMachine) return true;
        if (llvm::InitializeNativeTarget() || llvm::InitializeNativeTargetAsmPrinter()) {
            errorMessage = "failed to initialize the native LLVM target";
            return false;
        }
        const std::string triple = llvm::sys::getDefaultTargetTriple();
        std::string targetError;
        const llvm::Target* target = llvm::TargetRegistry::lookupTarget(triple, targetError);
        if (!target) {
            errorMessage = "cannot select target '" + triple + "': " + targetError;
            return false;
        }
        llvm::TargetOptions targetOptions;
#if LLVM_VERSION_MAJOR >= 16
        targetMachine.reset(target->createTargetMachine(
            triple, "generic", "", targetOptions, std::nullopt));
#else
        targetMachine.reset(target->createTargetMachine(
            triple, "generic", "", targetOptions, llvm::None));
#endif
        if (!targetMachine) {
            errorMessage = "failed to create target machine for '" + triple + "'";
            return false;
        }
        module.setTargetTriple(triple);
        module.setDataLayout(targetMachine->createDataLayout());
        return true;
    }

    bool optimizeModule() {
        if (options.optimizationLevel == 0) return true;
        if (!prepareTargetMachine()) return false;

        llvm::LoopAnalysisManager loopAnalyses;
        llvm::FunctionAnalysisManager functionAnalyses;
        llvm::CGSCCAnalysisManager cgsccAnalyses;
        llvm::ModuleAnalysisManager moduleAnalyses;
        llvm::PassInstrumentationCallbacks instrumentation;
        if (!options.passSnapshotDirectory.empty()) {
            instrumentation.registerAfterPassCallback(
                [this](llvm::StringRef passName, llvm::Any,
                       const llvm::PreservedAnalyses&) {
                    if (errorMessage.empty()) writeSnapshot(passName.str());
                });
        }
#if LLVM_VERSION_MAJOR >= 16
        llvm::PassBuilder passBuilder(targetMachine.get(), llvm::PipelineTuningOptions(),
                                      std::nullopt, &instrumentation);
#elif LLVM_VERSION_MAJOR >= 11
        llvm::PassBuilder passBuilder(targetMachine.get(), llvm::PipelineTuningOptions(),
                                      llvm::None, &instrumentation);
#else
        llvm::PassBuilder passBuilder(targetMachine.get(), llvm::None, &instrumentation);
#endif
        passBuilder.registerModuleAnalyses(moduleAnalyses);
        passBuilder.registerCGSCCAnalyses(cgsccAnalyses);
        passBuilder.registerFunctionAnalyses(functionAnalyses);
        passBuilder.registerLoopAnalyses(loopAnalyses);
        passBuilder.crossRegisterProxies(loopAnalyses, functionAnalyses,
                                         cgsccAnalyses, moduleAnalyses);
#if LLVM_VERSION_MAJOR >= 12
        llvm::OptimizationLevel level = llvm::OptimizationLevel::O1;
        if (options.optimizationLevel == 2) level = llvm::OptimizationLevel::O2;
        if (options.optimizationLevel >= 3) level = llvm::OptimizationLevel::O3;
#else
        llvm::PassBuilder::OptimizationLevel level = llvm::PassBuilder::O1;
        if (options.optimizationLevel == 2) level = llvm::PassBuilder::O2;
        if (options.optimizationLevel >= 3) level = llvm::PassBuilder::O3;
#endif
        llvm::ModulePassManager passes = passBuilder.buildPerModuleDefaultPipeline(level);
        passes.run(module, moduleAnalyses);
        return errorMessage.empty();
    }

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

    llvm::Value* lookup(const VariableExpr& variable) {
        const auto binding = typedProgram->variableBindings.find(&variable);
        if (binding == typedProgram->variableBindings.end()) return nullptr;
        const auto found = storage.find(binding->second);
        return found == storage.end() ? nullptr : found->second;
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

    llvm::Function* declareFunction(const TypedFunction& typedFunction) {
        std::vector<llvm::Type*> parameterTypes;
        parameterTypes.reserve(typedFunction.parameterTypes.size());
        for (const TypePtr& parameter : typedFunction.parameterTypes) {
            parameterTypes.push_back(llvmType(parameter));
        }
        auto* type = llvm::FunctionType::get(llvmType(typedFunction.returnType),
                                             parameterTypes, false);
        auto* function = llvm::Function::Create(type, llvm::Function::ExternalLinkage,
                                                typedFunction.name, module);
        std::size_t index = 0;
        for (auto& argument : function->args()) {
            if (typedFunction.definition) {
                argument.setName(typedFunction.definition->params[index].name);
            }
            ++index;
        }
        return function;
    }

    void declareGlobal(const TypedGlobal& global) {
        llvm::Type* type = llvmType(global.type);
        llvm::Constant* initializer = nullptr;
        if (global.definition) {
            const auto* number = dynamic_cast<const NumberExpr*>(global.definition->init.get());
            initializer = number
                ? static_cast<llvm::Constant*>(llvm::ConstantInt::get(
                      intType(), static_cast<std::uint64_t>(number->value), true))
                : llvm::Constant::getNullValue(type);
        }
        auto* variable = new llvm::GlobalVariable(
            module, type, false, llvm::GlobalValue::ExternalLinkage,
            initializer, global.name);
        storage[global.symbol] = variable;
    }

    llvm::Value* emitAddress(const Expr& expr) {
        if (const auto* variable = dynamic_cast<const VariableExpr*>(&expr)) {
            llvm::Value* slot = lookup(*variable);
            if (!slot) return failExpr(expr.line, "internal error: missing typed storage binding");
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
        setDebugLocation(expr.line, expr.column);
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
            const FunctionId functionId = typedProgram->callBindings.at(call);
            llvm::Function* callee = module.getFunction(typedProgram->function(functionId).name);
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

    bool emitBlock(const BlockStmt& block) {
        bool ok = true;
        for (const auto& statement : block.body) {
            if (builder.GetInsertBlock()->getTerminator()) break;
            if (!emitStmt(*statement)) {
                ok = false;
                break;
            }
        }
        return ok;
    }

    bool emitScoped(const Stmt& statement) {
        return emitStmt(statement);
    }

    bool emitStmt(const Stmt& stmt) {
        setDebugLocation(stmt.line, stmt.column);
        if (const auto* block = dynamic_cast<const BlockStmt*>(&stmt)) {
            return emitBlock(*block);
        }
        if (const auto* expression = dynamic_cast<const ExprStmt*>(&stmt)) {
            return emitExpr(*expression->expr) != nullptr;
        }
        if (const auto* declaration = dynamic_cast<const VarDeclStmt*>(&stmt)) {
            llvm::Function* function = builder.GetInsertBlock()->getParent();
            llvm::AllocaInst* slot = createAlloca(function, declaration->declaredType,
                                                  declaration->name);
            llvm::Value* initialValue = declaration->init
                ? emitExpr(*declaration->init)
                : llvm::Constant::getNullValue(llvmType(declaration->declaredType));
            if (!initialValue) return false;
            builder.CreateStore(initialValue, slot);
            storage[typedProgram->localDeclarations.at(declaration)] = slot;
            declareDebugVariable(slot, declaration->name, declaration->declaredType,
                                 declaration->line, declaration->column);
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

    bool emitFunction(const TypedFunction& typedFunction) {
        const Function& astFunction = *typedFunction.definition;
        llvm::Function* function = module.getFunction(typedFunction.name);

        auto* entry = llvm::BasicBlock::Create(context, "entry", function);
        builder.SetInsertPoint(entry);

        if (debugBuilder) {
            std::vector<llvm::Metadata*> types;
            types.push_back(debugType(astFunction.returnType));
            for (const Parameter& parameter : astFunction.params) {
                types.push_back(debugType(parameter.type));
            }
            auto* functionType = debugBuilder->createSubroutineType(
                debugBuilder->getOrCreateTypeArray(types));
            debugScope = debugBuilder->createFunction(
                debugFile, astFunction.name, astFunction.name, debugFile,
                astFunction.line, functionType, astFunction.line,
                llvm::DINode::FlagPrototyped, llvm::DISubprogram::SPFlagDefinition);
            function->setSubprogram(debugScope);
            setDebugLocation(astFunction.line, astFunction.column);
        }

        std::size_t index = 0;
        for (auto& argument : function->args()) {
            const std::string& parameterName = astFunction.params[index++].name;
            argument.setName(parameterName);
            llvm::AllocaInst* slot = createAlloca(
                function, astFunction.params[index - 1].type, parameterName);
            builder.CreateStore(&argument, slot);
            storage[typedFunction.parameterSymbols[index - 1]] = slot;
            const Parameter& parameter = astFunction.params[index - 1];
            declareDebugVariable(slot, parameterName, parameter.type, parameter.line,
                                 parameter.column, true, static_cast<unsigned>(index));
        }

        if (!emitBlock(*astFunction.body)) return false;
        if (!builder.GetInsertBlock()->getTerminator()) {
            if (astFunction.returnType->kind() == TypeKind::Void) builder.CreateRetVoid();
            else return fail(astFunction.line, "function '" + astFunction.name +
                                               "' may reach the end without returning");
        }

        if (!debugBuilder) {
            std::string verificationError;
            llvm::raw_string_ostream stream(verificationError);
            if (llvm::verifyFunction(*function, &stream)) {
                stream.flush();
                return fail(astFunction.line, "LLVM verification failed for '" +
                                             astFunction.name + "': " + verificationError);
            }
        }
        debugScope = nullptr;
        builder.SetCurrentDebugLocation(llvm::DebugLoc());
        return true;
    }

    bool generate(const TypedProgram& program) {
        typedProgram = &program;
        storage.clear();
        snapshotIndex = 0;
        lastSnapshotIR.clear();
        initializeDebugInfo();
        for (const TypedFunction& function : program.functions) {
            declareFunction(function);
        }
        for (const TypedGlobal& global : program.globals) declareGlobal(global);
        for (const TypedFunction& function : program.functions) {
            if (function.definition && !emitFunction(function)) return false;
        }
        if (debugBuilder) debugBuilder->finalize();
        if (!writeSnapshot("frontend", true)) return false;

        std::string verificationError;
        llvm::raw_string_ostream stream(verificationError);
        if (llvm::verifyModule(module, &stream)) {
            stream.flush();
            errorMessage = "LLVM module verification failed: " + verificationError;
            return false;
        }
        if (!optimizeModule()) return false;
        if (!writeSnapshot("final", true)) return false;
        std::string optimizedVerificationError;
        llvm::raw_string_ostream optimizedStream(optimizedVerificationError);
        if (llvm::verifyModule(module, &optimizedStream)) {
            optimizedStream.flush();
            errorMessage = "LLVM module verification failed after optimization: " +
                           optimizedVerificationError;
            return false;
        }
        if (options.emitObject && !prepareTargetMachine()) return false;
        return true;
    }
};

CodeGenerator::CodeGenerator(std::string moduleName, CodegenOptions options)
    : impl_(std::make_unique<Impl>(std::move(moduleName), options)) {}

CodeGenerator::~CodeGenerator() = default;

bool CodeGenerator::generate(const TypedProgram& program) {
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

bool CodeGenerator::writeObject(const std::string& outputPath) {
    if (!impl_->prepareTargetMachine()) return false;
    std::error_code ec;
    llvm::raw_fd_ostream output(outputPath, ec, llvm::sys::fs::OF_None);
    if (ec) {
        impl_->errorMessage = "cannot write '" + outputPath + "': " + ec.message();
        return false;
    }
    llvm::legacy::PassManager passes;
#if LLVM_VERSION_MAJOR >= 18
    const auto fileType = llvm::CodeGenFileType::ObjectFile;
#elif LLVM_VERSION_MAJOR >= 10
    const auto fileType = llvm::CGFT_ObjectFile;
#else
    const auto fileType = llvm::TargetMachine::CGFT_ObjectFile;
#endif
    if (impl_->targetMachine->addPassesToEmitFile(passes, output, nullptr, fileType)) {
        impl_->errorMessage = "target machine cannot emit an object file";
        return false;
    }
    passes.run(impl_->module);
    output.flush();
    return true;
}

void CodeGenerator::printIR() const {
    impl_->module.print(llvm::outs(), nullptr);
}

const std::string& CodeGenerator::error() const {
    return impl_->errorMessage;
}

} // namespace lumen
