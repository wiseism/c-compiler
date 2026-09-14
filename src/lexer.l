%option noyywrap nounput noinput yylineno nodefault

%{
#include "parser.hpp"

#include <cstdlib>
#include <string>

#define YY_USER_ACTION yylloc.first_line = yylloc.last_line = yylineno;
%}

%x COMMENT
DIGIT      [0-9]
IDENT      [A-Za-z_][A-Za-z0-9_]*

%%
[ \t\r]+                  ;
\n                        ;
"//"[^\n]*                ;
"/*"                      BEGIN(COMMENT);
<COMMENT>"*/"             BEGIN(INITIAL);
<COMMENT>\n               ;
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
[(){}\[\];,=+\-*/%!<>&]  return yytext[0];
.                         return INVALID;
%%
