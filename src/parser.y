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
std::vector<std::string> parseDiagnostics;
bool suppressParserDiagnostics = false;

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
    lumen::TypePtr* type;
}

%token INT CHAR VOID CONST EXTERN IF ELSE WHILE BREAK RETURN NULLPTR
%token EQ NE LE GE AND OR INVALID
%token <number> NUMBER
%token <text> IDENTIFIER STRING_LITERAL

%type <expr> expression assignment logical_or logical_and bitwise_xor equality relational additive multiplicative unary postfix primary
%type <exprs> argument_list argument_list_opt
%type <stmt> statement variable_decl return_stmt if_stmt while_stmt break_stmt expression_stmt
%type <block> block
%type <stmts> statement_list
%type <top> external_declaration function_definition extern_declaration
%type <top> function_declaration global_declaration
%type <tops> external_list
%type <params> parameter_list parameter_list_opt
%type <type> type_specifier base_type

%destructor { delete $$; } <text> <expr> <stmt> <block> <top> <params> <type>
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
    | external_list error ';' { yyerrok; $$ = $1; }
    ;

external_declaration
    : function_definition { $$ = $1; }
    | function_declaration { $$ = $1; }
    | extern_declaration   { $$ = $1; }
    | global_declaration   { $$ = $1; }
    ;

function_definition
    : type_specifier IDENTIFIER '(' parameter_list_opt ')' block {
          $$ = new lumen::Function(std::move(*$1), std::move(*$2), std::move(*$4),
                                   std::unique_ptr<lumen::BlockStmt>($6), @1.first_line,
                                   @1.first_column);
          delete $1;
          delete $2;
          delete $4;
      }
    ;

extern_declaration
    : EXTERN type_specifier IDENTIFIER '(' parameter_list_opt ')' ';' {
          $$ = new lumen::Prototype(std::move(*$2), std::move(*$3),
                                    std::move(*$5), true, @1.first_line, @1.first_column);
          delete $2;
          delete $3;
          delete $5;
      }
    ;

function_declaration
    : type_specifier IDENTIFIER '(' parameter_list_opt ')' ';' {
          $$ = new lumen::Prototype(std::move(*$1), std::move(*$2),
                                    std::move(*$4), false, @1.first_line, @1.first_column);
          delete $1;
          delete $2;
          delete $4;
      }
    ;

global_declaration
    : type_specifier IDENTIFIER ';' {
          $$ = new lumen::GlobalVar(std::move(*$1), std::move(*$2), nullptr,
                                    false, @1.first_line, @1.first_column);
          delete $1;
          delete $2;
      }
    | type_specifier IDENTIFIER '=' expression ';' {
          $$ = new lumen::GlobalVar(std::move(*$1), std::move(*$2),
                                    std::unique_ptr<lumen::Expr>($4), false, @1.first_line,
                                    @1.first_column);
          delete $1;
          delete $2;
      }
    | EXTERN type_specifier IDENTIFIER ';' {
          $$ = new lumen::GlobalVar(std::move(*$2), std::move(*$3), nullptr,
                                    true, @1.first_line, @1.first_column);
          delete $2;
          delete $3;
      }
    | type_specifier IDENTIFIER '[' NUMBER ']' ';' {
          $$ = new lumen::GlobalVar(
              lumen::Type::arrayOf(std::move(*$1), static_cast<std::size_t>($4)),
              std::move(*$2), nullptr, false, @1.first_line, @1.first_column);
          delete $1;
          delete $2;
      }
    | EXTERN type_specifier IDENTIFIER '[' NUMBER ']' ';' {
          $$ = new lumen::GlobalVar(
              lumen::Type::arrayOf(std::move(*$2), static_cast<std::size_t>($5)),
              std::move(*$3), nullptr, true, @1.first_line, @1.first_column);
          delete $2;
          delete $3;
      }
    ;

type_specifier
    : base_type { $$ = $1; }
    | type_specifier '*' {
          $$ = new lumen::TypePtr(lumen::Type::pointerTo(std::move(*$1)));
          delete $1;
      }
    ;

base_type
    : INT  { $$ = new lumen::TypePtr(lumen::Type::int32()); }
    | CHAR { $$ = new lumen::TypePtr(lumen::Type::char8()); }
    | VOID { $$ = new lumen::TypePtr(lumen::Type::voidType()); }
    ;

parameter_list_opt
    : %empty         { $$ = new std::vector<lumen::Parameter>(); }
    | VOID           { $$ = new std::vector<lumen::Parameter>(); }
    | parameter_list { $$ = $1; }
    ;

