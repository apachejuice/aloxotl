// apachejuice, 25.02.2024
// See LICENSE for details.
#include "debug.h"
#include "obj.h"
#include "value.h"

#include <stdio.h>
#include "chunk.h"

void disassemble_chunk (chunk *chunk, const char *name) {
    printf ("== %s ==\n", name);

    for (size offset = 0; offset < chunk->count;) {
        offset = disassemble_instruction (chunk, offset);
    }
}

static size simple_instruction (const char *name, size offset) {
    printf ("%s\n", name);
    return offset + 1;
}

static size byte_instruction (const char *name, chunk *chunk, size offset) {
    uint8 slot = chunk->code[offset + 1];
    printf ("%-16s %4d\n", name, slot);
    return offset + 2;
}

static size jump_instruction (const char *name, int sign, chunk *chunk,
                              size offset) {
    uint16 jump = (uint16) (chunk->code[offset + 1] << 8);
    jump |= chunk->code[offset + 2];
    printf ("%-16s %4zu -> %d\n", name, offset,
            (int32) offset + 3 + sign * jump);
    return offset + 3;
}

static size constant_instruction (const char *name, chunk *chunk, size offset) {
    uint8 constant = chunk->code[offset + 1];

    printf ("%-16s %4d '", name, constant);
    print_value (chunk->consts.values[constant]);
    printf ("'\n");

    return offset + 2;
}

int disassemble_instruction (chunk *chunk, size offset) {
    printf ("%04zu ", offset);
    if (offset > 0 && chunk->lines[offset] == chunk->lines[offset - 1]) {
        printf ("\t | ");
    } else {
        printf ("%4zu ", chunk->lines[offset]);
    }

    uint8 instr = chunk->code[offset];
    switch (instr) {
        case OP_CONSTANT:
            return constant_instruction ("OP_CONSTANT", chunk, offset);
        case OP_NIL: return simple_instruction ("OP_NIL", offset);
        case OP_TRUE: return simple_instruction ("OP_TRUE", offset);
        case OP_FALSE: return simple_instruction ("OP_FALSE", offset);
        case OP_NEGATE: return simple_instruction ("OP_NEGATE", offset);
        case OP_RETURN: return simple_instruction ("OP_RETURN", offset);
        case OP_ADD: return simple_instruction ("OP_ADD", offset);
        case OP_SUBTRACT: return simple_instruction ("OP_SUBTRACT", offset);
        case OP_MULTIPLY: return simple_instruction ("OP_MULTIPLY", offset);
        case OP_DIVIDE: return simple_instruction ("OP_DIVIDE", offset);
        case OP_NOT: return simple_instruction ("OP_NOT", offset);
        case OP_EQUAL: return simple_instruction ("OP_EQUAL", offset);
        case OP_GREATER: return simple_instruction ("OP_GREATER", offset);
        case OP_LESS: return simple_instruction ("OP_LESS", offset);
        case OP_PRINT: return simple_instruction ("OP_PRINT", offset);
        case OP_POP: return simple_instruction ("OP_POP", offset);
        case OP_DEFINE_GLOBAL:
            return constant_instruction ("OP_DEFINE_GLOBAL", chunk, offset);
        case OP_GET_GLOBAL:
            return constant_instruction ("OP_GET_GLOBAL", chunk, offset);
        case OP_SET_GLOBAL:
            return constant_instruction ("OP_SET_GLOBAL", chunk, offset);
        case OP_GET_LOCAL:
            return byte_instruction ("OP_GET_LOCAL", chunk, offset);
        case OP_SET_LOCAL:
            return byte_instruction ("OP_SET_LOCAL", chunk, offset);
        case OP_JUMP_IF_FALSE:
            return jump_instruction ("OP_JUMP_IF_FALSE", 1, chunk, offset);
        case OP_JUMP: return jump_instruction ("OP_JUMP", 1, chunk, offset);
        case OP_LOOP: return jump_instruction ("OP_LOOP", -1, chunk, offset);
        case OP_CALL: return byte_instruction ("OP_CALL", chunk, offset);
        case OP_GET_UPVALUE:
            return byte_instruction ("OP_GET_UPVALUE", chunk, offset);
        case OP_SET_UPVALUE:
            return byte_instruction ("OP_SET_UPVALUE", chunk, offset);
        case OP_CLOSE_UPVALUE:
            return simple_instruction ("OP_CLOSE_UPVALUE", offset);
        case OP_CLASS:
            return constant_instruction ("const char *name", chunk, offset);
        case OP_GET_PROPERTY:
            return constant_instruction ("OP_GET_PROPERTY", chunk, offset);
        case OP_SET_PROPERTY:
            return constant_instruction ("OP_SET_PROPERTY", chunk, offset);
        case OP_METHOD:
            return constant_instruction ("OP_METHOD", chunk, offset);

        case OP_CLOSURE: {
            offset++;
            uint8 constant = chunk->code[offset++];
            printf ("%-16s %4d ", "OP_CLOSURE", constant);
            print_value (chunk->consts.values[constant]);
            printf ("\n");

            obj_func *func = AS_FUNC (chunk->consts.values[constant]);
            for (int32 j = 0; j < func->upvalue_count; j++) {
                uint8 is_local = chunk->code[offset++];
                uint8 index    = chunk->code[offset++];
                printf ("%04zu\t|\t\t\t%s %d\n", offset - 2,
                        is_local ? "local" : "upvalue", index);
            }

            return offset;
        }

        default: printf ("Unknown opcode: %d\n", instr); return offset + 1;
    }
}
