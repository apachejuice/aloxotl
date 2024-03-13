// apachejuice, 25.02.2024
// See LICENSE for details.
#ifndef __ALOXOTL_MEMORY__
#define __ALOXOTL_MEMORY__

#include "common.h"
#include "value.h"

#define ALLOCATE(type, len) (type *) reallocate (NULL, 0, sizeof (type) * (len))

#define GROW_CAPACITY(capacity) ((capacity) < 8 ? 8 : (capacity) *2)
#define GROW_ARRAY(type, pointer, old_count, new_count)        \
    (type *) reallocate (pointer, sizeof (type) * (old_count), \
                         sizeof (type) * (new_count))

#define FREE_ARRAY(type, pointer, old_count) \
    reallocate (pointer, sizeof (type) * (old_count), 0)

#define FREE(type, pointer) reallocate (pointer, sizeof (type), 0)

void *reallocate (void *pointer, size old_size, size new_size);
void  collect_garbage (void);
void  mark_value (value val);
void  mark_object (obj *obj);
void  free_objects (void);

#endif
