// apachejuice, 27.02.2024
// See LICENSE for details.
#include "compiler.h"
#include "chunk.h"
#include "common.h"
#include "debug.h"
#include "memory.h"
#include "scanner.h"
#include "obj.h"

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct {
    token current;
    token previous;
    bool  had_error;
    bool  panic_mode;
} parser;
chunk *cur_chunk;

typedef enum {
    PREC_NONE,
    PREC_ASG,
    PREC_OR,
    PREC_AND,
    PREC_EQ,
    PREC_COMP,
    PREC_TERM,
    PREC_FACTOR,
    PREC_UNARY,
    PREC_CALL,
    PREC_PRIMARY,
} precedence;

typedef void (*parse_fn) (bool can_assign);

typedef struct {
    parse_fn   prefix;
    parse_fn   infix;
    precedence prec;
} parse_rule;

typedef struct {
    token name;
    int32 depth;
    bool  captured;
} localvar;

typedef struct {
    uint8 index;
    bool  is_local;
} upvalue;

#define LOCALS_MAX UINT8_COUNT

typedef enum {
    FTYPE_FUNC,
    FTYPE_INITIALIZER,
    FTYPE_METHOD,
    FTYPE_SCRIPT,
} func_type;

typedef struct _comp {
    struct _comp *enclosing;

    obj_func *func;
    func_type ftype;

    localvar locals[LOCALS_MAX];
    int32    local_count;
    upvalue  upvalues[UINT8_COUNT];
    int32    scope_depth;
} compiler_t;

typedef struct _classcomp {
    struct _classcomp *enclosing;
} class_compiler;

compiler_t     *current       = NULL;
class_compiler *current_class = NULL;

static void        expression (void);
static void        statement (void);
static void        declaration (void);
static void        parse_precedence (precedence prec);
static parse_rule *get_rule (token_type type);
static uint8       identifier_constant (token *name);
static int32       resolve_local (compiler_t *compiler, token *name);
static int32       resolve_upvalue (compiler_t *compiler, token *name);

static chunk *current_chunk (void) {
    return &current->func->chk;
}

static void error_at (token *tok, const char *msg, va_list ap) {
    if (parser.panic_mode) return;

    parser.panic_mode = true;
    fprintf (stderr, "[%zu:%zu] Error", tok->line, tok->column);

    if (tok->type == TOKEN_EOF) {
        fprintf (stderr, " at end");
    } else if (tok->type != TOKEN_ERROR) {
        fprintf (stderr, " at '%.*s'", (int) tok->len, tok->start);
    }

    fprintf (stderr, ": ");
    vfprintf (stderr, msg, ap);
    fprintf (stderr, "\n");

    parser.had_error = true;
}

static void error_at_current (const char *msg, ...) {
    va_list ap;
    va_start (ap, msg);

    error_at (&parser.current, msg, ap);
    va_end (ap);
}

static void error (const char *msg, ...) {
    va_list ap;
    va_start (ap, msg);

    error_at (&parser.previous, msg, ap);
    va_end (ap);
}

static void advance (void) {
    parser.previous = parser.current;

    while (1) {
        parser.current = scan_token ();
        if (parser.current.type != TOKEN_ERROR) break;

        error_at_current (parser.current.start);

        // Error tokens require freeing the lexeme.
        FREE (char, (char *) parser.current.start);
    }
}

static void consumev (token_type type, const char *msg, va_list ap) {
    if (parser.current.type == type) {
        advance ();
        return;
    }

    error_at (&parser.current, msg, ap);
}

static void consume (token_type type, const char *msg, ...) {
    va_list ap;
    va_start (ap, msg);
    consumev (type, msg, ap);
    va_end (ap);
}

static bool check (token_type type) {
    return parser.current.type == type;
}

static bool match (token_type type) {
    if (!check (type)) return false;

    advance ();
    return true;
}

static void emit_byte (uint8 byte) {
    write_chunk (current_chunk (), byte, parser.previous.line);
}

