#ifndef clox_object_h
#define clox_object_h

#include "chunk.h"
#include "common.h"
#include "table.h"
#include "value.h"

#define OBJ_TYPE(value) (AS_OBJ(value)->type)

#define IS_BOUND_METHOD(value) isObjType(value, OBJ_BOUND_METHOD)
#define IS_CLASS(value) isObjType(value, OBJ_CLASS)
#define IS_CLOSURE(value) isObjType(value, OBJ_CLOSURE)
#define IS_FUNCTION(value) isObjType(value, OBJ_FUNCTION)
#define IS_INSTANCE(value) isObjType(value, OBJ_INSTANCE)
#define IS_NATIVE(value) isObjType(value, OBJ_NATIVE)
#define IS_STRING(value) isObjType(value, OBJ_STRING)

#define AS_BOUND_METHOD(value) ((ObjBoundMethod *)AS_OBJ(value))
#define AS_CLASS(value) ((ObjClass *)AS_OBJ(value))
#define AS_CLOSURE(value) ((ObjClosure *)AS_OBJ(value))
#define AS_FUNCTION(value) ((ObjFunction *)AS_OBJ(value))
#define AS_INSTANCE(value) ((ObjInstance *)AS_OBJ(value))
#define AS_NATIVE(value) (((ObjNative *)AS_OBJ(value)))
#define AS_STRING(value) ((ObjString *)AS_OBJ(value))
#define AS_CSTRING(value) (((ObjString *)AS_OBJ(value))->chars)

// The heap-allocated object types.
typedef enum {
  OBJ_BOUND_METHOD,
  OBJ_CLASS,
  OBJ_CLOSURE,
  OBJ_FUNCTION,
  OBJ_INSTANCE,
  OBJ_NATIVE,
  OBJ_STRING,
  OBJ_UPVALUE
} ObjType;

// Base struct for all heap-allocated objects.
struct Obj {
  ObjType type;

  // Whether the object is marked as reachable during GC
  bool isMarked;

  // Linked list of all heap-allocated objects
  struct Obj *next;
};

// A function object.
typedef struct {
  // Obj header; align with Obj for easy casting
  Obj obj;

  // Number of parameters
  int arity;

  int upvalueCount;

  Chunk chunk;
  ObjString *name;
} ObjFunction;

// A native function type, which is a pointer to a C function, that takes an
// argument count and a pointer to the first argument on the stack.
typedef bool (*NativeFn)(int argCount, Value *args, Value *result);

// A native function object.
typedef struct {
  // Obj header; align with Obj for easy casting
  Obj obj;

  // Number of parameters, or, in variadic native functions, the minimum number
  // of arguments required to call the function
  int arity;

  // Whether the native function is variadic (takes a variable number of
  // arguments)
  bool isVariadic;

  // Pointer to the C function to call when the native function is called
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

// An upvalue object.
typedef struct ObjUpvalue {
  // Obj header; align with Obj for easy casting
  Obj obj;

  Value *location;

  // When the upvalue is closed, the closed-over value is stored here, and
  // 'location' points to this instead of the stack
  Value closed;

  // List of open upvalues pointing to variables on the stack
  struct ObjUpvalue *next;
} ObjUpvalue;

// A closure object.
typedef struct {
  // Obj header; align with Obj for easy casting
  Obj obj;

  ObjFunction *function;

  ObjUpvalue **upvalues;
  int upvalueCount;
} ObjClosure;

// A class object.
typedef struct {
  // Obj header; align with Obj for easy casting
  Obj obj;

  // Name of the class (not owned by the class object)
  ObjString *name;

  // Methods of the class
  Table methods;
} ObjClass;

// An instance object.
typedef struct {
  // Obj header; align with Obj for easy casting
  Obj obj;

  // Pointer to the class of the instance
  ObjClass *klass;

  // Instance fields
  Table fields;
} ObjInstance;

// A bound method object.
typedef struct {
  // Obj header; align with Obj for easy casting
  Obj obj;

  // The receiver of the method (the instance on which the method is called);
  // not owned by the bound method object
  Value receiver;

  // The method closure (not owned by the bound method object)
  ObjClosure *method;
} ObjBoundMethod;

/// @brief Allocates a new bound method object on the heap.
/// @param receiver The receiver of the method
/// @param method The method closure
/// @return A pointer to the heap-allocated bound method object
ObjBoundMethod *newBoundMethod(Value receiver, ObjClosure *method);

/// @brief Allocates a new class object on the heap.
/// @param name The name of the class
/// @return A pointer to the heap-allocated class object
ObjClass *newClass(ObjString *name);

/// @brief Allocates a new closure object on the heap.
/// @param function The function to wrap in the closure
/// @return A pointer to the heap-allocated closure object
ObjClosure *newClosure(ObjFunction *function);

/// @brief Allocates a new function object on the heap.
/// @return A pointer to the heap-allocated function object
ObjFunction *newFunction();

/// @brief Allocates a new instance object on the heap.
/// @param klass The class of the instance
/// @return A pointer to the heap-allocated instance object
///
/// The fields of an instance are added at runtime, so the instance is
/// initialized with an empty fields hash table.
ObjInstance *newInstance(ObjClass *klass);

/// @brief Allocates a new native function object on the heap.
/// @param arity The number of parameters the native function takes
/// @param isVariadic Whether the native function is variadic (takes a variable
/// number of arguments)
/// @param function The C function to call when the native function is
/// called
/// @return A pointer to the heap-allocated native function object
ObjNative *newNative(int arity, bool isVariadic, NativeFn function);

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

/// @brief Allocates a new upvalue object on the heap.
/// @param slot A pointer to the variable being captured
/// @return A pointer to the heap-allocated upvalue object
ObjUpvalue *newUpvalue(Value *slot);

/// @brief Prints a heap-allocated object to standard output.
/// @param value The object to print
void printObject(Value value);

static inline bool isObjType(Value value, ObjType type) {
  return IS_OBJ(value) && AS_OBJ(value)->type == type;
}

#endif
