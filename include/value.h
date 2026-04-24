#ifndef clox_value_h
#define clox_value_h

#include <string.h>

#include "common.h"

// The maximum number of elements an array can hold (around 16 million elements
// or 128 MB).
#define MAX_ARRAY_SIZE (1 << 24)

// Forward declaration of Obj (to avoid circular dependency).
typedef struct Obj Obj;

// Forward declaration of ObjString (to avoid circular dependency).
typedef struct ObjString ObjString;

#ifdef NAN_BOXING

// The sign bit is the highest bit of a 64-bit value. We use it to distinguish
// between Obj pointers and other types of values. Obj pointers have the sign
// bit set, while other values do not.
#define SIGN_BIT ((uint64_t)0x8000000000000000)

// A quiet NaN pattern used as a prefix for NaN-boxed values.
// Exponent is all 1s (NaN), and the top bit of the fraction is set
// to ensure a quiet NaN. The remaining bits are used for type tags
// and payload data.
#define QNAN ((uint64_t)0x7ffc000000000000)

// Type tag for a nil value (01 in the lowest 2 bits of the fraction).
#define TAG_NIL 1

// Type tag for a false value (10 in the lowest 2 bits of the fraction).
#define TAG_FALSE 2

// Type tag for a true value (11 in the lowest 2 bits of the fraction).
#define TAG_TRUE 3

// A single 64-bit value can represent a double, a bool, nil, or a pointer to an
// Obj. NaN values have the exponent bits all set to 1.
typedef uint64_t Value;

#define IS_BOOL(value) (((value) | 1) == TRUE_VAL)
#define IS_NIL(value) ((value) == NIL_VAL)
#define IS_NUMBER(value) (((value)&QNAN) != QNAN)
#define IS_OBJ(value) (((value) & (QNAN | SIGN_BIT)) == (QNAN | SIGN_BIT))

#define AS_BOOL(value) ((value) == TRUE_VAL)
#define AS_NUMBER(value) valueToNum(value)
#define AS_OBJ(value) ((Obj *)(uintptr_t)((value) & ~(SIGN_BIT | QNAN)))

#define BOOL_VAL(b) ((b) ? TRUE_VAL : FALSE_VAL)
#define FALSE_VAL ((Value)(uint64_t)(QNAN | TAG_FALSE))
#define TRUE_VAL ((Value)(uint64_t)(QNAN | TAG_TRUE))
#define NIL_VAL ((Value)(uint64_t)(QNAN | TAG_NIL))
#define NUMBER_VAL(num) numToValue(num)
#define OBJ_VAL(obj) (Value)(SIGN_BIT | QNAN | (uint64_t)(uintptr_t)(obj))

/// @brief Converts a Value to a double.
/// @param value The Value to convert
/// @return The converted double
///
/// Uses type punning to copy the bits of the Value, which is a uint64_t, into a
/// double.
static inline double valueToNum(Value value) {
  double num;

  // Copy the bits of the Value, which is a uint64_t, into a double
  memcpy(&num, &value, sizeof(Value));

  return num;
}

/// @brief Converts a double to a Value.
/// @param num The double to convert
/// @return The converted Value
///
/// Uses type punning to copy the bits of the double into a Value, which is a
/// uint64_t.
static inline Value numToValue(double num) {
  Value value;

  // Copy the bits of the double into the Value, which is a uint64_t
  memcpy(&value, &num, sizeof(double));

  return value;
}

#else

// Type tags for Value.
typedef enum {
  VAL_BOOL,
  VAL_NIL,
  VAL_NUMBER,

  // Heap-allocated types (memory managed)
  VAL_OBJ
} ValueType;

// A dynamically typed value, which can hold a bool, nil, number, or Obj
// pointer.
typedef struct {
  ValueType type;
  union {
    bool boolean;
    double number;

    // Pointer to heap-allocated object
    Obj *obj;
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

#endif

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