static inline void emit_bytes (uint8 b1, uint8 b2) {
    emit_byte (b1);
    emit_byte (b2);
}

static void emit_loop (int32 loop_start) {
    emit_byte (OP_LOOP);

    int32 offset = current_chunk ()->count - loop_start + 2;
    if (offset > UINT16_MAX)
        error ("Loop body too large! Maximum %d ops\n", UINT16_MAX);

    emit_byte ((offset >> 8) & 0xff);
    emit_byte (offset & 0xff);
}

static int32 emit_jump (uint8 instruction) {
    emit_byte (instruction);
    emit_bytes (0xff, 0xff);
    return current_chunk ()->count - 2;
}

static void implicit_return (void) {
    if (current->ftype == FTYPE_INITIALIZER) {
        emit_bytes (OP_GET_LOCAL, 0);
    } else {
        emit_byte (OP_NIL);
    }

    emit_byte (OP_NIL);
    emit_byte (OP_RETURN);
}

static uint8 make_constant (value val) {
    int32 constant = add_constant (current_chunk (), val);
    if (constant > UINT8_MAX) {
        error ("Too many constants in one chunk! Maximum %d\n", UINT8_MAX);
        return 0;
    }

    return (uint8) constant;
}

static void emit_constant (value val) {
    emit_bytes (OP_CONSTANT, make_constant (val));
}

static void patch_jump (int32 offset) {
    int32 jump = current_chunk ()->count - offset - 2;

    if (jump > UINT16_MAX) {
        error ("Too much code to jump over! offset = %d", jump);
    }

    current_chunk ()->code[offset]     = (jump >> 8) & 0xff;
    current_chunk ()->code[offset + 1] = jump & 0xff;
}

static void init_compiler (compiler_t *compiler, func_type ftype) {
    compiler->enclosing   = current;
    compiler->func        = NULL;
    compiler->local_count = 0;
    compiler->scope_depth = 0;
    compiler->ftype       = ftype;
    compiler->func        = new_func ();
    current               = compiler;

    if (ftype != FTYPE_SCRIPT) {
        current->func->name =
            copy_string (parser.previous.start, parser.previous.len);
    }

    localvar *local = &current->locals[current->local_count++];
    local->depth    = 0;
    local->captured = false;

    if (ftype != FTYPE_FUNC) {
        printf ("init_compiler: ftype %d\n", ftype);
        local->name.start = "this";
        local->name.len   = 4;
    } else {
        local->name.start = "";
        local->name.len   = 0;
    }
}

static obj_func *end_compiler (void) {
    implicit_return ();
    obj_func *func = current->func;

#ifdef DEBUG_PRINT_CODE
    if (!parser.had_error) {
        disassemble_chunk (current_chunk (),
                           func->name != NULL ? func->name->data : "<script>");
    }
#endif

    current = current->enclosing;
    return func;
}

static void begin_scope (void) {
    current->scope_depth++;
}

static void end_scope (void) {
    current->scope_depth--;

    while (current->local_count > 0 &&
           current->locals[current->local_count - 1].depth >
               current->scope_depth) {
        if (current->locals[current->local_count - 1].captured) {
            emit_byte (OP_CLOSE_UPVALUE);
        } else {
            emit_byte (OP_POP);
        }
        current->local_count--;
    }
}

static void binary (bool can_assign) {
    token_type  op_type = parser.previous.type;
    parse_rule *rule    = get_rule (op_type);
    parse_precedence (rule->prec + 1);

    switch (op_type) {
        case TOKEN_BANG_EQUAL: emit_bytes (OP_EQUAL, OP_NOT); break;
        case TOKEN_EQUAL_EQUAL: emit_byte (OP_EQUAL); break;
        case TOKEN_GREATER: emit_byte (OP_GREATER); break;
        case TOKEN_GREATER_EQUAL: emit_bytes (OP_LESS, OP_NOT); break;
        case TOKEN_LESS: emit_byte (OP_LESS); break;
        case TOKEN_LESS_EQUAL: emit_bytes (OP_GREATER, OP_NOT); break;
        case TOKEN_PLUS: emit_byte (OP_ADD); break;
        case TOKEN_MINUS: emit_byte (OP_SUBTRACT); break;
        case TOKEN_STAR: emit_byte (OP_MULTIPLY); break;
        case TOKEN_SLASH: emit_byte (OP_DIVIDE); break;

        default: return;
    }
}

