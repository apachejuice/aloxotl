// apachejuice, 03.03.2024
// See LICENSE for details.
#ifndef __ALOXOTL_TABLE__
#define __ALOXOTL_TABLE__
#include "common.h"
#include "value.h"

typedef struct _table_entry table_entry;

typedef struct {
    size         count;
    size         capacity;
    table_entry *entries;
} table;

void        init_table (table *tab);
void        free_table (table *tab);
bool        set_table (table *tab, obj_string *key, value val);
bool        get_table (table *tab, obj_string *key, value *val);
bool        delete_table (table *tab, obj_string *key);
void        add_all_table (table *from, table *to);
obj_string *table_find_string (table *tab, const char *data, size len,
                               uint32 hash);
void        table_remove_white (table *tab);
void        mark_table (table *tab);

#endif
