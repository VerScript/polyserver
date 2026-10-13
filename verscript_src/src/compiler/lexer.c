#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "../../include/lexer.h"

char **tracked_allocs = NULL;
int tracked_alloc_count = 0;
int tracked_alloc_capacity = 0;

void track_alloc(char *ptr) {
    if (ptr == NULL) return;
    if (tracked_alloc_count >= tracked_alloc_capacity) {
        int new_cap = (tracked_alloc_capacity == 0) ? 1024 : tracked_alloc_capacity * 2;
        char **tmp = realloc(tracked_allocs, new_cap * sizeof(char *));
        if (!tmp) {
            printf("ERROR: MemoryAllocationError: Tracked allocator out of memory\n");
            if (tracked_allocs) { free(tracked_allocs); }
            exit(1);
        }
        tracked_allocs = tmp;
        tracked_alloc_capacity = new_cap;
    }
    tracked_allocs[tracked_alloc_count++] = ptr;
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

void cleanup_lexer(void) {
    free_all_tracked();
    if (tracked_allocs) {
        free(tracked_allocs);
        tracked_allocs = NULL;
        tracked_alloc_capacity = 0;
    }
}

Token getNextToken(const char **cursor) {
    Token token;
    token.value = NULL;
    int had_leading_space = 0;
    const char *orig_cursor = *cursor;

    while (1) {
        // Skip whitespace
        while (isspace((unsigned char)**cursor)) {
            had_leading_space = 1;
            (*cursor)++;
        }

        // Skip comments starting with ! (single line !) or !! (multiline !!)
        // Unless ! is postfix factorial directly following an operand without space
        if (**cursor == '!') {
            if (*(*cursor + 1) == '=') {
                // != is not a comment, handled in symbols
                break;
            }
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
                had_leading_space = 1;
                continue;
            }
            
            // Single !
            // Check if factorial: NO leading whitespace AND directly attached to preceding operand
            if (!had_leading_space && *cursor > orig_cursor && 
                (isalnum((unsigned char)*(*cursor - 1)) || *(*cursor - 1) == '_' || *(*cursor - 1) == ')' || *(*cursor - 1) == ']')) {
                token.type = TOKEN_FACTORIAL;
                (*cursor)++;
                return token;
            }

            // Otherwise, it is a single-line comment !
            (*cursor)++;
            while (**cursor != '\n' && **cursor != '\0') (*cursor)++;
            had_leading_space = 1;
            continue;
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
        if (len == 3 && strncmp(start, "def", 3) == 0) {
            token.type = TOKEN_DEF;
            return token;
        }
        if (len == 4 && strncmp(start, "func", 4) == 0) {
            token.type = TOKEN_FUNC;
            return token;
        }
        if (len == 6 && strncmp(start, "method", 6) == 0) {
            token.type = TOKEN_METHOD;
            return token;
        }
        if (len == 7 && strncmp(start, "inbound", 7) == 0) {
            token.type = TOKEN_INBOUND;
            return token;
        }
        if (len == 8 && strncmp(start, "outbound", 8) == 0) {
            token.type = TOKEN_OUTBOUND;
            return token;
        }
        if (len == 5 && strncmp(start, "reply", 5) == 0) {
            token.type = TOKEN_REPLY;
            return token;
        }
        if (len == 3 && strncmp(start, "set", 3) == 0) {
            token.type = TOKEN_SET;
            return token;
        }
        if (len == 8 && strncmp(start, "DataType", 8) == 0) {
            token.type = TOKEN_DATATYPE;
            token.value = strdup("DataType");
            track_alloc(token.value);
            return token;
        }
        if (len == 7 && strncmp(start, "extends", 7) == 0) {
            token.type = TOKEN_EXTENDS;
            token.value = strdup("extends");
            track_alloc(token.value);
            return token;
        }
        if (len == 2 && strncmp(start, "it", 2) == 0) {
            token.type = TOKEN_IT;
            token.value = strdup("it");
            track_alloc(token.value);
            return token;
        }
        if (len == 3 && strncmp(start, "num", 3) == 0) {
            token.type = TOKEN_TYPE_NUM;
            token.value = strdup("num");
            track_alloc(token.value);
            return token;
        }
        if (len == 4 && strncmp(start, "bool", 4) == 0) {
            token.type = TOKEN_TYPE_BOOL;
            token.value = strdup("bool");
            track_alloc(token.value);
            return token;
        }
        if (len == 3 && strncmp(start, "str", 3) == 0) {
            token.type = TOKEN_TYPE_STR;
            token.value = strdup("str");
            track_alloc(token.value);
            return token;
        }
        if (len == 3 && strncmp(start, "arr", 3) == 0) {
            token.type = TOKEN_TYPE_ARR;
            token.value = strdup("arr");
            track_alloc(token.value);
            return token;
        }
        if (len == 6 && strncmp(start, "entity", 6) == 0) {
            token.type = TOKEN_TYPE_ENTITY;
            token.value = strdup("entity");
            track_alloc(token.value);
            return token;
        }
        if (len == 5 && strncmp(start, "class", 5) == 0) {
            token.type = TOKEN_CLASS;
            return token;
        }
        if (len == 3 && strncmp(start, "lib", 3) == 0) {
            token.type = TOKEN_LIB;
            return token;
        }
        if (len == 7 && strncmp(start, "library", 7) == 0) {
            token.type = TOKEN_LIB;
            return token;
        }
        if (len == 6 && strncmp(start, "static", 6) == 0) {
            token.type = TOKEN_STATIC;
            return token;
        }
        if (len == 7 && strncmp(start, "dynamic", 7) == 0) {
            token.type = TOKEN_DYNAMIC;
            return token;
        }
        if (len == 5 && strncmp(start, "const", 5) == 0) {
            token.type = TOKEN_CONST;
            return token;
        }
        if (len == 6 && strncmp(start, "public", 6) == 0) {
            token.type = TOKEN_PUBLIC;
            return token;
        }
        if (len == 7 && strncmp(start, "private", 7) == 0) {
            token.type = TOKEN_PRIVATE;
            return token;
        }
        if (len == 8 && strncmp(start, "outscope", 8) == 0) {
            token.type = TOKEN_OUTSCOPE;
            return token;
        }
        if (len == 4 && strncmp(start, "load", 4) == 0) {
            token.type = TOKEN_LOAD;
            return token;
        }
        if (len == 3 && strncmp(start, "and", 3) == 0) {
            token.type = TOKEN_BOOL_AND;
            return token;
        }
        if (len == 4 && strncmp(start, "nand", 4) == 0) {
            token.type = TOKEN_BOOL_NAND;
            return token;
        }
        if (len == 2 && strncmp(start, "or", 2) == 0) {
            token.type = TOKEN_BOOL_OR;
            return token;
        }
        if (len == 3 && strncmp(start, "nor", 3) == 0) {
            token.type = TOKEN_BOOL_NOR;
            return token;
        }
        if (len == 3 && strncmp(start, "xor", 3) == 0) {
            token.type = TOKEN_BOOL_XOR;
            return token;
        }
        if (len == 4 && strncmp(start, "xnor", 4) == 0) {
            token.type = TOKEN_BOOL_XNOR;
            return token;
        }
        if (len == 4 && strncmp(start, "xand", 4) == 0) {
            token.type = TOKEN_BOOL_XAND;
            return token;
        }
        if (len == 3 && strncmp(start, "not", 3) == 0) {
            token.type = TOKEN_NOT;
            return token;
        }

        // Native Math Keywords
        if (len == 4 && strncmp(start, "sqrt", 4) == 0) { token.type = TOKEN_SQRT; return token; }
        if (len == 2 && strncmp(start, "rt", 2) == 0) { token.type = TOKEN_RT; return token; }
        if (len == 3 && strncmp(start, "min", 3) == 0) { token.type = TOKEN_MIN; return token; }
        if (len == 3 && strncmp(start, "max", 3) == 0) { token.type = TOKEN_MAX; return token; }
        if (len == 5 && strncmp(start, "clamp", 5) == 0) { token.type = TOKEN_CLAMP; return token; }
        if (len == 4 && strncmp(start, "sign", 4) == 0) { token.type = TOKEN_SIGN; return token; }
        if (len == 3 && strncmp(start, "sgn", 3) == 0) { token.type = TOKEN_SGN; return token; }
        if (len == 6 && (strncmp(start, "divRem", 6) == 0 || strncmp(start, "divrem", 6) == 0)) { token.type = TOKEN_DIVREM; return token; }
        if (len == 6 && (strncmp(start, "isEven", 6) == 0 || strncmp(start, "iseven", 6) == 0)) { token.type = TOKEN_ISEVEN; return token; }
        if (len == 5 && (strncmp(start, "isOdd", 5) == 0 || strncmp(start, "isodd", 5) == 0)) { token.type = TOKEN_ISODD; return token; }
        if (len == 3 && strncmp(start, "gcd", 3) == 0) { token.type = TOKEN_GCD; return token; }
        if (len == 3 && strncmp(start, "hcf", 3) == 0) { token.type = TOKEN_HCF; return token; }
        if (len == 3 && strncmp(start, "lcm", 3) == 0) { token.type = TOKEN_LCM; return token; }
        if (len == 5 && strncmp(start, "floor", 5) == 0) { token.type = TOKEN_FLOOR; return token; }
        if (len == 4 && strncmp(start, "ceil", 4) == 0) { token.type = TOKEN_CEIL; return token; }
        if (len == 5 && strncmp(start, "round", 5) == 0) { token.type = TOKEN_ROUND; return token; }
        if (len == 3 && strncmp(start, "sin", 3) == 0) { token.type = TOKEN_SIN; return token; }
        if (len == 3 && strncmp(start, "cos", 3) == 0) { token.type = TOKEN_COS; return token; }
        if (len == 3 && strncmp(start, "tan", 3) == 0) { token.type = TOKEN_TAN; return token; }
        if (len == 5 && strncmp(start, "hypot", 5) == 0) { token.type = TOKEN_HYPOT; return token; }
        if (len == 8 && (strncmp(start, "degToRad", 8) == 0 || strncmp(start, "degtorad", 8) == 0)) { token.type = TOKEN_DEGTORAD; return token; }
        if (len == 8 && (strncmp(start, "radToDeg", 8) == 0 || strncmp(start, "radtodeg", 8) == 0)) { token.type = TOKEN_RADTODEG; return token; }
        if (len == 6 && (strncmp(start, "sinDeg", 6) == 0 || strncmp(start, "sindeg", 6) == 0)) { token.type = TOKEN_SINDEG; return token; }
        if (len == 6 && (strncmp(start, "cosDeg", 6) == 0 || strncmp(start, "cosdeg", 6) == 0)) { token.type = TOKEN_COSDEG; return token; }
        if (len == 2 && strncmp(start, "ln", 2) == 0) { token.type = TOKEN_LN; return token; }
        if (len == 3 && strncmp(start, "log", 3) == 0) { token.type = TOKEN_LOG; return token; }
        if (len == 4 && strncmp(start, "log2", 4) == 0) { token.type = TOKEN_LOG2; return token; }
        if (len == 3 && strncmp(start, "exp", 3) == 0) { token.type = TOKEN_EXP; return token; }
        if (len == 4 && strncmp(start, "lerp", 4) == 0) { token.type = TOKEN_LERP; return token; }
        if (len == 2 && strncmp(start, "by", 2) == 0) { token.type = TOKEN_BY; return token; }
        if (len == 2 && strncmp(start, "is", 2) == 0) { token.type = TOKEN_IS; return token; }
        if (len == 4 && strncmp(start, "even", 4) == 0) { token.type = TOKEN_EVEN; return token; }
        if (len == 3 && strncmp(start, "odd", 3) == 0) { token.type = TOKEN_ODD; return token; }
        if (len == 2 && strncmp(start, "pi", 2) == 0) { token.type = TOKEN_PI_CONST; return token; }
        if (len == 3 && strncmp(start, "tau", 3) == 0) { token.type = TOKEN_TAU_CONST; return token; }
        if (len == 3 && strncmp(start, "phi", 3) == 0) { token.type = TOKEN_PHI_CONST; return token; }
        if ((len == 3 && strncmp(start, "inf", 3) == 0) || (len == 8 && strncmp(start, "infinity", 8) == 0)) { token.type = TOKEN_INF_CONST; return token; }

        token.type = TOKEN_IDENTIFIER;
        token.value = malloc(len + 1);
        if (!token.value) { printf("ERROR: MemoryAllocationError\n"); exit(1); }
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
        if (!token.value) { printf("ERROR: MemoryAllocationError\n"); exit(1); }
        track_alloc(token.value);
        strncpy(token.value, start, len);
        token.value[len] = '\0';
        token.type = TOKEN_NUMBER;
        return token;
    }

    // Symbols
    if (strncmp(*cursor, "==", 2) == 0) { token.type = TOKEN_EQUAL; (*cursor) += 2; return token; }
    if (strncmp(*cursor, "!=", 2) == 0) { token.type = TOKEN_NOT_EQUAL; (*cursor) += 2; return token; }
    if (strncmp(*cursor, "x=", 2) == 0) { token.type = TOKEN_NOT_EQUAL; (*cursor) += 2; return token; }
    if (strncmp(*cursor, "x&", 2) == 0) { token.type = TOKEN_BOOL_XAMP; (*cursor) += 2; return token; }
    if (strncmp(*cursor, "&&", 2) == 0) { token.type = TOKEN_AMP; (*cursor) += 2; return token; }
    if (**cursor == '&') { token.type = TOKEN_AMP; (*cursor)++; return token; }
    if (strncmp(*cursor, "||", 2) == 0) { token.type = TOKEN_BOOL_OR; (*cursor) += 2; return token; }
    if (strncmp(*cursor, "//", 2) == 0) { token.type = TOKEN_SLASH_SLASH; (*cursor) += 2; return token; }
    if (**cursor == '|') { token.type = TOKEN_PIPE; (*cursor)++; return token; }
    if (**cursor == '^') { token.type = TOKEN_CARET; (*cursor)++; return token; }
    if (**cursor == '%') { token.type = TOKEN_PERCENT; (*cursor)++; return token; }
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
    if (**cursor == '(') { token.type = TOKEN_LPAREN; (*cursor)++; return token; }
    if (**cursor == ')') { token.type = TOKEN_RPAREN; (*cursor)++; return token; }
    if (**cursor == '[') { token.type = TOKEN_LBRACKET; (*cursor)++; return token; }
    if (**cursor == ']') { token.type = TOKEN_RBRACKET; (*cursor)++; return token; }
    if (**cursor == ',') { token.type = TOKEN_COMMA; (*cursor)++; return token; }
    if (**cursor == '.') { token.type = TOKEN_DOT; (*cursor)++; return token; }

    // Unicode math symbols
    if (strncmp(*cursor, "\xE2\x88\x9A", 3) == 0) { token.type = TOKEN_SQRT; (*cursor) += 3; return token; } // √
    if (strncmp(*cursor, "\xCF\x80", 2) == 0) { token.type = TOKEN_PI_CONST; (*cursor) += 2; return token; } // π
    if (strncmp(*cursor, "\xCF\x84", 2) == 0) { token.type = TOKEN_TAU_CONST; (*cursor) += 2; return token; } // τ
    if (strncmp(*cursor, "\xE2\x8C\x8A", 3) == 0) { token.type = TOKEN_FLOOR; (*cursor) += 3; return token; } // ⌊
    if (strncmp(*cursor, "\xE2\x8C\x8B", 3) == 0) { token.type = TOKEN_PIPE; (*cursor) += 3; return token; } // ⌋
    if (strncmp(*cursor, "\xE2\x8C\x88", 3) == 0) { token.type = TOKEN_CEIL; (*cursor) += 3; return token; } // ⌈
    if (strncmp(*cursor, "\xE2\x8C\x89", 3) == 0) { token.type = TOKEN_PIPE; (*cursor) += 3; return token; } // ⌉

    // Strings
    if (**cursor == '"') {
        (*cursor)++;
        const char *start = *cursor;
        while (**cursor != '"' && **cursor != '\0') (*cursor)++;
        size_t len = *cursor - start;
        token.value = malloc(len + 1);
        if (!token.value) { printf("ERROR: MemoryAllocationError\n"); exit(1); }
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
        if (!token.value) { printf("ERROR: MemoryAllocationError\n"); exit(1); }
        track_alloc(token.value);
        strncpy(token.value, start, len);
        token.value[len] = '\0';
        token.type = TOKEN_IDENTIFIER;
        return token;
    }

    // Unknown single character error
    token.type = TOKEN_ERROR;
    token.value = malloc(2);
    if (!token.value) { printf("ERROR: MemoryAllocationError\n"); exit(1); }
    track_alloc(token.value);
    token.value[0] = **cursor;
    token.value[1] = '\0';
    (*cursor)++;
    return token;
}
