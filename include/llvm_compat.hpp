#pragma once

#include <llvm/Config/llvm-config.h>
#include <llvm/IR/PassInstrumentation.h>
#include <llvm/Passes/PassBuilder.h>
#include <llvm/Support/CodeGen.h>
#include <llvm/Target/TargetMachine.h>

#include <functional>
#include <optional>

#if LLVM_VERSION_MAJOR < 8
#error "Lumen C requires LLVM 8 or newer"
#endif

namespace lumen::llvm_compat {

using AfterPassCallback = std::function<void(llvm::StringRef)>;

inline void registerAfterPassCallback(llvm::PassInstrumentationCallbacks& callbacks,
                                      AfterPassCallback callback) {
#if LLVM_VERSION_MAJOR >= 12
    callbacks.registerAfterPassCallback(
        [callback = std::move(callback)](llvm::StringRef name, llvm::Any,
                                         const llvm::PreservedAnalyses&) {
            callback(name);
        });
#else
    callbacks.registerAfterPassCallback(
        [callback = std::move(callback)](llvm::StringRef name, llvm::Any) {
            callback(name);
        });
#endif
}

inline llvm::PassBuilder makePassBuilder(
    llvm::TargetMachine* target,
    llvm::PassInstrumentationCallbacks* instrumentation) {
#if LLVM_VERSION_MAJOR >= 16
    return llvm::PassBuilder(target, llvm::PipelineTuningOptions(), std::nullopt,
                             instrumentation);
#elif LLVM_VERSION_MAJOR >= 11
    return llvm::PassBuilder(target, llvm::PipelineTuningOptions(), llvm::None,
                             instrumentation);
#else
    return llvm::PassBuilder(target, llvm::None, instrumentation);
#endif
}

#if LLVM_VERSION_MAJOR >= 12
using OptimizationLevel = llvm::OptimizationLevel;
inline OptimizationLevel optimizationLevel(unsigned level) {
    if (level >= 3) return llvm::OptimizationLevel::O3;
    if (level == 2) return llvm::OptimizationLevel::O2;
    return llvm::OptimizationLevel::O1;
}
#else
using OptimizationLevel = llvm::PassBuilder::OptimizationLevel;
inline OptimizationLevel optimizationLevel(unsigned level) {
    if (level >= 3) return llvm::PassBuilder::O3;
    if (level == 2) return llvm::PassBuilder::O2;
    return llvm::PassBuilder::O1;
}
#endif

inline auto objectFileType() {
#if LLVM_VERSION_MAJOR >= 18
    return llvm::CodeGenFileType::ObjectFile;
#elif LLVM_VERSION_MAJOR >= 10
    return llvm::CGFT_ObjectFile;
#else
    return llvm::TargetMachine::CGFT_ObjectFile;
#endif
}

} // namespace lumen::llvm_compat
