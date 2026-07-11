#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "../../include/lexer.h"

Token getNextToken(const char **cursor) {
    Token token;
    token.value = NULL;

    while (1) {
        // Skip whitespace
        while (isspace((unsigned char)**cursor)) (*cursor)++;

        // Skip comments starting with !
        if (**cursor == '!') {
            (*cursor)++;
            while (**cursor != '\n' && **cursor != '\0') (*cursor)++;
        } else {
            break;
        }
    }

    if (**cursor == '\0') {
        token.type = TOKEN_EOF;
        return token;
    }

    // Identifiers and Keywords
    if (isalpha((unsigned char)**cursor) || **cursor == '_') {
        const char *start = *cursor;
        while (isalnum((unsigned char)**cursor) || **cursor == '_') (*cursor)++;
        size_t len = *cursor - start;

        if (len == 7 && strncmp(start, "display", 7) == 0) {
            token.type = TOKEN_DISPLAY;
            return token;
        }
        if (len == 6 && strncmp(start, "prompt", 6) == 0) {
            token.type = TOKEN_PROMPT;
            return token;
        }

        token.type = TOKEN_IDENTIFIER;
        token.value = malloc(len + 1);
        strncpy(token.value, start, len);
        token.value[len] = '\0';
        return token;
    }

    // Numbers
    if (isdigit((unsigned char)**cursor)) {
        const char *start = *cursor;
        while (isdigit((unsigned char)**cursor)) (*cursor)++;
        // Simple integers for now
        size_t len = *cursor - start;
        token.type = TOKEN_NUMBER;
        token.value = malloc(len + 1);
        strncpy(token.value, start, len);
        token.value[len] = '\0';
        return token;
    }

    // Symbols
    if (**cursor == ':') { token.type = TOKEN_COLON; (*cursor)++; return token; }
    if (**cursor == '+') { token.type = TOKEN_PLUS; (*cursor)++; return token; }
    if (**cursor == '-') { token.type = TOKEN_MINUS; (*cursor)++; return token; }
    if (**cursor == '*') { token.type = TOKEN_STAR; (*cursor)++; return token; }
    if (**cursor == '/') { token.type = TOKEN_SLASH; (*cursor)++; return token; }

    // Strings
    if (**cursor == '"') {
        (*cursor)++;
        const char *start = *cursor;
        while (**cursor != '"' && **cursor != '\0') (*cursor)++;
        size_t len = *cursor - start;
        token.value = malloc(len + 1);
        strncpy(token.value, start, len);
        token.value[len] = '\0';
        token.type = TOKEN_STRING;
        if (**cursor == '"') (*cursor)++;
        return token;
    }

    // Unknown single character error
    token.type = TOKEN_ERROR;
    token.value = malloc(2);
    token.value[0] = **cursor;
    token.value[1] = '\0';
    (*cursor)++;
    return token;
}
