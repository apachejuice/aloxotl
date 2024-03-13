// apachejuice, 01.03.2024
// See LICENSE for details.
#ifndef __ALOXOTL_OBJ__
#define __ALOXOTL_OBJ__

#include "common.h"
#include "table.h"
#include "value.h"
#include "chunk.h"

#define IS_BOUND_METHOD(val) (is_obj_type (val, OBJ_BOUND_METHOD))
#define IS_CLASS(val) (is_obj_type (val, OBJ_CLASS))
#define IS_CLOSURE(val) (is_obj_type (val, OBJ_CLOSURE))
#define IS_FUNC(val) (is_obj_type (val, OBJ_FUNC))
#define IS_INSTANCE(val) (is_obj_type (val, OBJ_INSTANCE))
#define IS_NATIVE(val) (is_obj_type (val, OBJ_NATIVE))
#define IS_STRING(val) (is_obj_type (val, OBJ_STRING))
#define OBJ_TYPE(val) (AS_OBJ (val)->type)

#define AS_BOUND_METHOD(val) ((obj_bound_method *) AS_OBJ (val))
#define AS_CLASS(val) ((obj_class *) AS_OBJ (val))
#define AS_CLOSURE(val) ((obj_closure *) AS_OBJ (val))
#define AS_CSTRING(val) (AS_STRING (val)->data)
#define AS_FUNC(val) ((obj_func *) AS_OBJ (val))
#define AS_INSTANCE(val) ((obj_instance *) AS_OBJ (val))
#define AS_NATIVE(val) (((obj_native *) AS_OBJ (val))->callback)
#define AS_STRING(val) ((obj_string *) AS_OBJ (val))

typedef enum {
    OBJ_BOUND_METHOD,
    OBJ_CLASS,
    OBJ_CLOSURE,
    OBJ_FUNC,
    OBJ_INSTANCE,
    OBJ_NATIVE,
    OBJ_STRING,
    OBJ_UPVALUE,
} obj_type;

#define OBJ_TYPESTR(objt) (_obj_types[objt])
extern const char *const _obj_types[];

struct _obj {
    obj_type     type;
    bool         marked;
    struct _obj *next;
};

typedef struct {
    obj         base_ref;
    int32       arity;
    int32       upvalue_count;
    chunk       chk;
    obj_string *name;
} obj_func;

typedef value (*native_fn) (uint8 argc, value *args);

typedef struct {
    obj       base_ref;
    native_fn callback;
} obj_native;

struct _obj_string {
    obj    base_ref;
    size   len;
    char  *data;
    uint32 hash;
};

typedef struct _obj_upvalue {
    obj                  base_ref;
    value               *location;
    value                closed;
    struct _obj_upvalue *next;
} obj_upvalue;

typedef struct {
    obj           base_ref;
    obj_func     *func;
    obj_upvalue **upvalues;
    int32         upvalue_count;
} obj_closure;

// Call variables `klass`, not `class`.
typedef struct {
    obj         base_ref;
    obj_string *name;
    table       methods;
} obj_class;

typedef struct {
    obj        base_ref;
    obj_class *klass;
    table      fields;
} obj_instance;

typedef struct {
    obj          base_ref;
    value        reciever;
    obj_closure *method;
} obj_bound_method;

obj_bound_method *new_bound_method (value reciever, obj_closure *closure);
obj_class        *new_klass (obj_string *name);
obj_instance     *new_instance (obj_class *klass);
obj_closure      *new_closure (obj_func *func);
obj_func         *new_func (void);
obj_native       *new_native (native_fn callback);
obj_string       *take_string (char *data, size len);
obj_string       *copy_string (const char *data, size len);
obj_upvalue      *new_upvalue (value *slot);
void              print_object (value val);

static inline bool is_obj_type (value val, obj_type type) {
    return IS_OBJ (val) && OBJ_TYPE (val) == type;
}

#endif
