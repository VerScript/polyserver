#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdarg.h>
#include <setjmp.h>
#ifndef _USE_MATH_DEFINES
#define _USE_MATH_DEFINES
#endif
#include <math.h>
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#include "../include/opcodes.h"
#include "../include/lexer.h"

typedef enum { VAR_INT, VAR_STRING, VAR_BOOL, VAR_ARRAY, VAR_ENTITY } VarType;

struct Variable;
struct Routine;
struct Entity;
struct LibraryDef;

typedef struct MetadataStore {
    struct Variable *static_vars;
    int static_count;
    int static_capacity;
    struct Variable *dynamic_vars;
    int dynamic_count;
    int dynamic_capacity;
    struct Variable *thisstatic_vars;
    int thisstatic_count;
    int thisstatic_capacity;
    int override_all;
    int override_static;
} MetadataStore;

typedef struct Array {
    struct Variable *items;
    int count;
    int capacity;
} Array;

typedef struct Entity {
    char class_name[64];
    struct Variable *static_vars;
    int static_count;
    int static_capacity;
    int *static_is_public;
    struct Variable *dynamic_vars;
    int dynamic_count;
    int dynamic_capacity;
    struct Routine *methods;
    int method_count;
    int method_capacity;
    MetadataStore meta;
} Entity;

typedef struct Variable {
    char *name;
    VarType type;
    int int_val;
    char *string_val;
    Array *array_val;
    Entity *entity_val;
    int scope_level;
    char declared_type[64];
} Variable;

typedef struct ClassDef {
    char name[64];
    char params[16][64];
    char param_types[16][64];
    int param_count;
    int static_start_line;
    int static_end_line;
    int dynamic_start_line;
    int dynamic_end_line;
    MetadataStore meta;
} ClassDef;

#define MAX_CLASSES 64
ClassDef classes[MAX_CLASSES];
int class_count = 0;

typedef struct LibraryDef {
    char name[64];
    Variable *const_vars;
    int const_count;
    int const_capacity;
    Variable *dynamic_vars;
    int dynamic_count;
    int dynamic_capacity;
    struct Routine *routines;
    int routine_count;
    int routine_capacity;
    MetadataStore meta;
} LibraryDef;

#define MAX_LIBRARIES 64
LibraryDef libraries[MAX_LIBRARIES];
int library_count = 0;

Entity *current_entity = NULL;
LibraryDef *current_library = NULL;

ClassDef* find_class(const char *name) {
    for (int i = 0; i < class_count; i++) {
        if (strcmp(classes[i].name, name) == 0) return &classes[i];
    }
    return NULL;
}

LibraryDef* find_library(const char *name) {
    for (int i = 0; i < library_count; i++) {
        if (strcmp(libraries[i].name, name) == 0) return &libraries[i];
    }
    return NULL;
}

Variable *symtable = NULL;
int var_count = 0;
int var_capacity = 0;

int get_attribute_str(const char *full_text, const char *key, char *out_val, int max_len) {
    if (!full_text || !key || !out_val || max_len <= 0) return 0;
    out_val[0] = '\0';
    char search_pattern[64];
    snprintf(search_pattern, sizeof(search_pattern), "?%s=", key);
    const char *ptr = strstr(full_text, search_pattern);
    if (!ptr) {
        snprintf(search_pattern, sizeof(search_pattern), "%s=", key);
        ptr = strstr(full_text, search_pattern);
    }
    if (ptr) {
        ptr = strchr(ptr, '=');
        if (ptr) {
            ptr++;
            while (isspace((unsigned char)*ptr)) ptr++;
            int quote = (*ptr == '"' || *ptr == '\'') ? *ptr++ : 0;
            int idx = 0;
            while (*ptr != '\0' && idx < max_len - 1) {
                if (quote && *ptr == quote) break;
                if (!quote && (isspace((unsigned char)*ptr) || *ptr == '?')) break;
                out_val[idx++] = *ptr++;
            }
            out_val[idx] = '\0';
            return 1;
        }
    }
    return 0;
}

void apply_color(const char *col) {
    if (!col || col[0] == '\0') return;
    if (strcmp(col, "green") == 0) printf("\033[32m");
    else if (strcmp(col, "red") == 0) printf("\033[31m");
    else if (strcmp(col, "yellow") == 0) printf("\033[33m");
    else if (strcmp(col, "blue") == 0) printf("\033[34m");
    else if (strcmp(col, "purple") == 0) printf("\033[35m");
    else if (strcmp(col, "cyan") == 0) printf("\033[36m");
    else if (strcmp(col, "white") == 0) printf("\033[37m");
    else if (col[0] == '#') {
        unsigned int r = 0, g = 0, b = 0;
        int len = strlen(col);
        if (len == 7) {
            if (sscanf(col + 1, "%02x%02x%02x", &r, &g, &b) == 3) {
                printf("\033[38;2;%u;%u;%um", r, g, b);
            }
        } else if (len == 4) {
            if (sscanf(col + 1, "%1x%1x%1x", &r, &g, &b) == 3) {
                printf("\033[38;2;%u;%u;%um", r * 17, g * 17, b * 17);
            }
        }
    }
}

typedef struct {
    char from_arg[64]; // aliased attribute/arg name
    char to_arg[64];   // canonical attribute/arg name
} ArgMapping;

typedef struct {
    char target_cmd[64]; // original target command (e.g. "display")
    char alias_name[64]; // new alias name (e.g. "print")
    ArgMapping mappings[16];
    int mapping_count;
} CommandAlias;

#define MAX_ALIASES 128
CommandAlias aliases[MAX_ALIASES];
int alias_count = 0;

void add_command_alias(const char *target_cmd, const char *alias_name, ArgMapping *mappings, int mapping_count) {
    if (!target_cmd || !alias_name || !*target_cmd || !*alias_name) return;
    for (int i = 0; i < alias_count; i++) {
        if (strcmp(aliases[i].alias_name, alias_name) == 0) {
            strncpy(aliases[i].target_cmd, target_cmd, sizeof(aliases[i].target_cmd) - 1);
            aliases[i].target_cmd[sizeof(aliases[i].target_cmd) - 1] = '\0';
            aliases[i].mapping_count = mapping_count;
            for (int j = 0; j < mapping_count && j < 16; j++) {
                aliases[i].mappings[j] = mappings[j];
            }
            return;
        }
    }
    if (alias_count < MAX_ALIASES) {
        CommandAlias *a = &aliases[alias_count++];
        strncpy(a->target_cmd, target_cmd, sizeof(a->target_cmd) - 1);
        a->target_cmd[sizeof(a->target_cmd) - 1] = '\0';
        strncpy(a->alias_name, alias_name, sizeof(a->alias_name) - 1);
        a->alias_name[sizeof(a->alias_name) - 1] = '\0';
        a->mapping_count = mapping_count;
        for (int j = 0; j < mapping_count && j < 16; j++) {
            a->mappings[j] = mappings[j];
        }
    }
}

CommandAlias* find_alias(const char *alias_name) {
    for (int i = 0; i < alias_count; i++) {
        if (strcmp(aliases[i].alias_name, alias_name) == 0) return &aliases[i];
    }
    return NULL;
}

void parse_and_register_alias(const char *spec) {
    if (!spec) return;
    while (isspace((unsigned char)*spec)) spec++;
    if (strncmp(spec, "alias", 5) == 0 && (isspace((unsigned char)spec[5]) || spec[5] == ':')) {
        spec += 5;
        while (isspace((unsigned char)*spec)) spec++;
    }
    if (*spec == '\0' || *spec == ':') return;

    const char *p = spec;
    while (*p && (isalnum((unsigned char)*p) || *p == '_')) p++;
    int cmd1_len = p - spec;
    if (cmd1_len <= 0) return;
    char cmd1[64];
    if (cmd1_len >= 64) cmd1_len = 63;
    strncpy(cmd1, spec, cmd1_len);
    cmd1[cmd1_len] = '\0';

    while (isspace((unsigned char)*p)) p++;
    if (*p == ':') p++;
    while (isspace((unsigned char)*p)) p++;

    const char *p2 = p;
    while (*p2 && (isalnum((unsigned char)*p2) || *p2 == '_')) p2++;
    int cmd2_len = p2 - p;
    if (cmd2_len <= 0) return;
    char cmd2[64];
    if (cmd2_len >= 64) cmd2_len = 63;
    strncpy(cmd2, p, cmd2_len);
    cmd2[cmd2_len] = '\0';
    p = p2;

    ArgMapping mappings[16];
    int mapping_count = 0;

    while (isspace((unsigned char)*p)) p++;
    if (*p == '?') {
        p++;
        while (*p != '\0' && *p != '\n' && mapping_count < 16) {
            while (isspace((unsigned char)*p) || *p == ',') p++;
            if (*p == '\0' || *p == '\n') break;
            const char *arg_start = p;
            while (*p && (isalnum((unsigned char)*p) || *p == '_')) p++;
            int a1_len = p - arg_start;
            if (a1_len <= 0) break;
            char a1[64];
            if (a1_len >= 64) a1_len = 63;
            strncpy(a1, arg_start, a1_len);
            a1[a1_len] = '\0';

            while (isspace((unsigned char)*p)) p++;
            if (*p == '=') {
                p++;
                while (isspace((unsigned char)*p)) p++;
                const char *a2_start = p;
                while (*p && (isalnum((unsigned char)*p) || *p == '_')) p++;
                int a2_len = p - a2_start;
                if (a2_len > 0) {
                    char a2[64];
                    if (a2_len >= 64) a2_len = 63;
                    strncpy(a2, a2_start, a2_len);
                    a2[a2_len] = '\0';
                    strncpy(mappings[mapping_count].to_arg, a1, 63);
                    mappings[mapping_count].to_arg[63] = '\0';
                    strncpy(mappings[mapping_count].from_arg, a2, 63);
                    mappings[mapping_count].from_arg[63] = '\0';
                    mapping_count++;
                }
            } else {
                strncpy(mappings[mapping_count].from_arg, a1, 63);
                mappings[mapping_count].from_arg[63] = '\0';
                strncpy(mappings[mapping_count].to_arg, a1, 63);
                mappings[mapping_count].to_arg[63] = '\0';
                mapping_count++;
            }
        }
    }

    add_command_alias(cmd1, cmd2, mappings, mapping_count);
}

char* resolve_alias_line(const char *line_text, char *out_buf, size_t out_buf_size) {
    if (!line_text || !out_buf || out_buf_size == 0) return (char*)line_text;
    const char *p = line_text;
    while (isspace((unsigned char)*p)) p++;
    if (*p == '\0' || *p == '!') {
        strncpy(out_buf, line_text, out_buf_size - 1);
        out_buf[out_buf_size - 1] = '\0';
        return out_buf;
    }

    const char *cmd_start = p;
    while (*p && (isalnum((unsigned char)*p) || *p == '_')) p++;
    int cmd_len = p - cmd_start;
    if (cmd_len <= 0) {
        strncpy(out_buf, line_text, out_buf_size - 1);
        out_buf[out_buf_size - 1] = '\0';
        return out_buf;
    }

    char cmd[64];
    if (cmd_len >= 64) cmd_len = 63;
    strncpy(cmd, cmd_start, cmd_len);
    cmd[cmd_len] = '\0';

    CommandAlias *a = find_alias(cmd);
    if (!a) {
        strncpy(out_buf, line_text, out_buf_size - 1);
        out_buf[out_buf_size - 1] = '\0';
        return out_buf;
    }

    int prefix_len = cmd_start - line_text;
    int pos = 0;
    if (prefix_len > 0 && pos + prefix_len < (int)out_buf_size - 1) {
        strncpy(out_buf + pos, line_text, prefix_len);
        pos += prefix_len;
    }

    int tgt_len = strlen(a->target_cmd);
    if (pos + tgt_len < (int)out_buf_size - 1) {
        memmove(out_buf + pos, a->target_cmd, tgt_len + 1);
        pos += tgt_len;
    }

    const char *rem = p;
    while (*rem != '\0' && pos < (int)out_buf_size - 1) {
        if (*rem == '?') {
            const char *q_start = rem;
            rem++;
            const char *arg_start = rem;
            while (*rem && (isalnum((unsigned char)*rem) || *rem == '_')) rem++;
            int arg_len = rem - arg_start;
            char cur_arg[64] = "";
            if (arg_len > 0 && arg_len < 64) {
                strncpy(cur_arg, arg_start, arg_len);
                cur_arg[arg_len] = '\0';
            }

            const char *mapped_to = NULL;
            for (int m = 0; m < a->mapping_count; m++) {
                if (strcmp(a->mappings[m].from_arg, cur_arg) == 0) {
                    mapped_to = a->mappings[m].to_arg;
                    break;
                }
            }

            if (mapped_to) {
                out_buf[pos++] = '?';
                int m_len = strlen(mapped_to);
                if (pos + m_len < (int)out_buf_size - 1) {
                    memmove(out_buf + pos, mapped_to, m_len + 1);
                    pos += m_len;
                }
            } else {
                int orig_len = rem - q_start;
                if (pos + orig_len < (int)out_buf_size - 1) {
                    strncpy(out_buf + pos, q_start, orig_len);
                    pos += orig_len;
                }
            }
        } else {
            out_buf[pos++] = *rem++;
        }
    }
    out_buf[pos] = '\0';
    return out_buf;
}

#define MAX_JMP_STACK 64
jmp_buf jmp_env_stack[MAX_JMP_STACK];
int jmp_stack_ptr = 0;

#define MODE_DEFAULT 0
#define MODE_INTERNAL 1
#define MODE_EXTERNAL 2

char current_error_name[128] = "";
char current_error_msg[256] = "";

#define ERR_MODE_NORMAL 0
#define ERR_MODE_FORCE 1
#define ERR_MODE_CRITICAL 2
#define ERR_MODE_SUPPRESS 3

int error_mode = ERR_MODE_NORMAL;
jmp_buf suppress_jmp_env;
int suppress_jmp_active = 0;
int current_executing_line = 1;

char *source_buffer = NULL;

typedef struct {
    char *text;
    int indent;
    int line_num;
} Line;

Line *lines = NULL;
int line_count = 0;
int line_capacity = 0;

typedef struct {
    int has_replied;
    int int_val;
    char *string_val;
    Array *array_val;
    Entity *entity_val;
    VarType type;
} ReplyResult;

ReplyResult current_reply = {0, 0, NULL, NULL, NULL, VAR_INT};
int call_stack_ptr = 0;
int current_scope_depth = 0;

void reset_custom_datatypes(void);

void free_globals(void) {
    if (source_buffer) {
        free(source_buffer);
        source_buffer = NULL;
    }
    if (current_reply.string_val) {
        free(current_reply.string_val);
        current_reply.string_val = NULL;
    }
    call_stack_ptr = 0;
    current_scope_depth = 0;
    if (lines) {
        for (int i = 0; i < line_count; i++) {
            if (lines[i].text) {
                free(lines[i].text);
                lines[i].text = NULL;
            }
        }
        free(lines);
        lines = NULL;
    }
    if (symtable) {
        for (int i = 0; i < var_count; i++) {
            if (symtable[i].name) {
                free(symtable[i].name);
                symtable[i].name = NULL;
            }
            if (symtable[i].string_val) {
                free(symtable[i].string_val);
                symtable[i].string_val = NULL;
            }
        }
        free(symtable);
        symtable = NULL;
    }
    cleanup_lexer();
    reset_custom_datatypes();
}

typedef struct {
    int active;
    const char *expr;
    int triggered;
} WatchCondition;

WatchCondition active_watch = {0, NULL, 0};

int is_critical_error(const char *name) {
    if (strcmp(name, "MemoryAllocationError") == 0) return 1;
    if (strcmp(name, "SystemError") == 0) return 1;
    if (strcmp(name, "SyntaxError") == 0) return 1;
    if (strcmp(name, "IndentationError") == 0) return 1;
    return 0;
}

void throw_error(const char *name, const char *fmt, ...) {
    if (name != current_error_name) {
        int len = strlen(name);
        if (len > 127) len = 127;
        memmove(current_error_name, name, len);
        current_error_name[len] = '\0';
    }

    char temp_msg[256];
    va_list args;
    va_start(args, fmt);
    vsnprintf(temp_msg, sizeof(temp_msg), fmt, args);
    va_end(args);

    memmove(current_error_msg, temp_msg, strlen(temp_msg) + 1);

    if (error_mode == ERR_MODE_FORCE) {
        free_all_tracked();
        printf("ERROR: %s: %s\n", current_error_name, current_error_msg);
        fflush(stdout);
        fflush(stderr);
        free_globals();
        exit(1);
    }

    int is_crit = is_critical_error(name);
    int suppress = 0;
    if (error_mode == ERR_MODE_SUPPRESS) {
        suppress = 1;
    } else if (error_mode == ERR_MODE_CRITICAL && !is_crit) {
        suppress = 1;
    }

    if (suppress) {
        if (suppress_jmp_active) {
            longjmp(suppress_jmp_env, 1);
        }
    }

    if (jmp_stack_ptr > 0) {
        longjmp(jmp_env_stack[jmp_stack_ptr - 1], 1);
    } else {
        free_all_tracked();
        printf("ERROR: %s: %s\n", current_error_name, current_error_msg);
        fflush(stdout);
        fflush(stderr);
        free_globals();
        exit(1);
    }
}

int is_error_name(const char *name) {
    if (strcmp(name, "error") == 0) return 1;
    if (strcmp(name, "MemoryAllocationError") == 0) return 1;
    if (strcmp(name, "UndefinedVariableError") == 0) return 1;
    if (strcmp(name, "InvalidOperandError") == 0) return 1;
    if (strcmp(name, "DivisionByZeroError") == 0) return 1;
    if (strcmp(name, "IndentationError") == 0) return 1;
    if (strcmp(name, "LoopIterationError") == 0) return 1;
    if (strcmp(name, "LoopLimitError") == 0) return 1;
    if (strcmp(name, "LoopDirectionError") == 0) return 1;
    if (strcmp(name, "LoopStepError") == 0) return 1;
    if (strcmp(name, "ScopeViolationError") == 0) return 1;
    if (strcmp(name, "SyntaxError") == 0) return 1;
    if (strcmp(name, "RuntimeError") == 0) return 1;
    if (strcmp(name, "InvalidErrorNameError") == 0) return 1;
    if (strcmp(name, "SystemError") == 0) return 1;
    if (strcmp(name, "VisibilityError") == 0) return 1;
    if (strcmp(name, "ImmutableError") == 0) return 1;
    if (strcmp(name, "IndexOutOfBoundsError") == 0) return 1;
    if (strcmp(name, "EntityError") == 0) return 1;
    if (strcmp(name, "FileError") == 0) return 1;
    if (strcmp(name, "DomainError") == 0) return 1;
    if (strcmp(name, "AssertError") == 0) return 1;
    if (strcmp(name, "DataTypeError") == 0) return 1;
    return 0;
}

typedef enum {
    ROUTINE_FUNC,
    ROUTINE_METHOD
} RoutineKind;

typedef enum {
    PURITY_INBOUND,
    PURITY_OUTBOUND
} PurityKind;

typedef struct Routine {
    char name[64];
    RoutineKind kind;
    PurityKind purity;
    int is_private;
    char params[16][64];
    char param_types[16][64];
    int param_count;
    int body_start_line;
    int body_end_line;
    struct Entity *bound_entity;
    struct LibraryDef *bound_library;
} Routine;

void copy_variable(Variable *dst, const Variable *src);
char* array_to_string(const Array *arr);
char* entity_to_string(const Entity *ent);

Array* create_array(void) {
    Array *arr = malloc(sizeof(Array));
    if (!arr) { printf("ERROR: MemoryAllocationError\n"); exit(1); }
    track_alloc((char*)arr);
    arr->items = NULL;
    arr->count = 0;
    arr->capacity = 0;
    return arr;
}

void array_append(Array *arr, const Variable *val) {
    if (!arr) return;
    if (arr->count >= arr->capacity) {
        int new_cap = (arr->capacity == 0) ? 8 : arr->capacity * 2;
        if (arr->items) untrack_alloc((char*)arr->items);
        Variable *tmp = realloc(arr->items, new_cap * sizeof(Variable));
        if (!tmp) { printf("ERROR: MemoryAllocationError\n"); exit(1); }
        arr->items = tmp;
        track_alloc((char*)arr->items);
        arr->capacity = new_cap;
    }
    copy_variable(&arr->items[arr->count++], val);
}

void copy_variable(Variable *dst, const Variable *src) {
    if (!dst || !src) return;
    dst->name = src->name ? strdup(src->name) : NULL;
    if (dst->name) track_alloc(dst->name);
    dst->type = src->type;
    dst->int_val = src->int_val;
    dst->string_val = src->string_val ? strdup(src->string_val) : NULL;
    if (dst->string_val) track_alloc(dst->string_val);
    dst->array_val = src->array_val;
    dst->entity_val = src->entity_val;
    dst->scope_level = src->scope_level;
    strncpy(dst->declared_type, src->declared_type, 63);
    dst->declared_type[63] = '\0';
}

char* array_to_string(const Array *arr) {
    if (!arr || arr->count == 0) {
        char *res = strdup("[]");
        track_alloc(res);
        return res;
    }
    int cap = 256;
    char *buf = malloc(cap);
    if (!buf) { printf("ERROR: MemoryAllocationError\n"); exit(1); }
    track_alloc(buf);
    buf[0] = '[';
    buf[1] = '\0';
    int len = 1;

    for (int i = 0; i < arr->count; i++) {
        char item_buf[512] = "";
        Variable *it = &arr->items[i];
        if (it->type == VAR_INT) {
            snprintf(item_buf, sizeof(item_buf), "%d", it->int_val);
        } else if (it->type == VAR_BOOL) {
            snprintf(item_buf, sizeof(item_buf), "%s", it->int_val ? "true" : "false");
        } else if (it->type == VAR_STRING) {
            snprintf(item_buf, sizeof(item_buf), "\"%s\"", it->string_val ? it->string_val : "");
        } else if (it->type == VAR_ARRAY) {
            char *sub = array_to_string(it->array_val);
            strncpy(item_buf, sub, sizeof(item_buf) - 1);
        } else if (it->type == VAR_ENTITY) {
            snprintf(item_buf, sizeof(item_buf), "<Entity:%s>", it->entity_val ? it->entity_val->class_name : "unknown");
        }

        int item_len = strlen(item_buf);
        if (len + item_len + 4 >= cap) {
            cap = (len + item_len + 4) * 2;
            untrack_alloc(buf);
            buf = realloc(buf, cap);
            if (!buf) { printf("ERROR: MemoryAllocationError\n"); exit(1); }
            track_alloc(buf);
        }
        strcat(buf, item_buf);
        len += item_len;
        if (i < arr->count - 1) {
            strcat(buf, ", ");
            len += 2;
        }
    }
    strcat(buf, "]");
    return buf;
}

char* entity_to_string(const Entity *ent) {
    char buf[128];
    snprintf(buf, sizeof(buf), "<Entity:%s>", ent ? ent->class_name : "unknown");
    char *res = strdup(buf);
    track_alloc(res);
    return res;
}

// ═══════════════════════════════════════════════════════════════════
// NATIVE ARRAY OPERATIONS (camelCase)
// ═══════════════════════════════════════════════════════════════════

int array_len(const Array *arr) {
    return arr ? arr->count : 0;
}

void array_push(Array *arr, const Variable *val) {
    if (!arr) return;
    array_append(arr, val);
}

int array_pop(Array *arr, Variable *out_item, int line_num) {
    if (!arr || arr->count == 0) {
        throw_error("IndexOutOfBoundsError", "Cannot pop from empty array on line %d", line_num);
    }
    arr->count--;
    if (out_item) {
        copy_variable(out_item, &arr->items[arr->count]);
    }
    return 1;
}

int array_index_of(const Array *arr, const Variable *target) {
    if (!arr || !target) return -1;
    for (int i = 0; i < arr->count; i++) {
        Variable *it = &arr->items[i];
        if (it->type == target->type) {
            if (it->type == VAR_INT && it->int_val == target->int_val) return i;
            if (it->type == VAR_BOOL && it->int_val == target->int_val) return i;
            if (it->type == VAR_STRING) {
                const char *s1 = it->string_val ? it->string_val : "";
                const char *s2 = target->string_val ? target->string_val : "";
                if (strcmp(s1, s2) == 0) return i;
            }
        }
    }
    return -1;
}

int array_contains(const Array *arr, const Variable *target) {
    return array_index_of(arr, target) >= 0;
}

Array* array_slice(const Array *arr, int start, int end) {
    Array *res = create_array();
    if (!arr) return res;
    int count = arr->count;
    if (start < 0) start = 0;
    if (start > count) start = count;
    if (end < start) end = start;
    if (end > count) end = count;
    for (int i = start; i < end; i++) {
        array_append(res, &arr->items[i]);
    }
    return res;
}

Array* array_reverse(Array *arr) {
    if (!arr || arr->count <= 1) return arr;
    int i = 0, j = arr->count - 1;
    while (i < j) {
        Variable tmp = arr->items[i];
        arr->items[i] = arr->items[j];
        arr->items[j] = tmp;
        i++; j--;
    }
    return arr;
}

char* array_join(const Array *arr, const char *sep) {
    if (!sep) sep = "";
    if (!arr || arr->count == 0) {
        char *r = strdup("");
        track_alloc(r);
        return r;
    }
    int cap = 256;
    char *buf = malloc(cap);
    if (!buf) { printf("ERROR: MemoryAllocationError\n"); exit(1); }
    track_alloc(buf);
    buf[0] = '\0';
    int cur_len = 0;
    int sep_len = strlen(sep);

    for (int i = 0; i < arr->count; i++) {
        char item_buf[512] = "";
        Variable *it = &arr->items[i];
        if (it->type == VAR_INT) snprintf(item_buf, sizeof(item_buf), "%d", it->int_val);
        else if (it->type == VAR_BOOL) snprintf(item_buf, sizeof(item_buf), "%s", it->int_val ? "true" : "false");
        else if (it->type == VAR_STRING) snprintf(item_buf, sizeof(item_buf), "%s", it->string_val ? it->string_val : "");
        else if (it->type == VAR_ARRAY) {
            char *sub = array_to_string(it->array_val);
            strncpy(item_buf, sub, sizeof(item_buf) - 1);
        }
        int item_len = strlen(item_buf);
        if (cur_len + item_len + sep_len + 2 >= cap) {
            cap = (cur_len + item_len + sep_len + 2) * 2;
            untrack_alloc(buf);
            buf = realloc(buf, cap);
            if (!buf) { printf("ERROR: MemoryAllocationError\n"); exit(1); }
            track_alloc(buf);
        }
        strcat(buf, item_buf);
        cur_len += item_len;
        if (i < arr->count - 1 && sep_len > 0) {
            strcat(buf, sep);
            cur_len += sep_len;
        }
    }
    return buf;
}

void array_swap(Array *arr, int i, int j, int line_num) {
    if (!arr || i < 0 || i >= arr->count || j < 0 || j >= arr->count) {
        throw_error("IndexOutOfBoundsError", "Array index out of bounds in swap on line %d", line_num);
    }
    Variable tmp = arr->items[i];
    arr->items[i] = arr->items[j];
    arr->items[j] = tmp;
}

void array_clear(Array *arr) {
    if (!arr) return;
    for (int i = 0; i < arr->count; i++) {
        if (arr->items[i].string_val) free(arr->items[i].string_val);
        if (arr->items[i].name) free(arr->items[i].name);
    }
    arr->count = 0;
}

void array_fill(Array *arr, const Variable *val, int count) {
    if (!arr || !val) return;
    if (count > 0) {
        array_clear(arr);
        for (int i = 0; i < count; i++) {
            array_append(arr, val);
        }
    } else {
        for (int i = 0; i < arr->count; i++) {
            if (arr->items[i].string_val) free(arr->items[i].string_val);
            if (arr->items[i].name) free(arr->items[i].name);
            copy_variable(&arr->items[i], val);
        }
    }
}

int array_is_empty(const Array *arr) {
    return (!arr || arr->count == 0);
}

// ═══════════════════════════════════════════════════════════════════
// NATIVE STRING OPERATIONS (camelCase)
// ═══════════════════════════════════════════════════════════════════

int string_len(const char *str) {
    return str ? (int)strlen(str) : 0;
}

int string_index_of(const char *str, const char *sub) {
    if (!str || !sub) return -1;
    const char *pos = strstr(str, sub);
    return pos ? (int)(pos - str) : -1;
}

int string_contains(const char *str, const char *sub) {
    return string_index_of(str, sub) >= 0;
}

char* string_slice(const char *str, int start, int end) {
    if (!str) { char *r = strdup(""); track_alloc(r); return r; }
    int len = (int)strlen(str);
    if (start < 0) start = 0;
    if (start > len) start = len;
    if (end < start) end = start;
    if (end > len) end = len;
    int sub_len = end - start;
    char *buf = malloc(sub_len + 1);
    if (!buf) { printf("ERROR: MemoryAllocationError\n"); exit(1); }
    track_alloc(buf);
    memcpy(buf, str + start, sub_len);
    buf[sub_len] = '\0';
    return buf;
}

char* string_repeat(const char *str, int count) {
    if (!str || count <= 0) { char *r = strdup(""); track_alloc(r); return r; }
    int len = (int)strlen(str);
    int total = len * count;
    char *buf = malloc(total + 1);
    if (!buf) { printf("ERROR: MemoryAllocationError\n"); exit(1); }
    track_alloc(buf);
    buf[0] = '\0';
    for (int i = 0; i < count; i++) strcat(buf, str);
    return buf;
}

char* string_pad_left(const char *str, int target_len, const char *pad_char) {
    const char *p = (pad_char && pad_char[0] != '\0') ? pad_char : " ";
    int cur_len = str ? (int)strlen(str) : 0;
    if (cur_len >= target_len) {
        char *r = strdup(str ? str : "");
        track_alloc(r);
        return r;
    }
    int diff = target_len - cur_len;
    char *buf = malloc(target_len + 1);
    if (!buf) { printf("ERROR: MemoryAllocationError\n"); exit(1); }
    track_alloc(buf);
    buf[0] = '\0';
    int plen = strlen(p);
    for (int i = 0; i < diff; i++) {
        buf[i] = p[i % plen];
    }
    buf[diff] = '\0';
    strcat(buf, str ? str : "");
    return buf;
}

char* string_pad_right(const char *str, int target_len, const char *pad_char) {
    const char *p = (pad_char && pad_char[0] != '\0') ? pad_char : " ";
    int cur_len = str ? (int)strlen(str) : 0;
    if (cur_len >= target_len) {
        char *r = strdup(str ? str : "");
        track_alloc(r);
        return r;
    }
    char *buf = malloc(target_len + 1);
    if (!buf) { printf("ERROR: MemoryAllocationError\n"); exit(1); }
    track_alloc(buf);
    memmove(buf, str ? str : "", str ? strlen(str) + 1 : 1);
    int plen = strlen(p);
    for (int i = cur_len; i < target_len; i++) {
        buf[i] = p[(i - cur_len) % plen];
    }
    buf[target_len] = '\0';
    return buf;
}

int string_is_empty(const char *str) {
    return (!str || str[0] == '\0');
}

char* string_trim(const char *str) {
    if (!str) { char *r = strdup(""); track_alloc(r); return r; }
    while (isspace((unsigned char)*str)) str++;
    if (*str == '\0') { char *r = strdup(""); track_alloc(r); return r; }
    const char *end = str + strlen(str) - 1;
    while (end > str && isspace((unsigned char)*end)) end--;
    int len = (int)(end - str + 1);
    char *buf = malloc(len + 1);
    if (!buf) { printf("ERROR: MemoryAllocationError\n"); exit(1); }
    track_alloc(buf);
    memcpy(buf, str, len);
    buf[len] = '\0';
    return buf;
}

char* string_to_upper(const char *str) {
    if (!str) { char *r = strdup(""); track_alloc(r); return r; }
    char *buf = strdup(str);
    track_alloc(buf);
    for (char *p = buf; *p; p++) *p = toupper((unsigned char)*p);
    return buf;
}

char* string_to_lower(const char *str) {
    if (!str) { char *r = strdup(""); track_alloc(r); return r; }
    char *buf = strdup(str);
    track_alloc(buf);
    for (char *p = buf; *p; p++) *p = tolower((unsigned char)*p);
    return buf;
}

Array* string_split(const char *str, const char *sep) {
    Array *arr = create_array();
    if (!str) return arr;
    if (!sep || sep[0] == '\0') sep = " ";
    char *dup = strdup(str);
    int sep_len = strlen(sep);
    char *cursor = dup;
    while (cursor && *cursor) {
        char *pos = strstr(cursor, sep);
        if (pos) {
            *pos = '\0';
            Variable v;
            memset(&v, 0, sizeof(v));
            v.type = VAR_STRING;
            v.string_val = strdup(cursor);
            array_append(arr, &v);
            free(v.string_val);
            cursor = pos + sep_len;
        } else {
            Variable v;
            memset(&v, 0, sizeof(v));
            v.type = VAR_STRING;
            v.string_val = strdup(cursor);
            array_append(arr, &v);
            free(v.string_val);
            break;
        }
    }
    free(dup);
    return arr;
}