static uint8 argument_list (void) {
    uint8 argc = 0;
    if (!check (TOKEN_RIGHT_PAREN)) {
        do {
            expression ();
            if (argc == UINT8_MAX) {
                error ("Function cannot have more than 255 arguments");
            }

            argc++;
        } while (match (TOKEN_COMMA));
    }

    consume (TOKEN_RIGHT_PAREN, "Expected ')' after argument list");
    return argc;
}

static void call (bool can_assign) {
    uint8 argc = argument_list ();
    emit_bytes (OP_CALL, argc);
}

static void dot (bool can_assign) {
    consume (TOKEN_IDENTIFIER, "Expected property name to follow `.`");
    uint8 name = identifier_constant (&parser.previous);

    if (can_assign && match (TOKEN_EQUAL)) {
        expression ();
        emit_bytes (OP_SET_PROPERTY, name);
    } else {
        emit_bytes (OP_GET_PROPERTY, name);
    }
}

static void literal (bool can_assign) {
    switch (parser.previous.type) {
        case TOKEN_FALSE: emit_byte (OP_FALSE); break;
        case TOKEN_NIL: emit_byte (OP_NIL); break;
        case TOKEN_TRUE: emit_byte (OP_TRUE); break;

        default: return;
    }
}

static void grouping (bool can_assign) {
    expression ();
    consume (TOKEN_RIGHT_PAREN, "Expected ')' to end parentheses");
}

static void number (bool can_assign) {
    double value = strtod (parser.previous.start, NULL);
    emit_constant (NUMBER_VAL (value));
}

static void and_ (bool can_assign) {
    int32 end_jump = emit_jump (OP_JUMP_IF_FALSE);

    emit_byte (OP_POP);
    parse_precedence (PREC_AND);

    patch_jump (end_jump);
}

static void or_ (bool can_assign) {
    int32 else_jump = emit_jump (OP_JUMP_IF_FALSE);
    int32 end_jump  = emit_jump (OP_JUMP);

    patch_jump (else_jump);
    emit_byte (OP_POP);

    parse_precedence (PREC_OR);
    patch_jump (end_jump);
}

static void string (bool can_assign) {
    emit_constant (OBJ_VAL ((obj *) copy_string (parser.previous.start + 1,
                                                 parser.previous.len - 1)));
}

static void named_variable (token name, bool can_assign) {
    uint8 get_op, set_op;
    int32 arg = resolve_local (current, &name);
    if (arg != -1) {
        get_op = OP_GET_LOCAL;
        set_op = OP_SET_LOCAL;
    } else if ((arg = resolve_upvalue (current, &name)) != -1) {
        get_op = OP_GET_UPVALUE;
        set_op = OP_SET_UPVALUE;
    } else {
        arg    = identifier_constant (&name);
        get_op = OP_GET_GLOBAL;
        set_op = OP_SET_GLOBAL;
    }

    if (can_assign && match (TOKEN_EQUAL)) {
        expression ();
        emit_bytes (set_op, (uint8) arg);
    } else {
        emit_bytes (get_op, (uint8) arg);
    }
}

static void variable (bool can_assign) {
    named_variable (parser.previous, can_assign);
}

static void this_ (bool can_assign) {
    if (!current_class) {
        error ("`this` reference outside of class body");
        return;
    }

    variable (false);
}

static void unary (bool can_assign) {
    token_type op_type = parser.previous.type;

    parse_precedence (PREC_UNARY);
    switch (op_type) {
        case TOKEN_BANG: emit_byte (OP_NOT); break;
        case TOKEN_MINUS: emit_byte (OP_NEGATE); break;
        default: return;
    }
}

