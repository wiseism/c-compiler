%{
#include "ast.hpp"

#include <iostream>
#include <memory>
#include <string>
#include <utility>
#include <vector>

int yylex();
void yyerror(const char* message);

std::unique_ptr<lumen::Program> parsedProgram;

template <typename T>
static std::vector<std::unique_ptr<T>> adopt(std::vector<T*>* raw) {
    std::vector<std::unique_ptr<T>> result;
    result.reserve(raw->size());
    for (T* item : *raw) result.emplace_back(item);
    delete raw;
    return result;
}
%}

%code requires {
#include "ast.hpp"
#include <cstdint>
#include <string>
#include <vector>
}

%define parse.error detailed
%locations

%union {
    std::int64_t number;
    std::string* text;
    lumen::Expr* expr;
    lumen::Stmt* stmt;
    lumen::BlockStmt* block;
    lumen::TopLevel* top;
    std::vector<lumen::Expr*>* exprs;
    std::vector<lumen::Stmt*>* stmts;
    std::vector<lumen::TopLevel*>* tops;
    std::vector<lumen::Parameter>* params;
}

%token INT EXTERN IF ELSE WHILE RETURN
%token EQ NE LE GE AND OR INVALID
%token <number> NUMBER
%token <text> IDENTIFIER

%type <expr> expression assignment logical_or logical_and equality relational additive multiplicative unary primary
%type <exprs> argument_list argument_list_opt
%type <stmt> statement variable_decl return_stmt if_stmt while_stmt expression_stmt
%type <block> block
%type <stmts> statement_list
%type <top> external_declaration function_definition extern_declaration
%type <tops> external_list
%type <params> parameter_list parameter_list_opt

%destructor { delete $$; } <text> <expr> <stmt> <block> <top> <params>
%destructor { for (auto* p : *$$) delete p; delete $$; } <exprs> <stmts> <tops>

%precedence LOWER_THAN_ELSE
%precedence ELSE
%start translation_unit

%%

translation_unit
    : external_list {
          parsedProgram = std::make_unique<lumen::Program>();
          parsedProgram->declarations = adopt($1);
      }
    ;

external_list
    : %empty { $$ = new std::vector<lumen::TopLevel*>(); }
    | external_list external_declaration { $1->push_back($2); $$ = $1; }
    ;

external_declaration
    : function_definition { $$ = $1; }
    | extern_declaration   { $$ = $1; }
    ;

function_definition
    : INT IDENTIFIER '(' parameter_list_opt ')' block {
          $$ = new lumen::Function(lumen::Type::int32(), std::move(*$2), std::move(*$4),
                                   std::unique_ptr<lumen::BlockStmt>($6), @1.first_line);
          delete $2;
          delete $4;
      }
    ;

extern_declaration
    : EXTERN INT IDENTIFIER '(' parameter_list_opt ')' ';' {
          $$ = new lumen::Prototype(lumen::Type::int32(), std::move(*$3),
                                    std::move(*$5), true, @1.first_line);
          delete $3;
          delete $5;
      }
    ;

parameter_list_opt
    : %empty         { $$ = new std::vector<lumen::Parameter>(); }
    | parameter_list { $$ = $1; }
    ;

parameter_list
    : INT IDENTIFIER {
          $$ = new std::vector<lumen::Parameter>();
          $$->emplace_back(lumen::Type::int32(), std::move(*$2), @2.first_line);
          delete $2;
      }
    | parameter_list ',' INT IDENTIFIER {
          $1->emplace_back(lumen::Type::int32(), std::move(*$4), @4.first_line);
          delete $4;
          $$ = $1;
      }
    ;

block
    : '{' statement_list '}' { $$ = new lumen::BlockStmt(adopt($2), @1.first_line); }
    ;

statement_list
    : %empty { $$ = new std::vector<lumen::Stmt*>(); }
    | statement_list statement { $1->push_back($2); $$ = $1; }
    ;

statement
    : block           { $$ = $1; }
    | variable_decl   { $$ = $1; }
    | return_stmt     { $$ = $1; }
    | if_stmt         { $$ = $1; }
    | while_stmt      { $$ = $1; }
    | expression_stmt { $$ = $1; }
    ;

variable_decl
    : INT IDENTIFIER ';' {
          $$ = new lumen::VarDeclStmt(lumen::Type::int32(), std::move(*$2),
                                      nullptr, @1.first_line);
          delete $2;
      }
    | INT IDENTIFIER '=' expression ';' {
          $$ = new lumen::VarDeclStmt(lumen::Type::int32(), std::move(*$2),
                                      std::unique_ptr<lumen::Expr>($4), @1.first_line);
          delete $2;
      }
    ;

return_stmt
    : RETURN expression ';' {
          $$ = new lumen::ReturnStmt(std::unique_ptr<lumen::Expr>($2), @1.first_line);
      }
    ;

if_stmt
    : IF '(' expression ')' statement %prec LOWER_THAN_ELSE {
          $$ = new lumen::IfStmt(std::unique_ptr<lumen::Expr>($3),
                                 std::unique_ptr<lumen::Stmt>($5), nullptr, @1.first_line);
      }
    | IF '(' expression ')' statement ELSE statement {
          $$ = new lumen::IfStmt(std::unique_ptr<lumen::Expr>($3),
                                 std::unique_ptr<lumen::Stmt>($5),
                                 std::unique_ptr<lumen::Stmt>($7), @1.first_line);
      }
    ;

