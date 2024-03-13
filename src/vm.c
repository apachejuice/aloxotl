// apachejuice, 27.02.2024
// See LICENSE for details.
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <time.h>
#include <math.h>

#include "memory.h"
#include "obj.h"
#include "table.h"
#include "vm.h"
#include "chunk.h"
#include "common.h"
#include "compiler.h"
#include "debug.h"
#include "value.h"

VM vm;

#define dpop()  \
    do {        \
        pop (); \
        pop (); \
    } while (0)

static void reset_stack (void) {
    vm.stack_top     = vm.stack;
    vm.frame_count   = 0;
    vm.open_upvalues = NULL;
}

static void runtime_errorv (const char *msg, va_list ap) {
    vfprintf (stderr, msg, ap);
    va_end (ap);
    fputs ("\n", stderr);

    for (int32 i = vm.frame_count - 1; i >= 0; i--) {
        call_frame *frame       = &vm.frames[i];
        obj_func   *func        = frame->closure->func;
        size_t      instruction = frame->ip - func->chk.code - 1;
        fprintf (stderr, "[line %zu] in ", func->chk.lines[instruction]);
        if (func->name == NULL) {
            fprintf (stderr, "script\n");
        } else {
            fprintf (stderr, "%s()\n", func->name->data);
        }
    }
}

static void runtime_error (const char *msg, ...) {
    va_list ap;
    va_start (ap, msg);

    runtime_errorv (msg, ap);
    va_end (ap);

    reset_stack ();
}

static value native_clock (uint8 argc, value *args) {
    return NUMBER_VAL ((double) clock () / CLOCKS_PER_SEC);
}

static void define_native (const char *name, native_fn callback) {
    push (OBJ_VAL ((obj *) copy_string (name, (int32) strlen (name))));
    push (OBJ_VAL ((obj *) new_native (callback)));
    set_table (&vm.globals, AS_STRING (vm.stack[0]), vm.stack[1]);
    dpop ();
}

static void register_natives (void) {
    define_native ("clock", &native_clock);
}

void init_vm (void) {
    reset_stack ();
    vm.objects = NULL;

    vm.gray_capacity = 0;
    vm.gray_count    = 0;
    vm.gray_stack    = NULL;

    vm.heap_size   = 0;
    vm.gc_treshold = 1024 * 1024;

    init_table (&vm.strings);
    vm.init_string = NULL;
    vm.init_string = copy_string ("init", 4);

    init_table (&vm.globals);
    register_natives ();
}

void free_vm (void) {
    free_table (&vm.strings);
    free_table (&vm.globals);
    vm.init_string = NULL;
    free_objects ();
}

void push (value val) {
    *vm.stack_top = val;
    vm.stack_top++;
}

value pop (void) {
    vm.stack_top--;
    return *vm.stack_top;
}

static value peek (int dist) {
    return vm.stack_top[-1 - dist];
}

static bool call (obj_closure *closure, int argc) {
    obj_func *func = closure->func;

    if (argc != func->arity) {
        runtime_error ("Function %s expected %d arguments but got %d",
                       func->name->data, func->arity, argc);
        return false;
    }

    if (vm.frame_count == FRAMES_MAX) {
        runtime_error ("Stack overflow!");
        return false;
    }

    call_frame *frame = &vm.frames[vm.frame_count++];
    frame->closure    = closure;
    frame->ip         = func->chk.code;
    frame->slots      = vm.stack_top - argc;
    return true;
}

