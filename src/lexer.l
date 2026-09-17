%option noyywrap nounput noinput yylineno nodefault

%{
#include "parser.hpp"

#include <cstdlib>
#include <cstddef>
#include <string>

static std::string decodeString(const char* text, std::size_t length) {
    std::string result;
    for (std::size_t i = 1; i + 1 < length; ++i) {
        if (text[i] != '\\') {
            result.push_back(text[i]);
            continue;
        }
        ++i;
        switch (text[i]) {
        case 'n': result.push_back('\n'); break;
        case 'r': result.push_back('\r'); break;
        case 't': result.push_back('\t'); break;
        case '0': result.push_back('\0'); break;
        case '\\': result.push_back('\\'); break;
        case '"': result.push_back('"'); break;
        }
    }
    return result;
}

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
"char"                    return CHAR;
"const"                   return CONST;
"void"                    return VOID;
"null"                    return NULLPTR;
"extern"                  return EXTERN;
"if"                      return IF;
"else"                    return ELSE;
"while"                   return WHILE;
"break"                   return BREAK;
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
\"([^\"\\\n]|\\[\"\\nrt0])*\" {
                              yylval.text = new std::string(
                                  decodeString(yytext, static_cast<std::size_t>(yyleng)));
                              return STRING_LITERAL;
                          }
{IDENT}                    {
                              yylval.text = new std::string(yytext);
                              return IDENTIFIER;
                          }
[(){}\[\];,=+\-*/%!<>&^] return yytext[0];
.                         return INVALID;
%%
