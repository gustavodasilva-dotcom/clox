#include <stdlib.h>
#include <string.h>

#include "memory.h"
#include "object.h"
#include "table.h"
#include "value.h"

#define TABLE_MAX_LOAD 0.75

void initTable(Table *table) {
  // Table starts empty
  table->count = 0;

  // -1 capacity to indicate that the table is empty (since capacity is used as
  // a mask for hashing, it needs to be one less than a power of 2)
  table->capacity = -1;

  table->entries = NULL;
}

void freeTable(Table *table) {
  FREE_ARRAY(Entry, table->entries, table->capacity + 1);
  initTable(table);
}

static Entry *findEntry(Entry *entries, int capacity, ObjString *key) {
  // Get the starting index
  uint32_t index = key->hash & capacity;

  Entry *tombstone = NULL;

  for (;;) {
    Entry *entry = &entries[index];

    if (entry->key == NULL) {
      if (IS_NIL(entry->value)) {
        // Empty entry
        return tombstone != NULL ? tombstone : entry;
      } else {
        if (tombstone == NULL) {
          // Tombstone found
          tombstone = entry;
        }
      }
    } else if (entry->key == key) {
      // Entry found
      return entry;
    }

    // It's a collision; keep probing
    index = (index + 1) & capacity;
  }
}

static void adjustCapacity(Table *table, int capacity) {
  // Allocate space for the entries
  Entry *entries = ALLOCATE(Entry, capacity + 1);

  for (int i = 0; i <= capacity; i++) {
    // Initialize the entries to empty
    entries[i].key = NULL;
    entries[i].value = NIL_VAL;
  }

  // Reset table count (to recalculate ignoring tombstones)
  table->count = 0;

  // Rebuild table
  for (int i = 0; i <= table->capacity; i++) {
    Entry *entry = &table->entries[i];

    if (entry->key == NULL) {
      // If an empty bucket or tombstone, continue
      continue;
    }

    // Otherwise, find bucket in the new array
    Entry *dest = findEntry(entries, capacity, entry->key);

    // Update properties
    dest->key = entry->key;
    dest->value = entry->value;

    table->count++;
  }

  // Free the old bucket array
  FREE_ARRAY(Entry, table->entries, table->capacity + 1);

  // And update the table with the new bucket array
  table->entries = entries;
  table->capacity = capacity;
}

bool tableGet(Table *table, ObjString *key, Value *value) {
  if (table->count == 0) {
    // If table is empty, return false
    return false;
  }

  // Find entry
  Entry *entry = findEntry(table->entries, table->capacity, key);

  if (entry->key == NULL) {
    // If bucket is empty, return false
    return false;
  }

  // Copy value to output (shallow copy)
  *value = entry->value;

  return true;
}

bool tableSet(Table *table, ObjString *key, Value value) {
  // Check if the table needs to grow
  if (table->count + 1 > (table->capacity + 1) * TABLE_MAX_LOAD) {
    // If so, grow it
    int capacity = GROW_CAPACITY(table->capacity + 1) - 1;
    adjustCapacity(table, capacity);
  }

  Entry *entry = findEntry(table->entries, table->capacity, key);

  // Check if it's a new entry
  bool isNewKey = entry->key == NULL;

  if (isNewKey && IS_NIL(entry->value)) {
    // Increment count if it's an empty bucket, not a tombstone
    table->count++;
  }

  // Update the entry with the new key and value
  entry->key = key;
  entry->value = value;

  return isNewKey;
}

bool tableDelete(Table *table, ObjString *key) {
  if (table->count == 0) {
    // If table is empty, return false
    return false;
  }

  // Find entry
  Entry *entry = findEntry(table->entries, table->capacity, key);

  if (entry->key == NULL) {
    // If bucket is empty, return false
    return false;
  }

  // Place a tombstone in the entry
  entry->key = NULL;
  entry->value = BOOL_VAL(true);

  return true;
}

void tableAddAll(Table *from, Table *to) {
  for (int i = 0; i <= from->capacity; i++) {
    Entry *entry = &from->entries[i];

    if (entry->key != NULL) {
      // Add key/value pair to destination table
      tableSet(to, entry->key, entry->value);
    }
  }
}

ObjString *tableFindString(Table *table, const char *chars, int length,
                           uint32_t hash) {
  if (table->count == 0) {
    // Bail out if table is empty
    return NULL;
  }

  // Module the hash by the capacity to get the starting index
  uint32_t index = hash & table->capacity;

  for (;;) {
    Entry *entry = &table->entries[index];

    if (entry->key == NULL) {
      // Stop if find an empty non-tombstone entry
      if (IS_NIL(entry->value)) {
        return NULL;
      }
    } else if (entry->key->length == length && entry->key->hash == hash &&
               memcmp(entry->key->chars, chars, length) == 0) {
      // Found match
      return entry->key;
    }

    // Keep probing
    index = (index + 1) & table->capacity;
  }
}

void tableRemoveWhite(Table *table) {
  for (int i = 0; i <= table->capacity; i++) {
    Entry *entry = &table->entries[i];

    // If the entry is non-empty and the key is not marked, remove it from the
    // table
    if (entry->key != NULL && !entry->key->obj.isMarked) {
      tableDelete(table, entry->key);
    }
  }
}

void markTable(Table *table) {
  for (int i = 0; i <= table->capacity; i++) {
    Entry *entry = &table->entries[i];

    // Mark the key (string objects are heap-allocated)
    markObject((Obj *)entry->key);

    // Mark the value (which may be a heap-allocated object)
    markValue(entry->value);
  }
}
