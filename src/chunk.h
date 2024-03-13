// apachejuice, 25.02.2024
// See LICENSE for details.
#ifndef __ALOXOTL_CHUNK__
#define __ALOXOTL_CHUNK__

#include "common.h"
#include "value.h"

typedef enum {
    OP_ADD,
    OP_CALL,
    OP_CLASS,
    OP_CLOSE_UPVALUE,
    OP_CLOSURE,
    OP_CONSTANT,
    OP_DEFINE_GLOBAL,
    OP_DIVIDE,
    OP_EQUAL,
    OP_FALSE,
    OP_GET_GLOBAL,
    OP_GET_LOCAL,
    OP_GET_PROPERTY,
    OP_GET_UPVALUE,
    OP_GREATER,
    OP_JUMP_IF_FALSE,
    OP_JUMP,
    OP_LESS,
    OP_LOOP,
    OP_METHOD,
    OP_MULTIPLY,
    OP_NEGATE,
    OP_NIL,
    OP_NOT,
    OP_POP,
    OP_PRINT,
    OP_RETURN,
    OP_SET_GLOBAL,
    OP_SET_LOCAL,
    OP_SET_PROPERTY,
    OP_SET_UPVALUE,
    OP_SUBTRACT,
    OP_TRUE,
} opcode;

typedef struct {
    size        count;
    size        capacity;
    uint8      *code;
    value_array consts;
    size       *lines;
} chunk;

void init_chunk (chunk *chunk);
void free_chunk (chunk *chunk);
void write_chunk (chunk *chunk, uint8 byte, size line);
int  add_constant (chunk *chunk, value val);

#endif
