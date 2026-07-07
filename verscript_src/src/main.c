#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../include/opcodes.h"
#include "../include/lexer.h"

#define MAX_VARS 100

typedef enum { VAR_INT, VAR_STRING } VarType;

typedef struct {
    char name[64];
    VarType type;
    int int_val;
    char *string_val;
} Variable;

Variable symtable[MAX_VARS];
int var_count = 0;

Variable* get_var(const char *name) {
    for (int i = 0; i < var_count; i++) {
        if (strcmp(symtable[i].name, name) == 0) return &symtable[i];
    }
    return NULL;
}

Variable* set_var(const char *name) {
    Variable* v = get_var(name);
    if (v) return v;
    if (var_count >= MAX_VARS) return NULL;
    v = &symtable[var_count++];
    strncpy(v->name, name, 63);
    v->name[63] = '\0';
    v->string_val = NULL;
    return v;
}

Token peekToken(const char **cursor) {
    const char *temp = *cursor;
    return getNextToken(&temp);
}

// Simple evaluator for left-to-right math
int evaluate_expression(const char **cursor, char **out_str) {
    *out_str = NULL;
    Token t = getNextToken(cursor);
    int acc = 0;
    
    if (t.type == TOKEN_NUMBER) {
        acc = atoi(t.value);
    } else if (t.type == TOKEN_STRING) {
        *out_str = strdup(t.value);
    } else if (t.type == TOKEN_IDENTIFIER) {
        Variable *v = get_var(t.value);
        if (v) {
            if (v->type == VAR_INT) acc = v->int_val;
            else { *out_str = strdup(v->string_val); }
        } else {
            printf("ERROR: Undefined variable '%s'\n", t.value);
        }
    } else {
        printf("ERROR: Expected value in expression\n");
    }
    if (t.value) free(t.value);
    
    // Check for operators
    while (1) {
        Token op = peekToken(cursor);
        if (op.type == TOKEN_PLUS || op.type == TOKEN_MINUS || op.type == TOKEN_STAR || op.type == TOKEN_SLASH) {
            Token consumed_op = getNextToken(cursor); // consume op
            Token rhs = getNextToken(cursor);
            int rhs_val = 0;
            if (rhs.type == TOKEN_NUMBER) rhs_val = atoi(rhs.value);
            else if (rhs.type == TOKEN_IDENTIFIER) {
                Variable *v = get_var(rhs.value);
                if (v && v->type == VAR_INT) rhs_val = v->int_val;
            }
            
            if (op.type == TOKEN_PLUS) acc += rhs_val;
            else if (op.type == TOKEN_MINUS) acc -= rhs_val;
            else if (op.type == TOKEN_STAR) acc *= rhs_val;
            else if (op.type == TOKEN_SLASH) {
                if (rhs_val != 0) {
                    acc /= rhs_val;
                } else {
                    printf("ERROR: Division by zero\n");
                }
            }
            if (rhs.value) free(rhs.value);
            if (consumed_op.value) free(consumed_op.value);
            if (op.value) free(op.value);
        } else {
            if (op.value) free(op.value);
            break;
        }
    }
    
    return acc;
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        printf("Usage: %s <filename>\n", argv[0]);
        return 1;
    }

    FILE *file = fopen(argv[1], "r");
    if (!file) {
        printf("ERROR: Could not open file %s\n", argv[1]);
        return 1;
    }
    fseek(file, 0, SEEK_END);
    long length = ftell(file);
    fseek(file, 0, SEEK_SET);
    char *buffer = malloc(length + 1);
    if (!buffer) {
        fclose(file);
        return 1;
    }
    fread(buffer, 1, length, file);
    buffer[length] = '\0';
    fclose(file);

    const char *cursor = buffer;
    Token t;
    
    while ((t = getNextToken(&cursor)).type != TOKEN_EOF) {
        if (t.type == TOKEN_DISPLAY) {
            char *out_str = NULL;
            int val = evaluate_expression(&cursor, &out_str);
            if (out_str) {
                printf("%s\n", out_str);
                free(out_str);
            } else {
                printf("%d\n", val);
            }
        } 
        else if (t.type == TOKEN_PROMPT) {
            Token var_tok = getNextToken(&cursor);
            if (var_tok.type == TOKEN_IDENTIFIER) {
                Variable *v = set_var(var_tok.value);
                if (v) {
                    char input[256];
                    if (fgets(input, sizeof(input), stdin)) {
                        input[strcspn(input, "\n")] = 0; // Remove newline
                        char *endptr;
                        long lval = strtol(input, &endptr, 10);
                        if (*endptr == '\0' && input[0] != '\0') {
                            v->type = VAR_INT;
                            v->int_val = (int)lval;
                        } else {
                            v->type = VAR_STRING;
                            if (v->string_val) free(v->string_val);
                            v->string_val = strdup(input);
                        }
                    }
                } else {
                    printf("ERROR: Maximum variables reached, cannot create '%s'\n", var_tok.value);
                }
            } else {
                printf("ERROR: Expected variable name after prompt\n");
            }
            if (var_tok.value) free(var_tok.value);
        }
        else if (t.type == TOKEN_IDENTIFIER) {
            Token next = peekToken(&cursor);
            if (next.type == TOKEN_COLON) {
                Token consumed_colon = getNextToken(&cursor); // Consume COLON
                if (consumed_colon.value) free(consumed_colon.value);
                char *out_str = NULL;
                int val = evaluate_expression(&cursor, &out_str);
                Variable *v = set_var(t.value);
                if (v) {
                    if (out_str) {
                        v->type = VAR_STRING;
                        if (v->string_val) free(v->string_val);
                        v->string_val = out_str;
                    } else {
                        v->type = VAR_INT;
                        v->int_val = val;
                    }
                } else {
                    if (out_str) free(out_str);
                    printf("ERROR: Maximum variables reached, cannot create '%s'\n", t.value);
                }
            } else {
                printf("ERROR: Unexpected identifier '%s'\n", t.value);
            }
            if (next.value) free(next.value);
        }
        else if (t.type == TOKEN_ERROR) {
            printf("LEXER ERROR: Unexpected token '%s'\n", t.value ? t.value : "");
        }

        if (t.value) {
            free(t.value);
        }
    }

    free(buffer);
    return 0;
}
