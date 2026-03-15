#ifndef clox_value_h
#define clox_value_h

#include "common.h"

// Forward declare (to avoid circular dependency)
typedef struct Obj Obj;
typedef struct ObjString ObjString;

// Type tag
typedef enum {
  VAL_BOOL,
  VAL_NIL,
  VAL_NUMBER,
  VAL_OBJ // heap-allocated types (memory managed)
} ValueType;

typedef struct {
  ValueType type;
  union {
    bool boolean;
    double number;
    Obj *obj; // pointer to heap-allocated object
  } as;
} Value;

// Type checkers
#define IS_BOOL(value) ((value).type == VAL_BOOL)
#define IS_NIL(value) ((value).type == VAL_NIL)
#define IS_NUMBER(value) ((value).type == VAL_NUMBER)
#define IS_OBJ(value) ((value).type == VAL_OBJ)

// Type unwrapppers (return raw C type from a Value)
#define AS_OBJ(value) ((value).as.obj)
#define AS_BOOL(value) ((value).as.boolean)
#define AS_NUMBER(value) ((value).as.number)

// Type wrappers (create a Value from a raw C type)
#define BOOL_VAL(value) ((Value){VAL_BOOL, {.boolean = value}})
#define NIL_VAL ((Value){VAL_NIL, {.number = 0}})
#define NUMBER_VAL(value) ((Value){VAL_NUMBER, {.number = value}})
#define OBJ_VAL(object) ((Value){VAL_OBJ, {.obj = (Obj *)object}})

typedef struct {
  int capacity;
  int count;
  Value *values;
} ValueArray;

bool valuesEqual(Value a, Value b);
void initValueArray(ValueArray *array);
void freeValueArray(ValueArray *array);
void writeValueArray(ValueArray *array, Value value);
void printValue(Value value);

#endif
