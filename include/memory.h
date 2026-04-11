#ifndef clox_memory_h
#define clox_memory_h

#include "common.h"
#include "object.h"

#define ALLOCATE(type, count)                                                  \
  (type *)reallocate(NULL, 0, sizeof(type) * (count));

#define FREE(type, pointer) reallocate(pointer, sizeof(type), 0)

#define GROW_CAPACITY(capacity) ((capacity) < 8 ? 8 : (capacity)*2)

#define GROW_ARRAY(type, pointer, oldCount, newCount)                          \
  (type *)reallocate(pointer, sizeof(type) * (oldCount),                       \
                     sizeof(type) * (newCount))

#define FREE_ARRAY(type, pointer, oldCount)                                    \
  reallocate(pointer, sizeof(type) * (oldCount), 0)

/// @brief Reallocate memory for an object.
/// @param pointer Pointer to the existing memory block
/// @param oldSize Size of the existing memory block
/// @param newSize Size of the new memory block
/// @return Pointer to the reallocated memory block
///
/// This function handles memory allocation, deallocation, and resizing.
void *reallocate(void *pointer, size_t oldSize, size_t newSize);

/// @brief Marks an object as reachable.
/// @param object The object to mark
void markObject(Obj *object);

/// @brief Marks a value as reachable.
/// @param value The value to mark
void markValue(Value value);

/// @brief Collect garbage and free unused memory.
void collectGarbage();

/// @brief Free all heap-allocated objects.
void freeObjects();

#endif