while_stmt
    : WHILE '(' expression ')' statement {
          $$ = new lumen::WhileStmt(std::unique_ptr<lumen::Expr>($3),
                                    std::unique_ptr<lumen::Stmt>($5), @1.first_line);
      }
    ;

expression_stmt
    : expression ';' { $$ = new lumen::ExprStmt(std::unique_ptr<lumen::Expr>($1), @1.first_line); }
    ;

expression
    : assignment { $$ = $1; }
    ;

assignment
    : logical_or { $$ = $1; }
    | IDENTIFIER '=' assignment {
          $$ = new lumen::AssignExpr(std::move(*$1), std::unique_ptr<lumen::Expr>($3), @2.first_line);
          delete $1;
      }
    ;

logical_or
    : logical_and { $$ = $1; }
    | logical_or OR logical_and { $$ = new lumen::BinaryExpr("||", std::unique_ptr<lumen::Expr>($1), std::unique_ptr<lumen::Expr>($3), @2.first_line); }
    ;

logical_and
    : equality { $$ = $1; }
    | logical_and AND equality { $$ = new lumen::BinaryExpr("&&", std::unique_ptr<lumen::Expr>($1), std::unique_ptr<lumen::Expr>($3), @2.first_line); }
    ;

equality
    : relational { $$ = $1; }
    | equality EQ relational { $$ = new lumen::BinaryExpr("==", std::unique_ptr<lumen::Expr>($1), std::unique_ptr<lumen::Expr>($3), @2.first_line); }
    | equality NE relational { $$ = new lumen::BinaryExpr("!=", std::unique_ptr<lumen::Expr>($1), std::unique_ptr<lumen::Expr>($3), @2.first_line); }
    ;

relational
    : additive { $$ = $1; }
    | relational '<' additive { $$ = new lumen::BinaryExpr("<", std::unique_ptr<lumen::Expr>($1), std::unique_ptr<lumen::Expr>($3), @2.first_line); }
    | relational '>' additive { $$ = new lumen::BinaryExpr(">", std::unique_ptr<lumen::Expr>($1), std::unique_ptr<lumen::Expr>($3), @2.first_line); }
    | relational LE additive  { $$ = new lumen::BinaryExpr("<=", std::unique_ptr<lumen::Expr>($1), std::unique_ptr<lumen::Expr>($3), @2.first_line); }
    | relational GE additive  { $$ = new lumen::BinaryExpr(">=", std::unique_ptr<lumen::Expr>($1), std::unique_ptr<lumen::Expr>($3), @2.first_line); }
    ;

additive
    : multiplicative { $$ = $1; }
    | additive '+' multiplicative { $$ = new lumen::BinaryExpr("+", std::unique_ptr<lumen::Expr>($1), std::unique_ptr<lumen::Expr>($3), @2.first_line); }
    | additive '-' multiplicative { $$ = new lumen::BinaryExpr("-", std::unique_ptr<lumen::Expr>($1), std::unique_ptr<lumen::Expr>($3), @2.first_line); }
    ;

multiplicative
    : unary { $$ = $1; }
    | multiplicative '*' unary { $$ = new lumen::BinaryExpr("*", std::unique_ptr<lumen::Expr>($1), std::unique_ptr<lumen::Expr>($3), @2.first_line); }
    | multiplicative '/' unary { $$ = new lumen::BinaryExpr("/", std::unique_ptr<lumen::Expr>($1), std::unique_ptr<lumen::Expr>($3), @2.first_line); }
    | multiplicative '%' unary { $$ = new lumen::BinaryExpr("%", std::unique_ptr<lumen::Expr>($1), std::unique_ptr<lumen::Expr>($3), @2.first_line); }
    ;

unary
    : primary { $$ = $1; }
    | '-' unary { $$ = new lumen::UnaryExpr("-", std::unique_ptr<lumen::Expr>($2), @1.first_line); }
    | '!' unary { $$ = new lumen::UnaryExpr("!", std::unique_ptr<lumen::Expr>($2), @1.first_line); }
    ;

primary
    : NUMBER { $$ = new lumen::NumberExpr($1, @1.first_line); }
    | IDENTIFIER {
          $$ = new lumen::VariableExpr(std::move(*$1), @1.first_line);
          delete $1;
      }
    | IDENTIFIER '(' argument_list_opt ')' {
          $$ = new lumen::CallExpr(std::move(*$1), adopt($3), @1.first_line);
          delete $1;
      }
    | '(' expression ')' { $$ = $2; }
    ;

argument_list_opt
    : %empty        { $$ = new std::vector<lumen::Expr*>(); }
    | argument_list { $$ = $1; }
    ;

argument_list
    : assignment {
          $$ = new std::vector<lumen::Expr*>();
          $$->push_back($1);
      }
    | argument_list ',' assignment { $1->push_back($3); $$ = $1; }
    ;

%%

void yyerror(const char* message) {
    std::cerr << "line " << yylloc.first_line << ": " << message << '\n';
}