static bool call_value (value callee, uint8 argc) {
    if (IS_OBJ (callee)) {
        switch (OBJ_TYPE (callee)) {
            case OBJ_CLOSURE: return call (AS_CLOSURE (callee), argc);
            case OBJ_NATIVE: {
                native_fn native = AS_NATIVE (callee);
                value     result = native (argc, vm.stack_top - argc);
                vm.stack_top -= argc + 1;
                push (result);

                return true;
            }

            case OBJ_CLASS: {
                obj_class *klass = AS_CLASS (callee);
                vm.stack_top[-argc - 1] =
                    OBJ_VAL ((obj *) new_instance (klass));
                value initializer;
                if (get_table (&klass->methods, vm.init_string, &initializer)) {
                    return call (AS_CLOSURE (initializer), argc);
                } else if (argc != 0) {
                    runtime_error (
                        "Class with no initializer must recieve zero args: "
                        "got %d",
                        argc);
                    return false;
                }

                return true;
            }

            case OBJ_BOUND_METHOD: {
                obj_bound_method *bound = AS_BOUND_METHOD (callee);
                printf ("bound object: ");
                print_value (bound->reciever);
                printf ("\n");
                vm.stack_top[-argc - 1] = bound->reciever;
                return call (bound->method, argc);
            }

            default: break;
        }
    }

    runtime_error ("Can only call closures and classes, not %s",
                   VALUE_TYPESTR (callee));
    return false;
}

static bool bind_method (obj_class *klass, obj_string *name) {
    value method;
    if (!get_table (&klass->methods, name, &method)) {
        runtime_error ("Class %s has no property %s", klass->name->data,
                       name->data);
        return false;
    }

    obj_bound_method *bound = new_bound_method (peek (0), AS_CLOSURE (method));
    pop ();
    push (OBJ_VAL ((obj *) bound));
    return true;
}

static obj_upvalue *capture_upvalue (value *local) {
    obj_upvalue *prev_upvalue = NULL;
    obj_upvalue *upvalue      = vm.open_upvalues;

    while (upvalue != NULL && upvalue->location > local) {
        prev_upvalue = upvalue;
        upvalue      = upvalue->next;
    }

    if (upvalue != NULL && upvalue->location == local) {
        return upvalue;
    }

    obj_upvalue *created_upvalue = new_upvalue (local);
    created_upvalue->next        = upvalue;

    if (prev_upvalue == NULL) {
        vm.open_upvalues = created_upvalue;
    } else {
        prev_upvalue->next = created_upvalue;
    }

    return created_upvalue;
}

static void close_upvalues (value *last) {
    while (vm.open_upvalues != NULL && vm.open_upvalues->location >= last) {
        obj_upvalue *upvalue = vm.open_upvalues;
        upvalue->closed      = *upvalue->location;
        upvalue->location    = &upvalue->closed;
        vm.open_upvalues     = upvalue->next;
    }
}

static inline void define_method (obj_string *name) {
    value      method = peek (0);
    obj_class *klass  = AS_CLASS (peek (1));

    set_table (&klass->methods, name, method);
    pop ();
}

static bool is_falsey (value val) {
    return IS_NIL (val) || (IS_BOOL (val) && !AS_BOOL (val));
}

static void concatenate (void) {
    obj_string *b = AS_STRING (peek (0));
    obj_string *a = AS_STRING (peek (1));

    size  len  = a->len + b->len;
    char *data = ALLOCATE (char, len + 1);
    memcpy (data, a->data, a->len);
    memcpy (data + a->len, b->data, b->len);
    data[len] = 0;

    obj_string *result = take_string (data, len);
    dpop ();

    push (OBJ_VAL ((obj *) result));
}

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"

