// apachejuice, 25.02.2024
// See LICENSE for details.
#ifndef __ALOXOTL_VALUE__
#define __ALOXOTL_VALUE__

#include "common.h"

typedef struct _obj        obj;
typedef struct _obj_string obj_string;

typedef enum {
    VALUE_BOOL,
    VALUE_NIL,
    VALUE_NUMBER,
    VALUE_OBJ,

    _VALUETYPE_COUNT,
} value_type;

typedef struct _v {
    value_type type;
    union {
        bool   b;
        double n;
        obj   *o;
    } as;
} value;

#define VALUE_TYPESTR(val)                                    \
    (val.type == VALUE_OBJ ? OBJ_TYPESTR (AS_OBJ (val)->type) \
                           : _value_names[val.type])
extern const char *const _value_names[_VALUETYPE_COUNT];

#define IS_BOOL(val) ((val).type == VALUE_BOOL)
#define IS_NIL(val) ((val).type == VALUE_NIL)
#define IS_NUMBER(val) ((val).type == VALUE_NUMBER)
#define IS_OBJ(val) ((val).type == VALUE_OBJ)

// struct _v because passing *_VAL(value) is an error
#define BOOL_VAL(val) ((struct _v){.type = VALUE_BOOL, .as = {.b = val}})
#define NIL_VAL() ((struct _v){.type = VALUE_NIL, .as = {.n = 0}})
#define NUMBER_VAL(val) ((struct _v){.type = VALUE_NUMBER, .as = {.n = val}})
// This macro has '.o = (obj *) val' in the book.
// I omitted the cast because I want the call site to cast a value if needed.
// This reduces bugs, as you cannot pass an opaque type to the macro without
// casting.
#define OBJ_VAL(val) ((struct _v){.type = VALUE_OBJ, .as = {.o = val}})

#define AS_BOOL(val) ((val).as.b)
#define AS_NUMBER(val) ((val).as.n)
#define AS_OBJ(val) ((val).as.o)

typedef struct {
    size   capacity;
    size   count;
    value *values;
} value_array;

bool values_equal (value a, value b);

void init_value_array (value_array *array);
void write_value_array (value_array *array, value val);
void free_value_array (value_array *array);
void print_value (value val);

#endif
