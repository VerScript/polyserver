#ifndef LEXER_H
#define LEXER_H

typedef enum {
    TOKEN_DISPLAY,
    TOKEN_PROMPT,
    TOKEN_TRUE,
    TOKEN_FALSE,
    TOKEN_LOOP,
    TOKEN_ITERATE,
    TOKEN_FROM,
    TOKEN_TO,
    TOKEN_IF,
    TOKEN_THEN,
    TOKEN_ELSE,
    TOKEN_WHILE,
    TOKEN_UNTIL,
    TOKEN_EQUAL,
    TOKEN_GREATER,
    TOKEN_LESS,
    TOKEN_GREATER_EQUAL,
    TOKEN_LESS_EQUAL,
    TOKEN_NOT_EQUAL,
    TOKEN_STRING,
    TOKEN_IDENTIFIER,
    TOKEN_NUMBER,
    TOKEN_COLON,
    TOKEN_PLUS,
    TOKEN_MINUS,
    TOKEN_STAR,
    TOKEN_SLASH,
    TOKEN_QUESTION,
    TOKEN_THROW,
    TOKEN_INJECT,
    TOKEN_ALIAS,
    TOKEN_EOF,
    TOKEN_ERROR
} TokenType;

typedef struct {
    TokenType type;
    char *value;
} Token;

Token getNextToken(const char **cursor);

void track_alloc(char *ptr);
void untrack_alloc(char *ptr);
void free_all_tracked(void);

#endif
