#include "codegen.hpp"

#include <cstdint>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace lumen {

namespace {

struct FunctionSignature {
    TypePtr returnType;
    std::vector<TypePtr> parameterTypes;
    bool hasDefinition = false;
};

} // namespace

struct CodeGenerator::Impl {
    explicit Impl(std::string name) : moduleName(std::move(name)) {}

    std::string moduleName;
    std::string moduleIR;
    std::string errorMessage;
    std::unordered_map<std::string, FunctionSignature> functions;
    std::unordered_map<std::string, const GlobalVar*> globals;
    std::vector<std::string> globalOrder;

    std::ostringstream body;
    std::ostringstream allocas;
    std::vector<std::unordered_map<std::string, std::string>> scopes;
    int nextTemporary = 0;
    int nextSlot = 0;
    int nextLabel = 0;
    std::string currentLabel;
    bool terminated = false;
    bool usesTrap = false;

    static std::string irType(const TypePtr& type) {
        switch (type->kind()) {
        case TypeKind::Int32:
            return "i32";
        case TypeKind::Void:
            return "void";
        case TypeKind::Pointer:
            if (type->elementType()->kind() == TypeKind::Void) return "i8*";
            return irType(type->elementType()) + "*";
        case TypeKind::Array:
            return "[" + std::to_string(type->arrayLength()) + " x " +
                   irType(type->elementType()) + "]";
        case TypeKind::NullPointer:
            return "<unresolved-null>";
        }
        return "<invalid-type>";
    }

    bool fail(int line, const std::string& message) {
        if (errorMessage.empty()) {
            errorMessage = "line " + std::to_string(line) + ": " + message;
        }
        return false;
    }

    std::string failExpr(int line, const std::string& message) {
        fail(line, message);
        return {};
    }

    std::string temporary() { return "%t" + std::to_string(nextTemporary++); }
    std::string labelName(const std::string& stem) {
        return stem + "." + std::to_string(nextLabel++);
    }

    void instruction(const std::string& text) { body << "  " << text << '\n'; }

    void emitLabel(const std::string& label) {
        body << label << ":\n";
        currentLabel = label;
        terminated = false;
    }

    void branch(const std::string& label) {
        instruction("br label %" + label);
        terminated = true;
    }

    std::string lookup(const std::string& name) const {
        for (auto scope = scopes.rbegin(); scope != scopes.rend(); ++scope) {
            const auto found = scope->find(name);
            if (found != scope->end()) return found->second;
        }
        return globals.count(name) ? "@" + name : std::string();
    }

    std::string toBool(const std::string& value, const TypePtr& type) {
        const std::string result = temporary();
        if (isPointerType(type)) {
            instruction(result + " = icmp ne " + irType(type) + " " + value + ", null");
        } else {
            instruction(result + " = icmp ne i32 " + value + ", 0");
        }
        return result;
    }

    void emitTrapUnless(const std::string& condition, const std::string& stem) {
        const std::string validLabel = labelName(stem + ".ok");
        const std::string trapLabel = labelName(stem + ".trap");
        instruction("br i1 " + condition + ", label %" + validLabel +
                    ", label %" + trapLabel);
        terminated = true;
        emitLabel(trapLabel);
        instruction("call void @llvm.trap()");
        instruction("unreachable");
        terminated = true;
        emitLabel(validLabel);
        usesTrap = true;
    }

    void emitNullCheck(const std::string& pointer, const TypePtr& type) {
        const std::string condition = temporary();
        instruction(condition + " = icmp ne " + irType(type) + " " + pointer + ", null");
        emitTrapUnless(condition, "nullcheck");
    }

    void emitBoundsCheck(const std::string& index, std::size_t length) {
        const std::string nonNegative = temporary();
        instruction(nonNegative + " = icmp sge i32 " + index + ", 0");
        const std::string inRange = temporary();
        instruction(inRange + " = icmp slt i32 " + index + ", " +
                    std::to_string(length));
        const std::string valid = temporary();
        instruction(valid + " = and i1 " + nonNegative + ", " + inRange);
        emitTrapUnless(valid, "boundscheck");
    }

