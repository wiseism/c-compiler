#pragma once

#include <llvm/Config/llvm-config.h>
#include <llvm/IR/PassInstrumentation.h>
#include <llvm/IR/Module.h>
#include <llvm/Passes/PassBuilder.h>
#include <llvm/Support/CodeGen.h>
#if __has_include(<llvm/TargetParser/Triple.h>)
#include <llvm/TargetParser/Triple.h>
#else
#include <llvm/ADT/Triple.h>
#endif
#if __has_include(<llvm/MC/TargetRegistry.h>)
#include <llvm/MC/TargetRegistry.h>
#else
#include <llvm/Support/TargetRegistry.h>
#endif
#include <llvm/Target/TargetMachine.h>

#include <functional>
#include <optional>
#include <string>
#include <utility>

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

inline llvm::TargetMachine* createTargetMachine(const llvm::Target& target,
                                                const std::string& triple,
                                                const llvm::TargetOptions& options) {
#if LLVM_VERSION_MAJOR >= 21
    return target.createTargetMachine(llvm::Triple(triple), "generic", "", options,
                                      std::nullopt);
#elif LLVM_VERSION_MAJOR >= 16
    return target.createTargetMachine(triple, "generic", "", options, std::nullopt);
#else
    return target.createTargetMachine(triple, "generic", "", options, llvm::None);
#endif
}

inline void setTargetTriple(llvm::Module& module, const std::string& triple) {
#if LLVM_VERSION_MAJOR >= 21
    module.setTargetTriple(llvm::Triple(triple));
#else
    module.setTargetTriple(triple);
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
