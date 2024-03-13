// apachejuice, 25.02.2024
// See LICENSE for details.
#include "value.h"
#include "memory.h"
#include "obj.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>

const char *const _value_names[] = {
    "bool",
    "nil",
    "number",
    "object",
};

void init_value_array (value_array *array) {
    array->values   = NULL;
    array->capacity = 0;
    array->count    = 0;
}

void write_value_array (value_array *array, value val) {
    if (array->capacity < array->count + 1) {
        size old_capacity = array->capacity;
        array->capacity   = GROW_CAPACITY (old_capacity);
        array->values =
            GROW_ARRAY (value, array->values, old_capacity, array->capacity);
    }

    array->values[array->count] = val;
    array->count++;
}

void free_value_array (value_array *array) {
    FREE_ARRAY (value, array->values, array->capacity);
    init_value_array (array);
}

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wswitch"

void print_value (value val) {
    switch (val.type) {
        case VALUE_BOOL: printf (AS_BOOL (val) ? "true" : "false"); break;
        case VALUE_NIL: printf ("<nil>"); break;
        case VALUE_NUMBER: printf ("%g", AS_NUMBER (val)); break;
        case VALUE_OBJ: print_object (val); break;
    }
}

#pragma GCC diagnostic pop

bool values_equal (value a, value b) {
    if (a.type != b.type) {
        return false;
    }

    switch (a.type) {
        case VALUE_BOOL: return AS_BOOL (a) == AS_BOOL (b);
        case VALUE_NIL: return true;
        case VALUE_NUMBER: return AS_NUMBER (a) == AS_NUMBER (b);
        case VALUE_OBJ: return AS_OBJ (a) == AS_OBJ (b);

        default: return false;
    }
}