    std::string emitShortCircuit(const BinaryExpr& expr) {
        const std::string left = emitExpr(*expr.lhs);
        if (left.empty()) return {};
        const std::string leftBool = toBool(left, expr.lhs->inferredType);
        const std::string leftPredecessor = currentLabel;
        const std::string rhsLabel = labelName("logic.rhs");
        const std::string mergeLabel = labelName("logic.end");
        const bool isAnd = expr.op == "&&";

        instruction("br i1 " + leftBool + ", label %" +
                    (isAnd ? rhsLabel : mergeLabel) + ", label %" +
                    (isAnd ? mergeLabel : rhsLabel));
        terminated = true;

        emitLabel(rhsLabel);
        const std::string right = emitExpr(*expr.rhs);
        if (right.empty()) return {};
        const std::string rightBool = toBool(right, expr.rhs->inferredType);
        const std::string rightPredecessor = currentLabel;
        branch(mergeLabel);

        emitLabel(mergeLabel);
        const std::string phi = temporary();
        instruction(phi + " = phi i1 [ " + (isAnd ? "false" : "true") + ", %" +
                    leftPredecessor + " ], [ " + rightBool + ", %" +
                    rightPredecessor + " ]");
        const std::string result = temporary();
        instruction(result + " = zext i1 " + phi + " to i32");
        return result;
    }

    std::string emitAddress(const Expr& expr) {
        if (const auto* variable = dynamic_cast<const VariableExpr*>(&expr)) {
            const std::string slot = lookup(variable->name);
            if (slot.empty()) return failExpr(expr.line, "unknown variable '" + variable->name + "'");
            return slot;
        }
        if (const auto* unary = dynamic_cast<const UnaryExpr*>(&expr)) {
            if (unary->op != "*") return failExpr(expr.line, "expression is not addressable");
            const std::string pointer = emitExpr(*unary->operand);
            if (pointer.empty()) return {};
            emitNullCheck(pointer, unary->operand->inferredType);
            return pointer;
        }
        if (const auto* index = dynamic_cast<const IndexExpr*>(&expr)) {
            const std::string indexValue = emitExpr(*index->index);
            if (indexValue.empty()) return {};
            const std::string result = temporary();
            if (isArrayType(index->base->inferredType)) {
                const std::string arrayAddress = emitAddress(*index->base);
                if (arrayAddress.empty()) return {};
                emitBoundsCheck(indexValue, index->base->inferredType->arrayLength());
                const std::string arrayType = irType(index->base->inferredType);
                instruction(result + " = getelementptr inbounds " + arrayType + ", " +
                            arrayType + "* " + arrayAddress + ", i32 0, i32 " + indexValue);
            } else {
                const std::string pointer = emitExpr(*index->base);
                if (pointer.empty()) return {};
                emitNullCheck(pointer, index->base->inferredType);
                instruction(result + " = getelementptr inbounds " +
                            irType(index->base->inferredType->elementType()) + ", " +
                            irType(index->base->inferredType) + " " + pointer +
                            ", i32 " + indexValue);
            }
            return result;
        }
        return failExpr(expr.line, "expression is not addressable");
    }

