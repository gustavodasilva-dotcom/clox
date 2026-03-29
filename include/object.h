#ifndef clox_object_h
#define clox_object_h

#include "chunk.h"
#include "common.h"
#include "value.h"

#define OBJ_TYPE(value) (AS_OBJ(value)->type)

#define IS_FUNCTION(value) isObjType(value, OBJ_FUNCTION);
#define IS_STRING(value) isObjType(value, OBJ_STRING)

#define AS_FUNCTION(value) ((ObjFunction *)AS_OBJ(value))
#define AS_STRING(value) ((ObjString *)AS_OBJ(value))
#define AS_CSTRING(value) (((ObjString *)AS_OBJ(value))->chars)

typedef enum {
  OBJ_FUNCTION,
  OBJ_STRING,
} ObjType;

// base struct for all heap-allocated objects
struct Obj {
  ObjType type;
  struct Obj *next; // Linked list of all heap-allocated objects
};

// function object (an "inheritance" of 'Obj')
typedef struct {
  Obj obj;   // Alignment: 'Obj' must be first field
  int arity; // Number of parameters
  Chunk chunk;
  ObjString *name;
} ObjFunction;

// string object (an "inheritance" of 'Obj')
struct ObjString {
  Obj obj; // Alignment: 'Obj' must be first field
  int length;
  char *chars;
  uint32_t hash;
};

/// @brief Allocates a new function object on the heap.
/// @return A pointer to the heap-allocated function object
ObjFunction *newFunction();

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
