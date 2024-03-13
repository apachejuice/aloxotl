// apachejuice, 03.03.2024
// See LICENSE for details.
#include <stdlib.h>
#include <string.h>

#include "memory.h"
#include "obj.h"
#include "table.h"
#include "value.h"

#define TABLE_MAX_LOAD 0.75

struct _table_entry {
    obj_string *key;
    value       val;
};

void init_table (table *tab) {
    tab->count    = 0;
    tab->capacity = 0;
    tab->entries  = NULL;
}

void free_table (table *tab) {
    FREE_ARRAY (table_entry, tab->entries, tab->capacity);
    init_table (tab);
}

static table_entry *find_entry (table_entry *entries, size capacity,
                                obj_string *key) {
    uint32       index     = key->hash % capacity;
    table_entry *tombstone = NULL;

    while (1) {
        table_entry *entry = &entries[index];
        if (entry->key == NULL) {
            if (IS_NIL (entry->val)) {
                return tombstone != NULL ? tombstone : entry;
            } else {
                if (tombstone == NULL) tombstone = entry;
            }
        } else if (entry->key == key) {
            return entry;
        }

        index = (index + 1) % capacity;
    }
}

static void adjust_capacity (table *tab, size capacity) {
    table_entry *entries = ALLOCATE (table_entry, capacity);
    for (size i = 0; i < capacity; i++) {
        entries[i].key = NULL;
        entries[i].val = NIL_VAL ();
    }

    tab->count = 0;
    for (size i = 0; i < tab->capacity; i++) {
        table_entry *entry = &tab->entries[i];
        if (entry->key == NULL) continue;

        table_entry *dest = find_entry (entries, capacity, entry->key);
        dest->key         = entry->key;
        dest->val         = entry->val;

        tab->count++;
    }

    FREE_ARRAY (table_entry, tab->entries, tab->capacity);
    tab->entries  = entries;
    tab->capacity = capacity;
}

bool set_table (table *tab, obj_string *key, value val) {
    if (tab->count + 1 > tab->capacity * TABLE_MAX_LOAD) {
        size cap = GROW_CAPACITY (tab->capacity);
        adjust_capacity (tab, cap);
    }

    table_entry *entry  = find_entry (tab->entries, tab->capacity, key);
    bool         is_new = entry->key == NULL;
    if (is_new && IS_NIL (entry->val)) tab->count++;

    entry->key = key;
    entry->val = val;
    return is_new;
}

bool get_table (table *tab, obj_string *key, value *val) {
    if (tab->count == 0) return false;

    table_entry *entry = find_entry (tab->entries, tab->capacity, key);
    if (entry->key == NULL) return false;

    *val = entry->val;
    return true;
}

void add_all_table (table *from, table *to) {
    for (size i = 0; i < from->capacity; i++) {
        table_entry *entry = &from->entries[i];
        if (entry->key != NULL) {
            set_table (to, entry->key, entry->val);
        }
    }
}

bool delete_table (table *tab, obj_string *key) {
    if (tab->count == 0) return false;

    table_entry *entry = find_entry (tab->entries, tab->capacity, key);
    if (entry->key == NULL) return false;

    entry->key = NULL;
    entry->val = BOOL_VAL (true);
    return true;
}

obj_string *table_find_string (table *tab, const char *data, size len,
                               uint32 hash) {
    if (tab->count == 0) return NULL;

    uint32 index = hash % tab->capacity;
    while (1) {
        table_entry *entry = &tab->entries[index];
        if (entry->key == NULL) {
            if (IS_NIL (entry->val)) return NULL;
        } else if (entry->key->len == len && entry->key->hash == hash &&
                   memcmp (entry->key->data, data, len) == 0) {
            return entry->key;
        }

        index = (index + 1) % tab->capacity;
    }
}

void mark_table (table *tab) {
    for (size i = 0; i < tab->capacity; i++) {
        table_entry *entry = &tab->entries[i];
        mark_object ((obj *) entry->key);
        mark_value (entry->val);
    }
}

void table_remove_white (table *tab) {
    for (size i = 0; i < tab->capacity; i++) {
        table_entry *entry = &tab->entries[i];
        if (entry->key && !entry->key->base_ref.marked) {
            delete_table (tab, entry->key);
        }
    }
}