// ═══════════════════════════════════════════════════════════════════
// EMBEDDED STANDARD RELEASE CORE LIBRARIES
// ═══════════════════════════════════════════════════════════════════

static const char *CORE_LIB_STATS =
    "lib Stats\n"
    "  meta:\n"
    "    static:\n"
    "      category: \"Statistics\"\n"
    "      version: \"1.0.0\"\n"
    "  dynamic:\n"
    "    def func sum values\n"
    "      if values.length() = 0 then\n"
    "        reply 0\n"
    "      set total: 0\n"
    "      iterate i from 0 to values.length() - 1\n"
    "        set total: total + values[i]\n"
    "      reply total\n"
    "    def func mean values\n"
    "      if values.length() = 0 then\n"
    "        reply 0\n"
    "      set s: (sum values)\n"
    "      reply s / values.length()\n"
    "    def func minMax values\n"
    "      if values.length() = 0 then\n"
    "        reply [0, 0]\n"
    "      set curMin: values[0]\n"
    "      set curMax: values[0]\n"
    "      if values.length() > 1 then\n"
    "        iterate i from 1 to values.length() - 1\n"
    "          set x: values[i]\n"
    "          if x < curMin then\n"
    "            set curMin: x\n"
    "          if x > curMax then\n"
    "            set curMax: x\n"
    "      reply [curMin, curMax]\n"
    "    def func range values\n"
    "      set mm: (minMax values)\n"
    "      reply mm[1] - mm[0]\n";

static const char *CORE_LIB_ASSERT =
    "lib Assert\n"
    "  meta:\n"
    "    static:\n"
    "      category: \"Testing & Verification\"\n"
    "      version: \"1.0.0\"\n"
    "  dynamic:\n"
    "    def method equal actual expected message\n"
    "      if actual != expected then\n"
    "        throw \"AssertionError: \" + message\n"
    "    def method notEqual actual expected message\n"
    "      if actual = expected then\n"
    "        throw \"AssertionError: \" + message\n"
    "    def method isTrue condition message\n"
    "      if not condition then\n"
    "        throw \"AssertionError: \" + message\n"
    "    def method isFalse condition message\n"
    "      if condition then\n"
    "        throw \"AssertionError: \" + message\n";

const char* get_embedded_core_lib(const char *name) {
    if (strcmp(name, "Stats") == 0) return CORE_LIB_STATS;
    if (strcmp(name, "Assert") == 0) return CORE_LIB_ASSERT;
    return NULL;
}

#define MAX_ROUTINES 128
Routine routines[MAX_ROUTINES];
int routine_count = 0;

typedef struct {
    Routine *routine;
    int caller_scope_level;
} CallFrame;

#define MAX_CALL_STACK 64
CallFrame call_stack[MAX_CALL_STACK];

Routine* find_routine(const char *name) {
    if (current_entity) {
        for (int i = 0; i < current_entity->method_count; i++) {
            if (strcmp(current_entity->methods[i].name, name) == 0) {
                return &current_entity->methods[i];
            }
        }
    }
    for (int i = 0; i < routine_count; i++) {
        if (strcmp(routines[i].name, name) == 0) return &routines[i];
    }
    for (int lib_idx = 0; lib_idx < library_count; lib_idx++) {
        LibraryDef *lib = &libraries[lib_idx];
        for (int i = 0; i < lib->routine_count; i++) {
            if (strcmp(lib->routines[i].name, name) == 0) {
                return &lib->routines[i];
            }
        }
    }
    return NULL;
}

Variable* get_var(const char *name) {
    if (strcmp(name, "error") == 0) {
        Variable *v = NULL;
        for (int i = 0; i < var_count; i++) {
            if (strcmp(symtable[i].name, "error") == 0) {
                v = &symtable[i];
                break;
            }
        }
        if (!v) {
            if (var_count >= var_capacity) {
                int new_capacity = (var_capacity == 0) ? 100 : var_capacity * 2;
                Variable *tmp = realloc(symtable, new_capacity * sizeof(Variable));
                if (!tmp) {
                    printf("ERROR: MemoryAllocationError: Memory allocation failed\n");
                    free_globals();
                    exit(1);
                }
                symtable = tmp;
                var_capacity = new_capacity;
            }
            v = &symtable[var_count++];
            v->name = strdup("error");
            v->type = VAR_STRING;
            v->int_val = 0;
            v->string_val = strdup(current_error_name);
            v->scope_level = 0;
        } else {
            v->type = VAR_STRING;
            if (v->string_val) free(v->string_val);
            v->string_val = strdup(current_error_name);
        }
        return v;
    }
    for (int i = var_count - 1; i >= 0; i--) {
        if (strcmp(symtable[i].name, name) == 0) return &symtable[i];
    }
    if (current_entity) {
        for (int i = 0; i < current_entity->dynamic_count; i++) {
            if (strcmp(current_entity->dynamic_vars[i].name, name) == 0) {
                return &current_entity->dynamic_vars[i];
            }
        }
        for (int i = 0; i < current_entity->static_count; i++) {
            if (strcmp(current_entity->static_vars[i].name, name) == 0) {
                return &current_entity->static_vars[i];
            }
        }
    }
    for (int lib_idx = 0; lib_idx < library_count; lib_idx++) {
        LibraryDef *lib = &libraries[lib_idx];
        for (int i = 0; i < lib->const_count; i++) {
            if (strcmp(lib->const_vars[i].name, name) == 0) {
                return &lib->const_vars[i];
            }
        }
        for (int i = 0; i < lib->dynamic_count; i++) {
            if (strcmp(lib->dynamic_vars[i].name, name) == 0) {
                return &lib->dynamic_vars[i];
            }
        }
    }
    return NULL;
}

void pop_scope(int target_depth) {
    while (var_count > 0 && symtable[var_count - 1].scope_level > target_depth) {
        var_count--;
        if (symtable[var_count].name) {
            free(symtable[var_count].name);
            symtable[var_count].name = NULL;
        }
        if (symtable[var_count].string_val) {
            free(symtable[var_count].string_val);
            symtable[var_count].string_val = NULL;
        }
    }
}

int is_outbound_assign = 0;
int is_outscope_assign = 0;
int in_entity_instantiation = 0;

Variable* set_var_scoped(const char *name, int line_num) {
    if (strcmp(name, "error") == 0) {
        return get_var("error");
    }

    // Check if modifying a library constant
    for (int lib_idx = 0; lib_idx < library_count; lib_idx++) {
        LibraryDef *lib = &libraries[lib_idx];
        for (int i = 0; i < lib->const_count; i++) {
            if (strcmp(lib->const_vars[i].name, name) == 0) {
                throw_error("ImmutableError", "Cannot modify immutable library constant '%s' on line %d", name, line_num);
            }
        }
    }

    if (current_entity && (is_outbound_assign || in_entity_instantiation)) {
        for (int i = 0; i < current_entity->dynamic_count; i++) {
            if (strcmp(current_entity->dynamic_vars[i].name, name) == 0) {
                return &current_entity->dynamic_vars[i];
            }
        }
        if (current_entity->dynamic_count >= current_entity->dynamic_capacity) {
            int new_cap = (current_entity->dynamic_capacity == 0) ? 16 : current_entity->dynamic_capacity * 2;
            if (current_entity->dynamic_vars) untrack_alloc((char*)current_entity->dynamic_vars);
            Variable *tmp = realloc(current_entity->dynamic_vars, new_cap * sizeof(Variable));
            if (!tmp) throw_error("MemoryAllocationError", "Memory allocation failed for entity dynamic vars");
            current_entity->dynamic_vars = tmp;
            track_alloc((char*)current_entity->dynamic_vars);
            current_entity->dynamic_capacity = new_cap;
        }
        Variable *v = &current_entity->dynamic_vars[current_entity->dynamic_count++];
        memset(v, 0, sizeof(Variable));
        v->name = strdup(name);
        track_alloc(v->name);
        v->type = VAR_INT;
        return v;
    }

    if (is_outscope_assign) {
        for (int i = 0; i < var_count; i++) {
            if (symtable[i].scope_level == 0 && strcmp(symtable[i].name, name) == 0) {
                return &symtable[i];
            }
        }
        if (var_count >= var_capacity) {
            int new_capacity = (var_capacity == 0) ? 100 : var_capacity * 2;
            Variable *tmp = realloc(symtable, new_capacity * sizeof(Variable));
            if (!tmp) throw_error("MemoryAllocationError", "Memory allocation failed");
            symtable = tmp;
            var_capacity = new_capacity;
        }
        Variable *v = &symtable[var_count++];
        v->name = strdup(name);
        v->type = VAR_INT;
        v->int_val = 0;
        v->string_val = NULL;
        v->array_val = NULL;
        v->entity_val = NULL;
        v->scope_level = 0;
        return v;
    }

    Variable *existing = NULL;
    for (int i = var_count - 1; i >= 0; i--) {
        if (strcmp(symtable[i].name, name) == 0) {
            existing = &symtable[i];
            break;
        }
    }

    if (existing) {
        if (existing->scope_level < current_scope_depth) {
            PurityKind cur_purity = PURITY_OUTBOUND;
            const char *rname = "routine";
            if (call_stack_ptr > 0) {
                cur_purity = call_stack[call_stack_ptr - 1].routine->purity;
                rname = call_stack[call_stack_ptr - 1].routine->name;
            }
            if (cur_purity == PURITY_INBOUND) {
                throw_error("ScopeViolationError", "Inbound routine '%s' cannot modify outer variable '%s' on line %d", rname, name, line_num);
            }
            return existing;
        }
        return existing;
    }

    if (var_count >= var_capacity) {
        int new_capacity = (var_capacity == 0) ? 100 : var_capacity * 2;
        Variable *tmp = realloc(symtable, new_capacity * sizeof(Variable));
        if (!tmp) {
            throw_error("MemoryAllocationError", "Memory allocation failed");
        }
        symtable = tmp;
        var_capacity = new_capacity;
    }

    Variable *v = &symtable[var_count++];
    v->name = strdup(name);
    v->type = VAR_INT;
    v->int_val = 0;
    v->string_val = NULL;
    v->scope_level = current_scope_depth;
    return v;
}

Variable* set_var(const char *name) {
    return set_var_scoped(name, current_executing_line);
}

Token peekToken(const char **cursor) {
    const char *temp = *cursor;
    return getNextToken(&temp);
}

void freeToken(Token *t) {
    if (t->value) {
        untrack_alloc(t->value);
        free(t->value);
        t->value = NULL;
    }
}

// Forward declarations
void execute_line(const char *text, int line_num);
void execute_block(int start, int end);
int evaluate_expression(const char **cursor, char **out_str, int *out_type);
int evaluate_operand(const char **cursor, char **out_str, int *out_type);
void call_routine(Routine *r, const char **cursor, int line_num);
void call_routine_val(Routine *r, const char **cursor, char **out_str, int *out_type, int *out_int, int line_num);
void parse_lines(const char *buffer);
void load_library(const char *target, int line_num);
void load_library_source(const char *source, const char *origin_name, int line_num);

Entity* instantiate_entity(ClassDef *cd, const char **cursor, int line_num);
int evaluate_operand_val(const char **cursor, char **out_str, int *out_type, Array **out_arr, Entity **out_ent);
int evaluate_expression_val(const char **cursor, char **out_str, int *out_type, Array **out_arr, Entity **out_ent);
int dispatch_array_method(Array *arr, const char *method, const char **cursor, char **out_str, int *out_type, Array **out_arr, Entity **out_ent, int line_num);
int dispatch_string_method(const char *str, const char *method, const char **cursor, char **out_str, int *out_type, Array **out_arr, Entity **out_ent, int line_num);
int is_native_func_name(const char *name);

static inline int is_truthy(int val, const char *str_val, int type) {
    if (type == VAR_STRING) {
        return (str_val != NULL && str_val[0] != '\0' && strcmp(str_val, "false") != 0 && strcmp(str_val, "0") != 0);
    }
    return (val != 0);
}

// ═══════════════════════════════════════════════════════════════════
// CUSTOM DATATYPES & TYPE CONSTRAINT VALIDATION ENGINE
// ═══════════════════════════════════════════════════════════════════

typedef struct {
    char name[64];
    char base_type[64];
    int constraint_start_line;
    int constraint_end_line;
} CustomDataType;

#define MAX_CUSTOM_DATATYPES 64
static CustomDataType custom_datatypes[MAX_CUSTOM_DATATYPES];
static int custom_datatype_count = 0;

void reset_custom_datatypes(void) {
    custom_datatype_count = 0;
}

CustomDataType* find_custom_datatype(const char *name) {
    if (!name) return NULL;
    for (int i = 0; i < custom_datatype_count; i++) {
        if (strcmp(custom_datatypes[i].name, name) == 0) return &custom_datatypes[i];
    }
    return NULL;
}

int is_valid_type_name(const char *name) {
    if (!name || name[0] == '\0') return 0;
    if (strcmp(name, "num") == 0 || strcmp(name, "bool") == 0 || strcmp(name, "str") == 0 ||
        strcmp(name, "arr") == 0 || strcmp(name, "entity") == 0 || strcmp(name, "DataType") == 0) return 1;
    if (strcmp(name, "int") == 0 || strcmp(name, "posNum") == 0 || strcmp(name, "nonNegNum") == 0 ||
        strcmp(name, "byte") == 0 || strcmp(name, "port") == 0 || strcmp(name, "nonEmptyStr") == 0 ||
        strcmp(name, "char") == 0 || strcmp(name, "nonEmptyArr") == 0) return 1;
    if (find_custom_datatype(name)) return 1;
    return 0;
}

const char* get_type_name_from_token(const Token *t) {
    if (!t) return "";
    if (t->type == TOKEN_TYPE_NUM) return "num";
    if (t->type == TOKEN_TYPE_BOOL) return "bool";
    if (t->type == TOKEN_TYPE_STR) return "str";
    if (t->type == TOKEN_TYPE_ARR) return "arr";
    if (t->type == TOKEN_TYPE_ENTITY) return "entity";
    if (t->type == TOKEN_DATATYPE) return "DataType";
    if (t->type == TOKEN_IDENTIFIER && t->value) return t->value;
    return "";
}

int is_type_token(TokenType type, const char *val) {
    if (type == TOKEN_TYPE_NUM || type == TOKEN_TYPE_BOOL || type == TOKEN_TYPE_STR ||
        type == TOKEN_TYPE_ARR || type == TOKEN_TYPE_ENTITY || type == TOKEN_DATATYPE) return 1;
    if (type == TOKEN_IDENTIFIER && val && is_valid_type_name(val)) return 1;
    return 0;
}

void validate_value_type(const char *type_name, int int_val, const char *str_val, int val_type, Array *arr_val, Entity *ent_val, int line_num) {
    if (!type_name || type_name[0] == '\0') return;

    if (strcmp(type_name, "num") == 0) {
        if (val_type != VAR_INT) {
            throw_error("DataTypeError", "Type constraint failed: expected 'num' on line %d", line_num);
        }
        return;
    }
    if (strcmp(type_name, "bool") == 0) {
        if (val_type != VAR_BOOL) {
            throw_error("DataTypeError", "Type constraint failed: expected 'bool' on line %d", line_num);
        }
        return;
    }
    if (strcmp(type_name, "str") == 0) {
        if (val_type != VAR_STRING) {
            throw_error("DataTypeError", "Type constraint failed: expected 'str' on line %d", line_num);
        }
        return;
    }
    if (strcmp(type_name, "arr") == 0) {
        if (val_type != VAR_ARRAY) {
            throw_error("DataTypeError", "Type constraint failed: expected 'arr' on line %d", line_num);
        }
        return;
    }
    if (strcmp(type_name, "entity") == 0) {
        if (val_type != VAR_ENTITY) {
            throw_error("DataTypeError", "Type constraint failed: expected 'entity' on line %d", line_num);
        }
        return;
    }
    if (strcmp(type_name, "DataType") == 0) {
        if (val_type == VAR_STRING && str_val && (find_custom_datatype(str_val) || is_valid_type_name(str_val))) {
            return;
        }
        throw_error("DataTypeError", "Type constraint failed: expected defined 'DataType' on line %d", line_num);
        return;
    }

    // Built-in primitive constraints
    if (strcmp(type_name, "int") == 0) {
        if (val_type != VAR_INT) {
            throw_error("DataTypeError", "Constraint violated for 'int': expected num on line %d", line_num);
        }
        return;
    }
    if (strcmp(type_name, "posNum") == 0) {
        if (val_type != VAR_INT || int_val <= 0) {
            throw_error("DataTypeError", "Constraint violated for 'posNum': value must be > 0 on line %d", line_num);
        }
        return;
    }
    if (strcmp(type_name, "nonNegNum") == 0) {
        if (val_type != VAR_INT || int_val < 0) {
            throw_error("DataTypeError", "Constraint violated for 'nonNegNum': value must be >= 0 on line %d", line_num);
        }
        return;
    }
    if (strcmp(type_name, "byte") == 0) {
        if (val_type != VAR_INT || int_val < 0 || int_val > 255) {
            throw_error("DataTypeError", "Constraint violated for 'byte': value must be 0-255 on line %d", line_num);
        }
        return;
    }
    if (strcmp(type_name, "port") == 0) {
        if (val_type != VAR_INT || int_val < 1 || int_val > 65535) {
            throw_error("DataTypeError", "Constraint violated for 'port': value must be 1-65535 on line %d", line_num);
        }
        return;
    }
    if (strcmp(type_name, "nonEmptyStr") == 0) {
        if (val_type != VAR_STRING || !str_val || strlen(str_val) == 0) {
            throw_error("DataTypeError", "Constraint violated for 'nonEmptyStr': string cannot be empty on line %d", line_num);
        }
        return;
    }
    if (strcmp(type_name, "char") == 0) {
        if (val_type != VAR_STRING || !str_val || strlen(str_val) != 1) {
            throw_error("DataTypeError", "Constraint violated for 'char': string must have length 1 on line %d", line_num);
        }
        return;
    }
    if (strcmp(type_name, "nonEmptyArr") == 0) {
        if (val_type != VAR_ARRAY || !arr_val || arr_val->count == 0) {
            throw_error("DataTypeError", "Constraint violated for 'nonEmptyArr': array cannot be empty on line %d", line_num);
        }
        return;
    }

    CustomDataType *cdt = find_custom_datatype(type_name);
    if (!cdt) {
        throw_error("DataTypeError", "Unknown datatype '%s' on line %d", type_name, line_num);
        return;
    }

    // Validate base type
    validate_value_type(cdt->base_type, int_val, str_val, val_type, arr_val, ent_val, line_num);

    if (cdt->constraint_start_line <= cdt->constraint_end_line) {
        int prev_scope = current_scope_depth;
        current_scope_depth++;

        Variable *it_var = set_var_scoped("it", line_num);
        it_var->type = val_type;
        it_var->int_val = int_val;
        if (str_val) {
            it_var->string_val = strdup(str_val);
            track_alloc(it_var->string_val);
        } else {
            it_var->string_val = NULL;
        }
        it_var->array_val = arr_val;
        it_var->entity_val = ent_val;

        for (int c_idx = cdt->constraint_start_line; c_idx <= cdt->constraint_end_line; c_idx++) {
            Line *cln = &lines[c_idx];
            if (cln->text[0] == '\0' || cln->text[0] == '!') continue;

            char res_buf[2048];
            const char *eff = resolve_alias_line(cln->text, res_buf, sizeof(res_buf));
            const char *c_cursor = eff;

            char *out_s = NULL;
            int out_ty = VAR_INT;
            Array *out_a = NULL;
            Entity *out_e = NULL;
            int b_val = evaluate_expression_val(&c_cursor, &out_s, &out_ty, &out_a, &out_e);
            int pass = is_truthy(b_val, out_s, out_ty);
            if (out_s) { untrack_alloc(out_s); free(out_s); }

            if (!pass) {
                pop_scope(prev_scope);
                current_scope_depth = prev_scope;
                throw_error("DataTypeError", "Value violated DataType '%s' constraint on line %d", cdt->name, line_num);
            }
        }

        pop_scope(prev_scope);
        current_scope_depth = prev_scope;
    }
}

void parse_parameter_list(const char **cursor, char params[16][64], char param_types[16][64], int *param_count) {
    *param_count = 0;
    while (1) {
        Token ptok = getNextToken(cursor);
        if (ptok.type == TOKEN_EOF || ptok.type == TOKEN_RPAREN) {
            freeToken(&ptok);
            break;
        }
        if (ptok.type == TOKEN_LPAREN || ptok.type == TOKEN_COMMA) {
            freeToken(&ptok);
            continue;
        }

        if (is_type_token(ptok.type, ptok.value)) {
            char tname[64];
            strncpy(tname, get_type_name_from_token(&ptok), 63);
            tname[63] = '\0';
            freeToken(&ptok);

            Token name_tok = getNextToken(cursor);
            if (name_tok.type == TOKEN_IDENTIFIER && name_tok.value && *param_count < 16) {
                strncpy(params[*param_count], name_tok.value, 63);
                params[*param_count][63] = '\0';
                strncpy(param_types[*param_count], tname, 63);
                param_types[*param_count][63] = '\0';
                (*param_count)++;
            }
            freeToken(&name_tok);
            continue;
        }

        if (ptok.type == TOKEN_IDENTIFIER && ptok.value && *param_count < 16) {
            char pname[64];
            strncpy(pname, ptok.value, 63);
            pname[63] = '\0';
            freeToken(&ptok);

            Token peek_c = peekToken(cursor);
            if (peek_c.type == TOKEN_COLON) {
                freeToken(&peek_c);
                Token col = getNextToken(cursor); freeToken(&col);
                Token type_tok = getNextToken(cursor);
                char tname[64] = "";
                if (is_type_token(type_tok.type, type_tok.value)) {
                    strncpy(tname, get_type_name_from_token(&type_tok), 63);
                    tname[63] = '\0';
                }
                freeToken(&type_tok);

                strncpy(params[*param_count], pname, 63);
                params[*param_count][63] = '\0';
                strncpy(param_types[*param_count], tname, 63);
                param_types[*param_count][63] = '\0';
                (*param_count)++;
            } else {
                freeToken(&peek_c);
                strncpy(params[*param_count], pname, 63);
                params[*param_count][63] = '\0';
                param_types[*param_count][0] = '\0';
                (*param_count)++;
            }
            continue;
        }
        freeToken(&ptok);
    }
}

// ═══════════════════════════════════════════════════════════════════
// NATIVE PSEUDO & RANDOM GENERATION ENGINE
// ═══════════════════════════════════════════════════════════════════

static unsigned long long pseudo_seed = 123456789ULL;

void pseudo_srand(unsigned long long s) {
    pseudo_seed = s ? s : 1ULL;
}

unsigned long long pseudo_rand(void) {
    pseudo_seed = (pseudo_seed * 6364136223846793005ULL + 1442695040888963407ULL);
    return (pseudo_seed >> 32);
}

int is_pseudo_module_name(const char *name) {
    return (name && (strcmp(name, "Pseudo") == 0 || strcmp(name, "Random") == 0));
}

int is_pseudo_method_name(const char *name) {
    if (!name) return 0;
    if (strcmp(name, "seed") == 0 || strcmp(name, "next") == 0 ||
        strcmp(name, "random") == 0 || strcmp(name, "between") == 0 ||
        strcmp(name, "boolean") == 0 || strcmp(name, "bool") == 0 ||
        strcmp(name, "choice") == 0 || strcmp(name, "shuffle") == 0 ||
        strcmp(name, "sample") == 0 || strcmp(name, "uuid") == 0 ||
        strcmp(name, "name") == 0 || strcmp(name, "email") == 0 ||
        strcmp(name, "alphaNumeric") == 0 || strcmp(name, "alphanumeric") == 0) return 1;
    return 0;
}