parse_rule rules[] = {
    [TOKEN_AND]           = {NULL, and_, PREC_AND},
    [TOKEN_BANG_EQUAL]    = {NULL, binary, PREC_EQ},
    [TOKEN_BANG]          = {unary, NULL, PREC_NONE},
    [TOKEN_CLASS]         = {NULL, NULL, PREC_NONE},
    [TOKEN_COMMA]         = {NULL, NULL, PREC_NONE},
    [TOKEN_DOT]           = {NULL, dot, PREC_CALL},
    [TOKEN_ELSE]          = {NULL, NULL, PREC_NONE},
    [TOKEN_EOF]           = {NULL, NULL, PREC_NONE},
    [TOKEN_EQUAL_EQUAL]   = {NULL, binary, PREC_EQ},
    [TOKEN_EQUAL]         = {NULL, NULL, PREC_NONE},
    [TOKEN_ERROR]         = {NULL, NULL, PREC_NONE},
    [TOKEN_FALSE]         = {literal, NULL, PREC_NONE},
    [TOKEN_FOR]           = {NULL, NULL, PREC_NONE},
    [TOKEN_FUN]           = {NULL, NULL, PREC_NONE},
    [TOKEN_GREATER_EQUAL] = {NULL, binary, PREC_COMP},
    [TOKEN_GREATER]       = {NULL, binary, PREC_COMP},
    [TOKEN_IDENTIFIER]    = {variable, NULL, PREC_NONE},
    [TOKEN_IF]            = {NULL, NULL, PREC_NONE},
    [TOKEN_LEFT_BRACE]    = {NULL, NULL, PREC_NONE},
    [TOKEN_LEFT_PAREN]    = {grouping, call, PREC_CALL},
    [TOKEN_LESS_EQUAL]    = {NULL, binary, PREC_COMP},
    [TOKEN_LESS]          = {NULL, binary, PREC_COMP},
    [TOKEN_MINUS]         = {unary, binary, PREC_TERM},
    [TOKEN_NIL]           = {literal, NULL, PREC_NONE},
    [TOKEN_NUMBER]        = {number, NULL, PREC_NONE},
    [TOKEN_OR]            = {NULL, or_, PREC_OR},
    [TOKEN_PLUS]          = {NULL, binary, PREC_TERM},
    [TOKEN_PRINT]         = {NULL, NULL, PREC_NONE},
    [TOKEN_RETURN]        = {NULL, NULL, PREC_NONE},
    [TOKEN_RIGHT_BRACE]   = {NULL, NULL, PREC_NONE},
    [TOKEN_RIGHT_PAREN]   = {NULL, NULL, PREC_NONE},
    [TOKEN_SEMICOLON]     = {NULL, NULL, PREC_NONE},
    [TOKEN_SLASH]         = {NULL, binary, PREC_FACTOR},
    [TOKEN_STAR]          = {NULL, binary, PREC_FACTOR},
    [TOKEN_STRING]        = {string, NULL, PREC_NONE},
    [TOKEN_SUPER]         = {NULL, NULL, PREC_NONE},
    [TOKEN_THIS]          = {this_, NULL, PREC_NONE},
    [TOKEN_TRUE]          = {literal, NULL, PREC_NONE},
    [TOKEN_VAR]           = {NULL, NULL, PREC_NONE},
    [TOKEN_WHILE]         = {NULL, NULL, PREC_NONE},
};

static void parse_precedence (precedence prec) {
    advance ();
    parse_fn prefix_rule = get_rule (parser.previous.type)->prefix;
    if (prefix_rule == NULL) {
        error ("Expected expression");
        return;
    }

    bool can_assign = prec <= PREC_ASG;
    prefix_rule (can_assign);

    while (prec <= get_rule (parser.current.type)->prec) {
        advance ();
        parse_fn infix_rule = get_rule (parser.previous.type)->infix;
        infix_rule (can_assign);
    }

    if (can_assign && match (TOKEN_EQUAL)) {
        error ("Invalid assignment target");
    }
}