    std::string emitExpr(const Expr& expr) {
        if (const auto* number = dynamic_cast<const NumberExpr*>(&expr)) {
            const std::uint32_t bits = static_cast<std::uint32_t>(number->value);
            return std::to_string(static_cast<std::int32_t>(bits));
        }
        if (dynamic_cast<const NullExpr*>(&expr)) {
            if (!isPointerType(expr.inferredType)) {
                return failExpr(expr.line, "null literal has no pointer context");
            }
            return "null";
        }
        if (const auto* variable = dynamic_cast<const VariableExpr*>(&expr)) {
            if (isArrayType(expr.inferredType)) {
                return failExpr(expr.line, "array value requires an index");
            }
            const std::string slot = emitAddress(expr);
            if (slot.empty()) return {};
            const std::string result = temporary();
            const std::string type = irType(expr.inferredType);
            instruction(result + " = load " + type + ", " + type + "* " + slot);
            return result;
        }
        if (const auto* assign = dynamic_cast<const AssignExpr*>(&expr)) {
            const std::string slot = emitAddress(*assign->target);
            if (slot.empty()) return {};
            const std::string value = emitExpr(*assign->value);
            if (value.empty()) return {};
            const std::string type = irType(expr.inferredType);
            instruction("store " + type + " " + value + ", " + type + "* " + slot);
            return value;
        }
        if (const auto* unary = dynamic_cast<const UnaryExpr*>(&expr)) {
            if (unary->op == "&") return emitAddress(*unary->operand);
            if (unary->op == "*") {
                const std::string address = emitAddress(expr);
                if (address.empty()) return {};
                const std::string result = temporary();
                const std::string type = irType(expr.inferredType);
                instruction(result + " = load " + type + ", " + type + "* " + address);
                return result;
            }
            const std::string operand = emitExpr(*unary->operand);
            if (operand.empty()) return {};
            if (unary->op == "-") {
                const std::string result = temporary();
                instruction(result + " = sub i32 0, " + operand);
                return result;
            }
            if (unary->op == "!") {
                const std::string comparison = temporary();
                if (isPointerType(unary->operand->inferredType)) {
                    instruction(comparison + " = icmp eq " +
                                irType(unary->operand->inferredType) + " " + operand + ", null");
                } else {
                    instruction(comparison + " = icmp eq i32 " + operand + ", 0");
                }
                const std::string result = temporary();
                instruction(result + " = zext i1 " + comparison + " to i32");
                return result;
            }
            return failExpr(expr.line, "unsupported unary operator '" + unary->op + "'");
        }
        if (dynamic_cast<const IndexExpr*>(&expr)) {
            const std::string address = emitAddress(expr);
            if (address.empty()) return {};
            const std::string result = temporary();
            const std::string type = irType(expr.inferredType);
            instruction(result + " = load " + type + ", " + type + "* " + address);
            return result;
        }
        if (const auto* binary = dynamic_cast<const BinaryExpr*>(&expr)) {
            if (binary->op == "&&" || binary->op == "||") return emitShortCircuit(*binary);
            const std::string left = emitExpr(*binary->lhs);
            const std::string right = left.empty() ? std::string() : emitExpr(*binary->rhs);
            if (left.empty() || right.empty()) return {};

            const std::string result = temporary();
            std::string opcode;
            bool comparison = false;
            if (binary->op == "+") opcode = "add";
            else if (binary->op == "-") opcode = "sub";
            else if (binary->op == "*") opcode = "mul";
            else if (binary->op == "/") opcode = "sdiv";
            else if (binary->op == "%") opcode = "srem";
            else if (binary->op == "==") { opcode = "icmp eq"; comparison = true; }
            else if (binary->op == "!=") { opcode = "icmp ne"; comparison = true; }
            else if (binary->op == "<") { opcode = "icmp slt"; comparison = true; }
            else if (binary->op == ">") { opcode = "icmp sgt"; comparison = true; }
            else if (binary->op == "<=") { opcode = "icmp sle"; comparison = true; }
            else if (binary->op == ">=") { opcode = "icmp sge"; comparison = true; }
            else return failExpr(expr.line, "unsupported binary operator '" + binary->op + "'");

            const std::string operandType = (binary->op == "==" || binary->op == "!=")
                ? irType(binary->lhs->inferredType) : "i32";
            instruction(result + " = " + opcode + " " + operandType + " " + left +
                        ", " + right);
            if (!comparison) return result;
            const std::string widened = temporary();
            instruction(widened + " = zext i1 " + result + " to i32");
            return widened;
        }
        if (const auto* call = dynamic_cast<const CallExpr*>(&expr)) {
            const auto function = functions.find(call->callee);
            if (function == functions.end()) {
                return failExpr(expr.line, "unknown function '" + call->callee + "'");
            }
            if (function->second.parameterTypes.size() != call->args.size()) {
                return failExpr(expr.line, "function '" + call->callee + "' expects " +
                                std::to_string(function->second.parameterTypes.size()) +
                                " argument(s), got " +
                                std::to_string(call->args.size()));
            }
            std::string arguments;
            for (std::size_t i = 0; i < call->args.size(); ++i) {
                const std::string value = emitExpr(*call->args[i]);
                if (value.empty()) return {};
                if (i != 0) arguments += ", ";
                arguments += irType(function->second.parameterTypes[i]) + " " + value;
            }
            if (function->second.returnType->kind() == TypeKind::Void) {
                instruction("call void @" + call->callee + "(" + arguments + ")");
                return "void";
            }
            const std::string result = temporary();
            instruction(result + " = call " + irType(function->second.returnType) + " @" +
                        call->callee + "(" + arguments + ")");
            return result;
        }
        return failExpr(expr.line, "internal error: unknown expression node");
    }