int dispatch_pseudo_method(const char *method, const char **cursor, char **out_str, int *out_type, Array **out_arr, Entity **out_ent, int line_num) {
    (void)out_ent;
    *out_type = VAR_INT;
    if (out_str) *out_str = NULL;
    if (out_arr) *out_arr = NULL;

    Token lp = peekToken(cursor);
    int has_parens = 0;
    if (lp.type == TOKEN_LPAREN) {
        has_parens = 1;
        freeToken(&lp);
        Token t_lp = getNextToken(cursor); freeToken(&t_lp);
    } else {
        freeToken(&lp);
    }

    int acc = 0;

    if (strcmp(method, "seed") == 0) {
        char *s_str = NULL; int s_type = VAR_INT; Array *s_arr = NULL; Entity *s_ent = NULL;
        int s_val = evaluate_operand_val(cursor, &s_str, &s_type, &s_arr, &s_ent);
        if (s_str) { untrack_alloc(s_str); free(s_str); }
        pseudo_srand((unsigned long long)s_val);
        acc = s_val;
        *out_type = VAR_INT;
    }
    else if (strcmp(method, "next") == 0 || strcmp(method, "random") == 0) {
        acc = (int)(pseudo_rand() % 2147483647);
        *out_type = VAR_INT;
    }
    else if (strcmp(method, "between") == 0) {
        char *s1 = NULL, *s2 = NULL; int t1 = VAR_INT, t2 = VAR_INT;
        int min_v = evaluate_operand_val(cursor, &s1, &t1, NULL, NULL);
        if (s1) { untrack_alloc(s1); free(s1); }
        Token sep = peekToken(cursor);
        if (sep.type == TOKEN_COMMA) { freeToken(&sep); Token cm = getNextToken(cursor); freeToken(&cm); }
        else { freeToken(&sep); }
        int max_v = evaluate_operand_val(cursor, &s2, &t2, NULL, NULL);
        if (s2) { untrack_alloc(s2); free(s2); }
        if (min_v > max_v) { int tmp = min_v; min_v = max_v; max_v = tmp; }
        int span = max_v - min_v + 1;
        if (span <= 0) acc = min_v;
        else acc = min_v + (int)(pseudo_rand() % span);
        *out_type = VAR_INT;
    }
    else if (strcmp(method, "boolean") == 0 || strcmp(method, "bool") == 0) {
        acc = (pseudo_rand() % 2 == 0);
        *out_type = VAR_BOOL;
    }
    else if (strcmp(method, "choice") == 0) {
        char *s_str = NULL; int s_type = VAR_INT; Array *target_arr = NULL; Entity *s_ent = NULL;
        int num_v = evaluate_operand_val(cursor, &s_str, &s_type, &target_arr, &s_ent);
        if (target_arr && target_arr->count > 0) {
            int idx = (int)(pseudo_rand() % target_arr->count);
            Variable *item = &target_arr->items[idx];
            *out_type = item->type;
            acc = item->int_val;
            if (item->type == VAR_STRING && item->string_val) {
                if (out_str) { *out_str = strdup(item->string_val); track_alloc(*out_str); }
            } else if (item->type == VAR_ARRAY) {
                if (out_arr) *out_arr = item->array_val;
                if (out_str) { *out_str = array_to_string(item->array_val); }
            } else if (item->type == VAR_ENTITY) {
                if (out_ent) *out_ent = item->entity_val;
                if (out_str) { *out_str = entity_to_string(item->entity_val); }
            }
        } else {
            acc = num_v;
            *out_type = s_type;
            if (s_str && out_str) { *out_str = s_str; }
            else if (s_str) { untrack_alloc(s_str); free(s_str); }
        }
    }
    else if (strcmp(method, "shuffle") == 0) {
        char *s_str = NULL; int s_type = VAR_INT; Array *src_arr = NULL; Entity *s_ent = NULL;
        evaluate_operand_val(cursor, &s_str, &s_type, &src_arr, &s_ent);
        if (s_str) { untrack_alloc(s_str); free(s_str); }
        Array *new_arr = create_array();
        if (src_arr && src_arr->count > 0) {
            for (int k = 0; k < src_arr->count; k++) {
                array_append(new_arr, &src_arr->items[k]);
            }
            for (int k = new_arr->count - 1; k > 0; k--) {
                int j = (int)(pseudo_rand() % (k + 1));
                Variable temp = new_arr->items[k];
                new_arr->items[k] = new_arr->items[j];
                new_arr->items[j] = temp;
            }
        }
        *out_type = VAR_ARRAY;
        if (out_arr) *out_arr = new_arr;
        if (out_str) *out_str = array_to_string(new_arr);
    }
    else if (strcmp(method, "sample") == 0) {
        char *s_str = NULL; int s_type = VAR_INT; Array *src_arr = NULL; Entity *s_ent = NULL;
        evaluate_operand_val(cursor, &s_str, &s_type, &src_arr, &s_ent);
        if (s_str) { untrack_alloc(s_str); free(s_str); }
        Token sep = peekToken(cursor);
        if (sep.type == TOKEN_COMMA) { freeToken(&sep); Token cm = getNextToken(cursor); freeToken(&cm); }
        else { freeToken(&sep); }
        char *cnt_str = NULL; int cnt_type = VAR_INT;
        int count = evaluate_operand_val(cursor, &cnt_str, &cnt_type, NULL, NULL);
        if (cnt_str) { untrack_alloc(cnt_str); free(cnt_str); }

        Array *sampled = create_array();
        if (src_arr && src_arr->count > 0 && count > 0) {
            Array *temp = create_array();
            for (int k = 0; k < src_arr->count; k++) array_append(temp, &src_arr->items[k]);
            for (int k = temp->count - 1; k > 0; k--) {
                int j = (int)(pseudo_rand() % (k + 1));
                Variable t_var = temp->items[k]; temp->items[k] = temp->items[j]; temp->items[j] = t_var;
            }
            int take = (count > temp->count) ? temp->count : count;
            for (int k = 0; k < take; k++) array_append(sampled, &temp->items[k]);
        }
        *out_type = VAR_ARRAY;
        if (out_arr) *out_arr = sampled;
        if (out_str) *out_str = array_to_string(sampled);
    }
    else if (strcmp(method, "uuid") == 0) {
        char uuid_buf[64];
        snprintf(uuid_buf, sizeof(uuid_buf), "%08x-%04x-4%03x-%04x-%04x%08x",
                 (unsigned int)(pseudo_rand() & 0xFFFFFFFF),
                 (unsigned int)(pseudo_rand() & 0xFFFF),
                 (unsigned int)(pseudo_rand() & 0x0FFF),
                 (unsigned int)((pseudo_rand() & 0x3FFF) | 0x8000),
                 (unsigned int)(pseudo_rand() & 0xFFFF),
                 (unsigned int)(pseudo_rand() & 0xFFFFFFFF));
        *out_type = VAR_STRING;
        if (out_str) { *out_str = strdup(uuid_buf); track_alloc(*out_str); }
    }
    else if (strcmp(method, "name") == 0) {
        static const char *mock_names[] = {
            "Alexander Wright", "Elena Rostova", "Marcus Vance", "Aria Montgomery",
            "Kaelen Voss", "Sophia Sterling", "Damon Thorne", "Lyra Vance",
            "Julian Cross", "Seraphina Locke", "Caspian Drake", "Nova Calder",
            "Adrian Pierce", "Isolde Winter", "Theron Gallagher", "Vesper Hale",
            "Lucian Mercer", "Callista Finch", "Rowan Blackwood", "Genevieve Hayes"
        };
        int n_idx = (int)(pseudo_rand() % (sizeof(mock_names) / sizeof(mock_names[0])));
        *out_type = VAR_STRING;
        if (out_str) { *out_str = strdup(mock_names[n_idx]); track_alloc(*out_str); }
    }
    else if (strcmp(method, "email") == 0) {
        static const char *prefixes[] = { "alex.wright", "elena.r", "marcus.v", "aria.m", "kaelen.voss", "sophia.s", "damon.thorne" };
        static const char *domains[] = { "verscript.io", "cloudnet.org", "quantum.dev", "apex.corp", "matrix.net" };
        int p_idx = (int)(pseudo_rand() % (sizeof(prefixes) / sizeof(prefixes[0])));
        int d_idx = (int)(pseudo_rand() % (sizeof(domains) / sizeof(domains[0])));
        char email_buf[128];
        snprintf(email_buf, sizeof(email_buf), "%s%d@%s", prefixes[p_idx], (int)(pseudo_rand() % 99 + 1), domains[d_idx]);
        *out_type = VAR_STRING;
        if (out_str) { *out_str = strdup(email_buf); track_alloc(*out_str); }
    }
    else if (strcmp(method, "alphaNumeric") == 0 || strcmp(method, "alphanumeric") == 0) {
        int len = 8;
        Token peek_a = peekToken(cursor);
        if (peek_a.type != TOKEN_RPAREN && peek_a.type != TOKEN_EOF) {
            freeToken(&peek_a);
            char *s_l = NULL; int t_l = VAR_INT;
            len = evaluate_operand_val(cursor, &s_l, &t_l, NULL, NULL);
            if (s_l) { untrack_alloc(s_l); free(s_l); }
            if (len < 1) len = 1;
            if (len > 256) len = 256;
        } else {
            freeToken(&peek_a);
        }
        static const char charset[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
        char *str_res = malloc(len + 1);
        if (!str_res) { throw_error("MemoryAllocationError", "Memory allocation failed for alphanumeric string"); }
        track_alloc(str_res);
        for (int k = 0; k < len; k++) {
            str_res[k] = charset[pseudo_rand() % (sizeof(charset) - 1)];
        }
        str_res[len] = '\0';
        *out_type = VAR_STRING;
        if (out_str) *out_str = str_res;
    }
    else {
        throw_error("UndefinedVariableError", "Pseudo has no method '%s' on line %d", method, line_num);
    }

    if (has_parens) {
        Token rp = getNextToken(cursor);
        if (rp.type != TOKEN_RPAREN) { freeToken(&rp); throw_error("SyntaxError", "Expected ')' on line %d", line_num); }
        freeToken(&rp);
    }

    return acc;
}

MetadataStore* get_current_metadata_store(void) {
    if (current_entity) return &current_entity->meta;
    if (current_library) return &current_library->meta;
    return NULL;
}

void add_metadata_var(Variable **vars, int *count, int *capacity, const char *key, VarType type, int ival, const char *sval, Array *arr, Entity *ent) {
    for (int i = 0; i < *count; i++) {
        if (strcmp((*vars)[i].name, key) == 0) {
            (*vars)[i].type = type;
            (*vars)[i].int_val = ival;
            if ((*vars)[i].string_val) {
                untrack_alloc((*vars)[i].string_val);
                free((*vars)[i].string_val);
            }
            (*vars)[i].string_val = sval ? strdup(sval) : NULL;
            if ((*vars)[i].string_val) track_alloc((*vars)[i].string_val);
            (*vars)[i].array_val = arr;
            (*vars)[i].entity_val = ent;
            return;
        }
    }
    if (*count >= *capacity) {
        int new_cap = (*capacity == 0) ? 8 : (*capacity * 2);
        Variable *tmp = realloc(*vars, new_cap * sizeof(Variable));
        if (!tmp) throw_error("MemoryAllocationError", "Memory allocation failed for metadata");
        if (*vars) untrack_alloc((char*)*vars);
        *vars = tmp;
        track_alloc((char*)*vars);
        *capacity = new_cap;
    }
    Variable *nv = &(*vars)[(*count)++];
    memset(nv, 0, sizeof(Variable));
    nv->name = strdup(key);
    track_alloc(nv->name);
    nv->type = type;
    nv->int_val = ival;
    nv->string_val = sval ? strdup(sval) : NULL;
    if (nv->string_val) track_alloc(nv->string_val);
    nv->array_val = arr;
    nv->entity_val = ent;
}

void clone_metadata_store(MetadataStore *src, MetadataStore *dst) {
    memset(dst, 0, sizeof(MetadataStore));
    dst->override_all = src->override_all;
    dst->override_static = src->override_static;
    for (int i = 0; i < src->static_count; i++) {
        add_metadata_var(&dst->static_vars, &dst->static_count, &dst->static_capacity,
                         src->static_vars[i].name, src->static_vars[i].type, src->static_vars[i].int_val,
                         src->static_vars[i].string_val, src->static_vars[i].array_val, src->static_vars[i].entity_val);
    }
    for (int i = 0; i < src->dynamic_count; i++) {
        add_metadata_var(&dst->dynamic_vars, &dst->dynamic_count, &dst->dynamic_capacity,
                         src->dynamic_vars[i].name, src->dynamic_vars[i].type, src->dynamic_vars[i].int_val,
                         src->dynamic_vars[i].string_val, src->dynamic_vars[i].array_val, src->dynamic_vars[i].entity_val);
    }
    for (int i = 0; i < src->thisstatic_count; i++) {
        add_metadata_var(&dst->thisstatic_vars, &dst->thisstatic_count, &dst->thisstatic_capacity,
                         src->thisstatic_vars[i].name, src->thisstatic_vars[i].type, src->thisstatic_vars[i].int_val,
                         src->thisstatic_vars[i].string_val, src->thisstatic_vars[i].array_val, src->thisstatic_vars[i].entity_val);
    }
}

void init_auto_metadata(MetadataStore *ms, const char *name, const char *kind, const char *origin, int symbol_count) {
    if (!ms->override_all) {
        if (!ms->override_static) {
            add_metadata_var(&ms->static_vars, &ms->static_count, &ms->static_capacity, "name", VAR_STRING, 0, name, NULL, NULL);
            add_metadata_var(&ms->static_vars, &ms->static_count, &ms->static_capacity, "kind", VAR_STRING, 0, kind, NULL, NULL);
            add_metadata_var(&ms->static_vars, &ms->static_count, &ms->static_capacity, "version", VAR_STRING, 0, "1.0.0", NULL, NULL);
            add_metadata_var(&ms->static_vars, &ms->static_count, &ms->static_capacity, "origin", VAR_STRING, 0, origin ? origin : "core", NULL, NULL);
            add_metadata_var(&ms->static_vars, &ms->static_count, &ms->static_capacity, "symbols", VAR_INT, symbol_count, NULL, NULL, NULL);
        }
        add_metadata_var(&ms->dynamic_vars, &ms->dynamic_count, &ms->dynamic_capacity, "loadCount", VAR_INT, 1, NULL, NULL, NULL);
        static int run_id_counter = 1000;
        add_metadata_var(&ms->thisstatic_vars, &ms->thisstatic_count, &ms->thisstatic_capacity, "loadedAt", VAR_INT, ++run_id_counter, NULL, NULL, NULL);
    }
}

int is_meta_block_header(const char *text) {
    if (strncmp(text, "meta:", 5) == 0) return 1;
    if (strncmp(text, "meta ", 5) == 0) return 1;
    if (strncmp(text, "meta?", 5) == 0) return 1;
    if (strcmp(text, "meta") == 0) return 1;
    return 0;
}

void parse_meta_block(int start_idx, int end_idx, MetadataStore *ms) {
    int cur_sub = 0; // 1 = static, 2 = dynamic, 3 = thisstatic
    for (int idx = start_idx; idx <= end_idx; idx++) {
        Line *ln = &lines[idx];
        if (ln->text[0] == '\0' || ln->text[0] == '!') continue;
        if (strncmp(ln->text, "static:", 7) == 0 || strncmp(ln->text, "static ", 7) == 0 || strncmp(ln->text, "static?", 7) == 0 || strcmp(ln->text, "static") == 0) {
            cur_sub = 1;
            if (strstr(ln->text, "?override") != NULL) {
                ms->override_static = 1;
            }
        } else if (strncmp(ln->text, "dynamic:", 8) == 0 || strncmp(ln->text, "dynamic ", 8) == 0 || strcmp(ln->text, "dynamic") == 0) {
            cur_sub = 2;
        } else if (strncmp(ln->text, "thisstatic:", 11) == 0 || strncmp(ln->text, "thisstatic ", 11) == 0 || strcmp(ln->text, "thisstatic") == 0) {
            cur_sub = 3;
        } else {
            const char *c = ln->text;
            Token key_tok = getNextToken(&c);
            if (key_tok.type == TOKEN_IDENTIFIER) {
                Token col = getNextToken(&c);
                if (col.type == TOKEN_COLON) {
                    freeToken(&col);
                    char *out_str = NULL;
                    int out_type = VAR_INT;
                    Array *out_arr = NULL;
                    Entity *out_ent = NULL;
                    int val = evaluate_expression_val(&c, &out_str, &out_type, &out_arr, &out_ent);
                    if (cur_sub == 1) {
                        add_metadata_var(&ms->static_vars, &ms->static_count, &ms->static_capacity,
                                         key_tok.value, out_type, val, out_str, out_arr, out_ent);
                    } else if (cur_sub == 2) {
                        add_metadata_var(&ms->dynamic_vars, &ms->dynamic_count, &ms->dynamic_capacity,
                                         key_tok.value, out_type, val, out_str, out_arr, out_ent);
                    } else if (cur_sub == 3) {
                        add_metadata_var(&ms->thisstatic_vars, &ms->thisstatic_count, &ms->thisstatic_capacity,
                                         key_tok.value, out_type, val, out_str, out_arr, out_ent);
                    }
                } else {
                    freeToken(&col);
                }
            }
            freeToken(&key_tok);
        }
    }
}
int evaluate_argument(const char **cursor, char **out_str, int *out_type, int *out_int, Array **out_arr, Entity **out_ent) {
    Token peek = peekToken(cursor);
    if (peek.type == TOKEN_LPAREN) {
        freeToken(&peek);
        Token lp = getNextToken(cursor);
        freeToken(&lp);
        Token inner_peek = peekToken(cursor);
        Routine *r = NULL;
        if (inner_peek.type == TOKEN_IDENTIFIER) {
            r = find_routine(inner_peek.value);
        }
        if (r && r->kind == ROUTINE_FUNC) {
            Token fn_tok = getNextToken(cursor);
            freeToken(&fn_tok);
            call_routine_val(r, cursor, out_str, out_type, out_int, current_executing_line);
            if (out_arr) *out_arr = current_reply.array_val;
            if (out_ent) *out_ent = current_reply.entity_val;
        } else {
            *out_int = evaluate_expression_val(cursor, out_str, out_type, out_arr, out_ent);
        }
        freeToken(&inner_peek);
        Token rp = getNextToken(cursor);
        if (rp.type != TOKEN_RPAREN) {
            freeToken(&rp);
            throw_error("SyntaxError", "Expected ')' after argument expression on line %d", current_executing_line);
        }
        freeToken(&rp);
        return 1;
    }
    freeToken(&peek);
    *out_int = evaluate_operand_val(cursor, out_str, out_type, out_arr, out_ent);
    return 1;
}

void call_routine(Routine *r, const char **cursor, int line_num) {
    if (r->is_private) {
        if (r->bound_entity && current_entity != r->bound_entity) {
            throw_error("VisibilityError", "Cannot call private method '%s' outside entity on line %d", r->name, line_num);
        }
        if (r->bound_library && current_library != r->bound_library) {
            throw_error("VisibilityError", "Cannot call private routine '%s' outside library on line %d", r->name, line_num);
        }
    }
    if (call_stack_ptr >= MAX_CALL_STACK) {
        throw_error("SystemError", "Maximum call stack depth exceeded on line %d", line_num);
    }

    typedef struct {
        int int_val;
        char *str_val;
        int type;
        Array *arr_val;
        Entity *ent_val;
    } EvaluatedArg;

    EvaluatedArg evaluated_args[16];
    for (int p = 0; p < r->param_count; p++) {
        char *arg_str = NULL;
        int arg_type = VAR_INT;
        int arg_int = 0;
        Array *arg_arr = NULL;
        Entity *arg_ent = NULL;
        evaluate_argument(cursor, &arg_str, &arg_type, &arg_int, &arg_arr, &arg_ent);
        evaluated_args[p].int_val = arg_int;
        evaluated_args[p].str_val = arg_str;
        evaluated_args[p].type = arg_type;
        evaluated_args[p].arr_val = arg_arr;
        evaluated_args[p].ent_val = arg_ent;
        if (p < r->param_count - 1) {
            Token sep = peekToken(cursor);
            if (sep.type == TOKEN_COMMA) {
                freeToken(&sep);
                Token cm = getNextToken(cursor);
                freeToken(&cm);
            } else {
                freeToken(&sep);
            }
        }
    }

    int prev_scope = current_scope_depth;
    current_scope_depth++;

    CallFrame *frame = &call_stack[call_stack_ptr++];
    frame->routine = r;
    frame->caller_scope_level = prev_scope;

    for (int p = 0; p < r->param_count; p++) {
        if (r->param_types[p][0] != '\0') {
            validate_value_type(r->param_types[p], evaluated_args[p].int_val, evaluated_args[p].str_val, evaluated_args[p].type, evaluated_args[p].arr_val, evaluated_args[p].ent_val, line_num);
        }
        if (var_count >= var_capacity) {
            int new_capacity = (var_capacity == 0) ? 100 : var_capacity * 2;
            Variable *tmp = realloc(symtable, new_capacity * sizeof(Variable));
            if (!tmp) {
                throw_error("MemoryAllocationError", "Memory allocation failed");
            }
            symtable = tmp;
            var_capacity = new_capacity;
        }
        Variable *param_var = &symtable[var_count++];
        memset(param_var, 0, sizeof(Variable));
        param_var->name = strdup(r->params[p]);
        param_var->scope_level = current_scope_depth;
        param_var->type = evaluated_args[p].type;
        param_var->int_val = evaluated_args[p].int_val;
        param_var->array_val = evaluated_args[p].arr_val;
        param_var->entity_val = evaluated_args[p].ent_val;
        if (r->param_types[p][0] != '\0') {
            strncpy(param_var->declared_type, r->param_types[p], 63);
            param_var->declared_type[63] = '\0';
        }
        if (evaluated_args[p].str_val) {
            untrack_alloc(evaluated_args[p].str_val);
            param_var->string_val = evaluated_args[p].str_val;
        } else {
            param_var->string_val = NULL;
        }
    }

    ReplyResult prev_reply = current_reply;
    current_reply.has_replied = 0;
    current_reply.int_val = 0;
    current_reply.string_val = NULL;
    current_reply.type = VAR_INT;

    Entity *saved_ent = current_entity;
    LibraryDef *saved_lib = current_library;
    if (r->bound_entity) current_entity = r->bound_entity;
    if (r->bound_library) current_library = r->bound_library;

    if (r->body_start_line <= r->body_end_line) {
        execute_block(r->body_start_line, r->body_end_line);
    }

    current_entity = saved_ent;
    current_library = saved_lib;

    ReplyResult routine_reply = current_reply;

    pop_scope(prev_scope);
    current_scope_depth = prev_scope;
    call_stack_ptr--;

    if (routine_reply.string_val) {
        free(routine_reply.string_val);
    }
    current_reply = prev_reply;
}

void call_routine_val(Routine *r, const char **cursor, char **out_str, int *out_type, int *out_int, int line_num) {
    if (r->is_private) {
        if (r->bound_entity && current_entity != r->bound_entity) {
            throw_error("VisibilityError", "Cannot call private method '%s' outside entity on line %d", r->name, line_num);
        }
        if (r->bound_library && current_library != r->bound_library) {
            throw_error("VisibilityError", "Cannot call private routine '%s' outside library on line %d", r->name, line_num);
        }
    }
    if (r->kind == ROUTINE_METHOD) {
        throw_error("SyntaxError", "Cannot use method '%s' in an expression on line %d", r->name, line_num);
    }
    if (call_stack_ptr >= MAX_CALL_STACK) {
        throw_error("SystemError", "Maximum call stack depth exceeded on line %d", line_num);
    }

    typedef struct {
        int int_val;
        char *str_val;
        int type;
        Array *arr_val;
        Entity *ent_val;
    } EvaluatedArg;

    EvaluatedArg evaluated_args[16];
    for (int p = 0; p < r->param_count; p++) {
        char *arg_str = NULL;
        int arg_type = VAR_INT;
        int arg_int = 0;
        Array *arg_arr = NULL;
        Entity *arg_ent = NULL;
        evaluate_argument(cursor, &arg_str, &arg_type, &arg_int, &arg_arr, &arg_ent);
        evaluated_args[p].int_val = arg_int;
        evaluated_args[p].str_val = arg_str;
        evaluated_args[p].type = arg_type;
        evaluated_args[p].arr_val = arg_arr;
        evaluated_args[p].ent_val = arg_ent;
        if (p < r->param_count - 1) {
            Token sep = peekToken(cursor);
            if (sep.type == TOKEN_COMMA) {
                freeToken(&sep);
                Token cm = getNextToken(cursor);
                freeToken(&cm);
            } else {
                freeToken(&sep);
            }
        }
    }

    int prev_scope = current_scope_depth;
    current_scope_depth++;

    CallFrame *frame = &call_stack[call_stack_ptr++];
    frame->routine = r;
    frame->caller_scope_level = prev_scope;

    for (int p = 0; p < r->param_count; p++) {
        if (r->param_types[p][0] != '\0') {
            validate_value_type(r->param_types[p], evaluated_args[p].int_val, evaluated_args[p].str_val, evaluated_args[p].type, evaluated_args[p].arr_val, evaluated_args[p].ent_val, line_num);
        }
        if (var_count >= var_capacity) {
            int new_capacity = (var_capacity == 0) ? 100 : var_capacity * 2;
            Variable *tmp = realloc(symtable, new_capacity * sizeof(Variable));
            if (!tmp) {
                throw_error("MemoryAllocationError", "Memory allocation failed");
            }
            symtable = tmp;
            var_capacity = new_capacity;
        }
        Variable *param_var = &symtable[var_count++];
        memset(param_var, 0, sizeof(Variable));
        param_var->name = strdup(r->params[p]);
        param_var->scope_level = current_scope_depth;
        param_var->type = evaluated_args[p].type;
        param_var->int_val = evaluated_args[p].int_val;
        param_var->array_val = evaluated_args[p].arr_val;
        param_var->entity_val = evaluated_args[p].ent_val;
        if (r->param_types[p][0] != '\0') {
            strncpy(param_var->declared_type, r->param_types[p], 63);
            param_var->declared_type[63] = '\0';
        }
        if (evaluated_args[p].str_val) {
            untrack_alloc(evaluated_args[p].str_val);
            param_var->string_val = evaluated_args[p].str_val;
        } else {
            param_var->string_val = NULL;
        }
    }

    ReplyResult prev_reply = current_reply;
    current_reply.has_replied = 0;
    current_reply.int_val = 0;
    current_reply.string_val = NULL;
    current_reply.type = VAR_INT;

    Entity *saved_ent = current_entity;
    LibraryDef *saved_lib = current_library;
    if (r->bound_entity) current_entity = r->bound_entity;
    if (r->bound_library) current_library = r->bound_library;

    if (r->body_start_line <= r->body_end_line) {
        execute_block(r->body_start_line, r->body_end_line);
    }

    current_entity = saved_ent;
    current_library = saved_lib;

    ReplyResult routine_reply = current_reply;

    pop_scope(prev_scope);
    current_scope_depth = prev_scope;
    call_stack_ptr--;

    *out_int = routine_reply.int_val;
    *out_type = routine_reply.type;
    current_reply = prev_reply;
    if (routine_reply.type == VAR_ARRAY) {
        current_reply.array_val = routine_reply.array_val;
        if (!routine_reply.string_val) {
            *out_str = array_to_string(routine_reply.array_val);
        }
    }
    if (routine_reply.type == VAR_ENTITY) {
        current_reply.entity_val = routine_reply.entity_val;
        if (!routine_reply.string_val) {
            *out_str = entity_to_string(routine_reply.entity_val);
        }
    }
    if (routine_reply.string_val) {
        *out_str = routine_reply.string_val;
        track_alloc(*out_str);
    } else if (routine_reply.type != VAR_ARRAY && routine_reply.type != VAR_ENTITY) {
        *out_str = NULL;
    }
}

Entity* instantiate_entity(ClassDef *cd, const char **cursor, int line_num) {
    (void)line_num;
    Entity *ent = calloc(1, sizeof(Entity));
    if (!ent) throw_error("MemoryAllocationError", "Memory allocation failed for entity");
    track_alloc((char*)ent);
    strncpy(ent->class_name, cd->name, sizeof(ent->class_name) - 1);

    typedef struct {
        int int_val;
        char *str_val;
        int type;
        Array *arr_val;
        Entity *ent_val;
    } EvalParam;

    EvalParam evaluated_args[16];
    for (int p = 0; p < cd->param_count; p++) {
        char *arg_str = NULL;
        int arg_type = VAR_INT;
        int arg_int = 0;
        Array *arg_arr = NULL;
        Entity *arg_ent = NULL;
        evaluate_argument(cursor, &arg_str, &arg_type, &arg_int, &arg_arr, &arg_ent);
        evaluated_args[p].int_val = arg_int;
        evaluated_args[p].str_val = arg_str;
        evaluated_args[p].type = arg_type;
        evaluated_args[p].arr_val = arg_arr;
        evaluated_args[p].ent_val = arg_ent;
        if (p < cd->param_count - 1) {
            Token comma = peekToken(cursor);
            if (comma.type == TOKEN_COMMA) {
                freeToken(&comma);
                Token c = getNextToken(cursor); freeToken(&c);
            } else {
                freeToken(&comma);
            }
        }
    }

    int prev_scope = current_scope_depth;
    current_scope_depth++;

    for (int p = 0; p < cd->param_count; p++) {
        if (cd->param_types[p][0] != '\0') {
            validate_value_type(cd->param_types[p], evaluated_args[p].int_val, evaluated_args[p].str_val, evaluated_args[p].type, evaluated_args[p].arr_val, evaluated_args[p].ent_val, line_num);
        }
        if (var_count >= var_capacity) {
            int new_capacity = (var_capacity == 0) ? 100 : var_capacity * 2;
            Variable *tmp = realloc(symtable, new_capacity * sizeof(Variable));
            if (!tmp) throw_error("MemoryAllocationError", "Memory allocation failed");
            symtable = tmp;
            var_capacity = new_capacity;
        }
        Variable *param_var = &symtable[var_count++];
        memset(param_var, 0, sizeof(Variable));
        param_var->name = strdup(cd->params[p]);
        param_var->scope_level = current_scope_depth;
        param_var->type = evaluated_args[p].type;
        param_var->int_val = evaluated_args[p].int_val;
        if (cd->param_types[p][0] != '\0') {
            strncpy(param_var->declared_type, cd->param_types[p], 63);
            param_var->declared_type[63] = '\0';
        }
        if (evaluated_args[p].str_val) {
            untrack_alloc(evaluated_args[p].str_val);
            param_var->string_val = evaluated_args[p].str_val;
        } else {
            param_var->string_val = NULL;
        }
        param_var->array_val = evaluated_args[p].arr_val;
        param_var->entity_val = evaluated_args[p].ent_val;
    }

    clone_metadata_store(&cd->meta, &ent->meta);

    Entity *saved_ent = current_entity;
    current_entity = ent;

    // Execute static block if present
    if (cd->static_start_line <= cd->static_end_line) {
        for (int line_idx = cd->static_start_line; line_idx <= cd->static_end_line; line_idx++) {
            Line *ln = &lines[line_idx];
            if (ln->text[0] == '\0' || ln->text[0] == '!') continue;
            const char *c = ln->text;
            int is_pub = 0;
            Token t = peekToken(&c);
            if (t.type == TOKEN_PUBLIC) {
                freeToken(&t);
                Token pub = getNextToken(&c); freeToken(&pub);
                is_pub = 1;
            } else {
                freeToken(&t);
            }
            Token name_tok = getNextToken(&c);
            if (name_tok.type == TOKEN_ARR) {
                freeToken(&name_tok);
                name_tok = getNextToken(&c);
            }
            if (name_tok.type == TOKEN_IDENTIFIER) {
                Token col = getNextToken(&c);
                if (col.type == TOKEN_COLON) {
                    freeToken(&col);
                    char *out_str = NULL;
                    int out_type = VAR_INT;
                    Array *out_arr = NULL;
                    Entity *out_sub_ent = NULL;
                    int val = evaluate_expression_val(&c, &out_str, &out_type, &out_arr, &out_sub_ent);

                    if (ent->static_count >= ent->static_capacity) {
                        int new_cap = (ent->static_capacity == 0) ? 8 : ent->static_capacity * 2;
                        if (ent->static_vars) untrack_alloc((char*)ent->static_vars);
                        if (ent->static_is_public) untrack_alloc((char*)ent->static_is_public);
                        Variable *tmp = realloc(ent->static_vars, new_cap * sizeof(Variable));
                        int *ptmp = realloc(ent->static_is_public, new_cap * sizeof(int));
                        if (!tmp || !ptmp) throw_error("MemoryAllocationError", "Memory allocation failed");
                        ent->static_vars = tmp;
                        ent->static_is_public = ptmp;
                        track_alloc((char*)ent->static_vars);
                        track_alloc((char*)ent->static_is_public);
                        ent->static_capacity = new_cap;
                    }
                    Variable *sv = &ent->static_vars[ent->static_count];
                    memset(sv, 0, sizeof(Variable));
                    sv->name = strdup(name_tok.value);
                    track_alloc(sv->name);
                    sv->type = out_type;
                    sv->int_val = val;
                    sv->string_val = out_str;
                    sv->array_val = out_arr ? out_arr : current_reply.array_val;
                    sv->entity_val = out_sub_ent ? out_sub_ent : current_reply.entity_val;
                    ent->static_is_public[ent->static_count] = is_pub;
                    ent->static_count++;
                } else {
                    freeToken(&col);
                }
            }
            freeToken(&name_tok);
        }
    }

    // Execute dynamic block if present
    if (cd->dynamic_start_line <= cd->dynamic_end_line) {
        // Pass 1: register all def routines in entity methods (non-procedural!)
        for (int line_idx = cd->dynamic_start_line; line_idx <= cd->dynamic_end_line; line_idx++) {
            Line *ln = &lines[line_idx];
            if (ln->text[0] == '\0' || ln->text[0] == '!') continue;
            const char *c = ln->text;
            int is_priv = 0;
            Token t = peekToken(&c);
            if (t.type == TOKEN_PRIVATE) {
                freeToken(&t);
                Token priv = getNextToken(&c); freeToken(&priv);
                is_priv = 1;
            } else {
                freeToken(&t);
            }
            Token def_tok = peekToken(&c);
            if (def_tok.type == TOKEN_DEF) {
                freeToken(&def_tok);
                int meth_start = line_idx + 1;
                int meth_end = line_idx;
                while (meth_end + 1 <= cd->dynamic_end_line) {
                    Line *next = &lines[meth_end + 1];
                    if (next->text[0] == '\0' || next->text[0] == '!') { meth_end++; continue; }
                    if (next->indent > ln->indent) meth_end++;
                    else break;
                }
                // Parse method into ent->methods
                if (ent->method_count >= ent->method_capacity) {
                    int new_cap = (ent->method_capacity == 0) ? 8 : ent->method_capacity * 2;
                    if (ent->methods) untrack_alloc((char*)ent->methods);
                    Routine *tmp = realloc(ent->methods, new_cap * sizeof(Routine));
                    if (!tmp) throw_error("MemoryAllocationError", "Memory allocation failed");
                    ent->methods = tmp;
                    track_alloc((char*)ent->methods);
                    ent->method_capacity = new_cap;
                }
                Routine *mr = &ent->methods[ent->method_count++];
                memset(mr, 0, sizeof(Routine));
                mr->is_private = is_priv;
                mr->bound_entity = ent;
                mr->body_start_line = meth_start;
                mr->body_end_line = meth_end;

                Token d = getNextToken(&c); freeToken(&d); // def
                Token kind_tok = getNextToken(&c);
                if (kind_tok.type == TOKEN_INBOUND || kind_tok.type == TOKEN_OUTBOUND) {
                    mr->purity = (kind_tok.type == TOKEN_INBOUND) ? PURITY_INBOUND : PURITY_OUTBOUND;
                    freeToken(&kind_tok);
                    kind_tok = getNextToken(&c);
                } else {
                    mr->purity = PURITY_OUTBOUND;
                }
                mr->kind = (kind_tok.type == TOKEN_FUNC) ? ROUTINE_FUNC : ROUTINE_METHOD;
                freeToken(&kind_tok);

                Token mname = getNextToken(&c);
                if (mname.type == TOKEN_IDENTIFIER) {
                    strncpy(mr->name, mname.value, sizeof(mr->name) - 1);
                }
                freeToken(&mname);

                parse_parameter_list(&c, mr->params, mr->param_types, &mr->param_count);
                line_idx = meth_end;
            } else {
                freeToken(&def_tok);
            }
        }

        // Pass 2: evaluate dynamic properties
        in_entity_instantiation = 1;
        for (int line_idx = cd->dynamic_start_line; line_idx <= cd->dynamic_end_line; line_idx++) {
            Line *ln = &lines[line_idx];
            if (ln->text[0] == '\0' || ln->text[0] == '!') continue;
            if (strstr(ln->text, "def ") != NULL) {
                // Skip routine definitions
                int meth_end = line_idx;
                while (meth_end + 1 <= cd->dynamic_end_line) {
                    Line *next = &lines[meth_end + 1];
                    if (next->text[0] == '\0' || next->text[0] == '!') { meth_end++; continue; }
                    if (next->indent > ln->indent) meth_end++;
                    else break;
                }
                line_idx = meth_end;
                continue;
            }
            execute_line(ln->text, ln->line_num);
        }
        in_entity_instantiation = 0;
    }

    current_entity = saved_ent;
    pop_scope(prev_scope);
    current_scope_depth = prev_scope;

    return ent;
}

int is_native_func_name(const char *name) {
    if (!name) return 0;
    if (strcmp(name, "len") == 0 || strcmp(name, "length") == 0) return 1;
    if (strcmp(name, "isEmpty") == 0) return 1;
    if (strcmp(name, "indexOf") == 0) return 1;
    if (strcmp(name, "contains") == 0) return 1;
    if (strcmp(name, "slice") == 0) return 1;
    if (strcmp(name, "reverse") == 0) return 1;
    if (strcmp(name, "join") == 0) return 1;
    if (strcmp(name, "trim") == 0) return 1;
    if (strcmp(name, "toUpper") == 0) return 1;
    if (strcmp(name, "toLower") == 0) return 1;
    if (strcmp(name, "split") == 0) return 1;
    if (strcmp(name, "repeat") == 0) return 1;
    if (strcmp(name, "padLeft") == 0) return 1;
    if (strcmp(name, "padRight") == 0) return 1;
    return 0;
}

int dispatch_array_method(Array *arr, const char *method, const char **cursor, char **out_str, int *out_type, Array **out_arr, Entity **out_ent, int line_num) {
    (void)out_ent;
    *out_type = VAR_INT;
    if (out_str) *out_str = NULL;
    if (out_arr) *out_arr = NULL;

    Token lp = peekToken(cursor);
    int has_parens = 0;
    if (lp.type == TOKEN_LPAREN) {
        has_parens = 1;
        freeToken(&lp);
        Token t_lp = getNextToken(cursor);
        freeToken(&t_lp);
    } else {
        freeToken(&lp);
    }

    int arg_count = 0;
    Variable args[8];
    memset(args, 0, sizeof(args));

    if (has_parens) {
        Token p = peekToken(cursor);
        if (p.type != TOKEN_RPAREN && p.type != TOKEN_EOF) {
            freeToken(&p);
            while (arg_count < 8) {
                char *a_str = NULL;
                int a_type = VAR_INT;
                Array *a_arr = NULL;
                Entity *a_ent = NULL;
                int a_int = evaluate_expression_val(cursor, &a_str, &a_type, &a_arr, &a_ent);
                args[arg_count].type = a_type;
                args[arg_count].int_val = a_int;
                args[arg_count].string_val = a_str;
                args[arg_count].array_val = a_arr ? a_arr : current_reply.array_val;
                args[arg_count].entity_val = a_ent ? a_ent : current_reply.entity_val;
                arg_count++;

                Token sep = peekToken(cursor);
                if (sep.type == TOKEN_COMMA) {
                    freeToken(&sep);
                    Token cm = getNextToken(cursor); freeToken(&cm);
                } else {
                    freeToken(&sep);
                    break;
                }
            }
        } else {
            freeToken(&p);
        }
        Token rp = getNextToken(cursor);
        if (rp.type != TOKEN_RPAREN) {
            freeToken(&rp);
            throw_error("SyntaxError", "Expected ')' after array method call on line %d", line_num);
        }
        freeToken(&rp);
    }

    if (strcmp(method, "len") == 0 || strcmp(method, "length") == 0) {
        *out_type = VAR_INT;
        return array_len(arr);
    }
    if (strcmp(method, "isEmpty") == 0) {
        *out_type = VAR_BOOL;
        return array_is_empty(arr);
    }
    if (strcmp(method, "push") == 0) {
        if (arg_count > 0) array_push(arr, &args[0]);
        *out_type = VAR_INT;
        return arr ? arr->count : 0;
    }
    if (strcmp(method, "pop") == 0) {
        Variable popped;
        memset(&popped, 0, sizeof(popped));
        array_pop(arr, &popped, line_num);
        *out_type = popped.type;
        if (popped.type == VAR_STRING) {
            *out_str = popped.string_val ? strdup(popped.string_val) : strdup("");
            track_alloc(*out_str);
            return 0;
        }
        if (popped.type == VAR_ARRAY) {
            if (out_arr) *out_arr = popped.array_val;
            current_reply.array_val = popped.array_val;
            *out_str = array_to_string(popped.array_val);
            return 0;
        }
        if (popped.type == VAR_ENTITY) {
            if (out_ent) *out_ent = popped.entity_val;
            current_reply.entity_val = popped.entity_val;
            *out_str = entity_to_string(popped.entity_val);
            return 0;
        }
        return popped.int_val;
    }
    if (strcmp(method, "indexOf") == 0) {
        *out_type = VAR_INT;
        if (arg_count == 0) return -1;
        return array_index_of(arr, &args[0]);
    }
    if (strcmp(method, "contains") == 0) {
        *out_type = VAR_BOOL;
        if (arg_count == 0) return 0;
        return array_contains(arr, &args[0]);
    }
    if (strcmp(method, "slice") == 0) {
        int start = (arg_count > 0) ? args[0].int_val : 0;
        int end = (arg_count > 1) ? args[1].int_val : (arr ? arr->count : 0);
        Array *res = array_slice(arr, start, end);
        *out_type = VAR_ARRAY;
        if (out_arr) *out_arr = res;
        current_reply.array_val = res;
        *out_str = array_to_string(res);
        return 0;
    }
    if (strcmp(method, "reverse") == 0) {
        array_reverse(arr);
        *out_type = VAR_ARRAY;
        if (out_arr) *out_arr = arr;
        current_reply.array_val = arr;
        *out_str = array_to_string(arr);
        return 0;
    }
    if (strcmp(method, "join") == 0) {
        const char *sep = (arg_count > 0 && args[0].string_val) ? args[0].string_val : "";
        char *j = array_join(arr, sep);
        *out_type = VAR_STRING;
        *out_str = j;
        return 0;
    }
    if (strcmp(method, "swap") == 0) {
        int i1 = (arg_count > 0) ? args[0].int_val : 0;
        int i2 = (arg_count > 1) ? args[1].int_val : 0;
        array_swap(arr, i1, i2, line_num);
        *out_type = VAR_INT;
        return 1;
    }
    if (strcmp(method, "fill") == 0) {
        Variable *v = (arg_count > 0) ? &args[0] : NULL;
        int count = (arg_count > 1) ? args[1].int_val : 0;
        array_fill(arr, v, count);
        *out_type = VAR_ARRAY;
        if (out_arr) *out_arr = arr;
        current_reply.array_val = arr;
        *out_str = array_to_string(arr);
        return 0;
    }
    if (strcmp(method, "clear") == 0) {
        array_clear(arr);
        *out_type = VAR_INT;
        return 1;
    }

    throw_error("SyntaxError", "Unknown Array method '%s' on line %d", method, line_num);
    return 0;
}

int dispatch_string_method(const char *str, const char *method, const char **cursor, char **out_str, int *out_type, Array **out_arr, Entity **out_ent, int line_num) {
    (void)out_ent;
    *out_type = VAR_INT;
    if (out_str) *out_str = NULL;
    if (out_arr) *out_arr = NULL;

    Token lp = peekToken(cursor);
    int has_parens = 0;
    if (lp.type == TOKEN_LPAREN) {
        has_parens = 1;
        freeToken(&lp);
        Token t_lp = getNextToken(cursor);
        freeToken(&t_lp);
    } else {
        freeToken(&lp);
    }

    int arg_count = 0;
    Variable args[8];
    memset(args, 0, sizeof(args));

    if (has_parens) {
        Token p = peekToken(cursor);
        if (p.type != TOKEN_RPAREN && p.type != TOKEN_EOF) {
            freeToken(&p);
            while (arg_count < 8) {
                char *a_str = NULL;
                int a_type = VAR_INT;
                Array *a_arr = NULL;
                Entity *a_ent = NULL;
                int a_int = evaluate_expression_val(cursor, &a_str, &a_type, &a_arr, &a_ent);
                args[arg_count].type = a_type;
                args[arg_count].int_val = a_int;
                args[arg_count].string_val = a_str;
                args[arg_count].array_val = a_arr ? a_arr : current_reply.array_val;
                args[arg_count].entity_val = a_ent ? a_ent : current_reply.entity_val;
                arg_count++;

                Token sep = peekToken(cursor);
                if (sep.type == TOKEN_COMMA) {
                    freeToken(&sep);
                    Token cm = getNextToken(cursor); freeToken(&cm);
                } else {
                    freeToken(&sep);
                    break;
                }
            }
        } else {
            freeToken(&p);
        }
        Token rp = getNextToken(cursor);
        if (rp.type != TOKEN_RPAREN) {
            freeToken(&rp);
            throw_error("SyntaxError", "Expected ')' after string method call on line %d", line_num);
        }
        freeToken(&rp);
    }

    if (strcmp(method, "len") == 0 || strcmp(method, "length") == 0) {
        *out_type = VAR_INT;
        return string_len(str);
    }
    if (strcmp(method, "isEmpty") == 0) {
        *out_type = VAR_BOOL;
        return string_is_empty(str);
    }
    if (strcmp(method, "indexOf") == 0) {
        *out_type = VAR_INT;
        const char *sub = (arg_count > 0 && args[0].string_val) ? args[0].string_val : "";
        return string_index_of(str, sub);
    }
    if (strcmp(method, "contains") == 0) {
        *out_type = VAR_BOOL;
        const char *sub = (arg_count > 0 && args[0].string_val) ? args[0].string_val : "";
        return string_contains(str, sub);
    }
    if (strcmp(method, "slice") == 0) {
        int start = (arg_count > 0) ? args[0].int_val : 0;
        int end = (arg_count > 1) ? args[1].int_val : (str ? (int)strlen(str) : 0);
        char *sl = string_slice(str, start, end);
        *out_type = VAR_STRING;
        *out_str = sl;
        return 0;
    }
    if (strcmp(method, "repeat") == 0) {
        int count = (arg_count > 0) ? args[0].int_val : 1;
        char *rep = string_repeat(str, count);
        *out_type = VAR_STRING;
        *out_str = rep;
        return 0;
    }
    if (strcmp(method, "padLeft") == 0) {
        int target_len = (arg_count > 0) ? args[0].int_val : 0;
        const char *pad = (arg_count > 1 && args[1].string_val) ? args[1].string_val : " ";
        char *res = string_pad_left(str, target_len, pad);
        *out_type = VAR_STRING;
        *out_str = res;
        return 0;
    }
    if (strcmp(method, "padRight") == 0) {
        int target_len = (arg_count > 0) ? args[0].int_val : 0;
        const char *pad = (arg_count > 1 && args[1].string_val) ? args[1].string_val : " ";
        char *res = string_pad_right(str, target_len, pad);
        *out_type = VAR_STRING;
        *out_str = res;
        return 0;
    }
    if (strcmp(method, "trim") == 0) {
        char *res = string_trim(str);
        *out_type = VAR_STRING;
        *out_str = res;
        return 0;
    }
    if (strcmp(method, "toUpper") == 0) {
        char *res = string_to_upper(str);
        *out_type = VAR_STRING;
        *out_str = res;
        return 0;
    }
    if (strcmp(method, "toLower") == 0) {
        char *res = string_to_lower(str);
        *out_type = VAR_STRING;
        *out_str = res;
        return 0;
    }
    if (strcmp(method, "split") == 0) {
        const char *sep = (arg_count > 0 && args[0].string_val) ? args[0].string_val : " ";
        Array *res = string_split(str, sep);
        *out_type = VAR_ARRAY;
        if (out_arr) *out_arr = res;
        current_reply.array_val = res;
        *out_str = array_to_string(res);
        return 0;
    }

    throw_error("SyntaxError", "Unknown String method '%s' on line %d", method, line_num);
    return 0;
}

int evaluate_operand_val(const char **cursor, char **out_str, int *out_type, Array **out_arr, Entity **out_ent) {
    *out_str = NULL;
    *out_type = VAR_INT;
    if (out_arr) *out_arr = NULL;
    if (out_ent) *out_ent = NULL;

    Token t = getNextToken(cursor);
    int acc = 0;
    int sign = 1;

    if (t.type == TOKEN_MINUS) {
        sign = -1;
        freeToken(&t);
        t = getNextToken(cursor);
    }

    if (t.type == TOKEN_NOT) {
        freeToken(&t);
        char *sub_str = NULL;
        int sub_type = VAR_INT;
        Array *sub_arr = NULL;
        Entity *sub_ent = NULL;
        int sub_val = evaluate_operand_val(cursor, &sub_str, &sub_type, &sub_arr, &sub_ent);
        int truth = is_truthy(sub_val, sub_str, sub_type);
        if (sub_str) { untrack_alloc(sub_str); free(sub_str); }
        *out_str = NULL;
        *out_type = VAR_BOOL;
        return !truth;
    }

    if (t.type == TOKEN_NUMBER) {
        acc = atoi(t.value) * sign;
    } else if (t.type == TOKEN_TRUE) {
        if (sign == -1) {
            throw_error("InvalidOperandError", "Invalid operand for unary '-' on line %d", current_executing_line);
        }
        acc = 1;
        *out_type = VAR_BOOL;
    } else if (t.type == TOKEN_FALSE) {
        if (sign == -1) {
            throw_error("InvalidOperandError", "Invalid operand for unary '-' on line %d", current_executing_line);
        }
        acc = 0;
        *out_type = VAR_BOOL;
    } else if (t.type == TOKEN_STRING) {
        if (sign == -1) {
            throw_error("InvalidOperandError", "Invalid operand for unary '-' on line %d", current_executing_line);
        }
        *out_str = strdup(t.value);
        track_alloc(*out_str);
        *out_type = VAR_STRING;
        Token peek = peekToken(cursor);
        if (peek.type == TOKEN_DOT) {
            freeToken(&peek);
            Token dot = getNextToken(cursor); freeToken(&dot);
            Token mem = getNextToken(cursor);
            if (mem.type != TOKEN_IDENTIFIER) { freeToken(&mem); throw_error("SyntaxError", "Expected member name after '.' on line %d", current_executing_line); }
            acc = dispatch_string_method(*out_str, mem.value, cursor, out_str, out_type, out_arr, out_ent, current_executing_line);
            freeToken(&mem);
        } else {
            freeToken(&peek);
        }
    } else if (t.type == TOKEN_IT) {
        Variable *it_v = get_var("it");
        if (!it_v) {
            throw_error("UndefinedVariableError", "'it' is only accessible inside DataType constraint blocks on line %d", current_executing_line);
        }
        *out_type = it_v->type;
        if (it_v->type == VAR_INT) acc = it_v->int_val * sign;
        else if (it_v->type == VAR_BOOL) { acc = it_v->int_val; *out_type = VAR_BOOL; }
        else if (it_v->type == VAR_STRING) {
            *out_str = strdup(it_v->string_val ? it_v->string_val : "");
            track_alloc(*out_str);
        } else if (it_v->type == VAR_ARRAY) {
            if (out_arr) *out_arr = it_v->array_val;
            *out_str = array_to_string(it_v->array_val);
        } else if (it_v->type == VAR_ENTITY) {
            if (out_ent) *out_ent = it_v->entity_val;
            *out_str = entity_to_string(it_v->entity_val);
        }
    } else if (t.type == TOKEN_DATATYPE) {
        *out_type = VAR_STRING;
        *out_str = strdup("DataType");
        track_alloc(*out_str);
    } else if (t.type == TOKEN_TYPE_NUM || t.type == TOKEN_TYPE_BOOL || t.type == TOKEN_TYPE_STR ||
               t.type == TOKEN_TYPE_ARR || t.type == TOKEN_TYPE_ENTITY) {
        *out_type = VAR_STRING;
        *out_str = strdup(get_type_name_from_token(&t));
        track_alloc(*out_str);
    } else if (t.type == TOKEN_PIPE) {
        char *sub_str = NULL;
        int sub_type = VAR_INT;
        Array *sub_arr = NULL;
        Entity *sub_ent = NULL;
        int sub_val = evaluate_expression_val(cursor, &sub_str, &sub_type, &sub_arr, &sub_ent);
        Token close_pipe = getNextToken(cursor);
        if (close_pipe.type != TOKEN_PIPE) {
            freeToken(&close_pipe);
            throw_error("SyntaxError", "Expected closing '|' on line %d", current_executing_line);
        }
        freeToken(&close_pipe);
        if (sub_str) { untrack_alloc(sub_str); free(sub_str); }
        acc = abs(sub_val) * sign;
        *out_type = VAR_INT;
    } else if (t.type == TOKEN_SQRT) {
        char *arg_str = NULL;
        int arg_type = VAR_INT;
        int val = evaluate_operand_val(cursor, &arg_str, &arg_type, NULL, NULL);
        if (arg_str) { untrack_alloc(arg_str); free(arg_str); }
        if (val < 0) throw_error("DomainError", "Square root of negative number on line %d", current_executing_line);
        acc = (int)sqrt((double)val) * sign;
        *out_type = VAR_INT;
    } else if (t.type == TOKEN_RT) {
        char *s1 = NULL, *s2 = NULL;
        int t1 = VAR_INT, t2 = VAR_INT;
        int r = evaluate_operand_val(cursor, &s1, &t1, NULL, NULL);
        int v = evaluate_operand_val(cursor, &s2, &t2, NULL, NULL);
        if (s1) { untrack_alloc(s1); free(s1); }
        if (s2) { untrack_alloc(s2); free(s2); }
        if (r == 0) throw_error("DivisionByZeroError", "Zero root on line %d", current_executing_line);
        acc = ((int)round(pow((double)v, 1.0 / (double)r))) * sign;
        *out_type = VAR_INT;
    } else if (t.type == TOKEN_MIN) {
        Token p = peekToken(cursor);
        if (p.type == TOKEN_LBRACKET) {
            freeToken(&p);
            char *s = NULL; int ty = VAR_ARRAY; Array *arr = NULL;
            evaluate_operand_val(cursor, &s, &ty, &arr, NULL);
            if (s) { untrack_alloc(s); free(s); }
            if (arr && arr->count > 0) {
                int m = arr->items[0].int_val;
                for (int i = 1; i < arr->count; i++) {
                    if (arr->items[i].int_val < m) m = arr->items[i].int_val;
                }
                acc = m * sign;
            } else {
                acc = 0;
            }
        } else {
            freeToken(&p);
            char *s1 = NULL, *s2 = NULL; int t1 = VAR_INT, t2 = VAR_INT;
            int a = evaluate_operand_val(cursor, &s1, &t1, NULL, NULL);
            int b = evaluate_operand_val(cursor, &s2, &t2, NULL, NULL);
            if (s1) { untrack_alloc(s1); free(s1); }
            if (s2) { untrack_alloc(s2); free(s2); }
            acc = ((a < b) ? a : b) * sign;
        }
        *out_type = VAR_INT;
    } else if (t.type == TOKEN_MAX) {
        Token p = peekToken(cursor);
        if (p.type == TOKEN_LBRACKET) {
            freeToken(&p);
            char *s = NULL; int ty = VAR_ARRAY; Array *arr = NULL;
            evaluate_operand_val(cursor, &s, &ty, &arr, NULL);
            if (s) { untrack_alloc(s); free(s); }
            if (arr && arr->count > 0) {
                int m = arr->items[0].int_val;
                for (int i = 1; i < arr->count; i++) {
                    if (arr->items[i].int_val > m) m = arr->items[i].int_val;
                }
                acc = m * sign;
            } else {
                acc = 0;
            }
        } else {
            freeToken(&p);
            char *s1 = NULL, *s2 = NULL; int t1 = VAR_INT, t2 = VAR_INT;
            int a = evaluate_operand_val(cursor, &s1, &t1, NULL, NULL);
            int b = evaluate_operand_val(cursor, &s2, &t2, NULL, NULL);
            if (s1) { untrack_alloc(s1); free(s1); }
            if (s2) { untrack_alloc(s2); free(s2); }
            acc = ((a > b) ? a : b) * sign;
        }
        *out_type = VAR_INT;
    } else if (t.type == TOKEN_CLAMP) {
        char *s0 = NULL; int t0 = VAR_INT;
        int val = evaluate_operand_val(cursor, &s0, &t0, NULL, NULL);
        if (s0) { untrack_alloc(s0); free(s0); }
        Token p = peekToken(cursor);
        int low = 0, high = 0;
        if (p.type == TOKEN_FROM) {
            freeToken(&p);
            Token from_tok = getNextToken(cursor); freeToken(&from_tok);
            char *s1 = NULL; int t1 = VAR_INT;
            low = evaluate_operand_val(cursor, &s1, &t1, NULL, NULL);
            if (s1) { untrack_alloc(s1); free(s1); }
            Token to_tok = getNextToken(cursor);
            if (to_tok.type != TOKEN_TO) {
                freeToken(&to_tok);
                throw_error("SyntaxError", "Expected 'to' in clamp on line %d", current_executing_line);
            }
            freeToken(&to_tok);
            char *s2 = NULL; int t2 = VAR_INT;
            high = evaluate_operand_val(cursor, &s2, &t2, NULL, NULL);
            if (s2) { untrack_alloc(s2); free(s2); }
        } else {
            freeToken(&p);
            char *s1 = NULL, *s2 = NULL; int t1 = VAR_INT, t2 = VAR_INT;
            low = evaluate_operand_val(cursor, &s1, &t1, NULL, NULL);
            high = evaluate_operand_val(cursor, &s2, &t2, NULL, NULL);
            if (s1) { untrack_alloc(s1); free(s1); }
            if (s2) { untrack_alloc(s2); free(s2); }
        }
        acc = ((val < low) ? low : (val > high ? high : val)) * sign;
        *out_type = VAR_INT;
    } else if (t.type == TOKEN_SIGN || t.type == TOKEN_SGN) {
        char *s = NULL; int ty = VAR_INT;
        int val = evaluate_operand_val(cursor, &s, &ty, NULL, NULL);
        if (s) { untrack_alloc(s); free(s); }
        acc = ((val < 0) ? -1 : (val > 0 ? 1 : 0)) * sign;
        *out_type = VAR_INT;
    } else if (t.type == TOKEN_DIVREM) {
        char *s1 = NULL, *s2 = NULL; int t1 = VAR_INT, t2 = VAR_INT;
        int a = evaluate_operand_val(cursor, &s1, &t1, NULL, NULL);
        int b = evaluate_operand_val(cursor, &s2, &t2, NULL, NULL);
        if (s1) { untrack_alloc(s1); free(s1); }
        if (s2) { untrack_alloc(s2); free(s2); }
        if (b == 0) throw_error("DivisionByZeroError", "Division by zero in divRem on line %d", current_executing_line);
        Array *arr = create_array();
        Variable vq, vr;
        memset(&vq, 0, sizeof(vq)); vq.type = VAR_INT; vq.int_val = a / b;
        memset(&vr, 0, sizeof(vr)); vr.type = VAR_INT; vr.int_val = a % b;
        array_append(arr, &vq);
        array_append(arr, &vr);
        *out_type = VAR_ARRAY;
        if (out_arr) *out_arr = arr;
        current_reply.array_val = arr;
        *out_str = array_to_string(arr);
        acc = 0;
    } else if (t.type == TOKEN_ISEVEN || t.type == TOKEN_EVEN) {
        char *s = NULL; int ty = VAR_INT;
        int val = evaluate_operand_val(cursor, &s, &ty, NULL, NULL);
        if (s) { untrack_alloc(s); free(s); }
        acc = (val % 2 == 0);
        *out_type = VAR_BOOL;
    } else if (t.type == TOKEN_ISODD || t.type == TOKEN_ODD) {
        char *s = NULL; int ty = VAR_INT;
        int val = evaluate_operand_val(cursor, &s, &ty, NULL, NULL);
        if (s) { untrack_alloc(s); free(s); }
        acc = (val % 2 != 0);
        *out_type = VAR_BOOL;
    } else if (t.type == TOKEN_GCD || t.type == TOKEN_HCF) {
        Token p = peekToken(cursor);
        if (p.type == TOKEN_LBRACKET) {
            freeToken(&p);
            char *s = NULL; int ty = VAR_ARRAY; Array *arr = NULL;
            evaluate_operand_val(cursor, &s, &ty, &arr, NULL);
            if (s) { untrack_alloc(s); free(s); }
            if (arr && arr->count > 0) {
                int g = abs(arr->items[0].int_val);
                for (int i = 1; i < arr->count; i++) {
                    int y = abs(arr->items[i].int_val);
                    while (y != 0) { int tmp = y; y = g % y; g = tmp; }
                }
                acc = g * sign;
            } else {
                acc = 0;
            }
        } else {
            freeToken(&p);
            char *s1 = NULL, *s2 = NULL; int t1 = VAR_INT, t2 = VAR_INT;
            int a = abs(evaluate_operand_val(cursor, &s1, &t1, NULL, NULL));
            int b = abs(evaluate_operand_val(cursor, &s2, &t2, NULL, NULL));
            if (s1) { untrack_alloc(s1); free(s1); }
            if (s2) { untrack_alloc(s2); free(s2); }
            while (b != 0) { int tmp = b; b = a % b; a = tmp; }
            acc = a * sign;
        }
        *out_type = VAR_INT;
    } else if (t.type == TOKEN_LCM) {
        char *s1 = NULL, *s2 = NULL; int t1 = VAR_INT, t2 = VAR_INT;
        int a = abs(evaluate_operand_val(cursor, &s1, &t1, NULL, NULL));
        int b = abs(evaluate_operand_val(cursor, &s2, &t2, NULL, NULL));
        if (s1) { untrack_alloc(s1); free(s1); }
        if (s2) { untrack_alloc(s2); free(s2); }
        if (a == 0 || b == 0) {
            acc = 0;
        } else {
            int x = a, y = b;
            while (y != 0) { int tmp = y; y = x % y; x = tmp; }
            acc = ((a / x) * b) * sign;
        }
        *out_type = VAR_INT;
    } else if (t.type == TOKEN_FLOOR || t.type == TOKEN_CEIL || t.type == TOKEN_ROUND) {
        char *s = NULL; int ty = VAR_INT;
        int val = evaluate_operand_val(cursor, &s, &ty, NULL, NULL);
        if (s) { untrack_alloc(s); free(s); }
        if (t.type == TOKEN_ROUND) {
            Token peek2 = peekToken(cursor);
            if (peek2.type == TOKEN_NUMBER || peek2.type == TOKEN_IDENTIFIER) {
                freeToken(&peek2);
                char *s2 = NULL; int ty2 = VAR_INT;
                int prec = evaluate_operand_val(cursor, &s2, &ty2, NULL, NULL);
                if (s2) { untrack_alloc(s2); free(s2); }
                (void)prec;
            } else {
                freeToken(&peek2);
            }
        }
        acc = val * sign;
        *out_type = VAR_INT;
    } else if (t.type == TOKEN_SIN) {
        char *s = NULL; int ty = VAR_INT;
        int val = evaluate_operand_val(cursor, &s, &ty, NULL, NULL);
        if (s) { untrack_alloc(s); free(s); }
        acc = ((int)round(sin((double)val))) * sign;
        *out_type = VAR_INT;
    } else if (t.type == TOKEN_COS) {
        char *s = NULL; int ty = VAR_INT;
        int val = evaluate_operand_val(cursor, &s, &ty, NULL, NULL);
        if (s) { untrack_alloc(s); free(s); }
        acc = ((int)round(cos((double)val))) * sign;
        *out_type = VAR_INT;
    } else if (t.type == TOKEN_TAN) {
        char *s = NULL; int ty = VAR_INT;
        int val = evaluate_operand_val(cursor, &s, &ty, NULL, NULL);
        if (s) { untrack_alloc(s); free(s); }
        acc = ((int)round(tan((double)val))) * sign;
        *out_type = VAR_INT;
    } else if (t.type == TOKEN_HYPOT) {
        char *s1 = NULL, *s2 = NULL; int t1 = VAR_INT, t2 = VAR_INT;
        int a = evaluate_operand_val(cursor, &s1, &t1, NULL, NULL);
        int b = evaluate_operand_val(cursor, &s2, &t2, NULL, NULL);
        if (s1) { untrack_alloc(s1); free(s1); }
        if (s2) { untrack_alloc(s2); free(s2); }
        acc = ((int)round(hypot((double)a, (double)b))) * sign;
        *out_type = VAR_INT;
    } else if (t.type == TOKEN_DEGTORAD) {
        char *s = NULL; int ty = VAR_INT;
        int deg = evaluate_operand_val(cursor, &s, &ty, NULL, NULL);
        if (s) { untrack_alloc(s); free(s); }
        acc = ((int)round((double)deg * M_PI / 180.0)) * sign;
        *out_type = VAR_INT;
    } else if (t.type == TOKEN_RADTODEG) {
        char *s = NULL; int ty = VAR_INT;
        int rad = evaluate_operand_val(cursor, &s, &ty, NULL, NULL);
        if (s) { untrack_alloc(s); free(s); }
        acc = ((int)round((double)rad * 180.0 / M_PI)) * sign;
        *out_type = VAR_INT;
    } else if (t.type == TOKEN_SINDEG) {
        char *s = NULL; int ty = VAR_INT;
        int deg = evaluate_operand_val(cursor, &s, &ty, NULL, NULL);
        if (s) { untrack_alloc(s); free(s); }
        acc = ((int)round(sin((double)deg * M_PI / 180.0))) * sign;
        *out_type = VAR_INT;
    } else if (t.type == TOKEN_COSDEG) {
        char *s = NULL; int ty = VAR_INT;
        int deg = evaluate_operand_val(cursor, &s, &ty, NULL, NULL);
        if (s) { untrack_alloc(s); free(s); }
        acc = ((int)round(cos((double)deg * M_PI / 180.0))) * sign;
        *out_type = VAR_INT;
    } else if (t.type == TOKEN_LN) {
        char *s = NULL; int ty = VAR_INT;
        int val = evaluate_operand_val(cursor, &s, &ty, NULL, NULL);
        if (s) { untrack_alloc(s); free(s); }
        acc = ((val > 0) ? (int)round(log((double)val)) : 0) * sign;
        *out_type = VAR_INT;
    } else if (t.type == TOKEN_LOG) {
        char *s = NULL; int ty = VAR_INT;
        int val = evaluate_operand_val(cursor, &s, &ty, NULL, NULL);
        if (s) { untrack_alloc(s); free(s); }
        Token peek2 = peekToken(cursor);
        if (peek2.type == TOKEN_NUMBER || peek2.type == TOKEN_IDENTIFIER || peek2.type == TOKEN_LPAREN || peek2.type == TOKEN_PIPE) {
            freeToken(&peek2);
            char *s2 = NULL; int ty2 = VAR_INT;
            int arg2 = evaluate_operand_val(cursor, &s2, &ty2, NULL, NULL);
            if (s2) { untrack_alloc(s2); free(s2); }
            if (val > 1 && arg2 > 0) {
                acc = ((int)round(log((double)arg2) / log((double)val))) * sign;
            } else {
                acc = 0;
            }
        } else {
            freeToken(&peek2);
            acc = ((val > 0) ? (int)round(log10((double)val)) : 0) * sign;
        }
        *out_type = VAR_INT;
    } else if (t.type == TOKEN_LOG2) {
        char *s = NULL; int ty = VAR_INT;
        int val = evaluate_operand_val(cursor, &s, &ty, NULL, NULL);
        if (s) { untrack_alloc(s); free(s); }
        acc = ((val > 0) ? (int)round(log2((double)val)) : 0) * sign;
        *out_type = VAR_INT;
    } else if (t.type == TOKEN_EXP) {
        char *s = NULL; int ty = VAR_INT;
        int val = evaluate_operand_val(cursor, &s, &ty, NULL, NULL);
        if (s) { untrack_alloc(s); free(s); }
        acc = ((int)round(exp((double)val))) * sign;
        *out_type = VAR_INT;
    } else if (t.type == TOKEN_LERP) {
        Token p = peekToken(cursor);
        int a = 0, b = 0, t_val = 0;
        if (p.type == TOKEN_FROM) {
            freeToken(&p);
            Token from_tok = getNextToken(cursor); freeToken(&from_tok);
            char *s1 = NULL; int t1 = VAR_INT;
            a = evaluate_operand_val(cursor, &s1, &t1, NULL, NULL);
            if (s1) { untrack_alloc(s1); free(s1); }
            Token to_tok = getNextToken(cursor);
            if (to_tok.type != TOKEN_TO) { freeToken(&to_tok); throw_error("SyntaxError", "Expected 'to' in lerp on line %d", current_executing_line); }
            freeToken(&to_tok);
            char *s2 = NULL; int t2 = VAR_INT;
            b = evaluate_operand_val(cursor, &s2, &t2, NULL, NULL);
            if (s2) { untrack_alloc(s2); free(s2); }
            Token by_tok = getNextToken(cursor);
            if (by_tok.type != TOKEN_BY) { freeToken(&by_tok); throw_error("SyntaxError", "Expected 'by' in lerp on line %d", current_executing_line); }
            freeToken(&by_tok);
            char *s3 = NULL; int t3 = VAR_INT;
            t_val = evaluate_operand_val(cursor, &s3, &t3, NULL, NULL);
            if (s3) { untrack_alloc(s3); free(s3); }
        } else {
            freeToken(&p);
            char *s1 = NULL, *s2 = NULL, *s3 = NULL; int t1 = VAR_INT, t2 = VAR_INT, t3 = VAR_INT;
            a = evaluate_operand_val(cursor, &s1, &t1, NULL, NULL);
            b = evaluate_operand_val(cursor, &s2, &t2, NULL, NULL);
            t_val = evaluate_operand_val(cursor, &s3, &t3, NULL, NULL);
            if (s1) { untrack_alloc(s1); free(s1); }
            if (s2) { untrack_alloc(s2); free(s2); }
            if (s3) { untrack_alloc(s3); free(s3); }
        }
        acc = (a + (b - a) * t_val) * sign;
        *out_type = VAR_INT;
    } else if (t.type == TOKEN_PI_CONST) {
        acc = 3 * sign;
        *out_type = VAR_INT;
    } else if (t.type == TOKEN_TAU_CONST) {
        acc = 6 * sign;
        *out_type = VAR_INT;
    } else if (t.type == TOKEN_PHI_CONST) {
        acc = 16180 * sign;
        *out_type = VAR_INT;
    } else if (t.type == TOKEN_INF_CONST) {
        acc = 2147483647 * sign;
        *out_type = VAR_INT;
    } else if (t.type == TOKEN_LBRACKET) {
        // Array literal [item1, item2, ...]
        Array *arr = create_array();
        Token peek = peekToken(cursor);
        if (peek.type == TOKEN_RBRACKET) {
            freeToken(&peek);
            Token rb = getNextToken(cursor); freeToken(&rb);
        } else {
            freeToken(&peek);
            while (1) {
                char *elem_str = NULL;
                int elem_type = VAR_INT;
                int elem_int = 0;
                Array *elem_arr = NULL;
                Entity *elem_ent = NULL;
                evaluate_argument(cursor, &elem_str, &elem_type, &elem_int, &elem_arr, &elem_ent);
                Variable elem_var;
                memset(&elem_var, 0, sizeof(elem_var));
                elem_var.type = elem_type;
                elem_var.int_val = elem_int;
                elem_var.string_val = elem_str;
                elem_var.array_val = elem_arr;
                elem_var.entity_val = elem_ent;
                array_append(arr, &elem_var);

                Token sep = getNextToken(cursor);
                if (sep.type == TOKEN_COMMA) {
                    freeToken(&sep);
                    continue;
                } else if (sep.type == TOKEN_RBRACKET) {
                    freeToken(&sep);
                    break;
                } else {
                    freeToken(&sep);
                    throw_error("SyntaxError", "Expected ',' or ']' in array literal on line %d", current_executing_line);
                }
            }
        }
        *out_type = VAR_ARRAY;
        if (out_arr) *out_arr = arr;
        current_reply.array_val = arr;
        *out_str = array_to_string(arr);
        acc = 0;
        Token dot_peek = peekToken(cursor);
        if (dot_peek.type == TOKEN_DOT) {
            freeToken(&dot_peek);
            Token dot = getNextToken(cursor); freeToken(&dot);
            Token mem = getNextToken(cursor);
            if (mem.type != TOKEN_IDENTIFIER) { freeToken(&mem); throw_error("SyntaxError", "Expected member name after '.' on line %d", current_executing_line); }
            acc = dispatch_array_method(arr, mem.value, cursor, out_str, out_type, out_arr, out_ent, current_executing_line);
            freeToken(&mem);
        } else {
            freeToken(&dot_peek);
        }
    } else if (t.type == TOKEN_LPAREN) {
        Token peek = peekToken(cursor);
        Routine *r = NULL;
        if (peek.type == TOKEN_IDENTIFIER) {
            r = find_routine(peek.value);
        }
        if (r && r->kind == ROUTINE_FUNC) {
            Token fn_tok = getNextToken(cursor);
            freeToken(&fn_tok);
            call_routine_val(r, cursor, out_str, out_type, &acc, current_executing_line);
            if (out_arr && *out_type == VAR_ARRAY) *out_arr = current_reply.array_val;
            if (out_ent && *out_type == VAR_ENTITY) *out_ent = current_reply.entity_val;
        } else {
            acc = evaluate_expression_val(cursor, out_str, out_type, out_arr, out_ent);
        }
        freeToken(&peek);
        Token rp = getNextToken(cursor);
        if (rp.type != TOKEN_RPAREN) {
            char tokval[64] = "";
            if (rp.value) strncpy(tokval, rp.value, 63);
            int toktype = rp.type;
            freeToken(&rp);
            throw_error("SyntaxError", "Expected ')' on line %d, got token type %d ('%s')", current_executing_line, toktype, tokval);
        }
        freeToken(&rp);
        if (sign == -1) {
            if (*out_type != VAR_INT && *out_type != VAR_BOOL) {
                throw_error("InvalidOperandError", "Invalid operand for unary '-' on line %d", current_executing_line);
            }
            acc *= -1;
        }
    } else if (t.type == TOKEN_IDENTIFIER) {
        // 1. Check if class instantiation: ClassName(...)
        ClassDef *cd = find_class(t.value);
        Token peek = peekToken(cursor);
        if (cd && peek.type == TOKEN_LPAREN) {
            freeToken(&peek);
            Token lp = getNextToken(cursor); freeToken(&lp);
            Entity *ent = instantiate_entity(cd, cursor, current_executing_line);
            Token rp = getNextToken(cursor);
            if (rp.type != TOKEN_RPAREN) {
                freeToken(&rp);
                throw_error("SyntaxError", "Expected ')' after class instantiation on line %d", current_executing_line);
            }
            freeToken(&rp);
            *out_type = VAR_ENTITY;
            if (out_ent) *out_ent = ent;
            current_reply.entity_val = ent;
            *out_str = entity_to_string(ent);
            freeToken(&t);
            return 0;
        }

        // 2. Check if array indexing: arr[index]
        if (peek.type == TOKEN_LBRACKET) {
            freeToken(&peek);
            Token lb = getNextToken(cursor); freeToken(&lb);
            char *idx_str = NULL;
            int idx_type = VAR_INT;
            int idx_val = evaluate_expression(cursor, &idx_str, &idx_type);
            Token rb = getNextToken(cursor);
            if (rb.type != TOKEN_RBRACKET) {
                freeToken(&rb);
                throw_error("SyntaxError", "Expected ']' after array index on line %d", current_executing_line);
            }
            freeToken(&rb);

            Variable *v = get_var(t.value);
            if (!v || v->type != VAR_ARRAY || !v->array_val) {
                throw_error("UndefinedVariableError", "Variable '%s' is not an array on line %d", t.value, current_executing_line);
            }
            if (idx_val < 0 || idx_val >= v->array_val->count) {
                throw_error("IndexOutOfBoundsError", "Array index %d out of bounds (length %d) on line %d", idx_val, v->array_val->count, current_executing_line);
            }
            Variable *elem = &v->array_val->items[idx_val];
            *out_type = elem->type;
            if (elem->type == VAR_INT) acc = elem->int_val * sign;
            else if (elem->type == VAR_BOOL) { acc = elem->int_val; *out_type = VAR_BOOL; }
            else if (elem->type == VAR_STRING) {
                *out_str = elem->string_val ? strdup(elem->string_val) : strdup("");
                track_alloc(*out_str);
            } else if (elem->type == VAR_ARRAY) {
                if (out_arr) *out_arr = elem->array_val;
                current_reply.array_val = elem->array_val;
                *out_str = array_to_string(elem->array_val);
            } else if (elem->type == VAR_ENTITY) {
                if (out_ent) *out_ent = elem->entity_val;
                current_reply.entity_val = elem->entity_val;
                *out_str = entity_to_string(elem->entity_val);
            }
            freeToken(&t);
            return acc;
        }

        // 3. Check if member access or method call: ident.member
        if (strcmp(t.value, "meta") == 0 && peek.type == TOKEN_DOT) {
            freeToken(&peek);
            Token dot = getNextToken(cursor); freeToken(&dot);
            Token part = getNextToken(cursor);
            char partition_name[32] = "";
            if (part.type == TOKEN_STATIC || (part.type == TOKEN_IDENTIFIER && strcmp(part.value, "static") == 0)) {
                strncpy(partition_name, "static", 31);
            } else if (part.type == TOKEN_DYNAMIC || (part.type == TOKEN_IDENTIFIER && strcmp(part.value, "dynamic") == 0)) {
                strncpy(partition_name, "dynamic", 31);
            } else if (part.type == TOKEN_IDENTIFIER && strcmp(part.value, "thisstatic") == 0) {
                strncpy(partition_name, "thisstatic", 31);
            } else {
                freeToken(&part); freeToken(&t);
                throw_error("SyntaxError", "Expected metadata partition (static, dynamic, thisstatic) after 'meta.' on line %d", current_executing_line);
            }
            Token dot2 = getNextToken(cursor);
            if (dot2.type != TOKEN_DOT) {
                freeToken(&dot2); freeToken(&part); freeToken(&t);
                throw_error("SyntaxError", "Expected '.' after 'meta.%s' on line %d", partition_name, current_executing_line);
            }
            freeToken(&dot2);
            Token key = getNextToken(cursor);
            if (key.type != TOKEN_IDENTIFIER) {
                freeToken(&key); freeToken(&part); freeToken(&t);
                throw_error("SyntaxError", "Expected metadata key name after 'meta.%s.' on line %d", partition_name, current_executing_line);
            }
            MetadataStore *ms = get_current_metadata_store();
            if (!ms) {
                freeToken(&key); freeToken(&part); freeToken(&t);
                throw_error("VisibilityError", "Cannot access metadata outside library or class on line %d", current_executing_line);
            }
            Variable *vars = NULL;
            int count = 0;
            if (strcmp(partition_name, "static") == 0) {
                vars = ms->static_vars;
                count = ms->static_count;
            } else if (strcmp(partition_name, "dynamic") == 0) {
                vars = ms->dynamic_vars;
                count = ms->dynamic_count;
            } else if (strcmp(partition_name, "thisstatic") == 0) {
                vars = ms->thisstatic_vars;
                count = ms->thisstatic_count;
            }
            Variable *found = NULL;
            for (int k = 0; k < count; k++) {
                if (strcmp(vars[k].name, key.value) == 0) {
                    found = &vars[k];
                    break;
                }
            }
            if (!found) {
                char err_key[64] = "", err_p[64] = "";
                if (key.value) { strncpy(err_key, key.value, 63); err_key[63] = '\0'; }
                if (part.value) { strncpy(err_p, part.value, 63); err_p[63] = '\0'; }
                freeToken(&key); freeToken(&part); freeToken(&t);
                throw_error("UndefinedVariableError", "Metadata property '%s.%s' not found on line %d", err_p, err_key, current_executing_line);
            }
            *out_type = found->type;
            if (found->type == VAR_INT) acc = found->int_val * sign;
            else if (found->type == VAR_BOOL) { acc = found->int_val; *out_type = VAR_BOOL; }
            else if (found->type == VAR_STRING) { *out_str = strdup(found->string_val ? found->string_val : ""); track_alloc(*out_str); }
            else if (found->type == VAR_ARRAY) { if (out_arr) *out_arr = found->array_val; *out_str = array_to_string(found->array_val); }
            else if (found->type == VAR_ENTITY) { if (out_ent) *out_ent = found->entity_val; *out_str = entity_to_string(found->entity_val); }
            freeToken(&key); freeToken(&part); freeToken(&t);
            return acc;
        }

        if (peek.type == TOKEN_DOT) {
            freeToken(&peek);
            Token dot = getNextToken(cursor); freeToken(&dot);
            Token mem = getNextToken(cursor);
            if (mem.type != TOKEN_IDENTIFIER) {
                freeToken(&mem);
                throw_error("SyntaxError", "Expected member name after '.' on line %d", current_executing_line);
            }

            if (is_pseudo_module_name(t.value)) {
                acc = dispatch_pseudo_method(mem.value, cursor, out_str, out_type, out_arr, out_ent, current_executing_line);
                if (sign == -1 && *out_type == VAR_INT) acc *= -1;
                freeToken(&mem); freeToken(&t);
                return acc;
            }

            // Check if t.value is a Library
            LibraryDef *lib = find_library(t.value);
            if (lib) {
                if (strcmp(mem.value, "meta") == 0) {
                    char l_name[64] = ""; strncpy(l_name, lib->name, 63); l_name[63] = '\0';
                    freeToken(&mem); freeToken(&t);
                    throw_error("VisibilityError", "Metadata is strictly internal to library '%s' on line %d", l_name, current_executing_line);
                }
                Token call_peek = peekToken(cursor);
                // Check if class defined inside library
                ClassDef *cd = find_class(mem.value);
                if (cd && call_peek.type == TOKEN_LPAREN) {
                    freeToken(&call_peek);
                    Token lp = getNextToken(cursor); freeToken(&lp);
                    Entity *ent = instantiate_entity(cd, cursor, current_executing_line);
                    Token rp = getNextToken(cursor);
                    if (rp.type != TOKEN_RPAREN) { freeToken(&rp); throw_error("SyntaxError", "Expected ')' after class instantiation on line %d", current_executing_line); }
                    freeToken(&rp);
                    *out_type = VAR_ENTITY;
                    if (out_ent) *out_ent = ent;
                    current_reply.entity_val = ent;
                    *out_str = entity_to_string(ent);
                    freeToken(&mem); freeToken(&t);
                    return 0;
                }
                Routine *r = NULL;
                for (int i = 0; i < lib->routine_count; i++) {
                    if (strcmp(lib->routines[i].name, mem.value) == 0) { r = &lib->routines[i]; break; }
                }
                if (r) {
                    int has_parens = 0;
                    if (call_peek.type == TOKEN_LPAREN) {
                        has_parens = 1;
                        freeToken(&call_peek);
                        Token lp = getNextToken(cursor); freeToken(&lp);
                    } else {
                        freeToken(&call_peek);
                    }
                    call_routine_val(r, cursor, out_str, out_type, &acc, current_executing_line);
                    if (has_parens) {
                        Token rp = getNextToken(cursor);
                        if (rp.type != TOKEN_RPAREN) { freeToken(&rp); throw_error("SyntaxError", "Expected ')' on line %d", current_executing_line); }
                        freeToken(&rp);
                    }
                    freeToken(&mem);
                    freeToken(&t);
                    return acc;
                } else {
                    freeToken(&call_peek);
                    // Library const or dynamic variable
                    for (int i = 0; i < lib->const_count; i++) {
                        if (strcmp(lib->const_vars[i].name, mem.value) == 0) {
                            Variable *cv = &lib->const_vars[i];
                            *out_type = cv->type;
                            if (cv->type == VAR_INT) acc = cv->int_val * sign;
                            else if (cv->type == VAR_BOOL) { acc = cv->int_val; *out_type = VAR_BOOL; }
                            else if (cv->type == VAR_STRING) { *out_str = strdup(cv->string_val); track_alloc(*out_str); }
                            freeToken(&mem); freeToken(&t);
                            return acc;
                        }
                    }
                    for (int i = 0; i < lib->dynamic_count; i++) {
                        if (strcmp(lib->dynamic_vars[i].name, mem.value) == 0) {
                            Variable *dv = &lib->dynamic_vars[i];
                            *out_type = dv->type;
                            if (dv->type == VAR_INT) acc = dv->int_val * sign;
                            else if (dv->type == VAR_BOOL) { acc = dv->int_val; *out_type = VAR_BOOL; }
                            else if (dv->type == VAR_STRING) { *out_str = strdup(dv->string_val); track_alloc(*out_str); }
                            freeToken(&mem); freeToken(&t);
                            return acc;
                        }
                    }
                    throw_error("UndefinedVariableError", "Library '%s' has no property '%s' on line %d", lib->name, mem.value, current_executing_line);
                }
            }

            // Variable lookup (Array, String, Entity)
            Variable *ev = get_var(t.value);
            if (ev && ev->type == VAR_ARRAY) {
                acc = dispatch_array_method(ev->array_val, mem.value, cursor, out_str, out_type, out_arr, out_ent, current_executing_line);
                freeToken(&mem); freeToken(&t);
                while (1) {
                    Token chain_dot = peekToken(cursor);
                    if (chain_dot.type != TOKEN_DOT) { freeToken(&chain_dot); break; }
                    freeToken(&chain_dot);
                    Token d = getNextToken(cursor); freeToken(&d);
                    Token nm = getNextToken(cursor);
                    if (nm.type != TOKEN_IDENTIFIER) { freeToken(&nm); throw_error("SyntaxError", "Expected member name after '.' on line %d", current_executing_line); }
                    if (*out_type == VAR_ARRAY && out_arr && *out_arr) {
                        acc = dispatch_array_method(*out_arr, nm.value, cursor, out_str, out_type, out_arr, out_ent, current_executing_line);
                    } else if (*out_type == VAR_STRING && *out_str) {
                        acc = dispatch_string_method(*out_str, nm.value, cursor, out_str, out_type, out_arr, out_ent, current_executing_line);
                    } else {
                        freeToken(&nm);
                        throw_error("SyntaxError", "Cannot call member '%s' on non-object on line %d", nm.value, current_executing_line);
                    }
                    freeToken(&nm);
                }
                return acc;
            }
            if (ev && ev->type == VAR_STRING) {
                acc = dispatch_string_method(ev->string_val ? ev->string_val : "", mem.value, cursor, out_str, out_type, out_arr, out_ent, current_executing_line);
                freeToken(&mem); freeToken(&t);
                while (1) {
                    Token chain_dot = peekToken(cursor);
                    if (chain_dot.type != TOKEN_DOT) { freeToken(&chain_dot); break; }
                    freeToken(&chain_dot);
                    Token d = getNextToken(cursor); freeToken(&d);
                    Token nm = getNextToken(cursor);
                    if (nm.type != TOKEN_IDENTIFIER) { freeToken(&nm); throw_error("SyntaxError", "Expected member name after '.' on line %d", current_executing_line); }
                    if (*out_type == VAR_ARRAY && out_arr && *out_arr) {
                        acc = dispatch_array_method(*out_arr, nm.value, cursor, out_str, out_type, out_arr, out_ent, current_executing_line);
                    } else if (*out_type == VAR_STRING && *out_str) {
                        acc = dispatch_string_method(*out_str, nm.value, cursor, out_str, out_type, out_arr, out_ent, current_executing_line);
                    } else {
                        freeToken(&nm);
                        throw_error("SyntaxError", "Cannot call member '%s' on non-object on line %d", nm.value, current_executing_line);
                    }
                    freeToken(&nm);
                }
                return acc;
            }
            if (!ev || ev->type != VAR_ENTITY || !ev->entity_val) {
                throw_error("UndefinedVariableError", "Variable '%s' is not an entity on line %d", t.value, current_executing_line);
            }
            Entity *ent = ev->entity_val;
            if (strcmp(mem.value, "meta") == 0) {
                char c_name[64] = ""; strncpy(c_name, ent->class_name, 63); c_name[63] = '\0';
                freeToken(&mem); freeToken(&t);
                throw_error("VisibilityError", "Metadata is strictly internal to class '%s' on line %d", c_name, current_executing_line);
            }
            Token call_peek = peekToken(cursor);
            Routine *m = NULL;
            for (int i = 0; i < ent->method_count; i++) {
                if (strcmp(ent->methods[i].name, mem.value) == 0) { m = &ent->methods[i]; break; }
            }
            if (m) {
                int has_parens = 0;
                if (call_peek.type == TOKEN_LPAREN) {
                    has_parens = 1;
                    freeToken(&call_peek);
                    Token lp = getNextToken(cursor); freeToken(&lp);
                } else {
                    freeToken(&call_peek);
                }
                call_routine_val(m, cursor, out_str, out_type, &acc, current_executing_line);
                if (has_parens) {
                    Token rp = getNextToken(cursor);
                    if (rp.type != TOKEN_RPAREN) { freeToken(&rp); throw_error("SyntaxError", "Expected ')' on line %d", current_executing_line); }
                    freeToken(&rp);
                }
                freeToken(&mem);
                freeToken(&t);
                return acc;
            } else {
                freeToken(&call_peek);
                // Check dynamic properties
                for (int i = 0; i < ent->dynamic_count; i++) {
                    if (strcmp(ent->dynamic_vars[i].name, mem.value) == 0) {
                        Variable *prop = &ent->dynamic_vars[i];
                        *out_type = prop->type;
                        if (prop->type == VAR_INT) acc = prop->int_val * sign;
                        else if (prop->type == VAR_BOOL) { acc = prop->int_val; *out_type = VAR_BOOL; }
                        else if (prop->type == VAR_STRING) { *out_str = strdup(prop->string_val); track_alloc(*out_str); }
                        else if (prop->type == VAR_ARRAY) { if (out_arr) *out_arr = prop->array_val; *out_str = array_to_string(prop->array_val); }
                        else if (prop->type == VAR_ENTITY) { if (out_ent) *out_ent = prop->entity_val; *out_str = entity_to_string(prop->entity_val); }
                        freeToken(&mem); freeToken(&t);
                        return acc;
                    }
                }
                // Check static properties (check visibility if outside entity)
                for (int i = 0; i < ent->static_count; i++) {
                    if (strcmp(ent->static_vars[i].name, mem.value) == 0) {
                        if (current_entity != ent && !ent->static_is_public[i]) {
                            throw_error("VisibilityError", "Cannot access private static member '%s' outside entity on line %d", mem.value, current_executing_line);
                        }
                        Variable *prop = &ent->static_vars[i];
                        *out_type = prop->type;
                        if (prop->type == VAR_INT) acc = prop->int_val * sign;
                        else if (prop->type == VAR_BOOL) { acc = prop->int_val; *out_type = VAR_BOOL; }
                        else if (prop->type == VAR_STRING) { *out_str = strdup(prop->string_val); track_alloc(*out_str); }
                        else if (prop->type == VAR_ARRAY) { if (out_arr) *out_arr = prop->array_val; *out_str = array_to_string(prop->array_val); }
                        else if (prop->type == VAR_ENTITY) { if (out_ent) *out_ent = prop->entity_val; *out_str = entity_to_string(prop->entity_val); }
                        freeToken(&mem); freeToken(&t);
                        return acc;
                    }
                }
                throw_error("EntityError", "Entity '%s' has no property '%s' on line %d", ent->class_name, mem.value, current_executing_line);
            }
        }
        freeToken(&peek);

        // 4. Routine lookup (user routines or library routines)
        Routine *r = find_routine(t.value);
        if (r) {
            if (r->kind == ROUTINE_METHOD) {
                throw_error("SyntaxError", "Cannot use method '%s' in an expression on line %d", r->name, current_executing_line);
            }
            call_routine_val(r, cursor, out_str, out_type, &acc, current_executing_line);
            if (out_arr && *out_type == VAR_ARRAY) *out_arr = current_reply.array_val;
            if (out_ent && *out_type == VAR_ENTITY) *out_ent = current_reply.entity_val;
            if (sign == -1) {
                if (*out_type != VAR_INT && *out_type != VAR_BOOL) {
                    throw_error("InvalidOperandError", "Invalid operand for unary '-' on line %d", current_executing_line);
                }
                acc *= -1;
            }
        } else if (is_pseudo_method_name(t.value) && !get_var(t.value)) {
            acc = dispatch_pseudo_method(t.value, cursor, out_str, out_type, out_arr, out_ent, current_executing_line);
            if (sign == -1 && *out_type == VAR_INT) acc *= -1;
            freeToken(&t);
            return acc;
        } else if (is_native_func_name(t.value)) {
            Token call_peek = peekToken(cursor);
            if (call_peek.type == TOKEN_LPAREN) {
                freeToken(&call_peek);
                Token lp = getNextToken(cursor); freeToken(&lp);
                char *a1_str = NULL;
                int a1_type = VAR_INT;
                Array *a1_arr = NULL;
                Entity *a1_ent = NULL;
                int a1_int = 0;
                Token pcheck = peekToken(cursor);
                if (pcheck.type != TOKEN_RPAREN && pcheck.type != TOKEN_EOF) {
                    freeToken(&pcheck);
                    a1_int = evaluate_expression_val(cursor, &a1_str, &a1_type, &a1_arr, &a1_ent);
                } else {
                    freeToken(&pcheck);
                }
                (void)a1_int;

                Variable extra_args[7];
                int extra_count = 0;
                memset(extra_args, 0, sizeof(extra_args));
                while (extra_count < 7) {
                    Token sep = peekToken(cursor);
                    if (sep.type == TOKEN_COMMA) {
                        freeToken(&sep);
                        Token cm = getNextToken(cursor); freeToken(&cm);
                        char *ea_str = NULL;
                        int ea_type = VAR_INT;
                        Array *ea_arr = NULL;
                        Entity *ea_ent = NULL;
                        int ea_int = evaluate_expression_val(cursor, &ea_str, &ea_type, &ea_arr, &ea_ent);
                        extra_args[extra_count].type = ea_type;
                        extra_args[extra_count].int_val = ea_int;
                        extra_args[extra_count].string_val = ea_str;
                        extra_args[extra_count].array_val = ea_arr ? ea_arr : current_reply.array_val;
                        extra_args[extra_count].entity_val = ea_ent ? ea_ent : current_reply.entity_val;
                        extra_count++;
                    } else {
                        freeToken(&sep);
                        break;
                    }
                }
                Token rp = getNextToken(cursor);
                if (rp.type != TOKEN_RPAREN) { freeToken(&rp); throw_error("SyntaxError", "Expected ')' on line %d", current_executing_line); }
                freeToken(&rp);

                if (a1_type == VAR_ARRAY && (a1_arr || current_reply.array_val)) {
                    Array *target_arr = a1_arr ? a1_arr : current_reply.array_val;
                    if (strcmp(t.value, "len") == 0 || strcmp(t.value, "length") == 0) {
                        *out_type = VAR_INT;
                        acc = array_len(target_arr);
                    } else if (strcmp(t.value, "isEmpty") == 0) {
                        *out_type = VAR_BOOL;
                        acc = array_is_empty(target_arr);
                    } else if (strcmp(t.value, "indexOf") == 0) {
                        *out_type = VAR_INT;
                        acc = (extra_count > 0) ? array_index_of(target_arr, &extra_args[0]) : -1;
                    } else if (strcmp(t.value, "contains") == 0) {
                        *out_type = VAR_BOOL;
                        acc = (extra_count > 0) ? array_contains(target_arr, &extra_args[0]) : 0;
                    } else if (strcmp(t.value, "slice") == 0) {
                        int st = (extra_count > 0) ? extra_args[0].int_val : 0;
                        int en = (extra_count > 1) ? extra_args[1].int_val : target_arr->count;
                        Array *sl = array_slice(target_arr, st, en);
                        *out_type = VAR_ARRAY; if (out_arr) *out_arr = sl; current_reply.array_val = sl; *out_str = array_to_string(sl);
                        acc = 0;
                    } else if (strcmp(t.value, "reverse") == 0) {
                        array_reverse(target_arr);
                        *out_type = VAR_ARRAY; if (out_arr) *out_arr = target_arr; current_reply.array_val = target_arr; *out_str = array_to_string(target_arr);
                        acc = 0;
                    } else if (strcmp(t.value, "join") == 0) {
                        const char *sep = (extra_count > 0 && extra_args[0].string_val) ? extra_args[0].string_val : "";
                        *out_str = array_join(target_arr, sep);
                        *out_type = VAR_STRING;
                        acc = 0;
                    } else {
                        throw_error("SyntaxError", "Invalid array function '%s' on line %d", t.value, current_executing_line);
                    }
                } else if (a1_type == VAR_STRING || a1_str != NULL) {
                    const char *target_str = a1_str ? a1_str : "";
                    if (strcmp(t.value, "len") == 0 || strcmp(t.value, "length") == 0) {
                        *out_type = VAR_INT;
                        acc = string_len(target_str);
                    } else if (strcmp(t.value, "isEmpty") == 0) {
                        *out_type = VAR_BOOL;
                        acc = string_is_empty(target_str);
                    } else if (strcmp(t.value, "indexOf") == 0) {
                        *out_type = VAR_INT;
                        const char *sub = (extra_count > 0 && extra_args[0].string_val) ? extra_args[0].string_val : "";
                        acc = string_index_of(target_str, sub);
                    } else if (strcmp(t.value, "contains") == 0) {
                        *out_type = VAR_BOOL;
                        const char *sub = (extra_count > 0 && extra_args[0].string_val) ? extra_args[0].string_val : "";
                        acc = string_contains(target_str, sub);
                    } else if (strcmp(t.value, "slice") == 0) {
                        int st = (extra_count > 0) ? extra_args[0].int_val : 0;
                        int en = (extra_count > 1) ? extra_args[1].int_val : (int)strlen(target_str);
                        *out_str = string_slice(target_str, st, en);
                        *out_type = VAR_STRING;
                        acc = 0;
                    } else if (strcmp(t.value, "trim") == 0) {
                        *out_str = string_trim(target_str);
                        *out_type = VAR_STRING;
                        acc = 0;
                    } else if (strcmp(t.value, "toUpper") == 0) {
                        *out_str = string_to_upper(target_str);
                        *out_type = VAR_STRING;
                        acc = 0;
                    } else if (strcmp(t.value, "toLower") == 0) {
                        *out_str = string_to_lower(target_str);
                        *out_type = VAR_STRING;
                        acc = 0;
                    } else if (strcmp(t.value, "split") == 0) {
                        const char *sep = (extra_count > 0 && extra_args[0].string_val) ? extra_args[0].string_val : " ";
                        Array *sp = string_split(target_str, sep);
                        *out_type = VAR_ARRAY; if (out_arr) *out_arr = sp; current_reply.array_val = sp; *out_str = array_to_string(sp);
                        acc = 0;
                    } else if (strcmp(t.value, "repeat") == 0) {
                        int cnt = (extra_count > 0) ? extra_args[0].int_val : 1;
                        *out_str = string_repeat(target_str, cnt);
                        *out_type = VAR_STRING;
                        acc = 0;
                    } else if (strcmp(t.value, "padLeft") == 0) {
                        int tl = (extra_count > 0) ? extra_args[0].int_val : 0;
                        const char *pad = (extra_count > 1 && extra_args[1].string_val) ? extra_args[1].string_val : " ";
                        *out_str = string_pad_left(target_str, tl, pad);
                        *out_type = VAR_STRING;
                        acc = 0;
                    } else if (strcmp(t.value, "padRight") == 0) {
                        int tl = (extra_count > 0) ? extra_args[0].int_val : 0;
                        const char *pad = (extra_count > 1 && extra_args[1].string_val) ? extra_args[1].string_val : " ";
                        *out_str = string_pad_right(target_str, tl, pad);
                        *out_type = VAR_STRING;
                        acc = 0;
                    } else {
                        throw_error("SyntaxError", "Invalid string function '%s' on line %d", t.value, current_executing_line);
                    }
                } else {
                    if (strcmp(t.value, "len") == 0 || strcmp(t.value, "length") == 0) {
                        *out_type = VAR_INT;
                        acc = 0;
                    } else {
                        throw_error("SyntaxError", "Expected Array or String argument for '%s' on line %d", t.value, current_executing_line);
                    }
                }
                freeToken(&t);
                return acc;
            } else {
                freeToken(&call_peek);
                Variable *v = get_var(t.value);
                if (v) {
                    if (v->type == VAR_INT) acc = v->int_val * sign;
                    else if (v->type == VAR_BOOL) {
                        if (sign == -1) throw_error("InvalidOperandError", "Invalid operand for unary '-' on line %d", current_executing_line);
                        acc = v->int_val;
                        *out_type = VAR_BOOL;
                    } else if (v->type == VAR_STRING) {
                        if (sign == -1) throw_error("InvalidOperandError", "Invalid operand for unary '-' on line %d", current_executing_line);
                        *out_str = strdup(v->string_val ? v->string_val : "");
                        track_alloc(*out_str);
                        *out_type = VAR_STRING;
                    } else if (v->type == VAR_ARRAY) {
                        *out_type = VAR_ARRAY;
                        if (out_arr) *out_arr = v->array_val;
                        current_reply.array_val = v->array_val;
                        *out_str = array_to_string(v->array_val);
                    } else if (v->type == VAR_ENTITY) {
                        *out_type = VAR_ENTITY;
                        if (out_ent) *out_ent = v->entity_val;
                        current_reply.entity_val = v->entity_val;
                        *out_str = entity_to_string(v->entity_val);
                    }
                } else {
                    throw_error("UndefinedVariableError", "Undefined variable '%s' on line %d", t.value, current_executing_line);
                }
            }
        } else {
            Variable *v = get_var(t.value);
            if (v) {
                if (v->type == VAR_INT) acc = v->int_val * sign;
                else if (v->type == VAR_BOOL) {
                    if (sign == -1) throw_error("InvalidOperandError", "Invalid operand for unary '-' on line %d", current_executing_line);
                    acc = v->int_val;
                    *out_type = VAR_BOOL;
                } else if (v->type == VAR_STRING) {
                    if (sign == -1) throw_error("InvalidOperandError", "Invalid operand for unary '-' on line %d", current_executing_line);
                    *out_str = strdup(v->string_val ? v->string_val : "");
                    track_alloc(*out_str);
                    *out_type = VAR_STRING;
                } else if (v->type == VAR_ARRAY) {
                    *out_type = VAR_ARRAY;
                    if (out_arr) *out_arr = v->array_val;
                    current_reply.array_val = v->array_val;
                    *out_str = array_to_string(v->array_val);
                } else if (v->type == VAR_ENTITY) {
                    *out_type = VAR_ENTITY;
                    if (out_ent) *out_ent = v->entity_val;
                    current_reply.entity_val = v->entity_val;
                    *out_str = entity_to_string(v->entity_val);
                }
            } else {
                throw_error("UndefinedVariableError", "Undefined variable '%s' on line %d", t.value, current_executing_line);
            }
        }
    } else {
        throw_error("SyntaxError", "Expected value in expression on line %d", current_executing_line);
    }

    // Handle method chaining if followed by .method(...)
    while (1) {
        Token chain_dot = peekToken(cursor);
        if (chain_dot.type != TOKEN_DOT) {
            freeToken(&chain_dot);
            break;
        }
        freeToken(&chain_dot);
        Token dot = getNextToken(cursor); freeToken(&dot);
        Token next_mem = getNextToken(cursor);
        if (next_mem.type != TOKEN_IDENTIFIER) {
            freeToken(&next_mem);
            throw_error("SyntaxError", "Expected member name after '.' on line %d", current_executing_line);
        }
        if (*out_type == VAR_ARRAY && out_arr && *out_arr) {
            acc = dispatch_array_method(*out_arr, next_mem.value, cursor, out_str, out_type, out_arr, out_ent, current_executing_line);
        } else if (*out_type == VAR_STRING && *out_str) {
            acc = dispatch_string_method(*out_str, next_mem.value, cursor, out_str, out_type, out_arr, out_ent, current_executing_line);
        } else {
            freeToken(&next_mem);
            throw_error("SyntaxError", "Cannot call member '%s' on non-object on line %d", next_mem.value, current_executing_line);
        }
        freeToken(&next_mem);
    }

    Token peek_fact = peekToken(cursor);
    if (peek_fact.type == TOKEN_FACTORIAL) {
        freeToken(&peek_fact);
        Token fact_tok = getNextToken(cursor);
        freeToken(&fact_tok);
        if (*out_type == VAR_INT) {
            if (acc < 0) throw_error("DomainError", "Factorial of negative integer on line %d", current_executing_line);
            long long f = 1;
            for (int i = 2; i <= acc; i++) f *= i;
            acc = (int)f;
        }
    } else {
        freeToken(&peek_fact);
    }

    freeToken(&t);
    return acc;
}

int evaluate_operand(const char **cursor, char **out_str, int *out_type) {
    return evaluate_operand_val(cursor, out_str, out_type, NULL, NULL);
}

static int evaluate_power_val(const char **cursor, char **out_str, int *out_type, Array **out_arr, Entity **out_ent) {
    int acc = evaluate_operand_val(cursor, out_str, out_type, out_arr, out_ent);
    Token op = peekToken(cursor);
    if (op.type == TOKEN_CARET) {
        freeToken(&op);
        Token op_consumed = getNextToken(cursor);
        freeToken(&op_consumed);

        char *rhs_str = NULL;
        int rhs_type = VAR_INT;
        Array *rhs_arr = NULL;
        Entity *rhs_ent = NULL;
        // Right-associative exponentiation
        int rhs_val = evaluate_power_val(cursor, &rhs_str, &rhs_type, &rhs_arr, &rhs_ent);
        if (rhs_str) { untrack_alloc(rhs_str); free(rhs_str); }
        acc = (int)round(pow((double)acc, (double)rhs_val));
        *out_type = VAR_INT;
    } else {
        freeToken(&op);
    }
    return acc;
}

static int evaluate_multiplicative_val(const char **cursor, char **out_str, int *out_type, Array **out_arr, Entity **out_ent) {
    int acc = evaluate_power_val(cursor, out_str, out_type, out_arr, out_ent);

    while (1) {
        Token op = peekToken(cursor);
        if (op.type == TOKEN_STAR || op.type == TOKEN_SLASH || op.type == TOKEN_PERCENT || op.type == TOKEN_SLASH_SLASH) {
            TokenType op_type = op.type;
            freeToken(&op);
            Token op_consumed = getNextToken(cursor);
            freeToken(&op_consumed);

            char *rhs_str = NULL;
            int rhs_type = VAR_INT;
            Array *rhs_arr = NULL;
            Entity *rhs_ent = NULL;
            int rhs_val = evaluate_power_val(cursor, &rhs_str, &rhs_type, &rhs_arr, &rhs_ent);

            if (op_type == TOKEN_STAR) {
                if (rhs_str || (*out_type != VAR_INT && *out_type != VAR_BOOL)) {
                    throw_error("InvalidOperandError", "Invalid operands for operator '*' on line %d", current_executing_line);
                }
                acc *= rhs_val;
                *out_type = VAR_INT;
            } else if (op_type == TOKEN_SLASH) {
                if (rhs_str || (*out_type != VAR_INT && *out_type != VAR_BOOL)) {
                    throw_error("InvalidOperandError", "Invalid operands for operator '/' on line %d", current_executing_line);
                }
                if (rhs_val != 0) {
                    acc /= rhs_val;
                    *out_type = VAR_INT;
                } else {
                    throw_error("DivisionByZeroError", "Division by zero on line %d", current_executing_line);
                }
            } else if (op_type == TOKEN_PERCENT) {
                if (rhs_str || (*out_type != VAR_INT && *out_type != VAR_BOOL)) {
                    throw_error("InvalidOperandError", "Invalid operands for operator '%%' on line %d", current_executing_line);
                }
                if (rhs_val != 0) {
                    acc %= rhs_val;
                    *out_type = VAR_INT;
                } else {
                    throw_error("DivisionByZeroError", "Modulo by zero on line %d", current_executing_line);
                }
            } else if (op_type == TOKEN_SLASH_SLASH) {
                if (rhs_str || (*out_type != VAR_INT && *out_type != VAR_BOOL)) {
                    throw_error("InvalidOperandError", "Invalid operands for operator '//' on line %d", current_executing_line);
                }
                if (rhs_val != 0) {
                    acc /= rhs_val;
                    *out_type = VAR_INT;
                } else {
                    throw_error("DivisionByZeroError", "Integer division by zero on line %d", current_executing_line);
                }
            }
            if (rhs_str) { untrack_alloc(rhs_str); free(rhs_str); }
        } else {
            freeToken(&op);
            break;
        }
    }

    return acc;
}

static int evaluate_arithmetic_val(const char **cursor, char **out_str, int *out_type, Array **out_arr, Entity **out_ent) {
    int acc = evaluate_multiplicative_val(cursor, out_str, out_type, out_arr, out_ent);

    while (1) {
        Token op = peekToken(cursor);
        if (op.type == TOKEN_PLUS || op.type == TOKEN_MINUS) {
            TokenType op_type = op.type;
            freeToken(&op);
            Token op_consumed = getNextToken(cursor);
            freeToken(&op_consumed);

            char *rhs_str = NULL;
            int rhs_type = VAR_INT;
            Array *rhs_arr = NULL;
            Entity *rhs_ent = NULL;
            int rhs_val = evaluate_multiplicative_val(cursor, &rhs_str, &rhs_type, &rhs_arr, &rhs_ent);

            if (op_type == TOKEN_PLUS) {
                if (*out_str != NULL && rhs_str != NULL) {
                    size_t len1 = strlen(*out_str);
                    size_t len2 = strlen(rhs_str);
                    char *new_str = malloc(len1 + len2 + 1);
                    if (!new_str) throw_error("MemoryAllocationError", "Memory allocation failed");
                    track_alloc(new_str);
                    memcpy(new_str, *out_str, len1);
                    memcpy(new_str + len1, rhs_str, len2 + 1);
                    untrack_alloc(*out_str);
                    free(*out_str);
                    *out_str = new_str;
                } else if (*out_str != NULL && rhs_str == NULL) {
                    char num_str[32];
                    if (rhs_type == VAR_BOOL) {
                        snprintf(num_str, sizeof(num_str), "%s", rhs_val ? "true" : "false");
                    } else {
                        snprintf(num_str, sizeof(num_str), "%d", rhs_val);
                    }
                    size_t len1 = strlen(*out_str);
                    size_t len2 = strlen(num_str);
                    char *new_str = malloc(len1 + len2 + 1);
                    if (!new_str) throw_error("MemoryAllocationError", "Memory allocation failed");
                    track_alloc(new_str);
                    memcpy(new_str, *out_str, len1);
                    memcpy(new_str + len1, num_str, len2 + 1);
                    untrack_alloc(*out_str);
                    free(*out_str);
                    *out_str = new_str;
                } else if (*out_str == NULL && rhs_str != NULL) {
                    char num_str[32];
                    if (*out_type == VAR_BOOL) {
                        snprintf(num_str, sizeof(num_str), "%s", acc ? "true" : "false");
                    } else {
                        snprintf(num_str, sizeof(num_str), "%d", acc);
                    }
                    size_t len1 = strlen(num_str);
                    size_t len2 = strlen(rhs_str);
                    char *new_str = malloc(len1 + len2 + 1);
                    if (!new_str) throw_error("MemoryAllocationError", "Memory allocation failed");
                    track_alloc(new_str);
                    memcpy(new_str, num_str, len1);
                    memcpy(new_str + len1, rhs_str, len2 + 1);
                    *out_str = new_str;
                    *out_type = VAR_STRING;
                } else {
                    acc += rhs_val;
                    *out_type = VAR_INT;
                }
            } else if (op_type == TOKEN_MINUS) {
                if (rhs_str || (*out_type != VAR_INT && *out_type != VAR_BOOL)) {
                    throw_error("InvalidOperandError", "Invalid operands for operator '-' on line %d", current_executing_line);
                }
                acc -= rhs_val;
                *out_type = VAR_INT;
            }
            if (rhs_str) { untrack_alloc(rhs_str); free(rhs_str); }
        } else {
            freeToken(&op);
            break;
        }
    }

    return acc;
}

static int evaluate_comparison_val(const char **cursor, char **out_str, int *out_type, Array **out_arr, Entity **out_ent) {
    int acc = evaluate_arithmetic_val(cursor, out_str, out_type, out_arr, out_ent);

    Token op = peekToken(cursor);
    if (op.type == TOKEN_EQUAL || op.type == TOKEN_GREATER || op.type == TOKEN_LESS ||
        op.type == TOKEN_GREATER_EQUAL || op.type == TOKEN_LESS_EQUAL || op.type == TOKEN_NOT_EQUAL) {
        TokenType cmp_type = op.type;
        freeToken(&op);
        Token op_consumed = getNextToken(cursor);
        freeToken(&op_consumed);

        char *rhs_str = NULL;
        int rhs_type = VAR_INT;
        Array *rhs_arr = NULL;
        Entity *rhs_ent = NULL;
        int rhs_val = evaluate_arithmetic_val(cursor, &rhs_str, &rhs_type, &rhs_arr, &rhs_ent);

        int cmp_res = 0;
        if (*out_str != NULL && rhs_str != NULL) {
            int cmp = strcmp(*out_str, rhs_str);
            if (cmp_type == TOKEN_EQUAL) cmp_res = (cmp == 0);
            else if (cmp_type == TOKEN_NOT_EQUAL) cmp_res = (cmp != 0);
            else if (cmp_type == TOKEN_GREATER) cmp_res = (cmp > 0);
            else if (cmp_type == TOKEN_LESS) cmp_res = (cmp < 0);
            else if (cmp_type == TOKEN_GREATER_EQUAL) cmp_res = (cmp >= 0);
            else if (cmp_type == TOKEN_LESS_EQUAL) cmp_res = (cmp <= 0);
        } else if (*out_str == NULL && rhs_str == NULL) {
            if (cmp_type == TOKEN_EQUAL) cmp_res = (acc == rhs_val);
            else if (cmp_type == TOKEN_NOT_EQUAL) cmp_res = (acc != rhs_val);
            else if (cmp_type == TOKEN_GREATER) cmp_res = (acc > rhs_val);
            else if (cmp_type == TOKEN_LESS) cmp_res = (acc < rhs_val);
            else if (cmp_type == TOKEN_GREATER_EQUAL) cmp_res = (acc >= rhs_val);
            else if (cmp_type == TOKEN_LESS_EQUAL) cmp_res = (acc <= rhs_val);
        } else {
            if (cmp_type == TOKEN_EQUAL) cmp_res = 0;
            else if (cmp_type == TOKEN_NOT_EQUAL) cmp_res = 1;
            else {
                throw_error("InvalidOperandError", "Invalid comparison between string and non-string on line %d", current_executing_line);
            }
        }

        if (*out_str) {
            untrack_alloc(*out_str);
            free(*out_str);
            *out_str = NULL;
        }
        if (rhs_str) {
            untrack_alloc(rhs_str);
            free(rhs_str);
        }

        acc = cmp_res;
        *out_type = VAR_BOOL;
    } else if (op.type == TOKEN_IS) {
        freeToken(&op);
        Token is_tok = getNextToken(cursor); freeToken(&is_tok);
        Token pred_tok = getNextToken(cursor);
        if (pred_tok.type == TOKEN_EVEN) {
            freeToken(&pred_tok);
            acc = (acc % 2 == 0);
            *out_type = VAR_BOOL;
        } else if (pred_tok.type == TOKEN_ODD) {
            freeToken(&pred_tok);
            acc = (acc % 2 != 0);
            *out_type = VAR_BOOL;
        } else {
            freeToken(&pred_tok);
            throw_error("SyntaxError", "Expected 'even' or 'odd' after 'is' on line %d", current_executing_line);
        }
    } else {
        freeToken(&op);
    }

    return acc;
}

int evaluate_expression_val(const char **cursor, char **out_str, int *out_type, Array **out_arr, Entity **out_ent) {
    int acc = evaluate_comparison_val(cursor, out_str, out_type, out_arr, out_ent);

    while (1) {
        Token op = peekToken(cursor);
        if (op.type == TOKEN_AMP || op.type == TOKEN_BOOL_AND || op.type == TOKEN_BOOL_NAND ||
            op.type == TOKEN_BOOL_OR || op.type == TOKEN_BOOL_NOR ||
            op.type == TOKEN_BOOL_XOR || op.type == TOKEN_BOOL_XNOR ||
            op.type == TOKEN_BOOL_XAND || op.type == TOKEN_BOOL_XAMP) {
            TokenType log_op = op.type;
            freeToken(&op);
            Token op_consumed = getNextToken(cursor);
            freeToken(&op_consumed);

            char *rhs_str = NULL;
            int rhs_type = VAR_INT;
            Array *rhs_arr = NULL;
            Entity *rhs_ent = NULL;
            int rhs_val = evaluate_comparison_val(cursor, &rhs_str, &rhs_type, &rhs_arr, &rhs_ent);

            int lhs_truth = is_truthy(acc, *out_str, *out_type);
            int rhs_truth = is_truthy(rhs_val, rhs_str, rhs_type);

            int res = 0;
            if (log_op == TOKEN_AMP) {
                // & [normal AND]: true if both are true
                res = (lhs_truth && rhs_truth);
            } else if (log_op == TOKEN_BOOL_AND || log_op == TOKEN_BOOL_NAND) {
                // and [negated AND / NAND]: true unless both are true
                res = !(lhs_truth && rhs_truth);
            } else if (log_op == TOKEN_BOOL_OR) {
                // or [normal OR]: true if either is true
                res = (lhs_truth || rhs_truth);
            } else if (log_op == TOKEN_BOOL_NOR) {
                // nor [negated OR / NOR]: true only if both are false
                res = !(lhs_truth || rhs_truth);
            } else if (log_op == TOKEN_BOOL_XOR) {
                // xor [exclusive OR]: true if exactly one is true
                res = (lhs_truth != rhs_truth);
            } else if (log_op == TOKEN_BOOL_XNOR || log_op == TOKEN_BOOL_XAND || log_op == TOKEN_BOOL_XAMP) {
                // xnor / xand / x& [equivalence]: true if both are true or both are false
                res = (lhs_truth == rhs_truth);
            }

            if (*out_str) {
                untrack_alloc(*out_str);
                free(*out_str);
                *out_str = NULL;
            }
            if (rhs_str) {
                untrack_alloc(rhs_str);
                free(rhs_str);
            }

            acc = res;
            *out_type = VAR_BOOL;
        } else {
            freeToken(&op);
            break;
        }
    }

    return acc;
}

int evaluate_expression(const char **cursor, char **out_str, int *out_type) {
    return evaluate_expression_val(cursor, out_str, out_type, NULL, NULL);
}

void parse_and_register_routine(const char *def_line, int body_start, int body_end, int line_num) {
    const char *cursor = def_line;
    Token t = getNextToken(&cursor); // "def"
    freeToken(&t);

    int purity_specified = 0;
    PurityKind purity = PURITY_INBOUND;
    RoutineKind kind = ROUTINE_FUNC;

    Token tok = getNextToken(&cursor);
    if (tok.type == TOKEN_INBOUND) {
        purity_specified = 1;
        purity = PURITY_INBOUND;
        freeToken(&tok);
        tok = getNextToken(&cursor);
    } else if (tok.type == TOKEN_OUTBOUND) {
        purity_specified = 1;
        purity = PURITY_OUTBOUND;
        freeToken(&tok);
        tok = getNextToken(&cursor);
    }

    if (tok.type == TOKEN_FUNC) {
        kind = ROUTINE_FUNC;
        if (!purity_specified) purity = PURITY_INBOUND;
    } else if (tok.type == TOKEN_METHOD) {
        kind = ROUTINE_METHOD;
        if (!purity_specified) purity = PURITY_OUTBOUND;
    } else {
        freeToken(&tok);
        throw_error("SyntaxError", "Expected 'func' or 'method' in routine definition on line %d", line_num);
    }
    freeToken(&tok);

    Token name_tok = getNextToken(&cursor);
    if (name_tok.type != TOKEN_IDENTIFIER) {
        freeToken(&name_tok);
        throw_error("SyntaxError", "Expected routine name in definition on line %d", line_num);
    }

    char rname[64];
    strncpy(rname, name_tok.value, sizeof(rname) - 1);
    rname[sizeof(rname) - 1] = '\0';
    freeToken(&name_tok);

    Routine *r = NULL;
    for (int i = 0; i < routine_count; i++) {
        if (strcmp(routines[i].name, rname) == 0) {
            r = &routines[i];
            break;
        }
    }
    if (!r) {
        if (routine_count >= MAX_ROUTINES) {
            throw_error("SystemError", "Maximum routine count exceeded on line %d", line_num);
        }
        r = &routines[routine_count++];
    }

    strncpy(r->name, rname, sizeof(r->name) - 1);
    r->name[sizeof(r->name) - 1] = '\0';
    r->kind = kind;
    r->purity = purity;
    r->body_start_line = body_start;
    r->body_end_line = body_end;
    r->param_count = 0;
    parse_parameter_list(&cursor, r->params, r->param_types, &r->param_count);
}

void parse_lines(const char *buffer) {
    const char *p = buffer;
    int line_num = 1;
    int in_multiline_comment = 0;

    while (*p != '\0') {
        const char *eol = p;
        while (*eol != '\n' && *eol != '\0') eol++;

        int raw_len = eol - p;
        char *raw_line = malloc(raw_len + 1);
        if (!raw_line) {
            throw_error("MemoryAllocationError", "Memory allocation failed");
        }
        memcpy(raw_line, p, raw_len);
        raw_line[raw_len] = '\0';

        char *clean_line = calloc(1, raw_len + 1);
        if (!clean_line) {
            free(raw_line);
            throw_error("MemoryAllocationError", "Memory allocation failed");
        }
        int clean_pos = 0;
        char *src_ptr = raw_line;
        int in_string = 0;

        while (*src_ptr != '\0') {
            if (!in_multiline_comment && *src_ptr == '"') {
                in_string = !in_string;
                clean_line[clean_pos++] = *src_ptr++;
            } else {
                if (in_multiline_comment) {
                    if (src_ptr[0] == '!' && src_ptr[1] == '!') {
                        in_multiline_comment = 0;
                        src_ptr += 2;
                    } else {
                        src_ptr++;
                    }
                } else {
                    if (!in_string && src_ptr[0] == '!' && src_ptr[1] == '!') {
                        in_multiline_comment = 1;
                        src_ptr += 2;
                    } else if (!in_string && src_ptr[0] == '!' && src_ptr[1] != '!' && src_ptr[1] != '=') {
                        // single line comment
                        break;
                    } else {
                        clean_line[clean_pos++] = *src_ptr++;
                    }
                }
            }
        }
        clean_line[clean_pos] = '\0';

        int indent = 0;
        char *src_indent = raw_line;
        while (*src_indent == ' ' || *src_indent == '\t') {
            if (*src_indent == ' ') indent += 1;
            else indent += 4;
            src_indent++;
        }

        char *src = clean_line;
        while (*src == ' ' || *src == '\t') {
            src++;
        }

        char *code = strdup(src);
        free(clean_line);
        int len = strlen(code);
        while (len > 0 && isspace((unsigned char)code[len - 1])) {
            code[len - 1] = '\0';
            len--;
        }

        if (line_count >= line_capacity) {
            int new_capacity = (line_capacity == 0) ? 100 : line_capacity * 2;
            Line *tmp = realloc(lines, new_capacity * sizeof(Line));
            if (!tmp) {
                free(code);
                free(raw_line);
                throw_error("MemoryAllocationError", "Memory allocation failed");
            }
            lines = tmp;
            line_capacity = new_capacity;
        }
        lines[line_count].text = code;
        lines[line_count].indent = indent;
        lines[line_count].line_num = line_num;
        line_count++;

        free(raw_line);

        if (*eol == '\n') p = eol + 1;
        else p = eol;
        line_num++;
    }
}

void execute_line(const char *text, int line_num) {
    char resolved_buf[2048];
    const char *resolved_text = resolve_alias_line(text, resolved_buf, sizeof(resolved_buf));
    const char *cursor = resolved_text;
    Token t;
    while ((t = getNextToken(&cursor)).type != TOKEN_EOF) {
        if (t.type == TOKEN_ALIAS) {
            parse_and_register_alias(resolved_text);
            while (t.type != TOKEN_EOF) {
                freeToken(&t);
                t = getNextToken(&cursor);
            }
            continue;
        }
        else if (t.type == TOKEN_DISPLAY) {
            char col[64] = "";
            get_attribute_str(resolved_text, "color", col, sizeof(col));
            int newline = 1;
            char nl[32] = "";
            if (get_attribute_str(resolved_text, "newline", nl, sizeof(nl))) {
                if (strcmp(nl, "false") == 0 || strcmp(nl, "0") == 0) newline = 0;
            }
            if (strstr(resolved_text, "?inline") != NULL) newline = 0;

            if (col[0] != '\0') {
                apply_color(col);
            }

            char *out_str = NULL;
            int out_type = VAR_INT;
            Array *out_arr = NULL;
            Entity *out_ent = NULL;
            int val = evaluate_expression_val(&cursor, &out_str, &out_type, &out_arr, &out_ent);
            if (out_type == VAR_ARRAY) {
                Array *target = out_arr ? out_arr : current_reply.array_val;
                char *s = array_to_string(target);
                printf("%s", s ? s : "[]");
            } else if (out_type == VAR_ENTITY) {
                Entity *target = out_ent ? out_ent : current_reply.entity_val;
                char *s = entity_to_string(target);
                printf("%s", s ? s : "<Entity>");
            } else if (out_str) {
                printf("%s", out_str);
                untrack_alloc(out_str);
                free(out_str);
            } else if (out_type == VAR_BOOL) {
                printf("%s", val ? "true" : "false");
            } else {
                printf("%d", val);
            }
            if (col[0] != '\0') printf("\033[0m");
            if (newline) printf("\n");
        }
        else if (t.type == TOKEN_PROMPT) {
            Token var_tok = getNextToken(&cursor);
            if (var_tok.type == TOKEN_IDENTIFIER) {
                Variable *v = set_var(var_tok.value);
                char def_val[128] = "";
                get_attribute_str(resolved_text, "default", def_val, sizeof(def_val));
                char input[256];
                if (fgets(input, sizeof(input), stdin)) {
                    input[strcspn(input, "\r\n")] = 0;
                    if (input[0] == '\0' && def_val[0] != '\0') {
                        strncpy(input, def_val, sizeof(input) - 1);
                        input[sizeof(input) - 1] = '\0';
                    }
                    char *endptr;
                    long lval = strtol(input, &endptr, 10);
                    if (*endptr == '\0' && input[0] != '\0') {
                        v->type = VAR_INT;
                        v->int_val = (int)lval;
                        if (v->string_val) {
                            free(v->string_val);
                            v->string_val = NULL;
                        }
                    } else {
                        v->type = VAR_STRING;
                        if (v->string_val) free(v->string_val);
                        v->string_val = strdup(input);
                    }
                }
            } else {
                throw_error("SyntaxError", "Expected variable name after prompt on line %d", line_num);
            }
            freeToken(&var_tok);
        }
        else if (t.type == TOKEN_THROW) {
            Token err_tok = getNextToken(&cursor);
            if (err_tok.type == TOKEN_IDENTIFIER) {
                if (strcmp(err_tok.value, "error") == 0) {
                    if (current_error_name[0] == '\0') {
                        throw_error("RuntimeError", "No error is currently active to rethrow on line %d", line_num);
                    } else {
                        throw_error(current_error_name, "%s", current_error_msg);
                    }
                } else if (is_error_name(err_tok.value)) {
                    char custom_msg[256] = "";
                    get_attribute_str(resolved_text, "msg", custom_msg, sizeof(custom_msg));
                    if (custom_msg[0] != '\0') {
                        throw_error(err_tok.value, "%s", custom_msg);
                    } else {
                        throw_error(err_tok.value, "User thrown error");
                    }
                } else {
                    throw_error("SyntaxError", "Invalid error: %s error on line %d", err_tok.value, line_num);
                }
            } else {
                throw_error("SyntaxError", "Expected error name after throw on line %d", line_num);
            }
            freeToken(&err_tok);
        }
        else if (t.type == TOKEN_INJECT) {
            // Single-line inject token handler
            Token lang_tok = getNextToken(&cursor);
            freeToken(&lang_tok);
            while (t.type != TOKEN_EOF) {
                freeToken(&t);
                t = getNextToken(&cursor);
            }
            continue;
        }
        else if (t.type == TOKEN_SET) {
            Token var_tok = getNextToken(&cursor);
            char decl_type[64] = "";
            if (is_type_token(var_tok.type, var_tok.value)) {
                strncpy(decl_type, get_type_name_from_token(&var_tok), 63);
                decl_type[63] = '\0';
                freeToken(&var_tok);
                var_tok = getNextToken(&cursor);
            }
            if (var_tok.type != TOKEN_IDENTIFIER) {
                freeToken(&var_tok);
                throw_error("SyntaxError", "Expected variable name after 'set' on line %d", line_num);
            }
            Token colon = getNextToken(&cursor);
            if (colon.type != TOKEN_COLON) {
                freeToken(&colon);
                freeToken(&var_tok);
                throw_error("SyntaxError", "Expected ':' after variable name in 'set' on line %d", line_num);
            }
            freeToken(&colon);
            char *out_str = NULL;
            int out_type = VAR_INT;
            Array *out_arr = NULL;
            Entity *out_ent = NULL;
            int val = evaluate_expression_val(&cursor, &out_str, &out_type, &out_arr, &out_ent);
            if (decl_type[0] != '\0') {
                validate_value_type(decl_type, val, out_str, out_type, out_arr ? out_arr : current_reply.array_val, out_ent ? out_ent : current_reply.entity_val, line_num);
            }
            Variable *existing = get_var(var_tok.value);
            if (decl_type[0] == '\0' && existing && existing->declared_type[0] != '\0') {
                validate_value_type(existing->declared_type, val, out_str, out_type, out_arr ? out_arr : current_reply.array_val, out_ent ? out_ent : current_reply.entity_val, line_num);
                strncpy(decl_type, existing->declared_type, 63);
                decl_type[63] = '\0';
            }
            Variable *v = set_var_scoped(var_tok.value, line_num);
            if (decl_type[0] != '\0') {
                strncpy(v->declared_type, decl_type, 63);
                v->declared_type[63] = '\0';
            }
            v->type = out_type;
            if (out_type == VAR_ARRAY) {
                v->array_val = out_arr ? out_arr : current_reply.array_val;
            } else if (out_type == VAR_ENTITY) {
                v->entity_val = out_ent ? out_ent : current_reply.entity_val;
            } else if (out_str) {
                if (v->string_val) free(v->string_val);
                untrack_alloc(out_str);
                v->string_val = out_str;
            } else if (out_type == VAR_BOOL) {
                v->int_val = val;
                if (v->string_val) {
                    free(v->string_val);
                    v->string_val = NULL;
                }
            } else {
                v->int_val = val;
                if (v->string_val) {
                    free(v->string_val);
                    v->string_val = NULL;
                }
            }
            freeToken(&var_tok);
        }
        else if (t.type == TOKEN_REPLY) {
            if (call_stack_ptr == 0) {
                throw_error("SyntaxError", "'reply' statement outside of routine on line %d", line_num);
            }
            Routine *cur_routine = call_stack[call_stack_ptr - 1].routine;
            Token peek = peekToken(&cursor);
            if (peek.type != TOKEN_EOF) {
                freeToken(&peek);
                if (cur_routine->kind == ROUTINE_METHOD) {
                    throw_error("SyntaxError", "Methods cannot return values with reply on line %d", line_num);
                }
                char *out_str = NULL;
                int out_type = VAR_INT;
                Array *out_arr = NULL;
                Entity *out_ent = NULL;
                int val = evaluate_expression_val(&cursor, &out_str, &out_type, &out_arr, &out_ent);
                current_reply.has_replied = 1;
                current_reply.type = out_type;
                current_reply.int_val = val;
                current_reply.array_val = out_arr ? out_arr : current_reply.array_val;
                current_reply.entity_val = out_ent ? out_ent : current_reply.entity_val;
                if (current_reply.string_val) {
                    free(current_reply.string_val);
                    current_reply.string_val = NULL;
                }
                if (out_str && out_type == VAR_STRING) {
                    untrack_alloc(out_str);
                    current_reply.string_val = out_str;
                }
            } else {
                freeToken(&peek);
                current_reply.has_replied = 1;
                current_reply.type = VAR_INT;
                current_reply.int_val = 0;
            }
        }
        else if (t.type == TOKEN_ARR || t.type == TOKEN_TYPE_ARR) {
            Token var_tok = getNextToken(&cursor);
            if (var_tok.type != TOKEN_IDENTIFIER) {
                freeToken(&var_tok);
                throw_error("SyntaxError", "Expected variable name after 'arr' on line %d", line_num);
            }
            Token peek = peekToken(&cursor);
            Variable *v = set_var_scoped(var_tok.value, line_num);
            v->type = VAR_ARRAY;
            strncpy(v->declared_type, "arr", 63);
            v->declared_type[63] = '\0';
            if (peek.type == TOKEN_COLON) {
                Token col = getNextToken(&cursor); freeToken(&col);
                char *out_str = NULL;
                int out_type = VAR_INT;
                Array *out_arr = NULL;
                Entity *out_ent = NULL;
                evaluate_expression_val(&cursor, &out_str, &out_type, &out_arr, &out_ent);
                if (out_type == VAR_ARRAY) {
                    v->array_val = out_arr ? out_arr : current_reply.array_val;
                } else {
                    Array *single = create_array();
                    Variable item;
                    memset(&item, 0, sizeof(item));
                    item.type = out_type;
                    item.string_val = out_str;
                    array_append(single, &item);
                    v->array_val = single;
                }
            } else {
                v->array_val = create_array();
            }
            freeToken(&peek);
            freeToken(&var_tok);
        }
        else if (t.type == TOKEN_TYPE_NUM || t.type == TOKEN_TYPE_BOOL || t.type == TOKEN_TYPE_STR ||
                 t.type == TOKEN_TYPE_ENTITY || t.type == TOKEN_DATATYPE) {
            char decl_type[64];
            strncpy(decl_type, get_type_name_from_token(&t), 63);
            decl_type[63] = '\0';
            Token var_tok = getNextToken(&cursor);
            if (var_tok.type != TOKEN_IDENTIFIER) {
                freeToken(&var_tok);
                throw_error("SyntaxError", "Expected variable name after datatype '%s' on line %d", decl_type, line_num);
            }
            Token colon = getNextToken(&cursor);
            if (colon.type != TOKEN_COLON) {
                freeToken(&colon);
                freeToken(&var_tok);
                throw_error("SyntaxError", "Expected ':' after variable name in type declaration on line %d", line_num);
            }
            freeToken(&colon);
            char *out_str = NULL;
            int out_type = VAR_INT;
            Array *out_arr = NULL;
            Entity *out_ent = NULL;
            int val = evaluate_expression_val(&cursor, &out_str, &out_type, &out_arr, &out_ent);
            validate_value_type(decl_type, val, out_str, out_type, out_arr ? out_arr : current_reply.array_val, out_ent ? out_ent : current_reply.entity_val, line_num);
            Variable *v = set_var_scoped(var_tok.value, line_num);
            strncpy(v->declared_type, decl_type, 63);
            v->declared_type[63] = '\0';
            v->type = out_type;
            if (out_type == VAR_ARRAY) {
                v->array_val = out_arr ? out_arr : current_reply.array_val;
            } else if (out_type == VAR_ENTITY) {
                v->entity_val = out_ent ? out_ent : current_reply.entity_val;
            } else if (out_str) {
                if (v->string_val) free(v->string_val);
                untrack_alloc(out_str);
                v->string_val = out_str;
            } else if (out_type == VAR_BOOL) {
                v->int_val = val;
                if (v->string_val) { free(v->string_val); v->string_val = NULL; }
            } else {
                v->int_val = val;
                if (v->string_val) { free(v->string_val); v->string_val = NULL; }
            }
            freeToken(&var_tok);
        }
        else if (t.type == TOKEN_OUTSCOPE) {
            is_outscope_assign = 1;
            continue;
        }
        else if (t.type == TOKEN_OUTBOUND) {
            is_outbound_assign = 1;
            continue;
        }
        else if (t.type == TOKEN_PUBLIC) {
            continue;
        }
        else if (t.type == TOKEN_LOAD) {
            Token target_tok = getNextToken(&cursor);
            if (target_tok.type != TOKEN_IDENTIFIER && target_tok.type != TOKEN_STRING) {
                freeToken(&target_tok);
                throw_error("SyntaxError", "Expected library name or path after 'load' on line %d", line_num);
            }
            char full_path[256];
            strncpy(full_path, target_tok.value ? target_tok.value : "", sizeof(full_path) - 1);
            full_path[sizeof(full_path) - 1] = '\0';
            Token next_p = peekToken(&cursor);
            while (next_p.type == TOKEN_DOT || next_p.type == TOKEN_SLASH) {
                freeToken(&next_p);
                Token sym = getNextToken(&cursor);
                strncat(full_path, sym.value ? sym.value : (sym.type == TOKEN_DOT ? "." : "/"), sizeof(full_path) - strlen(full_path) - 1);
                freeToken(&sym);
                Token part = getNextToken(&cursor);
                if (part.value) {
                    strncat(full_path, part.value, sizeof(full_path) - strlen(full_path) - 1);
                }
                freeToken(&part);
                next_p = peekToken(&cursor);
            }
            freeToken(&next_p);
            load_library(full_path, line_num);
            freeToken(&target_tok);
            continue;
        }
        else if (t.type == TOKEN_IDENTIFIER) {
            Token next = peekToken(&cursor);
            if (is_valid_type_name(t.value) && next.type == TOKEN_IDENTIFIER) {
                char decl_type[64];
                strncpy(decl_type, t.value, 63);
                decl_type[63] = '\0';
                freeToken(&next);
                Token var_tok = getNextToken(&cursor);
                Token colon = getNextToken(&cursor);
                if (colon.type != TOKEN_COLON) {
                    freeToken(&colon);
                    freeToken(&var_tok);
                    freeToken(&t);
                    throw_error("SyntaxError", "Expected ':' after variable name in type declaration on line %d", line_num);
                }
                freeToken(&colon);
                char *out_str = NULL;
                int out_type = VAR_INT;
                Array *out_arr = NULL;
                Entity *out_ent = NULL;
                int val = evaluate_expression_val(&cursor, &out_str, &out_type, &out_arr, &out_ent);
                validate_value_type(decl_type, val, out_str, out_type, out_arr ? out_arr : current_reply.array_val, out_ent ? out_ent : current_reply.entity_val, line_num);
                Variable *v = set_var_scoped(var_tok.value, line_num);
                strncpy(v->declared_type, decl_type, 63);
                v->declared_type[63] = '\0';
                v->type = out_type;
                if (out_type == VAR_ARRAY) {
                    v->array_val = out_arr ? out_arr : current_reply.array_val;
                } else if (out_type == VAR_ENTITY) {
                    v->entity_val = out_ent ? out_ent : current_reply.entity_val;
                } else if (out_str) {
                    if (v->string_val) free(v->string_val);
                    untrack_alloc(out_str);
                    v->string_val = out_str;
                } else if (out_type == VAR_BOOL) {
                    v->int_val = val;
                    if (v->string_val) { free(v->string_val); v->string_val = NULL; }
                } else {
                    v->int_val = val;
                    if (v->string_val) { free(v->string_val); v->string_val = NULL; }
                }
                freeToken(&var_tok);
                freeToken(&t);
                continue;
            }
            // Array element assignment: arr[idx]: val
            if (next.type == TOKEN_LBRACKET) {
                freeToken(&next);
                Token lb = getNextToken(&cursor); freeToken(&lb);
                char *idx_str = NULL;
                int idx_type = VAR_INT;
                int idx_val = evaluate_expression(&cursor, &idx_str, &idx_type);
                Token rb = getNextToken(&cursor);
                if (rb.type != TOKEN_RBRACKET) {
                    freeToken(&rb);
                    throw_error("SyntaxError", "Expected ']' after array index on line %d", line_num);
                }
                freeToken(&rb);
                Token colon = getNextToken(&cursor);
                if (colon.type != TOKEN_COLON) {
                    freeToken(&colon);
                    throw_error("SyntaxError", "Expected ':' after array index assignment on line %d", line_num);
                }
                freeToken(&colon);
                char *out_str = NULL;
                int out_type = VAR_INT;
                Array *out_arr = NULL;
                Entity *out_ent = NULL;
                int val = evaluate_expression_val(&cursor, &out_str, &out_type, &out_arr, &out_ent);

                Variable *v = get_var(t.value);
                if (!v || v->type != VAR_ARRAY || !v->array_val) {
                    throw_error("UndefinedVariableError", "Variable '%s' is not an array on line %d", t.value, line_num);
                }
                if (idx_val < 0 || idx_val >= v->array_val->count) {
                    throw_error("IndexOutOfBoundsError", "Array index %d out of bounds (length %d) on line %d", idx_val, v->array_val->count, line_num);
                }
                Variable *elem = &v->array_val->items[idx_val];
                elem->type = out_type;
                elem->int_val = val;
                elem->string_val = out_str;
                elem->array_val = out_arr ? out_arr : current_reply.array_val;
                elem->entity_val = out_ent ? out_ent : current_reply.entity_val;
            }
            // Dot notation: ident.prop: val or ident.method(...)
            else if (next.type == TOKEN_DOT) {
                freeToken(&next);
                if (strcmp(t.value, "meta") == 0) {
                    Token dot = getNextToken(&cursor); freeToken(&dot);
                    Token part = getNextToken(&cursor);
                    char partition_name[32] = "";
                    if (part.type == TOKEN_STATIC || (part.type == TOKEN_IDENTIFIER && strcmp(part.value, "static") == 0)) {
                        strncpy(partition_name, "static", 31);
                    } else if (part.type == TOKEN_DYNAMIC || (part.type == TOKEN_IDENTIFIER && strcmp(part.value, "dynamic") == 0)) {
                        strncpy(partition_name, "dynamic", 31);
                    } else if (part.type == TOKEN_IDENTIFIER && strcmp(part.value, "thisstatic") == 0) {
                        strncpy(partition_name, "thisstatic", 31);
                    } else {
                        freeToken(&part); freeToken(&t);
                        throw_error("SyntaxError", "Expected metadata partition after 'meta.' on line %d", line_num);
                    }
                    Token dot2 = getNextToken(&cursor);
                    if (dot2.type != TOKEN_DOT) {
                        freeToken(&dot2); freeToken(&part); freeToken(&t);
                        throw_error("SyntaxError", "Expected '.' after 'meta.%s' on line %d", partition_name, line_num);
                    }
                    freeToken(&dot2);
                    Token key = getNextToken(&cursor);
                    if (key.type != TOKEN_IDENTIFIER) {
                        freeToken(&key); freeToken(&part); freeToken(&t);
                        throw_error("SyntaxError", "Expected metadata key name after 'meta.%s.' on line %d", partition_name, line_num);
                    }
                    Token col = getNextToken(&cursor);
                    if (col.type != TOKEN_COLON) {
                        freeToken(&col); freeToken(&key); freeToken(&part); freeToken(&t);
                        throw_error("SyntaxError", "Expected ':' after metadata property on line %d", line_num);
                    }
                    freeToken(&col);
                    MetadataStore *ms = get_current_metadata_store();
                    if (!ms) {
                        freeToken(&key); freeToken(&part); freeToken(&t);
                        throw_error("VisibilityError", "Cannot access metadata outside library or class on line %d", line_num);
                    }
                    if (strcmp(partition_name, "static") == 0) {
                        char k[64] = ""; if (key.value) { strncpy(k, key.value, 63); k[63] = '\0'; }
                        freeToken(&key); freeToken(&part); freeToken(&t);
                        throw_error("ImmutableError", "Cannot modify immutable metadata property 'meta.static.%s' on line %d", k, line_num);
                    } else if (strcmp(partition_name, "thisstatic") == 0) {
                        char k[64] = ""; if (key.value) { strncpy(k, key.value, 63); k[63] = '\0'; }
                        freeToken(&key); freeToken(&part); freeToken(&t);
                        throw_error("ImmutableError", "Cannot modify immutable metadata property 'meta.thisstatic.%s' on line %d", k, line_num);
                    } else if (strcmp(partition_name, "dynamic") == 0) {
                        char *out_str = NULL;
                        int out_type = VAR_INT;
                        Array *out_arr = NULL;
                        Entity *out_ent = NULL;
                        int val = evaluate_expression_val(&cursor, &out_str, &out_type, &out_arr, &out_ent);
                        add_metadata_var(&ms->dynamic_vars, &ms->dynamic_count, &ms->dynamic_capacity,
                                         key.value, out_type, val, out_str, out_arr, out_ent);
                        freeToken(&key); freeToken(&part); freeToken(&t);
                        continue;
                    }
                }
                Token dot = getNextToken(&cursor); freeToken(&dot);
                Token mem = getNextToken(&cursor);
                if (mem.type != TOKEN_IDENTIFIER) {
                    freeToken(&mem);
                    throw_error("SyntaxError", "Expected member name after '.' on line %d", line_num);
                }
                if (is_pseudo_module_name(t.value)) {
                    char *out_str = NULL;
                    int out_type = VAR_INT;
                    Array *out_arr = NULL;
                    Entity *out_ent = NULL;
                    dispatch_pseudo_method(mem.value, &cursor, &out_str, &out_type, &out_arr, &out_ent, line_num);
                    if (out_str) { untrack_alloc(out_str); free(out_str); }
                    freeToken(&mem);
                    freeToken(&t);
                    continue;
                }
                Token after_mem = peekToken(&cursor);
                if (after_mem.type == TOKEN_COLON) {
                    freeToken(&after_mem);
                    Token colon = getNextToken(&cursor); freeToken(&colon);
                    char *out_str = NULL;
                    int out_type = VAR_INT;
                    Array *out_arr = NULL;
                    Entity *out_ent = NULL;
                    int val = evaluate_expression_val(&cursor, &out_str, &out_type, &out_arr, &out_ent);

                    LibraryDef *lib = find_library(t.value);
                    if (lib) {
                        if (strcmp(mem.value, "meta") == 0) {
                            char l_name[64] = ""; strncpy(l_name, lib->name, 63); l_name[63] = '\0';
                            freeToken(&mem); freeToken(&t);
                            throw_error("VisibilityError", "Metadata is strictly internal to library '%s' on line %d", l_name, line_num);
                        }
                        for (int k = 0; k < lib->const_count; k++) {
                            if (strcmp(lib->const_vars[k].name, mem.value) == 0) {
                                throw_error("ImmutableError", "Cannot modify immutable library constant '%s' on line %d", mem.value, line_num);
                            }
                        }
                        int found = 0;
                        for (int k = 0; k < lib->dynamic_count; k++) {
                            if (strcmp(lib->dynamic_vars[k].name, mem.value) == 0) {
                                lib->dynamic_vars[k].type = out_type;
                                lib->dynamic_vars[k].int_val = val;
                                lib->dynamic_vars[k].string_val = out_str;
                                found = 1;
                                break;
                            }
                        }
                        if (!found) {
                            int new_cap = (lib->dynamic_capacity == 0) ? 8 : lib->dynamic_capacity * 2;
                            if (lib->dynamic_vars) untrack_alloc((char*)lib->dynamic_vars);
                            Variable *tmp = realloc(lib->dynamic_vars, new_cap * sizeof(Variable));
                            if (!tmp) throw_error("MemoryAllocationError", "Memory allocation failed");
                            lib->dynamic_vars = tmp;
                            track_alloc((char*)lib->dynamic_vars);
                            lib->dynamic_capacity = new_cap;
                            Variable *nv = &lib->dynamic_vars[lib->dynamic_count++];
                            memset(nv, 0, sizeof(Variable));
                            nv->name = strdup(mem.value);
                            track_alloc(nv->name);
                            nv->type = out_type;
                            nv->int_val = val;
                            nv->string_val = out_str;
                        }
                    } else {
                        Variable *ev = get_var(t.value);
                        if (!ev || ev->type != VAR_ENTITY || !ev->entity_val) {
                            throw_error("UndefinedVariableError", "Variable '%s' is not an entity on line %d", t.value, line_num);
                        }
                        Entity *ent = ev->entity_val;
                        if (strcmp(mem.value, "meta") == 0) {
                            char c_name[64] = ""; strncpy(c_name, ent->class_name, 63); c_name[63] = '\0';
                            freeToken(&mem); freeToken(&t);
                            throw_error("VisibilityError", "Metadata is strictly internal to class '%s' on line %d", c_name, line_num);
                        }
                        for (int k = 0; k < ent->static_count; k++) {
                            if (strcmp(ent->static_vars[k].name, mem.value) == 0) {
                                throw_error("ImmutableError", "Cannot modify immutable static property '%s' on line %d", mem.value, line_num);
                            }
                        }
                        int found = 0;
                        for (int k = 0; k < ent->dynamic_count; k++) {
                            if (strcmp(ent->dynamic_vars[k].name, mem.value) == 0) {
                                ent->dynamic_vars[k].type = out_type;
                                ent->dynamic_vars[k].int_val = val;
                                ent->dynamic_vars[k].string_val = out_str;
                                ent->dynamic_vars[k].array_val = out_arr ? out_arr : current_reply.array_val;
                                ent->dynamic_vars[k].entity_val = out_ent ? out_ent : current_reply.entity_val;
                                found = 1;
                                break;
                            }
                        }
                        if (!found) {
                            int new_cap = (ent->dynamic_capacity == 0) ? 8 : ent->dynamic_capacity * 2;
                            if (ent->dynamic_vars) untrack_alloc((char*)ent->dynamic_vars);
                            Variable *tmp = realloc(ent->dynamic_vars, new_cap * sizeof(Variable));
                            if (!tmp) throw_error("MemoryAllocationError", "Memory allocation failed");
                            ent->dynamic_vars = tmp;
                            track_alloc((char*)ent->dynamic_vars);
                            ent->dynamic_capacity = new_cap;
                            Variable *nv = &ent->dynamic_vars[ent->dynamic_count++];
                            memset(nv, 0, sizeof(Variable));
                            nv->name = strdup(mem.value);
                            track_alloc(nv->name);
                            nv->type = out_type;
                            nv->int_val = val;
                            nv->string_val = out_str;
                            nv->array_val = out_arr ? out_arr : current_reply.array_val;
                            nv->entity_val = out_ent ? out_ent : current_reply.entity_val;
                        }
                    }
                } else {
                    int has_parens = 0;
                    if (after_mem.type == TOKEN_LPAREN) {
                        has_parens = 1;
                        freeToken(&after_mem);
                        Token lp = getNextToken(&cursor); freeToken(&lp);
                    } else {
                        freeToken(&after_mem);
                    }
                    LibraryDef *lib = find_library(t.value);
                    if (lib) {
                        if (strcmp(mem.value, "meta") == 0) {
                            char l_name[64] = ""; strncpy(l_name, lib->name, 63); l_name[63] = '\0';
                            freeToken(&mem); freeToken(&t);
                            throw_error("VisibilityError", "Metadata is strictly internal to library '%s' on line %d", l_name, line_num);
                        }
                        Routine *r = NULL;
                        for (int k = 0; k < lib->routine_count; k++) {
                            if (strcmp(lib->routines[k].name, mem.value) == 0) { r = &lib->routines[k]; break; }
                        }
                        if (!r) throw_error("SyntaxError", "Library '%s' has no routine '%s' on line %d", lib->name, mem.value, line_num);
                        call_routine(r, &cursor, line_num);
                        if (has_parens) {
                            Token rp = getNextToken(&cursor);
                            if (rp.type != TOKEN_RPAREN) { freeToken(&rp); throw_error("SyntaxError", "Expected ')' on line %d", line_num); }
                            freeToken(&rp);
                        }
                    } else {
                        Variable *ev = get_var(t.value);
                        if (ev && ev->type == VAR_ARRAY) {
                            char *m_str = NULL;
                            int m_type = VAR_INT;
                            Array *m_arr = NULL;
                            Entity *m_ent = NULL;
                            dispatch_array_method(ev->array_val, mem.value, &cursor, &m_str, &m_type, &m_arr, &m_ent, line_num);
                            freeToken(&mem);
                            continue;
                        } else if (ev && ev->type == VAR_STRING) {
                            char *m_str = NULL;
                            int m_type = VAR_INT;
                            Array *m_arr = NULL;
                            Entity *m_ent = NULL;
                            dispatch_string_method(ev->string_val ? ev->string_val : "", mem.value, &cursor, &m_str, &m_type, &m_arr, &m_ent, line_num);
                            freeToken(&mem);
                            continue;
                        } else if (ev && ev->type == VAR_ENTITY && ev->entity_val) {
                            Entity *ent = ev->entity_val;
                            if (strcmp(mem.value, "meta") == 0) {
                                char c_name[64] = ""; strncpy(c_name, ent->class_name, 63); c_name[63] = '\0';
                                freeToken(&mem); freeToken(&t);
                                throw_error("VisibilityError", "Metadata is strictly internal to class '%s' on line %d", c_name, line_num);
                            }
                            Routine *m = NULL;
                            for (int k = 0; k < ent->method_count; k++) {
                                if (strcmp(ent->methods[k].name, mem.value) == 0) { m = &ent->methods[k]; break; }
                            }
                            if (!m) throw_error("EntityError", "Entity '%s' has no method '%s' on line %d", ent->class_name, mem.value, line_num);
                            call_routine(m, &cursor, line_num);
                            if (has_parens) {
                                Token rp = getNextToken(&cursor);
                                if (rp.type != TOKEN_RPAREN) { freeToken(&rp); throw_error("SyntaxError", "Expected ')' on line %d", line_num); }
                                freeToken(&rp);
                            }
                        } else {
                            throw_error("UndefinedVariableError", "Variable '%s' is not an entity or object on line %d", t.value, line_num);
                        }
                    }
                }
                freeToken(&mem);
            }
            // Normal assignment: ident: expr
            else if (next.type == TOKEN_COLON) {
                freeToken(&next);
                Token colon_consumed = getNextToken(&cursor);
                freeToken(&colon_consumed);
                char *out_str = NULL;
                int out_type = VAR_INT;
                Array *out_arr = NULL;
                Entity *out_ent = NULL;
                int val = evaluate_expression_val(&cursor, &out_str, &out_type, &out_arr, &out_ent);
                Variable *existing = get_var(t.value);
                if (existing && existing->declared_type[0] != '\0') {
                    validate_value_type(existing->declared_type, val, out_str, out_type, out_arr ? out_arr : current_reply.array_val, out_ent ? out_ent : current_reply.entity_val, line_num);
                }
                Variable *v = set_var_scoped(t.value, line_num);
                if (existing && existing->declared_type[0] != '\0') {
                    strncpy(v->declared_type, existing->declared_type, 63);
                    v->declared_type[63] = '\0';
                }
                is_outscope_assign = 0;
                is_outbound_assign = 0;
                v->type = out_type;
                if (out_type == VAR_ARRAY) {
                    v->array_val = out_arr ? out_arr : current_reply.array_val;
                } else if (out_type == VAR_ENTITY) {
                    v->entity_val = out_ent ? out_ent : current_reply.entity_val;
                } else if (out_str) {
                    if (symtable && v >= symtable && v < symtable + var_capacity) {
                        if (v->string_val) free(v->string_val);
                        untrack_alloc(out_str);
                        v->string_val = out_str;
                    } else {
                        v->string_val = out_str;
                    }
                } else if (out_type == VAR_BOOL) {
                    v->int_val = val;
                    if (v->string_val) { free(v->string_val); v->string_val = NULL; }
                } else {
                    v->int_val = val;
                    if (v->string_val) { free(v->string_val); v->string_val = NULL; }
                }
            } else {
                freeToken(&next);
                Routine *r = find_routine(t.value);
                if (r) {
                    call_routine(r, &cursor, line_num);
                } else if (is_pseudo_method_name(t.value)) {
                    char *out_str = NULL;
                    int out_type = VAR_INT;
                    Array *out_arr = NULL;
                    Entity *out_ent = NULL;
                    dispatch_pseudo_method(t.value, &cursor, &out_str, &out_type, &out_arr, &out_ent, line_num);
                    if (out_str) { untrack_alloc(out_str); free(out_str); }
                } else {
                    throw_error("SyntaxError", "Unexpected identifier '%s' on line %d", t.value, line_num);
                }
            }
        }
        else if (t.type == TOKEN_QUESTION) {
            Token name_tok = getNextToken(&cursor);
            if (name_tok.type == TOKEN_IDENTIFIER) {
                Token peek = peekToken(&cursor);
                if (peek.type == TOKEN_EQUAL) {
                    freeToken(&peek);
                    Token eq = getNextToken(&cursor); freeToken(&eq);
                    Token val_tok = getNextToken(&cursor); freeToken(&val_tok);
                } else {
                    freeToken(&peek);
                }
            }
            freeToken(&name_tok);
        }
        else if (t.type == TOKEN_ERROR) {
            throw_error("SyntaxError", "Unexpected token '%s' on line %d", t.value ? t.value : "", line_num);
        }
        freeToken(&t);
    }
}

void execute_block(int start, int end) {
    int expected_indent = -1;
    int i = start;
    while (i <= end) {
        if (active_watch.active && active_watch.triggered) {
            break;
        }
        if (current_reply.has_replied) {
            break;
        }

        Line *line = &lines[i];
        current_executing_line = line->line_num;
        if (line->text[0] == '\0' || line->text[0] == '!') {
            i++;
            continue;
        }
        if (expected_indent == -1) {
            expected_indent = line->indent;
        } else if (line->indent != expected_indent) {
            throw_error("IndentationError", "Indentation error on line %d (expected %d, got %d)", line->line_num, expected_indent, line->indent);
        }

        int prev_suppress_active = suppress_jmp_active;
        jmp_buf prev_suppress_env;
        memcpy(prev_suppress_env, suppress_jmp_env, sizeof(jmp_buf));

        char resolved_line_buf[2048];
        const char *effective_text = resolve_alias_line(line->text, resolved_line_buf, sizeof(resolved_line_buf));

        suppress_jmp_active = 1;
        if (setjmp(suppress_jmp_env) == 0) {
            if (strncmp(effective_text, "class ", 6) == 0 || strncmp(effective_text, "class(", 6) == 0) {
                int block_start = i + 1;
                int block_end = i;
                while (block_end + 1 <= end) {
                    Line *next = &lines[block_end + 1];
                    if (next->text[0] == '\0' || next->text[0] == '!') { block_end++; continue; }
                    if (next->indent > line->indent) block_end++;
                    else break;
                }

                if (class_count >= MAX_CLASSES) throw_error("SystemError", "Max classes exceeded on line %d", line->line_num);
                ClassDef *cd = &classes[class_count++];
                memset(cd, 0, sizeof(ClassDef));
                cd->static_start_line = 1;
                cd->static_end_line = 0;
                cd->dynamic_start_line = 1;
                cd->dynamic_end_line = 0;

                const char *cc = effective_text + 5;
                while (isspace((unsigned char)*cc)) cc++;
                const char *name_start = cc;
                while (isalnum((unsigned char)*cc) || *cc == '_') cc++;
                int nlen = cc - name_start;
                if (nlen > 63) nlen = 63;
                strncpy(cd->name, name_start, nlen);
                cd->name[nlen] = '\0';
                while (isspace((unsigned char)*cc)) cc++;
                if (*cc == '(') {
                    cc++;
                    parse_parameter_list(&cc, cd->params, cd->param_types, &cd->param_count);
                }

                int cur_section = 0; // 1 = static, 2 = dynamic
                for (int idx = block_start; idx <= block_end; idx++) {
                    Line *ln = &lines[idx];
                    if (ln->text[0] == '\0' || ln->text[0] == '!') continue;
                    if (strcmp(ln->text, "static:") == 0 || strncmp(ln->text, "static:", 7) == 0) {
                        cur_section = 1;
                        cd->static_start_line = idx + 1;
                        cd->static_end_line = idx;
                    } else if (strcmp(ln->text, "dynamic:") == 0 || strncmp(ln->text, "dynamic:", 8) == 0) {
                        cur_section = 2;
                        cd->dynamic_start_line = idx + 1;
                        cd->dynamic_end_line = idx;
                    } else if (is_meta_block_header(ln->text)) {
                        cur_section = 3;
                        if (strstr(ln->text, "?override") != NULL) {
                            cd->meta.override_all = 1;
                        }
                        int m_start = idx + 1;
                        int m_end = idx;
                        while (m_end + 1 <= block_end) {
                            Line *next = &lines[m_end + 1];
                            if (next->text[0] == '\0' || next->text[0] == '!') { m_end++; continue; }
                            if (next->indent > ln->indent) m_end++;
                            else break;
                        }
                        parse_meta_block(m_start, m_end, &cd->meta);
                        idx = m_end;
                        continue;
                    } else {
                        if (cur_section == 1) cd->static_end_line = idx;
                        else if (cur_section == 2) cd->dynamic_end_line = idx;
                    }
                }
                init_auto_metadata(&cd->meta, cd->name, "class", "source", cd->param_count);
                i = block_end + 1;
            }
            else if (strncmp(effective_text, "DataType ", 9) == 0) {
                int block_start = i + 1;
                int block_end = i;
                while (block_end + 1 <= end) {
                    Line *next = &lines[block_end + 1];
                    if (next->text[0] == '\0' || next->text[0] == '!') { block_end++; continue; }
                    if (next->indent > line->indent) block_end++;
                    else break;
                }

                const char *cc = effective_text + 8;
                while (isspace((unsigned char)*cc)) cc++;
                const char *name_start = cc;
                while (isalnum((unsigned char)*cc) || *cc == '_') cc++;
                int nlen = cc - name_start;
                char dt_name[64] = "";
                if (nlen > 63) nlen = 63;
                strncpy(dt_name, name_start, nlen);
                dt_name[nlen] = '\0';

                while (isspace((unsigned char)*cc)) cc++;
                if (strncmp(cc, "extends", 7) != 0) {
                    throw_error("SyntaxError", "Expected 'extends' after DataType name on line %d", line->line_num);
                }
                cc += 7;
                while (isspace((unsigned char)*cc)) cc++;
                const char *base_start = cc;
                while (isalnum((unsigned char)*cc) || *cc == '_') cc++;
                int blen = cc - base_start;
                char base_name[64] = "";
                if (blen > 63) blen = 63;
                strncpy(base_name, base_start, blen);
                base_name[blen] = '\0';

                if (!is_valid_type_name(base_name)) {
                    throw_error("DataTypeError", "Unknown base DataType '%s' on line %d", base_name, line->line_num);
                }

                if (custom_datatype_count >= MAX_CUSTOM_DATATYPES) {
                    throw_error("SystemError", "Maximum custom datatypes exceeded on line %d", line->line_num);
                }
                CustomDataType *cdt = &custom_datatypes[custom_datatype_count++];
                strncpy(cdt->name, dt_name, 63);
                cdt->name[63] = '\0';
                strncpy(cdt->base_type, base_name, 63);
                cdt->base_type[63] = '\0';
                cdt->constraint_start_line = block_start;
                cdt->constraint_end_line = block_end;

                Variable *dt_var = set_var_scoped(dt_name, line->line_num);
                dt_var->type = VAR_STRING;
                dt_var->string_val = strdup("DataType");
                track_alloc(dt_var->string_val);
                strncpy(dt_var->declared_type, "DataType", 63);
                dt_var->declared_type[63] = '\0';

                i = block_end + 1;
            }
            else if (strncmp(effective_text, "lib ", 4) == 0 || strncmp(effective_text, "library ", 8) == 0) {
                int block_start = i + 1;
                int block_end = i;
                while (block_end + 1 <= end) {
                    Line *next = &lines[block_end + 1];
                    if (next->text[0] == '\0' || next->text[0] == '!') { block_end++; continue; }
                    if (next->indent > line->indent) block_end++;
                    else break;
                }

                if (library_count >= MAX_LIBRARIES) throw_error("SystemError", "Max libraries exceeded on line %d", line->line_num);
                LibraryDef *lib = &libraries[library_count++];
                memset(lib, 0, sizeof(LibraryDef));

                const char *cc = effective_text;
                while (*cc && !isspace((unsigned char)*cc)) cc++;
                while (isspace((unsigned char)*cc)) cc++;
                const char *name_start = cc;
                while (isalnum((unsigned char)*cc) || *cc == '_') cc++;
                int nlen = cc - name_start;
                if (nlen > 63) nlen = 63;
                strncpy(lib->name, name_start, nlen);
                lib->name[nlen] = '\0';

                int cur_sec = 0; // 1 = const/static, 2 = dynamic
                LibraryDef *prev_lib_ctx = current_library;
                current_library = lib;

                for (int idx = block_start; idx <= block_end; idx++) {
                    Line *ln = &lines[idx];
                    if (ln->text[0] == '\0' || ln->text[0] == '!') continue;
                    if (strcmp(ln->text, "const:") == 0 || strncmp(ln->text, "const:", 6) == 0 ||
                        strcmp(ln->text, "static:") == 0 || strncmp(ln->text, "static:", 7) == 0) {
                        cur_sec = 1;
                    } else if (strcmp(ln->text, "dynamic:") == 0 || strncmp(ln->text, "dynamic:", 8) == 0) {
                        cur_sec = 2;
                    } else if (is_meta_block_header(ln->text)) {
                        if (strstr(ln->text, "?override") != NULL) {
                            lib->meta.override_all = 1;
                        }
                        int m_start = idx + 1;
                        int m_end = idx;
                        while (m_end + 1 <= block_end) {
                            Line *next = &lines[m_end + 1];
                            if (next->text[0] == '\0' || next->text[0] == '!') { m_end++; continue; }
                            if (next->indent > ln->indent) m_end++;
                            else break;
                        }
                        parse_meta_block(m_start, m_end, &lib->meta);
                        idx = m_end;
                        continue;
                    } else if (strncmp(ln->text, "class ", 6) == 0 || strncmp(ln->text, "class(", 6) == 0) {
                        int c_start = idx + 1;
                        int c_end = idx;
                        while (c_end + 1 <= block_end) {
                            Line *next = &lines[c_end + 1];
                            if (next->text[0] == '\0' || next->text[0] == '!') { c_end++; continue; }
                            if (next->indent > ln->indent) c_end++;
                            else break;
                        }
                        if (class_count >= MAX_CLASSES) throw_error("SystemError", "Max classes exceeded on line %d", ln->line_num);
                        ClassDef *cd = &classes[class_count++];
                        memset(cd, 0, sizeof(ClassDef));
                        cd->static_start_line = 1;
                        cd->static_end_line = 0;
                        cd->dynamic_start_line = 1;
                        cd->dynamic_end_line = 0;

                        const char *clc = ln->text + 5;
                        while (isspace((unsigned char)*clc)) clc++;
                        const char *cname_start = clc;
                        while (isalnum((unsigned char)*clc) || *clc == '_') clc++;
                        int cnlen = clc - cname_start;
                        if (cnlen > 63) cnlen = 63;
                        strncpy(cd->name, cname_start, cnlen);
                        cd->name[cnlen] = '\0';
                        while (isspace((unsigned char)*clc)) clc++;
                        if (*clc == '(') {
                            clc++;
                            parse_parameter_list(&clc, cd->params, cd->param_types, &cd->param_count);
                        }

                        int c_section = 0; // 1 = static, 2 = dynamic
                        for (int c_idx = c_start; c_idx <= c_end; c_idx++) {
                            Line *cln = &lines[c_idx];
                            if (cln->text[0] == '\0' || cln->text[0] == '!') continue;
                            if (strcmp(cln->text, "static:") == 0 || strncmp(cln->text, "static:", 7) == 0) {
                                c_section = 1;
                                cd->static_start_line = c_idx + 1;
                                cd->static_end_line = c_idx;
                            } else if (strcmp(cln->text, "dynamic:") == 0 || strncmp(cln->text, "dynamic:", 8) == 0) {
                                c_section = 2;
                                cd->dynamic_start_line = c_idx + 1;
                                cd->dynamic_end_line = c_idx;
                            } else if (is_meta_block_header(cln->text)) {
                                c_section = 3;
                                if (strstr(cln->text, "?override") != NULL) {
                                    cd->meta.override_all = 1;
                                }
                                int m_start = c_idx + 1;
                                int m_end = c_idx;
                                while (m_end + 1 <= c_end) {
                                    Line *next = &lines[m_end + 1];
                                    if (next->text[0] == '\0' || next->text[0] == '!') { m_end++; continue; }
                                    if (next->indent > cln->indent) m_end++;
                                    else break;
                                }
                                parse_meta_block(m_start, m_end, &cd->meta);
                                c_idx = m_end;
                                continue;
                            } else {
                                if (c_section == 1) cd->static_end_line = c_idx;
                                else if (c_section == 2) cd->dynamic_end_line = c_idx;
                            }
                        }
                        init_auto_metadata(&cd->meta, cd->name, "class", "source", cd->param_count);
                        idx = c_end;
                        continue;
                    } else {
                        const char *c = ln->text;
                        int is_priv = 0;
                        Token t = peekToken(&c);
                        if (t.type == TOKEN_PRIVATE) {
                            freeToken(&t);
                            Token priv = getNextToken(&c); freeToken(&priv);
                            is_priv = 1;
                        } else { freeToken(&t); }

                        Token def_tok = peekToken(&c);
                        if (def_tok.type == TOKEN_DEF) {
                            freeToken(&def_tok);
                            int meth_start = idx + 1;
                            int meth_end = idx;
                            while (meth_end + 1 <= block_end) {
                                Line *next = &lines[meth_end + 1];
                                if (next->text[0] == '\0' || next->text[0] == '!') { meth_end++; continue; }
                                if (next->indent > ln->indent) meth_end++;
                                else break;
                            }
                            if (lib->routine_count >= lib->routine_capacity) {
                                int new_cap = (lib->routine_capacity == 0) ? 8 : lib->routine_capacity * 2;
                                if (lib->routines) untrack_alloc((char*)lib->routines);
                                Routine *tmp = realloc(lib->routines, new_cap * sizeof(Routine));
                                if (!tmp) throw_error("MemoryAllocationError", "Memory allocation failed");
                                lib->routines = tmp;
                                track_alloc((char*)lib->routines);
                                lib->routine_capacity = new_cap;
                            }
                            Routine *lr = &lib->routines[lib->routine_count++];
                            memset(lr, 0, sizeof(Routine));
                            lr->is_private = is_priv;
                            lr->bound_library = lib;
                            lr->body_start_line = meth_start;
                            lr->body_end_line = meth_end;

                            Token d = getNextToken(&c); freeToken(&d);
                            Token kind_tok = getNextToken(&c);
                            lr->kind = (kind_tok.type == TOKEN_FUNC) ? ROUTINE_FUNC : ROUTINE_METHOD;
                            freeToken(&kind_tok);
                            Token rname = getNextToken(&c);
                            if (rname.type == TOKEN_IDENTIFIER) {
                                strncpy(lr->name, rname.value, 63);
                                lr->name[63] = '\0';
                            }
                            parse_parameter_list(&c, lr->params, lr->param_types, &lr->param_count);
                            idx = meth_end;
                        } else {
                            freeToken(&def_tok);
                            Token vname = getNextToken(&c);
                            if (vname.type == TOKEN_IDENTIFIER) {
                                Token col = getNextToken(&c);
                                if (col.type == TOKEN_COLON) {
                                    freeToken(&col);
                                    char *out_str = NULL;
                                    int out_type = VAR_INT;
                                    Array *out_arr = NULL;
                                    Entity *out_ent = NULL;
                                    int val = evaluate_expression_val(&c, &out_str, &out_type, &out_arr, &out_ent);
                                    if (cur_sec == 1) {
                                        if (lib->const_count >= lib->const_capacity) {
                                            int new_cap = (lib->const_capacity == 0) ? 8 : lib->const_capacity * 2;
                                            if (lib->const_vars) untrack_alloc((char*)lib->const_vars);
                                            Variable *tmp = realloc(lib->const_vars, new_cap * sizeof(Variable));
                                            if (!tmp) throw_error("MemoryAllocationError", "Memory allocation failed");
                                            lib->const_vars = tmp;
                                            track_alloc((char*)lib->const_vars);
                                            lib->const_capacity = new_cap;
                                        }
                                        Variable *cv = &lib->const_vars[lib->const_count++];
                                        memset(cv, 0, sizeof(Variable));
                                        cv->name = strdup(vname.value);
                                        track_alloc(cv->name);
                                        cv->type = out_type;
                                        cv->int_val = val;
                                        cv->string_val = out_str;
                                    } else {
                                        if (lib->dynamic_count >= lib->dynamic_capacity) {
                                            int new_cap = (lib->dynamic_capacity == 0) ? 8 : lib->dynamic_capacity * 2;
                                            if (lib->dynamic_vars) untrack_alloc((char*)lib->dynamic_vars);
                                            Variable *tmp = realloc(lib->dynamic_vars, new_cap * sizeof(Variable));
                                            if (!tmp) throw_error("MemoryAllocationError", "Memory allocation failed");
                                            lib->dynamic_vars = tmp;
                                            track_alloc((char*)lib->dynamic_vars);
                                            lib->dynamic_capacity = new_cap;
                                        }
                                        Variable *dv = &lib->dynamic_vars[lib->dynamic_count++];
                                        memset(dv, 0, sizeof(Variable));
                                        dv->name = strdup(vname.value);
                                        track_alloc(dv->name);
                                        dv->type = out_type;
                                        dv->int_val = val;
                                        dv->string_val = out_str;
                                    }
                                } else { freeToken(&col); }
                            }
                            freeToken(&vname);
                        }
                    }
                }
                init_auto_metadata(&lib->meta, lib->name, "library", "core", lib->routine_count + lib->const_count + lib->dynamic_count);
                current_library = prev_lib_ctx;
                i = block_end + 1;
            }
            else if (strncmp(effective_text, "def ", 4) == 0) {
                int block_start = i + 1;
                int block_end = i;
                while (block_end + 1 <= end) {
                    Line *next = &lines[block_end + 1];
                    if (next->text[0] == '\0' || next->text[0] == '!') {
                        block_end++;
                        continue;
                    }
                    if (next->indent > line->indent) {
                        block_end++;
                    } else {
                        break;
                    }
                }
                parse_and_register_routine(effective_text, block_start, block_end, line->line_num);
                i = block_end + 1;
            }
            else if (strncmp(line->text, "alias:", 6) == 0 || strcmp(line->text, "alias:") == 0 || strcmp(line->text, "alias") == 0) {
                int block_start = i + 1;
                int block_end = i;
                while (block_end + 1 <= end) {
                    Line *next = &lines[block_end + 1];
                    if (next->text[0] == '\0' || next->text[0] == '!') {
                        block_end++;
                        continue;
                    }
                    if (next->indent > line->indent) {
                        block_end++;
                    } else {
                        break;
                    }
                }
                for (int k = block_start; k <= block_end; k++) {
                    if (lines[k].text[0] != '\0' && lines[k].text[0] != '!') {
                        parse_and_register_alias(lines[k].text);
                    }
                }
                i = block_end + 1;
            }
            else if (strncmp(line->text, "alias ", 6) == 0) {
                parse_and_register_alias(line->text);
                i++;
            }
            else if (strncmp(effective_text, "loop ", 5) == 0 || strcmp(effective_text, "loop") == 0) {
            int block_start = i + 1;
            int block_end = i;
            while (block_end + 1 <= end) {
                Line *next = &lines[block_end + 1];
                if (next->text[0] == '\0' || next->text[0] == '!') {
                    block_end++;
                    continue;
                }
                if (next->indent > line->indent) {
                    block_end++;
                } else {
                    break;
                }
            }

            const char *cursor = effective_text + 4;
            char *out_str = NULL;
            int out_type = VAR_INT;
            int iters = evaluate_expression(&cursor, &out_str, &out_type);
            if (out_str) {
                untrack_alloc(out_str);
                free(out_str);
            }
            if (out_type == VAR_STRING) {
                throw_error("LoopIterationError", "Loop iterations must be numeric on line %d", line->line_num);
            }

            int step_val = 1;
            while (isspace((unsigned char)*cursor)) cursor++;
            if (strncmp(cursor, "step", 4) == 0 && (isspace((unsigned char)cursor[4]) || cursor[4] == '\0')) {
                cursor += 4;
                char *step_out_str = NULL;
                int step_out_type = VAR_INT;
                step_val = evaluate_expression(&cursor, &step_out_str, &step_out_type);
                if (step_out_str) {
                    untrack_alloc(step_out_str);
                    free(step_out_str);
                }
                if (step_out_type == VAR_STRING) {
                    throw_error("LoopStepError", "Loop step must be numeric on line %d", line->line_num);
                }
            }

            if (iters > 0 && step_val > iters) {
                throw_error("LoopStepError", "Loop step larger than loop size on line %d", line->line_num);
            }
            if (step_val <= 0) {
                throw_error("LoopStepError", "Loop step must be greater than 0 on line %d", line->line_num);
            }

            for (int k = 0; k < iters; k += step_val) {
                if (current_reply.has_replied) break;
                execute_block(block_start, block_end);
                if (current_reply.has_replied) break;
                if (active_watch.active && active_watch.triggered) break;
            }
            i = block_end + 1;
        }
            else if (strncmp(effective_text, "iterate ", 8) == 0) {
            int block_start = i + 1;
            int block_end = i;
            while (block_end + 1 <= end) {
                Line *next = &lines[block_end + 1];
                if (next->text[0] == '\0' || next->text[0] == '!') {
                    block_end++;
                    continue;
                }
                if (next->indent > line->indent) {
                    block_end++;
                } else {
                    break;
                }
            }

            const char *cursor = effective_text + 8;
            while (isspace((unsigned char)*cursor)) cursor++;
            const char *id_start = cursor;
            while (isalnum((unsigned char)*cursor) || *cursor == '_') cursor++;
            int id_len = cursor - id_start;
            if (id_len == 0) {
                throw_error("SyntaxError", "Expected identifier after iterate on line %d", line->line_num);
            }
            if (id_len >= 64) {
                id_len = 63;
            }
            char var_name[64];
            strncpy(var_name, id_start, id_len);
            var_name[id_len] = '\0';

            while (isspace((unsigned char)*cursor)) cursor++;
            int start_val = 0;
            if (strncmp(cursor, "from", 4) == 0 && isspace((unsigned char)cursor[4])) {
                cursor += 4;
                char *out_str = NULL;
                int out_type = VAR_INT;
                start_val = evaluate_expression(&cursor, &out_str, &out_type);
                if (out_str) {
                    untrack_alloc(out_str);
                    free(out_str);
                }
                if (out_type == VAR_STRING) {
                    throw_error("LoopIterationError", "Iterate start must be numeric on line %d", line->line_num);
                }
            } else {
                throw_error("SyntaxError", "Expected 'from' after identifier in iterate on line %d", line->line_num);
            }

            while (isspace((unsigned char)*cursor)) cursor++;
            int end_val = 0;
            if (strncmp(cursor, "to", 2) == 0 && isspace((unsigned char)cursor[2])) {
                cursor += 2;
                char *out_str = NULL;
                int out_type = VAR_INT;
                end_val = evaluate_expression(&cursor, &out_str, &out_type);
                if (out_str) {
                    untrack_alloc(out_str);
                    free(out_str);
                }
                if (out_type == VAR_STRING) {
                    throw_error("LoopIterationError", "Iterate end must be numeric on line %d", line->line_num);
                }
            } else {
                throw_error("SyntaxError", "Expected 'to' after from-value in iterate on line %d", line->line_num);
            }

            int step_val = 1;
            while (isspace((unsigned char)*cursor)) cursor++;
            if (strncmp(cursor, "step", 4) == 0 && (isspace((unsigned char)cursor[4]) || cursor[4] == '\0')) {
                cursor += 4;
                char *step_out_str = NULL;
                int step_out_type = VAR_INT;
                step_val = evaluate_expression(&cursor, &step_out_str, &step_out_type);
                if (step_out_str) {
                    untrack_alloc(step_out_str);
                    free(step_out_str);
                }
                if (step_out_type == VAR_STRING) {
                    throw_error("LoopStepError", "Loop step must be numeric on line %d", line->line_num);
                }
            }

            if (start_val > end_val) {
                throw_error("LoopDirectionError", "start index greater than end index on line %d", line->line_num);
            }

            if ((end_val - start_val + 1) > 0 && step_val > (end_val - start_val + 1)) {
                throw_error("LoopStepError", "Loop step larger than loop size on line %d", line->line_num);
            }
            if (step_val <= 0) {
                throw_error("LoopStepError", "Loop step must be greater than 0 on line %d", line->line_num);
            }

            Variable *v = set_var(var_name);
            for (int idx_val = start_val; idx_val <= end_val; idx_val += step_val) {
                if (current_reply.has_replied) break;
                v->type = VAR_INT;
                v->int_val = idx_val;
                if (v->string_val) {
                    free(v->string_val);
                    v->string_val = NULL;
                }
                execute_block(block_start, block_end);
                if (current_reply.has_replied) break;
                if (active_watch.active && active_watch.triggered) break;
            }
            i = block_end + 1;
        }
        else if (strncmp(effective_text, "if ", 3) == 0) {
            const char *cursor = effective_text + 3;
            const char *then_ptr = strstr(cursor, " then");
            if (!then_ptr) {
                throw_error("SyntaxError", "Expected 'then' after if condition on line %d", line->line_num);
            }
            int cond_len = then_ptr - cursor;
            char *cond_str = malloc(cond_len + 1);
            if (!cond_str) throw_error("MemoryAllocationError", "Memory allocation failed");
            track_alloc(cond_str);
            strncpy(cond_str, cursor, cond_len);
            cond_str[cond_len] = '\0';

            const char *cond_cursor = cond_str;
            char *out_str = NULL;
            int out_type = VAR_INT;
            int cond_val = evaluate_expression(&cond_cursor, &out_str, &out_type);
            if (out_str) {
                untrack_alloc(out_str);
                free(out_str);
                throw_error("InvalidOperandError", "Condition must evaluate to a boolean or number on line %d", line->line_num);
            }
            untrack_alloc(cond_str);
            free(cond_str);

            int block_start = i + 1;
            int block_end = i;
            while (block_end + 1 <= end) {
                Line *next = &lines[block_end + 1];
                if (next->text[0] == '\0' || next->text[0] == '!') {
                    block_end++;
                    continue;
                }
                if (next->indent > line->indent) {
                    block_end++;
                } else {
                    break;
                }
            }

            int executed = 0;
            if (cond_val) {
                execute_block(block_start, block_end);
                executed = 1;
            }

            int current_idx = block_end + 1;
            while (current_idx <= end) {
                Line *lookahead = &lines[current_idx];
                if (lookahead->text[0] == '\0' || lookahead->text[0] == '!') {
                    current_idx++;
                    continue;
                }
                if (lookahead->indent != line->indent) {
                    break;
                }

                char resolved_lookahead_buf[2048];
                const char *eff_lookahead = resolve_alias_line(lookahead->text, resolved_lookahead_buf, sizeof(resolved_lookahead_buf));

                if (strncmp(eff_lookahead, "else if ", 8) == 0) {
                    int elif_start = current_idx + 1;
                    int elif_end = current_idx;
                    while (elif_end + 1 <= end) {
                        Line *next = &lines[elif_end + 1];
                        if (next->text[0] == '\0' || next->text[0] == '!') {
                            elif_end++;
                            continue;
                        }
                        if (next->indent > lookahead->indent) {
                            elif_end++;
                        } else {
                            break;
                        }
                    }

                    if (!executed) {
                        const char *elif_cursor = eff_lookahead + 8;
                        const char *elif_then_ptr = strstr(elif_cursor, " then");
                        if (!elif_then_ptr) {
                            throw_error("SyntaxError", "Expected 'then' after else if condition on line %d", lookahead->line_num);
                        }
                        int elif_cond_len = elif_then_ptr - elif_cursor;
                        char *elif_cond_str = malloc(elif_cond_len + 1);
                        if (!elif_cond_str) throw_error("MemoryAllocationError", "Memory allocation failed");
                        track_alloc(elif_cond_str);
                        strncpy(elif_cond_str, elif_cursor, elif_cond_len);
                        elif_cond_str[elif_cond_len] = '\0';

                        const char *elif_cond_cursor = elif_cond_str;
                        char *elif_out_str = NULL;
                        int elif_out_type = VAR_INT;
                        int elif_cond_val = evaluate_expression(&elif_cond_cursor, &elif_out_str, &elif_out_type);
                        if (elif_out_str) {
                            untrack_alloc(elif_out_str);
                            free(elif_out_str);
                            throw_error("InvalidOperandError", "Condition must evaluate to a boolean or number on line %d", lookahead->line_num);
                        }
                        untrack_alloc(elif_cond_str);
                        free(elif_cond_str);

                        if (elif_cond_val) {
                            execute_block(elif_start, elif_end);
                            executed = 1;
                        }
                    }
                    current_idx = elif_end + 1;
                }
                else if (strncmp(eff_lookahead, "else ", 5) == 0 || strcmp(eff_lookahead, "else") == 0) {
                    int else_start = current_idx + 1;
                    int else_end = current_idx;
                    while (else_end + 1 <= end) {
                        Line *next = &lines[else_end + 1];
                        if (next->text[0] == '\0' || next->text[0] == '!') {
                            else_end++;
                            continue;
                        }
                        if (next->indent > lookahead->indent) {
                            else_end++;
                        } else {
                            break;
                        }
                    }

                    if (!executed) {
                        execute_block(else_start, else_end);
                        executed = 1;
                    }
                    current_idx = else_end + 1;
                }
                else {
                    break;
                }
            }
            if (active_watch.active && active_watch.triggered) {
                i = current_idx;
                break;
            }
            i = current_idx;
        }
        else if (strncmp(effective_text, "while ", 6) == 0) {
            int block_start = i + 1;
            int block_end = i;
            while (block_end + 1 <= end) {
                Line *next = &lines[block_end + 1];
                if (next->text[0] == '\0' || next->text[0] == '!') {
                    block_end++;
                    continue;
                }
                if (next->indent > line->indent) {
                    block_end++;
                } else {
                    break;
                }
            }

            int step_val = 1;
            const char *cursor = effective_text + 6;
            const char *step_ptr = strstr(cursor, " step");
            if (step_ptr) {
                if (isspace(*(step_ptr - 1)) && (isspace(*(step_ptr + 5)) || *(step_ptr + 5) == '\0')) {
                    const char *step_cursor = step_ptr + 6;
                    char *step_out_str = NULL;
                    int step_out_type = VAR_INT;
                    step_val = evaluate_expression(&step_cursor, &step_out_str, &step_out_type);
                    if (step_out_str || step_out_type == VAR_STRING) {
                        throw_error("LoopStepError", "Loop step must be numeric on line %d", line->line_num);
                    }
                    if (step_val <= 0) {
                        throw_error("LoopStepError", "Loop step must be greater than 0 on line %d", line->line_num);
                    }
                } else {
                    step_ptr = NULL;
                }
            }

            int cond_len = step_ptr ? (int)(step_ptr - cursor) : (int)strlen(cursor);
            char *cond_buf = malloc(cond_len + 1);
            if (!cond_buf) throw_error("MemoryAllocationError", "Memory allocation failed");
            track_alloc(cond_buf);
            strncpy(cond_buf, cursor, cond_len);
            cond_buf[cond_len] = '\0';

            int iter_count = 0;
            while (1) {
                const char *eval_cursor = cond_buf;
                char *out_str = NULL;
                int out_type = VAR_INT;
                int cond_val = evaluate_expression(&eval_cursor, &out_str, &out_type);
                if (out_str) {
                    untrack_alloc(out_str);
                    free(out_str);
                    untrack_alloc(cond_buf);
            free(cond_buf);
                    throw_error("InvalidOperandError", "While condition must evaluate to a boolean or number on line %d", line->line_num);
                }
                if (!cond_val) break;

                if (iter_count % step_val == 0) {
                    execute_block(block_start, block_end);
                    if (current_reply.has_replied) break;
                }
                iter_count++;
                if (active_watch.active && active_watch.triggered) break;
            }
            untrack_alloc(cond_buf);
            free(cond_buf);
            if (step_val > iter_count && iter_count > 0) {
                throw_error("LoopStepError", "Loop step larger than loop size on line %d", line->line_num);
            }
            i = block_end + 1;
        }
        else if (strncmp(effective_text, "until ", 6) == 0) {
            int block_start = i + 1;
            int block_end = i;
            while (block_end + 1 <= end) {
                Line *next = &lines[block_end + 1];
                if (next->text[0] == '\0' || next->text[0] == '!') {
                    block_end++;
                    continue;
                }
                if (next->indent > line->indent) {
                    block_end++;
                } else {
                    break;
                }
            }

            int step_val = 1;
            const char *cursor = effective_text + 6;
            const char *step_ptr = strstr(cursor, " step");
            if (step_ptr) {
                if (isspace(*(step_ptr - 1)) && (isspace(*(step_ptr + 5)) || *(step_ptr + 5) == '\0')) {
                    const char *step_cursor = step_ptr + 6;
                    char *step_out_str = NULL;
                    int step_out_type = VAR_INT;
                    step_val = evaluate_expression(&step_cursor, &step_out_str, &step_out_type);
                    if (step_out_str || step_out_type == VAR_STRING) {
                        throw_error("LoopStepError", "Loop step must be numeric on line %d", line->line_num);
                    }
                    if (step_val <= 0) {
                        throw_error("LoopStepError", "Loop step must be greater than 0 on line %d", line->line_num);
                    }
                } else {
                    step_ptr = NULL;
                }
            }

            int cond_len = step_ptr ? (int)(step_ptr - cursor) : (int)strlen(cursor);
            char *cond_buf = malloc(cond_len + 1);
            if (!cond_buf) throw_error("MemoryAllocationError", "Memory allocation failed");
            track_alloc(cond_buf);
            strncpy(cond_buf, cursor, cond_len);
            cond_buf[cond_len] = '\0';

            int iter_count = 0;
            while (1) {
                const char *eval_cursor = cond_buf;
                char *out_str = NULL;
                int out_type = VAR_INT;
                int cond_val = evaluate_expression(&eval_cursor, &out_str, &out_type);
                if (out_str) {
                    untrack_alloc(out_str);
                    free(out_str);
                    untrack_alloc(cond_buf);
            free(cond_buf);
                    throw_error("InvalidOperandError", "Until condition must evaluate to a boolean or number on line %d", line->line_num);
                }
                if (cond_val) break;

                if (iter_count % step_val == 0) {
                    execute_block(block_start, block_end);
                    if (current_reply.has_replied) break;
                }
                iter_count++;
                if (active_watch.active && active_watch.triggered) break;
            }
            untrack_alloc(cond_buf);
            free(cond_buf);
            if (step_val > iter_count && iter_count > 0) {
                throw_error("LoopStepError", "Loop step larger than loop size on line %d", line->line_num);
            }
            i = block_end + 1;
        }
        else if (strcmp(effective_text, "do") == 0) {
            int do_start = i + 1;
            int do_end = i;
            while (do_end + 1 <= end) {
                Line *next = &lines[do_end + 1];
                if (next->text[0] == '\0' || next->text[0] == '!') {
                    do_end++;
                    continue;
                }
                if (next->indent > line->indent) {
                    do_end++;
                } else {
                    break;
                }
            }

            int unless_idx = do_end + 1;
            while (unless_idx <= end) {
                Line *next = &lines[unless_idx];
                if (next->text[0] == '\0' || next->text[0] == '!') {
                    unless_idx++;
                    continue;
                }
                break;
            }

            if (unless_idx > end || lines[unless_idx].indent != line->indent || strncmp(lines[unless_idx].text, "unless ", 7) != 0) {
                throw_error("SyntaxError", "Expected 'unless' block matching 'do' on line %d", line->line_num);
            }

            Line *unless_line = &lines[unless_idx];
            const char *unless_cursor = unless_line->text + 7;

            // Parse mode
            int mode = MODE_DEFAULT;
            if (strncmp(unless_cursor, "internal ", 9) == 0) {
                mode = MODE_INTERNAL;
                unless_cursor += 9;
            } else if (strncmp(unless_cursor, "external ", 9) == 0) {
                mode = MODE_EXTERNAL;
                unless_cursor += 9;
            }

            const char *unless_expr = unless_cursor;

            int unless_start = unless_idx + 1;
            int unless_end = unless_idx;
            while (unless_end + 1 <= end) {
                Line *next = &lines[unless_end + 1];
                if (next->text[0] == '\0' || next->text[0] == '!') {
                    unless_end++;
                    continue;
                }
                if (next->indent > unless_line->indent) {
                    unless_end++;
                } else {
                    break;
                }
            }

            if (is_error_name(unless_expr)) {
                // If mode is default, errors default to internal
                if (mode == MODE_DEFAULT) {
                    mode = MODE_INTERNAL;
                }

                if (jmp_stack_ptr >= MAX_JMP_STACK) {
                    throw_error("SystemError", "Jump stack overflow on line %d", line->line_num);
                }

                int saved_scope = current_scope_depth;
                int saved_call_ptr = call_stack_ptr;

                if (setjmp(jmp_env_stack[jmp_stack_ptr++]) == 0) {
                    execute_block(do_start, do_end);
                    jmp_stack_ptr--;
                } else {
                    jmp_stack_ptr--;
                    pop_scope(saved_scope);
                    current_scope_depth = saved_scope;
                    call_stack_ptr = saved_call_ptr;

                    int catch_all = (strcmp(unless_expr, "error") == 0);
                    int catch_spec = (strcmp(unless_expr, current_error_name) == 0);
                    if (catch_all || catch_spec) {
                        Variable *err_var = set_var("error");
                        err_var->type = VAR_STRING;
                        if (err_var->string_val) free(err_var->string_val);
                        err_var->string_val = strdup(current_error_name);

                        execute_block(unless_start, unless_end);
                    } else {
                        throw_error(current_error_name, "%s", current_error_msg);
                    }
                }
            } else {
                // Bools default to external
                if (mode == MODE_DEFAULT) {
                    mode = MODE_EXTERNAL;
                }

                if (mode == MODE_EXTERNAL) {
                    // Evaluate condition beforehand
                    const char *expr_cursor = unless_expr;
                    char *out_str = NULL;
                    int out_type = VAR_INT;
                    int cond_val = evaluate_expression(&expr_cursor, &out_str, &out_type);
                    if (out_str) {
                        untrack_alloc(out_str);
                        free(out_str);
                        throw_error("InvalidOperandError", "Condition must evaluate to a boolean or number on line %d", unless_line->line_num);
                    }
                    if (cond_val) {
                        execute_block(unless_start, unless_end);
                    } else {
                        execute_block(do_start, do_end);
                    }
                } else {
                    // Mode is internal: checks before executing, and then after every line
                    const char *expr_cursor = unless_expr;
                    char *out_str = NULL;
                    int out_type = VAR_INT;
                    int start_val = evaluate_expression(&expr_cursor, &out_str, &out_type);
                    if (out_str) {
                        untrack_alloc(out_str);
                        free(out_str);
                        throw_error("InvalidOperandError", "Condition must evaluate to a boolean or number on line %d", unless_line->line_num);
                    }

                    if (start_val) {
                        execute_block(unless_start, unless_end);
                    } else {
                        WatchCondition prev_watch = active_watch;
                        active_watch.active = 1;
                        active_watch.expr = unless_expr;
                        active_watch.triggered = 0;

                        execute_block(do_start, do_end);

                        int triggered = active_watch.triggered;
                        active_watch = prev_watch;

                        if (triggered) {
                            execute_block(unless_start, unless_end);
                        }
                    }
                }
            }

            i = unless_end + 1;
        }
        else if (strcmp(effective_text, "ForceErrors") == 0) {
            int block_start = i + 1;
            int block_end = i;
            while (block_end + 1 <= end) {
                Line *next = &lines[block_end + 1];
                if (next->text[0] == '\0' || next->text[0] == '!') {
                    block_end++;
                    continue;
                }
                if (next->indent > line->indent) {
                    block_end++;
                } else {
                    break;
                }
            }
            int prev_mode = error_mode;
            error_mode = ERR_MODE_FORCE;
            execute_block(block_start, block_end);
            error_mode = prev_mode;
            i = block_end + 1;
        }
        else if (strcmp(effective_text, "CriticalErrors") == 0) {
            int block_start = i + 1;
            int block_end = i;
            while (block_end + 1 <= end) {
                Line *next = &lines[block_end + 1];
                if (next->text[0] == '\0' || next->text[0] == '!') {
                    block_end++;
                    continue;
                }
                if (next->indent > line->indent) {
                    block_end++;
                } else {
                    break;
                }
            }
            int prev_mode = error_mode;
            error_mode = ERR_MODE_CRITICAL;
            execute_block(block_start, block_end);
            error_mode = prev_mode;
            i = block_end + 1;
        }
        else if (strncmp(effective_text, "inject", 6) == 0 && (effective_text[6] == ' ' || effective_text[6] == '\0')) {
            const char *lang_ptr = effective_text + 6;
            while (*lang_ptr == ' ') lang_ptr++;
            char lang[64] = "verscript";
            if (*lang_ptr != '\0') {
                int l_idx = 0;
                while (*lang_ptr != '\0' && *lang_ptr != ' ' && *lang_ptr != '?' && l_idx < 63) {
                    lang[l_idx++] = tolower((unsigned char)*lang_ptr++);
                }
                lang[l_idx] = '\0';
            }

            int block_start = i + 1;
            int block_end = i;
            while (block_end + 1 <= end) {
                Line *next = &lines[block_end + 1];
                if (next->text[0] == '\0' || next->text[0] == '!') {
                    block_end++;
                    continue;
                }
                if (next->indent > line->indent) {
                    block_end++;
                } else {
                    break;
                }
            }

            char injected_code[4096] = "";
            int pos = 0;
            int base_indent = (block_start <= block_end) ? lines[block_start].indent : 0;
            for (int k = block_start; k <= block_end; k++) {
                if (lines[k].text[0] == '\0' || lines[k].text[0] == '!') continue;
                int spaces_to_keep = lines[k].indent - base_indent;
                for (int s = 0; s < spaces_to_keep && pos < 4090; s++) injected_code[pos++] = ' ';
                int line_len = strlen(lines[k].text);
                if (pos + line_len + 1 < 4090) {
                    memmove(injected_code + pos, lines[k].text, line_len + 1);
                    pos += line_len;
                    injected_code[pos++] = '\n';
                }
            }
            injected_code[pos] = '\0';

            if (strcmp(lang, "verscript") == 0 || strcmp(lang, "vrs") == 0 || strcmp(lang, "eval") == 0) {
                int prev_line_count = line_count;
                parse_lines(injected_code);
                if (line_count > prev_line_count) {
                    execute_block(prev_line_count, line_count - 1);
                    for (int idx = prev_line_count; idx < line_count; idx++) {
                        if (lines[idx].text) free(lines[idx].text);
                    }
                    line_count = prev_line_count;
                }
            } else {
                char col[64] = "";
                get_attribute_str(effective_text, "color", col, sizeof(col));
                if (col[0] != '\0') {
                    apply_color(col);
                }
                int num_lines = 0;
                for (int k = block_start; k <= block_end; k++) {
                    if (lines[k].text[0] != '\0' && lines[k].text[0] != '!') num_lines++;
                }
                printf("[Inject:%s] Evaluated %d code line(s) cleanly.\n", lang, num_lines);
                if (col[0] != '\0') printf("\033[0m");
            }

            i = block_end + 1;
        }
        else if (strcmp(effective_text, "SuppressErrors") == 0) {
            int block_start = i + 1;
            int block_end = i;
            while (block_end + 1 <= end) {
                Line *next = &lines[block_end + 1];
                if (next->text[0] == '\0' || next->text[0] == '!') {
                    block_end++;
                    continue;
                }
                if (next->indent > line->indent) {
                    block_end++;
                } else {
                    break;
                }
            }
            int prev_mode = error_mode;
            error_mode = ERR_MODE_SUPPRESS;
            execute_block(block_start, block_end);
            error_mode = prev_mode;
            i = block_end + 1;
        }
        else {
            execute_line(effective_text, line->line_num);
            if (current_reply.has_replied) {
                break;
            }
            if (active_watch.active) {
                const char *cursor = active_watch.expr;
                char *out_str = NULL;
                int out_type = VAR_INT;
                int val = evaluate_expression(&cursor, &out_str, &out_type);
                if (out_str) {
                    untrack_alloc(out_str);
                    free(out_str);
                    throw_error("InvalidOperandError", "Watch condition must evaluate to a boolean or number on line %d", line->line_num);
                }
                if (val) {
                    active_watch.triggered = 1;
                    break;
                }
            }
            i++;
        }
    } else {
        // Suppressed error occurred. Skip statement or block.
        int block_end = i;
        if (strncmp(effective_text, "DataType ", 9) == 0 ||
            strncmp(effective_text, "def ", 4) == 0 ||
            strncmp(effective_text, "loop ", 5) == 0 || strcmp(effective_text, "loop") == 0 ||
            strncmp(effective_text, "iterate ", 8) == 0 ||
            strncmp(effective_text, "if ", 3) == 0 ||
            strncmp(effective_text, "while ", 6) == 0 ||
            strncmp(effective_text, "until ", 6) == 0 ||
            strcmp(effective_text, "do") == 0 ||
            strcmp(effective_text, "ForceErrors") == 0 ||
            strcmp(effective_text, "CriticalErrors") == 0 ||
            strcmp(effective_text, "SuppressErrors") == 0) {

            while (block_end + 1 <= end) {
                Line *next = &lines[block_end + 1];
                if (next->text[0] == '\0' || next->text[0] == '!') {
                    block_end++;
                    continue;
                }
                if (next->indent > line->indent) {
                    block_end++;
                } else {
                    break;
                }
            }

            if (strcmp(line->text, "do") == 0) {
                int unless_idx = block_end + 1;
                while (unless_idx <= end) {
                    Line *next = &lines[unless_idx];
                    if (next->text[0] == '\0' || next->text[0] == '!') {
                        unless_idx++;
                        continue;
                    }
                    break;
                }
                if (unless_idx <= end && lines[unless_idx].indent == line->indent && strncmp(lines[unless_idx].text, "unless ", 7) == 0) {
                    int unless_end = unless_idx;
                    while (unless_end + 1 <= end) {
                        Line *next = &lines[unless_end + 1];
                        if (next->text[0] == '\0' || next->text[0] == '!') {
                            unless_end++;
                            continue;
                        }
                        if (next->indent > lines[unless_idx].indent) {
                            unless_end++;
                        } else {
                            break;
                        }
                    }
                    block_end = unless_end;
                }
            }
            i = block_end + 1;
        } else {
            i++;
        }
    }

    suppress_jmp_active = prev_suppress_active;
    memcpy(suppress_jmp_env, prev_suppress_env, sizeof(jmp_buf));
    }
}

static char loaded_libraries[64][128];
static int loaded_library_count = 0;

int is_library_loaded(const char *name) {
    for (int i = 0; i < loaded_library_count; i++) {
        if (strcmp(loaded_libraries[i], name) == 0) return 1;
    }
    return 0;
}

void mark_library_loaded(const char *name) {
    if (loaded_library_count < 64) {
        strncpy(loaded_libraries[loaded_library_count++], name, 127);
    }
}

void load_library_source(const char *source, const char *origin_name, int line_num) {
    (void)origin_name;
    (void)line_num;
    int start_idx = line_count;
    parse_lines(source);
    if (line_count > start_idx) {
        execute_block(start_idx, line_count - 1);
    }
}

void load_library(const char *target, int line_num) {
    if (!target || target[0] == '\0') {
        throw_error("FileError", "Invalid library name on line %d", line_num);
    }
    if (is_library_loaded(target)) {
        return;
    }

    const char *embedded = get_embedded_core_lib(target);

    char candidates[4][256];
    int cand_count = 0;

    snprintf(candidates[cand_count++], sizeof(candidates[0]), "%s", target);
    snprintf(candidates[cand_count++], sizeof(candidates[0]), "%s.lib.vrs", target);
    snprintf(candidates[cand_count++], sizeof(candidates[0]), "core_libs/%s.lib.vrs", target);
    snprintf(candidates[cand_count++], sizeof(candidates[0]), "%s.vrs", target);

    FILE *f = NULL;
    for (int i = 0; i < cand_count; i++) {
        f = fopen(candidates[i], "rb");
        if (f) break;
    }

    if (f) {
        fseek(f, 0, SEEK_END);
        long len = ftell(f);
        if (len < 0) len = 0;
        fseek(f, 0, SEEK_SET);
        char *buf = malloc(len + 1);
        if (!buf) {
            fclose(f);
            throw_error("MemoryAllocationError", "Memory allocation failed loading library '%s' on line %d", target, line_num);
        }
        size_t read_bytes = fread(buf, 1, len, f);
        buf[read_bytes] = '\0';
        fclose(f);

        load_library_source(buf, target, line_num);
        free(buf);
        mark_library_loaded(target);
        return;
    }

    if (embedded) {
        load_library_source(embedded, target, line_num);
        mark_library_loaded(target);
        return;
    }

    throw_error("FileError", "Cannot load library '%s': file not found on line %d", target, line_num);
}

void init_math_constants(void) {
    Variable *v;
    v = set_var("pi"); v->type = VAR_INT; v->int_val = 3;
    v = set_var("tau"); v->type = VAR_INT; v->int_val = 6;
    v = set_var("phi"); v->type = VAR_INT; v->int_val = 16180;
    v = set_var("e"); v->type = VAR_INT; v->int_val = 2;
    v = set_var("inf"); v->type = VAR_INT; v->int_val = 2147483647;
    v = set_var("infinity"); v->type = VAR_INT; v->int_val = 2147483647;
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        printf("Usage: %s <filename>\n", argv[0]);
        return 1;
    }

    FILE *file = fopen(argv[1], "rb");
    if (!file) {
        printf("ERROR: Could not open file %s\n", argv[1]);
        return 1;
    }
    fseek(file, 0, SEEK_END);
    long length = ftell(file);
    if (length < 0) {
        printf("ERROR: Could not determine file size\n");
        fclose(file);
        return 1;
    }
    fseek(file, 0, SEEK_SET);
    source_buffer = malloc(length + 1);
    if (!source_buffer) {
        fclose(file);
        return 1;
    }
    size_t read_bytes = fread(source_buffer, 1, length, file);
    source_buffer[read_bytes] = '\0';
    fclose(file);

    init_math_constants();
    parse_lines(source_buffer);

    if (line_count > 0) {
        execute_block(0, line_count - 1);
    }

    free_globals();

    return 0;
}