static uint8 identifier_constant (token *name) {
    return make_constant (
        OBJ_VAL ((obj *) copy_string (name->start, name->len)));
}

static bool identifiers_equal (token *a, token *b) {
    if (a->len != b->len) return false;
    return memcmp (a->start, b->start, a->len) == 0;
}

static int32 resolve_local (compiler_t *compiler, token *name) {
    for (int32 i = compiler->local_count - 1; i >= 0; i--) {
        localvar *local = &compiler->locals[i];
        printf ("local: %.*s\n", (int) local->name.len, local->name.start);
        if (identifiers_equal (name, &local->name)) {
            if (local->depth == -1) {
                error ("Self-referencing local variable '%.*s' in initializer",
                       local->name.len, local->name.start);
            }
            return i;
        }
    }

    return -1;
}

static int32 add_upvalue (compiler_t *compiler, uint8 index, bool is_local) {
    int32 upvalue_count = compiler->func->upvalue_count;

    for (int32 i = 0; i < upvalue_count; i++) {
        upvalue *upval = &compiler->upvalues[i];
        if (upval->index == index && upval->is_local == is_local) {
            return i;
        }
    }

    if (upvalue_count == UINT8_COUNT) {
        error ("Too many captured variables in closure");
        return 0;
    }

    compiler->upvalues[upvalue_count].is_local = is_local;
    compiler->upvalues[upvalue_count].index    = index;

    return compiler->func->upvalue_count++;
}

static int32 resolve_upvalue (compiler_t *compiler, token *name) {
    if (compiler->enclosing == NULL) return -1;

    int32 local = resolve_local (compiler->enclosing, name);
    if (local != -1) {
        compiler->enclosing->locals[local].captured = true;
        return add_upvalue (compiler, (uint8) local, true);
    }

    int32 upvalue = resolve_upvalue (compiler->enclosing, name);
    if (upvalue != -1) {
        return add_upvalue (compiler, (uint8) upvalue, false);
    }

    return -1;
}

static void add_local (token name) {
    if (current->local_count == LOCALS_MAX) {
        error ("Too many local variables in function, limit %d", LOCALS_MAX);
        return;
    }

    localvar *local = &current->locals[current->local_count++];
    local->name     = name;
    local->depth    = -1;
    local->captured = false;
}

static void declare_variable (void) {
    if (current->scope_depth == 0) return;

    token *name = &parser.previous;
    for (int32 i = current->local_count - 1; i >= 0; i--) {
        localvar *local = &current->locals[i];
        if (local->depth != -1 && local->depth < current->scope_depth) {
            break;
        }

        if (identifiers_equal (name, &local->name)) {
            error ("Redeclaration of variable '%.*s'", name->len, name->start);
        }
    }

    add_local (*name);
}

static uint8 parse_variable (const char *errmsg, ...) {
    va_list ap;
    va_start (ap, errmsg);

    consumev (TOKEN_IDENTIFIER, errmsg, ap);
    va_end (ap);

    declare_variable ();
    if (current->scope_depth > 0) return 0;

    return identifier_constant (&parser.previous);
}

static void mark_initialized (void) {
    if (current->scope_depth == 0) return;

    current->locals[current->local_count - 1].depth = current->scope_depth;
}

static void define_variable (uint8 global) {
    if (current->scope_depth > 0) {
        mark_initialized ();
        return;
    }

    emit_bytes (OP_DEFINE_GLOBAL, global);
}

static parse_rule *get_rule (token_type type) {
    return &rules[type];
}

static void expression (void) {
    parse_precedence (PREC_ASG);
}

static void block (void) {
    while (!check (TOKEN_RIGHT_BRACE) && !check (TOKEN_EOF)) {
        declaration ();
    }

    consume (TOKEN_RIGHT_BRACE, "Expected '}' to end a block");
}

