#ifndef clox_object_h
#define clox_object_h

#include "chunk.h"
#include "common.h"
#include "value.h"

#define OBJ_TYPE(value) (AS_OBJ(value)->type)

#define IS_FUNCTION(value) isObjType(value, OBJ_FUNCTION)
#define IS_NATIVE(value) isObjType(value, OBJ_NATIVE)
#define IS_STRING(value) isObjType(value, OBJ_STRING)

#define AS_FUNCTION(value) ((ObjFunction *)AS_OBJ(value))
#define AS_NATIVE(value) (((ObjNative *)AS_OBJ(value))->function)
#define AS_STRING(value) ((ObjString *)AS_OBJ(value))
#define AS_CSTRING(value) (((ObjString *)AS_OBJ(value))->chars)

// The heap-allocated object types.
typedef enum {
  OBJ_FUNCTION,
  OBJ_NATIVE,
  OBJ_STRING,
} ObjType;

// Base struct for all heap-allocated objects.
struct Obj {
  ObjType type;

  // Linked list of all heap-allocated objects
  struct Obj *next;
};

// A function object.
typedef struct {
  // Obj header; align with Obj for easy casting
  Obj obj;

  // Number of parameters
  int arity;

  Chunk chunk;
  ObjString *name;
} ObjFunction;

// A native function type, which is a pointer to a C function, that takes an
// argument count and a pointer to the first argument on the stack.
typedef Value (*NativeFn)(int argCount, Value *args);

// A native function object.
typedef struct {
  // Obj header; align with Obj for easy casting
  Obj obj;

  NativeFn function;
} ObjNative;

// A string object.
struct ObjString {
  // Obj header; align with Obj for easy casting
  Obj obj;

  int length;
  char *chars;
  uint32_t hash;
};

/// @brief Allocates a new function object on the heap.
/// @return A pointer to the heap-allocated function object
ObjFunction *newFunction();

/// @brief Allocates a new native function object on the heap.
/// @param function The C function to call when the native function is called
/// @return A pointer to the heap-allocated native function object
ObjNative *newNative(NativeFn function);

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
