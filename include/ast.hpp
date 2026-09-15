#pragma once

#include "type.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace lumen {

struct Node {
    explicit Node(int sourceLine, int sourceColumn = 1)
        : line(sourceLine), column(sourceColumn) {}
    virtual ~Node() = default;
    int line;
    int column;
};

struct Expr : Node {
    using Node::Node;
    TypePtr inferredType;
};

struct NumberExpr final : Expr {
    NumberExpr(std::int64_t number, int sourceLine, int sourceColumn = 1)
        : Expr(sourceLine, sourceColumn), value(number) {}
    std::int64_t value;
};

struct NullExpr final : Expr {
    explicit NullExpr(int sourceLine, int sourceColumn = 1)
        : Expr(sourceLine, sourceColumn) {}
};

struct VariableExpr final : Expr {
    VariableExpr(std::string variableName, int sourceLine, int sourceColumn = 1)
        : Expr(sourceLine, sourceColumn), name(std::move(variableName)) {}
    std::string name;
};

struct AssignExpr final : Expr {
    AssignExpr(std::unique_ptr<Expr> destination, std::unique_ptr<Expr> expression,
               int sourceLine, int sourceColumn = 1)
        : Expr(sourceLine, sourceColumn), target(std::move(destination)),
          value(std::move(expression)) {}
    std::unique_ptr<Expr> target;
    std::unique_ptr<Expr> value;
};

struct UnaryExpr final : Expr {
    UnaryExpr(std::string operation, std::unique_ptr<Expr> expression, int sourceLine,
              int sourceColumn = 1)
        : Expr(sourceLine, sourceColumn), op(std::move(operation)),
          operand(std::move(expression)) {}
    std::string op;
    std::unique_ptr<Expr> operand;
};

struct BinaryExpr final : Expr {
    BinaryExpr(std::string operation, std::unique_ptr<Expr> left,
               std::unique_ptr<Expr> right, int sourceLine, int sourceColumn = 1)
        : Expr(sourceLine, sourceColumn), op(std::move(operation)), lhs(std::move(left)),
          rhs(std::move(right)) {}
    std::string op;
    std::unique_ptr<Expr> lhs;
    std::unique_ptr<Expr> rhs;
};

struct CallExpr final : Expr {
    CallExpr(std::string functionName, std::vector<std::unique_ptr<Expr>> arguments,
             int sourceLine, int sourceColumn = 1)
        : Expr(sourceLine, sourceColumn), callee(std::move(functionName)),
          args(std::move(arguments)) {}
    std::string callee;
    std::vector<std::unique_ptr<Expr>> args;
};

struct IndexExpr final : Expr {
    IndexExpr(std::unique_ptr<Expr> baseExpression, std::unique_ptr<Expr> indexExpression,
              int sourceLine, int sourceColumn = 1)
        : Expr(sourceLine, sourceColumn), base(std::move(baseExpression)),
          index(std::move(indexExpression)) {}
    std::unique_ptr<Expr> base;
    std::unique_ptr<Expr> index;
};

struct Stmt : Node { using Node::Node; };

struct BlockStmt final : Stmt {
    BlockStmt(std::vector<std::unique_ptr<Stmt>> statements, int sourceLine,
              int sourceColumn = 1)
        : Stmt(sourceLine, sourceColumn), body(std::move(statements)) {}
    std::vector<std::unique_ptr<Stmt>> body;
};

struct ExprStmt final : Stmt {
    ExprStmt(std::unique_ptr<Expr> expression, int sourceLine, int sourceColumn = 1)
        : Stmt(sourceLine, sourceColumn), expr(std::move(expression)) {}
    std::unique_ptr<Expr> expr;
};

struct VarDeclStmt final : Stmt {
    VarDeclStmt(TypePtr type, std::string variableName,
                std::unique_ptr<Expr> initializer, int sourceLine, int sourceColumn = 1)
        : Stmt(sourceLine, sourceColumn), declaredType(std::move(type)),
          name(std::move(variableName)),
          init(std::move(initializer)) {}
    TypePtr declaredType;
    std::string name;
    std::unique_ptr<Expr> init;
};

struct ReturnStmt final : Stmt {
    ReturnStmt(std::unique_ptr<Expr> expression, int sourceLine, int sourceColumn = 1)
        : Stmt(sourceLine, sourceColumn), value(std::move(expression)) {}
    std::unique_ptr<Expr> value;
};

struct IfStmt final : Stmt {
    IfStmt(std::unique_ptr<Expr> condition, std::unique_ptr<Stmt> thenStatement,
           std::unique_ptr<Stmt> elseStatement, int sourceLine, int sourceColumn = 1)
        : Stmt(sourceLine, sourceColumn), cond(std::move(condition)),
          thenBranch(std::move(thenStatement)),
          elseBranch(std::move(elseStatement)) {}
    std::unique_ptr<Expr> cond;
    std::unique_ptr<Stmt> thenBranch;
    std::unique_ptr<Stmt> elseBranch;
};

struct WhileStmt final : Stmt {
    WhileStmt(std::unique_ptr<Expr> condition, std::unique_ptr<Stmt> loopBody, int sourceLine,
              int sourceColumn = 1)
        : Stmt(sourceLine, sourceColumn), cond(std::move(condition)),
          body(std::move(loopBody)) {}
    std::unique_ptr<Expr> cond;
    std::unique_ptr<Stmt> body;
};

struct TopLevel : Node { using Node::Node; };

struct Parameter {
    Parameter(TypePtr parameterType, std::string parameterName, int sourceLine,
              int sourceColumn = 1)
        : type(std::move(parameterType)), name(std::move(parameterName)), line(sourceLine),
          column(sourceColumn) {}
    TypePtr type;
    std::string name;
    int line;
    int column;
};

struct Prototype final : TopLevel {
    Prototype(TypePtr resultType, std::string functionName,
              std::vector<Parameter> parameters, bool external, int sourceLine,
              int sourceColumn = 1)
        : TopLevel(sourceLine, sourceColumn), returnType(std::move(resultType)),
          name(std::move(functionName)),
          params(std::move(parameters)), isExtern(external) {}
    TypePtr returnType;
    std::string name;
    std::vector<Parameter> params;
    bool isExtern;
};

struct GlobalVar final : TopLevel {
    GlobalVar(TypePtr type, std::string variableName, std::unique_ptr<Expr> initializer,
              bool external, int sourceLine, int sourceColumn = 1)
        : TopLevel(sourceLine, sourceColumn), declaredType(std::move(type)),
          name(std::move(variableName)),
          init(std::move(initializer)), isExtern(external) {}
    TypePtr declaredType;
    std::string name;
    std::unique_ptr<Expr> init;
    bool isExtern;
};

struct Function final : TopLevel {
    Function(TypePtr resultType, std::string functionName,
             std::vector<Parameter> parameters, std::unique_ptr<BlockStmt> functionBody,
             int sourceLine, int sourceColumn = 1)
        : TopLevel(sourceLine, sourceColumn), returnType(std::move(resultType)),
          name(std::move(functionName)),
          params(std::move(parameters)), body(std::move(functionBody)) {}
    TypePtr returnType;
    std::string name;
    std::vector<Parameter> params;
    std::unique_ptr<BlockStmt> body;
};

struct Program {
    std::vector<std::unique_ptr<TopLevel>> declarations;
};

} // namespace lumen