static interpret_result run (void) {
    call_frame *frame = &vm.frames[vm.frame_count - 1];

#define READ_BYTE() (*frame->ip++)
#define READ_CONSTANT() (frame->closure->func->chk.consts.values[READ_BYTE ()])
#define READ_SHORT() \
    (frame->ip += 2, (uint16) ((frame->ip[-2] << 8) | frame->ip[-1]))
#define READ_STRING() AS_STRING (READ_CONSTANT ())
#define BINARY_OP(vt, op)                                     \
    do {                                                      \
        if (!IS_NUMBER (peek (0)) || !IS_NUMBER (peek (1))) { \
            runtime_error ("Operands must be numbers.");      \
            return INTERPRET_RUNTIME_ERROR;                   \
        }                                                     \
        double b = AS_NUMBER (pop ());                        \
        double a = AS_NUMBER (pop ());                        \
        push (vt (a op b));                                   \
    } while (false)

    while (1) {
#ifdef DEBUG_TRACE_EXECUTION
        printf ("\t\t");
        for (value *slot = vm.stack; slot < vm.stack_top; slot++) {
            printf ("[ ");
            print_value (*slot);
            printf (" ]");
        }

        printf ("\n");
        obj_func *func = frame->closure->func;
        disassemble_instruction (&func->chk,
                                 (size) (frame->ip - func->chk.code));
#endif
        uint8 instruction;
        switch (instruction = READ_BYTE ()) {
            case OP_CONSTANT: {
                value constant = READ_CONSTANT ();
                push (constant);
                print_value (constant);
                printf ("\n");
                break;
            }

            case OP_NIL: push (NIL_VAL ()); break;
            case OP_TRUE: push (BOOL_VAL (true)); break;
            case OP_FALSE: push (BOOL_VAL (false)); break;

            case OP_NEGATE:
                if (!IS_NUMBER (peek (0))) {
                    runtime_error ("Operand must be a number");
                    return INTERPRET_RUNTIME_ERROR;
                }

                push (NUMBER_VAL (-AS_NUMBER (pop ())));
                break;

            case OP_ADD:
                if (IS_STRING (peek (0)) && IS_STRING (peek (1))) {
                    concatenate ();
                } else if (IS_NUMBER (peek (0)) && IS_NUMBER (peek (1))) {
                    double b = AS_NUMBER (pop ());
                    double a = AS_NUMBER (pop ());
                    push (NUMBER_VAL (a + b));
                } else {
                    runtime_error (
                        "Operands must be two numbers or two strings.");
                    return INTERPRET_RUNTIME_ERROR;
                }
                break;

            case OP_SUBTRACT: BINARY_OP (NUMBER_VAL, -); break;
            case OP_MULTIPLY: BINARY_OP (NUMBER_VAL, *); break;
            case OP_DIVIDE: BINARY_OP (NUMBER_VAL, /); break;

            case OP_NOT: push (BOOL_VAL (is_falsey (pop ()))); break;
            case OP_EQUAL: {
                value b = pop ();
                value a = pop ();

                push (BOOL_VAL (values_equal (a, b)));
                break;
            }

            case OP_GREATER: BINARY_OP (BOOL_VAL, >); break;
            case OP_LESS: BINARY_OP (BOOL_VAL, <); break;
            case OP_POP: pop (); break;

            case OP_DEFINE_GLOBAL: {
                obj_string *name = READ_STRING ();
                set_table (&vm.globals, name, peek (0));
                pop ();
                break;
            }

            case OP_GET_GLOBAL: {
                obj_string *name = READ_STRING ();
                value       val;
                if (!get_table (&vm.globals, name, &val)) {
                    runtime_error ("Undefined variable '%s'", name->data);
                    return INTERPRET_RUNTIME_ERROR;
                }

                push (val);
                break;
            }

            case OP_SET_GLOBAL: {
                obj_string *name = READ_STRING ();
                if (set_table (&vm.globals, name, peek (0))) {
                    delete_table (&vm.globals, name);
                    runtime_error ("Reference to undefined variable '%s'",
                                   name->data);
                    return INTERPRET_RUNTIME_ERROR;
                }

                break;
            }

            case OP_GET_LOCAL: {
                uint8 slot = READ_BYTE ();
                push (frame->slots[slot]);
                break;
            }

            case OP_SET_LOCAL: {
                uint8 slot         = READ_BYTE ();
                frame->slots[slot] = peek (0);
                break;
            }

            case OP_JUMP_IF_FALSE: {
                uint16 offset = READ_SHORT ();
                if (is_falsey (peek (0))) frame->ip += offset;
                break;
            }

            case OP_JUMP: {
                uint16 offset = READ_SHORT ();
                frame->ip += offset;
                break;
            }

            case OP_LOOP: {
                uint16 offset = READ_SHORT ();
                frame->ip -= offset;
                break;
            }

            case OP_PRINT: {
                print_value (pop ());
                printf ("\n");
                break;
            }

            case OP_CALL: {
                uint8 argc = READ_BYTE ();
                if (!call_value (peek (argc), argc)) {
                    return INTERPRET_RUNTIME_ERROR;
                }

                frame = &vm.frames[vm.frame_count - 1];
                break;
            }

            case OP_CLOSURE: {
                obj_func    *func    = AS_FUNC (READ_CONSTANT ());
                obj_closure *closure = new_closure (func);
                push (OBJ_VAL ((obj *) closure));

                for (int32 i = 0; i < closure->upvalue_count; i++) {
                    uint8 is_local = READ_BYTE ();
                    uint8 index    = READ_BYTE ();
                    if (is_local) {
                        closure->upvalues[i] =
                            capture_upvalue (frame->slots + index);

                    } else {
                        closure->upvalues[i] = frame->closure->upvalues[index];
                    }
                }

                break;
            }

            case OP_GET_UPVALUE: {
                uint8 slot = READ_BYTE ();
                push (*frame->closure->upvalues[slot]->location);
                break;
            }

            case OP_SET_UPVALUE: {
                uint8 slot                                = READ_BYTE ();
                *frame->closure->upvalues[slot]->location = peek (0);
                break;
            }

            case OP_CLOSE_UPVALUE: {
                close_upvalues (vm.stack_top - 1);
                pop ();
                break;
            }

            case OP_CLASS: {
                push (OBJ_VAL ((obj *) new_klass (READ_STRING ())));
                break;
            }

            case OP_GET_PROPERTY: {
                if (!IS_INSTANCE (peek (0))) {
                    runtime_error ("Only classes have properties, not %s",
                                   VALUE_TYPESTR (peek (0)));
                    return INTERPRET_RUNTIME_ERROR;
                }

                obj_instance *instance = AS_INSTANCE (peek (0));
                obj_string   *name     = READ_STRING ();

                value val;
                if (get_table (&instance->fields, name, &val)) {
                    pop ();
                    push (val);
                    break;
                }

                if (!bind_method (instance->klass, name)) {
                    return INTERPRET_RUNTIME_ERROR;
                }

                runtime_error ("No property %s defined for class %s",
                               name->data, instance->klass->name->data);
                return INTERPRET_RUNTIME_ERROR;
            }

            case OP_SET_PROPERTY: {
                if (!IS_INSTANCE (peek (1))) {
                    runtime_error ("Only classes have properties, not %s",
                                   VALUE_TYPESTR (peek (1)));
                    return INTERPRET_RUNTIME_ERROR;
                }

                obj_instance *instance = AS_INSTANCE (peek (1));
                set_table (&instance->fields, READ_STRING (), peek (0));
                value val = pop ();

                // Cannot use dpop since value needs to be stored
                pop ();
                push (val);
                break;
            }

            case OP_METHOD: define_method (READ_STRING ()); break;

            case OP_RETURN: {
                value result = pop ();
                close_upvalues (frame->slots);
                vm.frame_count--;
                if (vm.frame_count == 0) {
                    pop ();
                    return INTERPRET_OK;
                }

                vm.stack_top = frame->slots - 1;
                push (result);
                frame = &vm.frames[vm.frame_count - 1];

                break;
            }
        }
    }

#undef READ_BYTE
#undef READ_CONSTANT
#undef READ_SHORT
#undef READ_STRING
#undef BINARY_OP
}

interpret_result interpret (const char *source) {
    obj_func *func = compile (source);
    if (func == NULL) return INTERPRET_COMPILE_ERROR;

    push (OBJ_VAL ((obj *) func));
    obj_closure *closure = new_closure (func);
    pop ();
    push (OBJ_VAL ((obj *) closure));
    call (closure, 0);

    return run ();
}