    bool emitBlock(const BlockStmt& block, bool introduceScope) {
        if (introduceScope) scopes.emplace_back();
        bool ok = true;
        for (const auto& statement : block.body) {
            if (terminated) break;
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
            return !emitExpr(*expression->expr).empty();
        }
        if (const auto* declaration = dynamic_cast<const VarDeclStmt*>(&stmt)) {
            auto& scope = scopes.back();
            if (scope.count(declaration->name)) {
                return fail(stmt.line, "redeclaration of variable '" + declaration->name + "'");
            }
            const std::string type = irType(declaration->declaredType);
            const std::string value = declaration->init ? emitExpr(*declaration->init)
                : (isPointerType(declaration->declaredType) ? "null" :
                   isArrayType(declaration->declaredType) ? "zeroinitializer" : "0");
            if (value.empty()) return false;
            const std::string slot = "%v" + std::to_string(nextSlot++);
            allocas << "  " << slot << " = alloca " << type << "\n";
            instruction("store " + type + " " + value + ", " + type + "* " + slot);
            scope.emplace(declaration->name, slot);
            return true;
        }
        if (const auto* returnStmt = dynamic_cast<const ReturnStmt*>(&stmt)) {
            if (!returnStmt->value) {
                instruction("ret void");
                terminated = true;
                return true;
            }
            const std::string value = emitExpr(*returnStmt->value);
            if (value.empty()) return false;
            instruction("ret " + irType(returnStmt->value->inferredType) + " " + value);
            terminated = true;
            return true;
        }
        if (const auto* ifStmt = dynamic_cast<const IfStmt*>(&stmt)) {
            const std::string condition = emitExpr(*ifStmt->cond);
            if (condition.empty()) return false;
            const std::string conditionBool = toBool(condition, ifStmt->cond->inferredType);
            const std::string thenLabel = labelName("if.then");
            const std::string elseLabel = ifStmt->elseBranch ? labelName("if.else") : std::string();
            const std::string mergeLabel = labelName("if.end");
            instruction("br i1 " + conditionBool + ", label %" + thenLabel + ", label %" +
                        (elseLabel.empty() ? mergeLabel : elseLabel));
            terminated = true;

            emitLabel(thenLabel);
            if (!emitScoped(*ifStmt->thenBranch)) return false;
            const bool thenFallsThrough = !terminated;
            if (thenFallsThrough) branch(mergeLabel);

            bool elseFallsThrough = true;
            if (ifStmt->elseBranch) {
                emitLabel(elseLabel);
                if (!emitScoped(*ifStmt->elseBranch)) return false;
                elseFallsThrough = !terminated;
                if (elseFallsThrough) branch(mergeLabel);
            }

            emitLabel(mergeLabel);
            if (ifStmt->elseBranch && !thenFallsThrough && !elseFallsThrough) {
                instruction("unreachable");
                terminated = true;
            }
            return true;
        }
        if (const auto* whileStmt = dynamic_cast<const WhileStmt*>(&stmt)) {
            const std::string conditionLabel = labelName("while.cond");
            const std::string bodyLabel = labelName("while.body");
            const std::string endLabel = labelName("while.end");
            branch(conditionLabel);

            emitLabel(conditionLabel);
            const std::string condition = emitExpr(*whileStmt->cond);
            if (condition.empty()) return false;
            const std::string conditionBool = toBool(condition, whileStmt->cond->inferredType);
            instruction("br i1 " + conditionBool + ", label %" + bodyLabel +
                        ", label %" + endLabel);
            terminated = true;

            emitLabel(bodyLabel);
            if (!emitScoped(*whileStmt->body)) return false;
            if (!terminated) branch(conditionLabel);
            emitLabel(endLabel);
            return true;
        }
        return fail(stmt.line, "internal error: unknown statement node");
    }

    bool registerFunction(const TypePtr& returnType, const std::string& name,
                          const std::vector<Parameter>& params,
                          bool definition, int line) {
        std::vector<TypePtr> parameterTypes;
        for (const auto& parameter : params) parameterTypes.push_back(parameter.type);
        auto [position, inserted] = functions.emplace(
            name, FunctionSignature{returnType, parameterTypes, false});
        if (!inserted && (position->second.parameterTypes.size() != params.size() ||
                          !sameType(position->second.returnType, returnType))) {
            return fail(line, "conflicting declaration of function '" + name + "'");
        }
        if (definition) {
            if (position->second.hasDefinition) {
                return fail(line, "redefinition of function '" + name + "'");
            }
            position->second.hasDefinition = true;
        }
        return true;
    }

    bool emitFunction(const Function& function, std::ostringstream& output) {
        body.str({});
        body.clear();
        allocas.str({});
        allocas.clear();
        scopes.clear();
        scopes.emplace_back();
        nextTemporary = 0;
        nextSlot = 0;
        nextLabel = 0;
        currentLabel = "entry";
        terminated = false;

        std::string parameters;
        for (std::size_t i = 0; i < function.params.size(); ++i) {
            const std::string& name = function.params[i].name;
            if (scopes.back().count(name)) {
                return fail(function.line, "duplicate parameter '" + name + "'");
            }
            if (i != 0) parameters += ", ";
            const std::string argument = "%arg" + std::to_string(i);
            const std::string slot = "%v" + std::to_string(nextSlot++);
            const std::string type = irType(function.params[i].type);
            parameters += type + " " + argument;
            allocas << "  " << slot << " = alloca " << type << "\n";
            body << "  store " << type << ' ' << argument << ", " << type << "* "
                 << slot << '\n';
            scopes.back().emplace(name, slot);
        }

        if (!emitBlock(*function.body, false)) return false;
        if (!terminated) {
            if (function.returnType->kind() == TypeKind::Void) instruction("ret void");
            else return fail(function.line, "function '" + function.name +
                                           "' may reach the end without returning");
        }

        output << "define " << irType(function.returnType)
               << " @" << function.name << '(' << parameters << ") {\nentry:\n"
               << allocas.str() << body.str() << "}\n\n";
        return true;
    }

