// apachejuice, 25.02.2024
// See LICENSE for details.
#include "chunk.h"
#include "common.h"
#include "memory.h"
#include "value.h"
#include "vm.h"

void init_chunk (chunk *chunk) {
    chunk->count    = 0;
    chunk->capacity = 0;
    chunk->code     = NULL;
    chunk->lines    = NULL;

    init_value_array (&chunk->consts);
}

void free_chunk (chunk *chunk) {
    FREE_ARRAY (uint8, chunk->code, chunk->capacity);
    FREE_ARRAY (size, chunk->lines, chunk->capacity);

    free_value_array (&chunk->consts);
    init_chunk (chunk);
}

void write_chunk (chunk *chunk, uint8 byte, size line) {
    if (chunk->capacity < chunk->count + 1) {
        size old_capacity = chunk->capacity;
        chunk->capacity   = GROW_CAPACITY (old_capacity);
        chunk->code =
            GROW_ARRAY (uint8, chunk->code, old_capacity, chunk->capacity);
        chunk->lines =
            GROW_ARRAY (size, chunk->lines, old_capacity, chunk->capacity);
    }

    chunk->code[chunk->count]  = byte;
    chunk->lines[chunk->count] = line;
    chunk->count++;
}

int add_constant (chunk *chunk, value val) {
    push (val);
    write_value_array (&chunk->consts, val);
    pop ();

    return chunk->consts.count - 1;
}