parameter_list
    : type_specifier IDENTIFIER {
          $$ = new std::vector<lumen::Parameter>();
          $$->emplace_back(std::move(*$1), std::move(*$2), @2.first_line, @2.first_column);
          delete $1;
          delete $2;
      }
    | parameter_list ',' type_specifier IDENTIFIER {
          $1->emplace_back(std::move(*$3), std::move(*$4), @4.first_line, @4.first_column);
          delete $3;
          delete $4;
          $$ = $1;
      }
    ;

block
    : '{' statement_list '}' {
          $$ = new lumen::BlockStmt(adopt($2), @1.first_line, @1.first_column);
      }
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
    | break_stmt      { $$ = $1; }
    | expression_stmt { $$ = $1; }
    | error ';' {
          yyerrok;
          $$ = new lumen::BlockStmt({}, @1.first_line, @1.first_column);
      }
    ;

variable_decl
    : type_specifier IDENTIFIER ';' {
          $$ = new lumen::VarDeclStmt(std::move(*$1), std::move(*$2),
                                      nullptr, false, @1.first_line, @1.first_column);
          delete $1;
          delete $2;
      }
    | type_specifier IDENTIFIER '=' expression ';' {
          $$ = new lumen::VarDeclStmt(std::move(*$1), std::move(*$2),
                                      std::unique_ptr<lumen::Expr>($4), false, @1.first_line,
                                      @1.first_column);
          delete $1;
          delete $2;
      }
    | type_specifier IDENTIFIER '[' NUMBER ']' ';' {
          $$ = new lumen::VarDeclStmt(
              lumen::Type::arrayOf(std::move(*$1), static_cast<std::size_t>($4)),
              std::move(*$2), nullptr, false, @1.first_line, @1.first_column);
          delete $1;
          delete $2;
      }
    | CONST type_specifier IDENTIFIER '=' expression ';' {
          $$ = new lumen::VarDeclStmt(std::move(*$2), std::move(*$3),
                                      std::unique_ptr<lumen::Expr>($5), true, @1.first_line,
                                      @1.first_column);
          delete $2;
          delete $3;
      }
    | CONST type_specifier IDENTIFIER ';' {
          $$ = new lumen::VarDeclStmt(std::move(*$2), std::move(*$3),
                                      nullptr, true, @1.first_line, @1.first_column);
          delete $2;
          delete $3;
      }
    ;

return_stmt
    : RETURN ';' {
          $$ = new lumen::ReturnStmt(nullptr, @1.first_line, @1.first_column);
      }
    | RETURN expression ';' {
          $$ = new lumen::ReturnStmt(std::unique_ptr<lumen::Expr>($2), @1.first_line,
                                     @1.first_column);
      }
    ;

if_stmt
    : IF '(' expression ')' statement %prec LOWER_THAN_ELSE {
          $$ = new lumen::IfStmt(std::unique_ptr<lumen::Expr>($3),
                                 std::unique_ptr<lumen::Stmt>($5), nullptr, @1.first_line,
                                 @1.first_column);
      }
    | IF '(' expression ')' statement ELSE statement {
          $$ = new lumen::IfStmt(std::unique_ptr<lumen::Expr>($3),
                                 std::unique_ptr<lumen::Stmt>($5),
                                 std::unique_ptr<lumen::Stmt>($7), @1.first_line,
                                 @1.first_column);
      }
    ;

while_stmt
    : WHILE '(' expression ')' statement {
          $$ = new lumen::WhileStmt(std::unique_ptr<lumen::Expr>($3),
                                    std::unique_ptr<lumen::Stmt>($5), @1.first_line,
                                    @1.first_column);
      }
    ;

break_stmt
    : BREAK ';' {
          $$ = new lumen::BreakStmt(@1.first_line, @1.first_column);
      }
    ;

expression_stmt
    : expression ';' {
          $$ = new lumen::ExprStmt(std::unique_ptr<lumen::Expr>($1), @1.first_line,
                                   @1.first_column);
      }
    ;

expression
    : assignment { $$ = $1; }
    ;

assignment
    : logical_or { $$ = $1; }
    | unary '=' assignment {
          $$ = new lumen::AssignExpr(std::unique_ptr<lumen::Expr>($1),
                                     std::unique_ptr<lumen::Expr>($3), @2.first_line,
                                     @2.first_column);
      }
    ;

logical_or
    : logical_and { $$ = $1; }
    | logical_or OR logical_and { $$ = new lumen::BinaryExpr("||", std::unique_ptr<lumen::Expr>($1), std::unique_ptr<lumen::Expr>($3), @2.first_line, @2.first_column); }
    ;

logical_and
    : bitwise_xor { $$ = $1; }
    | logical_and AND bitwise_xor { $$ = new lumen::BinaryExpr("&&", std::unique_ptr<lumen::Expr>($1), std::unique_ptr<lumen::Expr>($3), @2.first_line, @2.first_column); }
    ;