    bool generate(const Program& program) {
        errorMessage.clear();
        functions.clear();
        globals.clear();
        globalOrder.clear();
        usesTrap = false;
        for (const auto& declaration : program.declarations) {
            if (const auto* prototype = dynamic_cast<const Prototype*>(declaration.get())) {
                if (!registerFunction(prototype->returnType, prototype->name,
                                      prototype->params, false, prototype->line)) return false;
            } else if (const auto* function = dynamic_cast<const Function*>(declaration.get())) {
                if (!registerFunction(function->returnType, function->name,
                                      function->params, true, function->line)) return false;
            } else if (const auto* global = dynamic_cast<const GlobalVar*>(declaration.get())) {
                if (!globals.count(global->name)) globalOrder.push_back(global->name);
                if (!global->isExtern || !globals.count(global->name)) globals[global->name] = global;
            }
        }

        std::ostringstream declarations;
        std::unordered_set<std::string> emittedDeclarations;
        for (const auto& declaration : program.declarations) {
            const auto* prototype = dynamic_cast<const Prototype*>(declaration.get());
            if (!prototype || functions.at(prototype->name).hasDefinition ||
                !emittedDeclarations.insert(prototype->name).second) continue;
            declarations << "declare " << irType(prototype->returnType)
                         << " @" << prototype->name << '(';
            for (std::size_t i = 0; i < prototype->params.size(); ++i) {
                if (i != 0) declarations << ", ";
                declarations << irType(prototype->params[i].type);
            }
            declarations << ")\n";
        }
        if (!emittedDeclarations.empty()) declarations << '\n';

        std::ostringstream globalDefinitions;
        for (const std::string& name : globalOrder) {
            const GlobalVar* global = globals.at(name);
            const std::string type = irType(global->declaredType);
            globalDefinitions << '@' << name << " = ";
            if (global->isExtern) {
                globalDefinitions << "external global " << type << "\n";
            } else {
                const auto* initializer = dynamic_cast<const NumberExpr*>(global->init.get());
                std::string value;
                if (initializer) {
                    value = std::to_string(static_cast<std::int32_t>(
                        static_cast<std::uint32_t>(initializer->value)));
                } else if (isPointerType(global->declaredType)) {
                    value = "null";
                } else if (isArrayType(global->declaredType)) {
                    value = "zeroinitializer";
                } else {
                    value = "0";
                }
                globalDefinitions << "global " << type << ' ' << value << '\n';
            }
        }
        if (!globalOrder.empty()) globalDefinitions << '\n';

        std::ostringstream definitions;
        for (const auto& declaration : program.declarations) {
            if (const auto* function = dynamic_cast<const Function*>(declaration.get())) {
                if (!emitFunction(*function, definitions)) return false;
            }
        }

        std::ostringstream module;
        module << "; ModuleID = '" << moduleName << "'\n";
        if (usesTrap) module << "declare void @llvm.trap()\n\n";
        module << declarations.str() << globalDefinitions.str() << definitions.str();
        moduleIR = module.str();
        return true;
    }
};

CodeGenerator::CodeGenerator(std::string moduleName)
    : impl_(std::make_unique<Impl>(std::move(moduleName))) {}

CodeGenerator::~CodeGenerator() = default;

bool CodeGenerator::generate(const Program& program) { return impl_->generate(program); }

bool CodeGenerator::writeIR(const std::string& outputPath) const {
    std::ofstream output(outputPath);
    if (!output) {
        impl_->errorMessage = "cannot write '" + outputPath + "'";
        return false;
    }
    output << impl_->moduleIR;
    if (!output) {
        impl_->errorMessage = "failed while writing '" + outputPath + "'";
        return false;
    }
    return true;
}

void CodeGenerator::printIR() const { std::cout << impl_->moduleIR; }

const std::string& CodeGenerator::error() const { return impl_->errorMessage; }

} // namespace lumen
