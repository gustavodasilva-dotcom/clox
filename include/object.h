#ifndef clox_object_h
#define clox_object_h

#include "common.h"
#include "value.h"

#define OBJ_TYPE(value) (AS_OBJ(value)->type)

#define IS_STRING(value) isObjType(value, OBJ_STRING)

#define AS_STRING(value) ((ObjString *)AS_OBJ(value))
#define AS_CSTRING(value) (((ObjString *)AS_OBJ(value))->chars)

typedef enum {
  OBJ_STRING,
} ObjType;

// base struct for all heap-allocated objects
struct Obj {
  ObjType type;
};

// string object (an "inheritance" of 'Obj')
struct ObjString {
  Obj obj; // alignment: 'Obj' must be first field
  int length;
  char *chars;
};

/// @brief Allocates a new string object on the heap.
/// @param chars The string
/// @param length The length of the string
/// @return A pointer to the heap-allocated string object
ObjString *takeString(char *chars, int length);

/// @brief Copies a string into heap-allocated memory.
/// @param chars The string to copy
/// @param length The length of the string
/// @return A pointer to the heap-allocated string object
ObjString *copyString(const char *chars, int length);

void printObject(Value value);

static inline bool isObjType(Value value, ObjType type) {
  return IS_OBJ(value) && AS_OBJ(value)->type == type;
}

#endif
