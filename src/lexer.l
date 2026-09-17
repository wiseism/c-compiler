%option noyywrap nounput noinput yylineno nodefault

%{
#include "parser.hpp"

#include <cstdlib>
#include <string>

#ifdef _WIN32
#define YY_NO_UNISTD_H
#include <io.h>
#define isatty _isatty
#define fileno _fileno
#endif

int yycolumn = 1;

#define YY_USER_ACTION                                                        \
    yylloc.first_line = yylloc.last_line = yylineno;                          \
    yylloc.first_column = yycolumn;                                           \
    yylloc.last_column = yycolumn + static_cast<int>(yyleng) - 1;             \
    yycolumn += static_cast<int>(yyleng);
%}

%x COMMENT
DIGIT      [0-9]
IDENT      [A-Za-z_][A-Za-z0-9_]*

%%
[ \t\r]+                  ;
\n                        { yycolumn = 1; }
"//"[^\n]*                ;
"/*"                      BEGIN(COMMENT);
<COMMENT>"*/"             BEGIN(INITIAL);
<COMMENT>\n               { yycolumn = 1; }
<COMMENT>.                ;
<COMMENT><<EOF>>          return INVALID;

"int"                     return INT;
"void"                    return VOID;
"null"                    return NULLPTR;
"extern"                  return EXTERN;
"if"                      return IF;
"else"                    return ELSE;
"while"                   return WHILE;
"return"                  return RETURN;

"=="                      return EQ;
"!="                      return NE;
"<="                      return LE;
">="                      return GE;
"&&"                      return AND;
"||"                      return OR;

{DIGIT}+                  {
                              yylval.number = std::strtoll(yytext, nullptr, 10);
                              return NUMBER;
                          }
{IDENT}                    {
                              yylval.text = new std::string(yytext);
                              return IDENTIFIER;
                          }
[(){}\[\];,=+\-*/%!<>&^] return yytext[0];
.                         return INVALID;
%%