bitwise_xor
    : equality { $$ = $1; }
    | bitwise_xor '^' equality { $$ = new lumen::BinaryExpr("^", std::unique_ptr<lumen::Expr>($1), std::unique_ptr<lumen::Expr>($3), @2.first_line, @2.first_column); }
    ;

equality
    : relational { $$ = $1; }
    | equality EQ relational { $$ = new lumen::BinaryExpr("==", std::unique_ptr<lumen::Expr>($1), std::unique_ptr<lumen::Expr>($3), @2.first_line, @2.first_column); }
    | equality NE relational { $$ = new lumen::BinaryExpr("!=", std::unique_ptr<lumen::Expr>($1), std::unique_ptr<lumen::Expr>($3), @2.first_line, @2.first_column); }
    ;

relational
    : additive { $$ = $1; }
    | relational '<' additive { $$ = new lumen::BinaryExpr("<", std::unique_ptr<lumen::Expr>($1), std::unique_ptr<lumen::Expr>($3), @2.first_line, @2.first_column); }
    | relational '>' additive { $$ = new lumen::BinaryExpr(">", std::unique_ptr<lumen::Expr>($1), std::unique_ptr<lumen::Expr>($3), @2.first_line, @2.first_column); }
    | relational LE additive  { $$ = new lumen::BinaryExpr("<=", std::unique_ptr<lumen::Expr>($1), std::unique_ptr<lumen::Expr>($3), @2.first_line, @2.first_column); }
    | relational GE additive  { $$ = new lumen::BinaryExpr(">=", std::unique_ptr<lumen::Expr>($1), std::unique_ptr<lumen::Expr>($3), @2.first_line, @2.first_column); }
    ;

additive
    : multiplicative { $$ = $1; }
    | additive '+' multiplicative { $$ = new lumen::BinaryExpr("+", std::unique_ptr<lumen::Expr>($1), std::unique_ptr<lumen::Expr>($3), @2.first_line, @2.first_column); }
    | additive '-' multiplicative { $$ = new lumen::BinaryExpr("-", std::unique_ptr<lumen::Expr>($1), std::unique_ptr<lumen::Expr>($3), @2.first_line, @2.first_column); }
    ;

multiplicative
    : unary { $$ = $1; }
    | multiplicative '*' unary { $$ = new lumen::BinaryExpr("*", std::unique_ptr<lumen::Expr>($1), std::unique_ptr<lumen::Expr>($3), @2.first_line, @2.first_column); }
    | multiplicative '/' unary { $$ = new lumen::BinaryExpr("/", std::unique_ptr<lumen::Expr>($1), std::unique_ptr<lumen::Expr>($3), @2.first_line, @2.first_column); }
    | multiplicative '%' unary { $$ = new lumen::BinaryExpr("%", std::unique_ptr<lumen::Expr>($1), std::unique_ptr<lumen::Expr>($3), @2.first_line, @2.first_column); }
    ;

unary
    : postfix { $$ = $1; }
    | '-' unary { $$ = new lumen::UnaryExpr("-", std::unique_ptr<lumen::Expr>($2), @1.first_line, @1.first_column); }
    | '!' unary { $$ = new lumen::UnaryExpr("!", std::unique_ptr<lumen::Expr>($2), @1.first_line, @1.first_column); }
    | '&' unary { $$ = new lumen::UnaryExpr("&", std::unique_ptr<lumen::Expr>($2), @1.first_line, @1.first_column); }
    | '*' unary { $$ = new lumen::UnaryExpr("*", std::unique_ptr<lumen::Expr>($2), @1.first_line, @1.first_column); }
    ;

postfix
    : primary { $$ = $1; }
    | postfix '[' expression ']' {
          $$ = new lumen::IndexExpr(std::unique_ptr<lumen::Expr>($1),
                                    std::unique_ptr<lumen::Expr>($3), @2.first_line,
                                    @2.first_column);
      }
    ;

primary
    : NUMBER { $$ = new lumen::NumberExpr($1, @1.first_line, @1.first_column); }
    | STRING_LITERAL {
          $$ = new lumen::StringExpr(std::move(*$1), @1.first_line, @1.first_column);
          delete $1;
      }
    | NULLPTR { $$ = new lumen::NullExpr(@1.first_line, @1.first_column); }
    | IDENTIFIER {
          $$ = new lumen::VariableExpr(std::move(*$1), @1.first_line, @1.first_column);
          delete $1;
      }
    | IDENTIFIER '(' argument_list_opt ')' {
          $$ = new lumen::CallExpr(std::move(*$1), adopt($3), @1.first_line,
                                   @1.first_column);
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
    std::string diagnostic = "line " + std::to_string(yylloc.first_line) + ":" +
                             std::to_string(yylloc.first_column) + ": " + message;
    parseDiagnostics.push_back(diagnostic);
    if (!suppressParserDiagnostics) std::cerr << diagnostic << '\n';
}
