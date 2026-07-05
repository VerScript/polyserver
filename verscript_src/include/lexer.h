#ifndef LEXER_H
#define LEXER_H

typedef enum {
    TOKEN_DISPLAY,
    TOKEN_PROMPT,
    TOKEN_STRING,
    TOKEN_IDENTIFIER,
    TOKEN_NUMBER,
    TOKEN_COLON,
    TOKEN_PLUS,
    TOKEN_MINUS,
    TOKEN_STAR,
    TOKEN_SLASH,
    TOKEN_EOF,
    TOKEN_ERROR
} TokenType;

typedef struct {
    TokenType type;
    char *value;
} Token;

Token getNextToken(const char **cursor);

#endif
