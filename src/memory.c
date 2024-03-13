// apachejuice, 25.02.2024
// See LICENSE for details.
#include "memory.h"
#include "chunk.h"
#include "compiler.h"
#include "obj.h"
#include "value.h"
#include "vm.h"

#include <stdlib.h>

#ifdef DEBUG_LOG_GC
#include <stdio.h>
#include "debug.h"
#endif

#define GC_HEAP_GROW_FACTOR (2)

extern VM vm;

void *reallocate (void *ptr, size old_size, size new_size) {
    vm.heap_size += new_size - old_size;
    if (new_size > old_size) {
#ifdef DEBUG_STRESS_GC
        collect_garbage ();
#endif

        if (vm.heap_size > vm.gc_treshold) {
            collect_garbage ();
        }
    }

    if (!new_size) {
        free (ptr);
        return NULL;
    }

    void *result = realloc (ptr, new_size);
    if (!result) {
        exit (1);
    }

    return result;
}

static void free_object (obj *obj) {
    if (!obj) return;

#ifdef DEBUG_LOG_GC
    printf ("%p free type %s\n", (void *) obj, OBJ_TYPESTR (obj->type));
#endif

    switch (obj->type) {
        case OBJ_BOUND_METHOD: FREE (obj_bound_method, obj); break;

        case OBJ_INSTANCE: {
            obj_instance *instance = (obj_instance *) obj;
            free_table (&instance->fields);
            FREE (obj_instance, obj);
            break;
        }

        case OBJ_CLASS: {
            obj_class *klass = (obj_class *) obj;
            free_table (&klass->methods);
            FREE (obj_class, obj);
            break;
        }

        case OBJ_STRING: {
            obj_string *str = (obj_string *) obj;
            FREE_ARRAY (char, str->data, str->len + 1);
            FREE (obj_string, obj);
            break;
        }

        case OBJ_FUNC: {
            obj_func *func = (obj_func *) obj;
            free_chunk (&func->chk);
            FREE (obj_func, obj);
            break;
        }

        case OBJ_NATIVE: {
            FREE (obj_native, obj);
            break;
        }

        case OBJ_CLOSURE: {
            obj_closure *closure = (obj_closure *) obj;
            FREE_ARRAY (obj_upvalue *, closure->upvalues,
                        closure->upvalue_count);
            FREE (obj_closure, obj);
            break;
        }

        case OBJ_UPVALUE: {
            FREE (obj_upvalue, obj);
            break;
        }
    }
}

void mark_value (value val) {
    if (IS_OBJ (val)) mark_object (AS_OBJ (val));
}

void mark_object (obj *object) {
    if (!object || object->marked) return;

#ifdef DEBUG_LOG_GC
    printf ("%p marked ", (void *) obj);
    print_value (OBJ_VAL (obj));
    printf ("\n");
#endif

    object->marked = true;

    if (vm.gray_capacity < vm.gray_count + 1) {
        vm.gray_capacity = GROW_CAPACITY (vm.gray_capacity);
        vm.gray_stack =
            (obj **) realloc (vm.gray_stack, sizeof (obj *) * vm.gray_capacity);

        if (vm.gray_stack == NULL) exit (1);
    }

    vm.gray_stack[vm.gray_count++] = object;
}

static void mark_array (value_array *array) {
    for (size i = 0; i < array->count; i++) {
        mark_value (array->values[i]);
    }
}

static void mark_roots (void) {
    for (value *slot = vm.stack; slot < vm.stack_top; slot++) {
        mark_value (*slot);
    }

    for (int32 i = 0; i < vm.frame_count; i++) {
        mark_object ((obj *) vm.frames[i].closure);
    }

    for (obj_upvalue *upvalue = vm.open_upvalues; upvalue != NULL;
         upvalue              = upvalue->next) {
        mark_object ((obj *) upvalue);
    }

    mark_table (&vm.globals);
    mark_compiler_roots ();
    mark_object ((obj *) vm.init_string);
}

static void blacken_object (obj *object) {
#ifdef DEBUG_LOG_GC
    printf ("%p blacken ", (void *) obj);
    print_value (OBJ_VAL (obj));
    printf ("\n");
#endif

    switch (object->type) {
        case OBJ_STRING:
        case OBJ_NATIVE: break;

        case OBJ_BOUND_METHOD: {
            obj_bound_method *bound = (obj_bound_method *) object;
            mark_value (bound->reciever);
            mark_object ((obj *) bound->method);
            break;
        }

        case OBJ_CLASS: {
            obj_class *klass = (obj_class *) object;
            mark_object ((obj *) klass->name);
            mark_table (&klass->methods);
            break;
        }

        case OBJ_INSTANCE: {
            obj_instance *instance = (obj_instance *) object;
            mark_object ((obj *) instance->klass);
            mark_table (&instance->fields);

            break;
        }

        case OBJ_UPVALUE: mark_value (((obj_upvalue *) object)->closed); break;

        case OBJ_FUNC: {
            obj_func *func = (obj_func *) object;
            mark_object ((obj *) func->name);
            mark_array (&func->chk.consts);
            break;
        }

        case OBJ_CLOSURE: {
            obj_closure *closure = (obj_closure *) object;
            mark_object ((obj *) closure->func);
            for (int32 i = 0; i < closure->upvalue_count; i++) {
                mark_object ((obj *) closure->upvalues[i]);
            }

            break;
        }
    }
}

static void trace_references (void) {
    while (vm.gray_count > 0) {
        obj *obj = vm.gray_stack[--vm.gray_count];
        blacken_object (obj);
    }
}

static void sweep (void) {
    obj *prev   = NULL;
    obj *object = vm.objects;

    while (object) {
        if (object->marked) {
            object->marked = false;
            prev           = object;
            object         = object->next;
        } else {
            obj *unreached = object;
            object         = object->next;
            if (prev) {
                prev->next = object;
            } else {
                vm.objects = object;
            }

            free_object (unreached);
        }
    }
}

void collect_garbage (void) {
#ifdef DEBUG_LOG_GC
    printf ("-- GC BEGIN --\n");
    size before = vm.heap_size;
#endif

    mark_roots ();
    trace_references ();
    table_remove_white (&vm.strings);
    sweep ();

    vm.gc_treshold = vm.heap_size * GC_HEAP_GROW_FACTOR;

#ifdef DEBUG_LOG_GC
    printf ("-- GC END --\n");
    printf (
        "\tcollected %zu bytes (from %zu to %zu), next collection triggered at "
        "%zu\n",
        before - vm.heap_size, before, vm.heap_size, vm.gc_treshold);
#endif
}

void free_objects (void) {
    /* obj *obj = vm.objects;
    while (obj != NULL) {
        obj *next = obj->next;
        free_object (next);
        obj = next;
    } */

    free (vm.gray_stack);
}