static void function (func_type type) {
    compiler_t compiler;
    init_compiler (&compiler, type);
    begin_scope ();

    consume (TOKEN_LEFT_PAREN, "Expected '(' after a function name");
    if (!check (TOKEN_RIGHT_PAREN)) {
        do {
            current->func->arity++;
            if (current->func->arity > UINT8_MAX) {
                error_at_current (
                    "Function %s cannot have more than 255 parameters",
                    current->func->name->data);
            }

            uint8 constant = parse_variable ("Expected parameter name");
            define_variable (constant);
        } while (match (TOKEN_COMMA));
    }

    consume (TOKEN_RIGHT_PAREN, "Expected ')' to end a parameter list");
    consume (TOKEN_LEFT_BRACE, "Expected '{' for a function body");

    block ();
    obj_func *func = end_compiler ();
    emit_bytes (OP_CLOSURE, make_constant (OBJ_VAL ((obj *) func)));

    for (int32 i = 0; i < func->upvalue_count; i++) {
        emit_byte (compiler.upvalues[i].is_local ? 1 : 0);
        emit_byte (compiler.upvalues[i].index);
    }
}

static void method (void) {
    consume (TOKEN_IDENTIFIER, "Expected method name");
    uint8 constant = identifier_constant (&parser.previous);

    func_type ftype = FTYPE_METHOD;
    if (parser.previous.len == 4 &&
        memcmp (parser.previous.start, "init", 4) == 0) {
        ftype = FTYPE_INITIALIZER;
    }

    function (ftype);
    emit_bytes (OP_METHOD, constant);
}

static void class_declaration (void) {
    consume (TOKEN_IDENTIFIER, "Expected class name");
    token class_name = parser.previous;
    uint8 name_const = identifier_constant (&parser.previous);
    declare_variable ();

    emit_bytes (OP_CLASS, name_const);
    define_variable (name_const);

    class_compiler class_comp;
    class_comp.enclosing = current_class;
    current_class        = &class_comp;

    named_variable (class_name, false);

    consume (TOKEN_LEFT_BRACE, "Expected '{' before class body");
    while (!check (TOKEN_RIGHT_BRACE) && !check (TOKEN_EOF)) {
        method ();
    }

    consume (TOKEN_RIGHT_BRACE, "Expected '}' to end class body");
    emit_byte (OP_POP);

    current_class = current_class->enclosing;
}

static void fun_declaration (void) {
    uint8 global = parse_variable ("Expected function name");
    mark_initialized ();
    function (FTYPE_FUNC);
    define_variable (global);
}

static void var_declaration (void) {
    uint8 global = parse_variable ("Expected variable name");

    if (match (TOKEN_EQUAL)) {
        expression ();
    } else {
        emit_byte (OP_NIL);
    }

    consume (TOKEN_SEMICOLON, "Expected ';' to end a statement");
    define_variable (global);
}

static void expression_statement (void) {
    expression ();
    consume (TOKEN_SEMICOLON, "Expected ';' to end a statement");
    emit_byte (OP_POP);
}

static void if_statement (void) {
    consume (TOKEN_LEFT_PAREN, "Expected '(' after `if`");
    expression ();
    consume (TOKEN_RIGHT_PAREN, "Expected ')' to end if condition");

    int32 then_jump = emit_jump (OP_JUMP_IF_FALSE);
    emit_byte (OP_POP);
    statement ();

    int32 else_jump = emit_jump (OP_JUMP);
    patch_jump (then_jump);
    emit_byte (OP_POP);

    if (match (TOKEN_ELSE)) statement ();
    patch_jump (else_jump);
}

static void print_statement (void) {
    expression ();
    consume (TOKEN_SEMICOLON, "Expected ';' to end a statement");
    emit_byte (OP_PRINT);
}

static void return_statement (void) {
    if (current->ftype == FTYPE_SCRIPT) {
        error ("Return outside of function");
    }

    if (match (TOKEN_SEMICOLON)) {
        implicit_return ();
    } else {
        if (current->ftype == FTYPE_INITIALIZER) {
            error ("Illegal return in initializer");
        }

        expression ();
        consume (TOKEN_SEMICOLON, "Expected ';' to end a statement");
        emit_byte (OP_RETURN);
    }
}

