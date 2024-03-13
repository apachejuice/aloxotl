// apachejuice, 01.03.2024
// See LICENSE for details.
#include "obj.h"
#include "chunk.h"
#include "memory.h"
#include "value.h"
#include "vm.h"

#include <stdio.h>
#include <string.h>

extern VM vm;

const char *const _obj_types[] = {
    "bound_method", "class",  "closure", "func",
    "instance",     "native", "string",  "upvalue",
};

#define ALLOCATE_OBJ(type, obj_type) \
    (type *) allocate_object (sizeof (type), obj_type)

static obj *allocate_object (size sz, obj_type type) {
    obj *obj    = reallocate (NULL, 0, sz);
    obj->type   = type;
    obj->marked = false;

    obj->next  = vm.objects;
    vm.objects = obj;

#ifdef DEBUG_LOG_GC
    printf ("%p allocate %zu for %s\n", (void *) obj, sz, OBJ_TYPESTR (type));
#endif

    return obj;
}

obj_class *new_klass (obj_string *name) {
    obj_class *klass = ALLOCATE_OBJ (obj_class, OBJ_CLASS);
    klass->name      = name;
    init_table (&klass->methods);

    return klass;
}

obj_instance *new_instance (obj_class *klass) {
    obj_instance *instance = ALLOCATE_OBJ (obj_instance, OBJ_INSTANCE);
    instance->klass        = klass;
    init_table (&instance->fields);

    return instance;
}

obj_bound_method *new_bound_method (value reciever, obj_closure *closure) {
    obj_bound_method *bound = ALLOCATE_OBJ (obj_bound_method, OBJ_BOUND_METHOD);
    bound->reciever         = reciever;
    bound->method           = closure;

    return bound;
}

obj_closure *new_closure (obj_func *func) {
    obj_upvalue **upvalues = ALLOCATE (obj_upvalue *, func->upvalue_count);
    for (int32 i = 0; i < func->upvalue_count; i++) {
        upvalues[i] = NULL;
    }

    obj_closure *closure   = ALLOCATE_OBJ (obj_closure, OBJ_CLOSURE);
    closure->func          = func;
    closure->upvalues      = upvalues;
    closure->upvalue_count = func->upvalue_count;

    return closure;
}

obj_func *new_func (void) {
    obj_func *func      = ALLOCATE_OBJ (obj_func, OBJ_FUNC);
    func->arity         = 0;
    func->name          = NULL;
    func->upvalue_count = 0;
    init_chunk (&func->chk);

    return func;
}

obj_native *new_native (native_fn callback) {
    obj_native *native = ALLOCATE_OBJ (obj_native, OBJ_NATIVE);
    native->callback   = callback;
    return native;
}

static obj_string *allocate_string (char *data, size len, uint32 hash) {
    obj_string *str = ALLOCATE_OBJ (obj_string, OBJ_STRING);
    str->len        = len;
    str->data       = data;
    str->hash       = hash;

    push (OBJ_VAL ((obj *) str));
    set_table (&vm.strings, str, NIL_VAL ());
    pop ();

    return str;
}

static uint32 hash_string (const char *data, size len) {
    uint32 hash = 2166136261u;
    for (size i = 0; i < len; i++) {
        hash ^= (uint8) data[i];
        hash *= 16777619;
    }

    return hash;
}

obj_string *take_string (char *data, size len) {
    uint32      hash     = hash_string (data, len);
    obj_string *interned = table_find_string (&vm.strings, data, len, hash);
    if (interned != NULL) {
        FREE_ARRAY (char, data, len + 1);
        return interned;
    }

    return allocate_string (data, len, hash_string (data, len));
}

obj_string *copy_string (const char *data, size len) {
    char *heap_data = ALLOCATE (char, len + 1);
    memcpy (heap_data, data, len);
    heap_data[len] = 0;

    uint32      hash     = hash_string (data, len);
    obj_string *interned = table_find_string (&vm.strings, data, len, hash);
    if (interned != NULL) {
        FREE_ARRAY (char, heap_data, len + 1);
        return interned;
    }

    return allocate_string (heap_data, len, hash);
}

obj_upvalue *new_upvalue (value *slot) {
    obj_upvalue *upvalue = ALLOCATE_OBJ (obj_upvalue, OBJ_UPVALUE);
    upvalue->location    = slot;
    upvalue->closed      = NIL_VAL ();

    return upvalue;
}

static void print_func (obj_func *func) {
    if (func->name == NULL) {
        printf ("<script>");
    } else {
        printf ("<function %s (%d) at %p>", func->name->data, func->arity,
                (void *) func);
    }
}

void print_object (value val) {
    switch (OBJ_TYPE (val)) {
        case OBJ_BOUND_METHOD:
            print_func (AS_BOUND_METHOD (val)->method->func);
            break;
        case OBJ_INSTANCE:
            printf ("<instance of %s at %p>",
                    AS_INSTANCE (val)->klass->name->data,
                    (void *) AS_INSTANCE (val));
            break;
        case OBJ_CLASS:
            printf ("<class object %s at %p>", AS_CLASS (val)->name->data,
                    (void *) AS_CLASS (val));
            break;
        case OBJ_CLOSURE: print_func (AS_CLOSURE (val)->func); break;
        case OBJ_STRING: printf ("%s", AS_CSTRING (val)); break;
        case OBJ_FUNC: print_func (AS_FUNC (val)); break;
        case OBJ_NATIVE:
            printf ("<native code at %p>", (void *) AS_OBJ (val));
            break;
        case OBJ_UPVALUE: printf ("upvalue"); break;
    }
}
