// apachejuice, 27.02.2024
// See LICENSE for details.
#ifndef __ALOXOTL_VM__
#define __ALOXOTL_VM__

#include "chunk.h"
#include "common.h"
#include "obj.h"
#include "table.h"
#include "value.h"

#define FRAMES_MAX 64
#define STACK_MAX (FRAMES_MAX * UINT8_COUNT)

typedef struct {
    obj_closure *closure;
    uint8       *ip;
    value       *slots;
} call_frame;

typedef struct {
    int32      frame_count;
    call_frame frames[FRAMES_MAX];

    chunk       *cur_chunk;
    uint8       *ip;
    value        stack[STACK_MAX];
    value       *stack_top;
    obj         *objects;
    size         gray_count;
    size         gray_capacity;
    obj        **gray_stack;
    table        strings;
    obj_string  *init_string;
    table        globals;
    obj_upvalue *open_upvalues;
    size         heap_size;
    size         gc_treshold;
} VM;

typedef enum {
    INTERPRET_OK,
    INTERPRET_COMPILE_ERROR,
    INTERPRET_RUNTIME_ERROR,
} interpret_result;

void             init_vm (void);
void             free_vm (void);
interpret_result interpret (const char *source);
void             push (value val);
value            pop (void);

#endif
