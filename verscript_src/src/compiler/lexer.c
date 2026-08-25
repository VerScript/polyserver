#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "../../include/lexer.h"

char *tracked_allocs[1024];
int tracked_alloc_count = 0;

void track_alloc(char *ptr) {
    if (tracked_alloc_count < 1024 && ptr != NULL) {
        tracked_allocs[tracked_alloc_count++] = ptr;
    }
}

void untrack_alloc(char *ptr) {
    for (int i = 0; i < tracked_alloc_count; i++) {
        if (tracked_allocs[i] == ptr) {
            tracked_allocs[i] = tracked_allocs[--tracked_alloc_count];
            return;
        }
    }
}

void free_all_tracked(void) {
    for (int i = 0; i < tracked_alloc_count; i++) {
        if (tracked_allocs[i]) {
            free(tracked_allocs[i]);
            tracked_allocs[i] = NULL;
        }
    }
    tracked_alloc_count = 0;
}

Token getNextToken(const char **cursor) {
    Token token;
    token.value = NULL;

    while (1) {
        // Skip whitespace
        while (isspace((unsigned char)**cursor)) (*cursor)++;

        // Skip comments starting with ! (single line !) or !! (multiline !!)
        if (**cursor == '!') {
            if (*(*cursor + 1) == '!') {
                // Multiline comment !! ... !!
                *cursor += 2;
                while (**cursor != '\0') {
                    if (**cursor == '!' && *(*cursor + 1) == '!') {
                        *cursor += 2;
                        break;
                    }
                    (*cursor)++;
                }
            } else {
                // Single-line comment !
                (*cursor)++;
                while (**cursor != '\n' && **cursor != '\0') (*cursor)++;
            }
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
        if (len == 4 && strncmp(start, "true", 4) == 0) {
            token.type = TOKEN_TRUE;
            return token;
        }
        if (len == 5 && strncmp(start, "false", 5) == 0) {
            token.type = TOKEN_FALSE;
            return token;
        }
        if (len == 4 && strncmp(start, "loop", 4) == 0) {
            token.type = TOKEN_LOOP;
            return token;
        }
        if (len == 7 && strncmp(start, "iterate", 7) == 0) {
            token.type = TOKEN_ITERATE;
            return token;
        }
        if (len == 4 && strncmp(start, "from", 4) == 0) {
            token.type = TOKEN_FROM;
            return token;
        }
        if (len == 2 && strncmp(start, "to", 2) == 0) {
            token.type = TOKEN_TO;
            return token;
        }
        if (len == 2 && strncmp(start, "if", 2) == 0) {
            token.type = TOKEN_IF;
            return token;
        }
        if (len == 4 && strncmp(start, "then", 4) == 0) {
            token.type = TOKEN_THEN;
            return token;
        }
        if (len == 4 && strncmp(start, "else", 4) == 0) {
            token.type = TOKEN_ELSE;
            return token;
        }
        if (len == 5 && strncmp(start, "while", 5) == 0) {
            token.type = TOKEN_WHILE;
            return token;
        }
        if (len == 5 && strncmp(start, "until", 5) == 0) {
            token.type = TOKEN_UNTIL;
            return token;
        }
        if (len == 5 && strncmp(start, "throw", 5) == 0) {
            token.type = TOKEN_THROW;
            return token;
        }
        if (len == 6 && strncmp(start, "inject", 6) == 0) {
            token.type = TOKEN_INJECT;
            return token;
        }
        if (len == 5 && strncmp(start, "alias", 5) == 0) {
            token.type = TOKEN_ALIAS;
            return token;
        }

        token.type = TOKEN_IDENTIFIER;
        token.value = malloc(len + 1);
        track_alloc(token.value);
        strncpy(token.value, start, len);
        token.value[len] = '\0';
        return token;
    }

    // Numbers
    if (isdigit((unsigned char)**cursor)) {
        const char *start = *cursor;
        while (isdigit((unsigned char)**cursor)) (*cursor)++;
        size_t len = *cursor - start;
        token.value = malloc(len + 1);
        track_alloc(token.value);
        strncpy(token.value, start, len);
        token.value[len] = '\0';
        token.type = TOKEN_NUMBER;
        return token;
    }

    // Symbols
    if (strncmp(*cursor, "x=", 2) == 0) { token.type = TOKEN_NOT_EQUAL; (*cursor) += 2; return token; }
    if (strncmp(*cursor, ">=", 2) == 0) { token.type = TOKEN_GREATER_EQUAL; (*cursor) += 2; return token; }
    if (strncmp(*cursor, "<=", 2) == 0) { token.type = TOKEN_LESS_EQUAL; (*cursor) += 2; return token; }
    if (**cursor == '=') { token.type = TOKEN_EQUAL; (*cursor)++; return token; }
    if (**cursor == '>') { token.type = TOKEN_GREATER; (*cursor)++; return token; }
    if (**cursor == '<') { token.type = TOKEN_LESS; (*cursor)++; return token; }
    if (**cursor == ':') { token.type = TOKEN_COLON; (*cursor)++; return token; }
    if (**cursor == '+') { token.type = TOKEN_PLUS; (*cursor)++; return token; }
    if (**cursor == '-') { token.type = TOKEN_MINUS; (*cursor)++; return token; }
    if (**cursor == '*') { token.type = TOKEN_STAR; (*cursor)++; return token; }
    if (**cursor == '/') { token.type = TOKEN_SLASH; (*cursor)++; return token; }
    if (**cursor == '?') { token.type = TOKEN_QUESTION; (*cursor)++; return token; }

    // Strings
    if (**cursor == '"') {
        (*cursor)++;
        const char *start = *cursor;
        while (**cursor != '"' && **cursor != '\0') (*cursor)++;
        size_t len = *cursor - start;
        token.value = malloc(len + 1);
        track_alloc(token.value);
        strncpy(token.value, start, len);
        token.value[len] = '\0';
        token.type = TOKEN_STRING;
        if (**cursor == '"') (*cursor)++;
        return token;
    }

    // Hex colors or color values like #123456 or #fff
    if (**cursor == '#') {
        const char *start = *cursor;
        (*cursor)++;
        while (isxdigit((unsigned char)**cursor) || isalnum((unsigned char)**cursor) || **cursor == '_') (*cursor)++;
        size_t len = *cursor - start;
        token.value = malloc(len + 1);
        track_alloc(token.value);
        strncpy(token.value, start, len);
        token.value[len] = '\0';
        token.type = TOKEN_IDENTIFIER;
        return token;
    }

    // Unknown single character error
    token.type = TOKEN_ERROR;
    token.value = malloc(2);
    track_alloc(token.value);
    token.value[0] = **cursor;
    token.value[1] = '\0';
    (*cursor)++;
    return token;
}
