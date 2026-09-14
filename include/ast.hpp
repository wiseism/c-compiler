#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace lumen {

struct Node {
    explicit Node(int sourceLine) : line(sourceLine) {}
    virtual ~Node() = default;
    int line;
};

struct Expr : Node { using Node::Node; };

struct NumberExpr final : Expr {
    NumberExpr(std::int64_t number, int sourceLine) : Expr(sourceLine), value(number) {}
    std::int64_t value;
};

struct VariableExpr final : Expr {
    VariableExpr(std::string variableName, int sourceLine)
        : Expr(sourceLine), name(std::move(variableName)) {}
    std::string name;
};

struct AssignExpr final : Expr {
    AssignExpr(std::string variableName, std::unique_ptr<Expr> expression, int sourceLine)
        : Expr(sourceLine), name(std::move(variableName)), value(std::move(expression)) {}
    std::string name;
    std::unique_ptr<Expr> value;
};

struct UnaryExpr final : Expr {
    UnaryExpr(std::string operation, std::unique_ptr<Expr> expression, int sourceLine)
        : Expr(sourceLine), op(std::move(operation)), operand(std::move(expression)) {}
    std::string op;
    std::unique_ptr<Expr> operand;
};

struct BinaryExpr final : Expr {
    BinaryExpr(std::string operation, std::unique_ptr<Expr> left,
               std::unique_ptr<Expr> right, int sourceLine)
        : Expr(sourceLine), op(std::move(operation)), lhs(std::move(left)),
          rhs(std::move(right)) {}
    std::string op;
    std::unique_ptr<Expr> lhs;
    std::unique_ptr<Expr> rhs;
};

struct CallExpr final : Expr {
    CallExpr(std::string functionName, std::vector<std::unique_ptr<Expr>> arguments,
             int sourceLine)
        : Expr(sourceLine), callee(std::move(functionName)), args(std::move(arguments)) {}
    std::string callee;
    std::vector<std::unique_ptr<Expr>> args;
};

struct Stmt : Node { using Node::Node; };

struct BlockStmt final : Stmt {
    BlockStmt(std::vector<std::unique_ptr<Stmt>> statements, int sourceLine)
        : Stmt(sourceLine), body(std::move(statements)) {}
    std::vector<std::unique_ptr<Stmt>> body;
};

struct ExprStmt final : Stmt {
    ExprStmt(std::unique_ptr<Expr> expression, int sourceLine)
        : Stmt(sourceLine), expr(std::move(expression)) {}
    std::unique_ptr<Expr> expr;
};

struct VarDeclStmt final : Stmt {
    VarDeclStmt(std::string variableName, std::unique_ptr<Expr> initializer, int sourceLine)
        : Stmt(sourceLine), name(std::move(variableName)), init(std::move(initializer)) {}
    std::string name;
    std::unique_ptr<Expr> init;
};

struct ReturnStmt final : Stmt {
    ReturnStmt(std::unique_ptr<Expr> expression, int sourceLine)
        : Stmt(sourceLine), value(std::move(expression)) {}
    std::unique_ptr<Expr> value;
};

struct IfStmt final : Stmt {
    IfStmt(std::unique_ptr<Expr> condition, std::unique_ptr<Stmt> thenStatement,
           std::unique_ptr<Stmt> elseStatement, int sourceLine)
        : Stmt(sourceLine), cond(std::move(condition)), thenBranch(std::move(thenStatement)),
          elseBranch(std::move(elseStatement)) {}
    std::unique_ptr<Expr> cond;
    std::unique_ptr<Stmt> thenBranch;
    std::unique_ptr<Stmt> elseBranch;
};

struct WhileStmt final : Stmt {
    WhileStmt(std::unique_ptr<Expr> condition, std::unique_ptr<Stmt> loopBody, int sourceLine)
        : Stmt(sourceLine), cond(std::move(condition)), body(std::move(loopBody)) {}
    std::unique_ptr<Expr> cond;
    std::unique_ptr<Stmt> body;
};

struct TopLevel : Node { using Node::Node; };

struct Prototype final : TopLevel {
    Prototype(std::string functionName, std::vector<std::string> parameters,
              bool external, int sourceLine)
        : TopLevel(sourceLine), name(std::move(functionName)), params(std::move(parameters)),
          isExtern(external) {}
    std::string name;
    std::vector<std::string> params;
    bool isExtern;
};

struct Function final : TopLevel {
    Function(std::string functionName, std::vector<std::string> parameters,
             std::unique_ptr<BlockStmt> functionBody, int sourceLine)
        : TopLevel(sourceLine), name(std::move(functionName)), params(std::move(parameters)),
          body(std::move(functionBody)) {}
    std::string name;
    std::vector<std::string> params;
    std::unique_ptr<BlockStmt> body;
};

struct Program {
    std::vector<std::unique_ptr<TopLevel>> declarations;
};

} // namespace lumen
