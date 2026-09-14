#include "ast.hpp"
#include "constant_fold.hpp"
#include "semantic.hpp"
#include "type.hpp"

#include <cassert>
#include <memory>
#include <vector>

int main() {
    using namespace lumen;

    assert(Type::int32() == Type::int32());
    assert(Type::voidType() == Type::voidType());
    assert(!sameType(Type::int32(), Type::voidType()));
    assert(sameType(Type::pointerTo(Type::int32()), Type::pointerTo(Type::int32())));
    assert(!sameType(Type::arrayOf(Type::int32(), 4), Type::arrayOf(Type::int32(), 8)));
    assert(Type::arrayOf(Type::pointerTo(Type::int32()), 3)->toString() == "int*[3]");

    auto number = std::make_unique<NumberExpr>(42, 2);
    NumberExpr* numberNode = number.get();
    std::vector<std::unique_ptr<Stmt>> statements;
    statements.push_back(std::make_unique<ReturnStmt>(std::move(number), 2));
    auto block = std::make_unique<BlockStmt>(std::move(statements), 1);

    Program program;
    program.declarations.push_back(std::make_unique<Function>(
        Type::int32(), "main", std::vector<Parameter>{}, std::move(block), 1));

    SemanticAnalyzer analyzer;
    assert(analyzer.analyze(program));
    assert(sameType(numberNode->inferredType, Type::int32()));

    auto product = std::make_unique<BinaryExpr>(
        "*", std::make_unique<NumberExpr>(6, 1),
        std::make_unique<NumberExpr>(7, 1), 1);
    auto foldedReturn = std::make_unique<ReturnStmt>(std::move(product), 1);
    ReturnStmt* foldedReturnNode = foldedReturn.get();
    std::vector<std::unique_ptr<Stmt>> foldedStatements;
    foldedStatements.push_back(std::move(foldedReturn));
    auto foldedBlock = std::make_unique<BlockStmt>(std::move(foldedStatements), 1);
    Program foldedProgram;
    foldedProgram.declarations.push_back(std::make_unique<Function>(
        Type::int32(), "folded", std::vector<Parameter>{}, std::move(foldedBlock), 1));
    assert(analyzer.analyze(foldedProgram));

    ConstantFolder folder;
    assert(folder.run(foldedProgram));
    const auto* foldedNumber = dynamic_cast<NumberExpr*>(foldedReturnNode->value.get());
    assert(foldedNumber != nullptr);
    assert(foldedNumber->value == 42);
    assert(sameType(foldedNumber->inferredType, Type::int32()));
    return 0;
}