static void while_statement (void) {
    int32 loop_start = current_chunk ()->count;
    consume (TOKEN_LEFT_PAREN, "Expected '(' after `while`");
    expression ();
    consume (TOKEN_RIGHT_PAREN, "Expected ')' to end while condition");

    int32 exit_jump = emit_jump (OP_JUMP_IF_FALSE);
    emit_byte (OP_POP);
    statement ();
    emit_loop (loop_start);

    patch_jump (exit_jump);
    emit_byte (OP_POP);
}

static void for_statement (void) {
    begin_scope ();

    consume (TOKEN_LEFT_PAREN, "Expected '(' after `for`");
    if (match (TOKEN_VAR)) {
        var_declaration ();
    } else if (!match (TOKEN_SEMICOLON)) {
        expression_statement ();
    }

    int32 loop_start = current_chunk ()->count;
    int32 exit_jump  = -1;

    if (!match (TOKEN_SEMICOLON)) {
        expression ();
        consume (TOKEN_SEMICOLON, "Expected ';' after for loop condition");

        exit_jump = emit_jump (OP_JUMP_IF_FALSE);
        emit_byte (OP_POP);
    }

    if (!match (TOKEN_RIGHT_PAREN)) {
        int32 body_jump       = emit_jump (OP_JUMP);
        int32 increment_start = current_chunk ()->count;
        expression ();
        emit_byte (OP_POP);
        consume (TOKEN_RIGHT_PAREN, "Expected ')' after for loop clauses");

        emit_loop (loop_start);
        loop_start = increment_start;
        patch_jump (body_jump);
    }

    statement ();
    emit_loop (loop_start);

    if (exit_jump != -1) {
        patch_jump (exit_jump);
        emit_byte (OP_POP);
    }

    end_scope ();
}

static void synchronize (void) {
    parser.panic_mode = false;

    while (parser.current.type != TOKEN_EOF) {
        if (parser.previous.type == TOKEN_SEMICOLON) return;
        switch (parser.current.type) {
            case TOKEN_CLASS:
            case TOKEN_FUN:
            case TOKEN_VAR:
            case TOKEN_FOR:
            case TOKEN_IF:
            case TOKEN_WHILE:
            case TOKEN_PRINT:
            case TOKEN_RETURN: return;

            default: {
            }
        }

        advance ();
    }
}

static void declaration (void) {
    if (match (TOKEN_CLASS)) {
        class_declaration ();
    } else if (match (TOKEN_VAR)) {
        var_declaration ();
    } else if (match (TOKEN_FUN)) {
        fun_declaration ();
    } else {
        statement ();
    }

    if (parser.panic_mode) synchronize ();
}

static void statement (void) {
    if (match (TOKEN_PRINT)) {
        print_statement ();
    } else if (match (TOKEN_LEFT_BRACE)) {
        begin_scope ();
        block ();
        end_scope ();
    } else if (match (TOKEN_WHILE)) {
        while_statement ();
    } else if (match (TOKEN_FOR)) {
        for_statement ();
    } else if (match (TOKEN_IF)) {
        if_statement ();
    } else if (match (TOKEN_RETURN)) {
        return_statement ();
    } else {
        expression_statement ();
    }
}

obj_func *compile (const char *source) {
    init_scanner (source);

    compiler_t compiler;
    init_compiler (&compiler, FTYPE_SCRIPT);

    parser.had_error  = false;
    parser.panic_mode = false;

    advance ();

    while (!match (TOKEN_EOF)) {
        declaration ();
    }

    obj_func *func = end_compiler ();
    return parser.had_error ? NULL : func;
}

void mark_compiler_roots (void) {
    compiler_t *compiler = current;
    while (compiler != NULL) {
        mark_object ((obj *) compiler->func);
        compiler = compiler->enclosing;
    }
}
