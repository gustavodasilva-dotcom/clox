#include <limits.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "common.h"
#include "compiler.h"
#include "debug.h"
#include "memory.h"
#include "object.h"
#include "vm.h"

VM vm;

/// @brief Resets the VM's stack and call frame state, typically after a runtime
/// error.
static void resetStack() {
  // Set the stack top to the beginning of the stack (decays to a pointer to the
  // first element)
  vm.stackTop = vm.stack;

  // Empty call frame stack
  vm.frameCount = 0;

  vm.openUpvalues = NULL;
}

/// @brief Prints a runtime error message and resets the VM's stack and call
/// frame state.
/// @param format The format string for the error message
/// @param ... The arguments for the format string
static void runtimeError(const char *format, ...) {
  // Format error message
  va_list args;
  va_start(args, format);
  vfprintf(stderr, format, args);
  va_end(args);

  fputs("\n", stderr);

  // Print stack trace (from most recent call frame to oldest)
  for (int i = vm.frameCount - 1; i >= 0; i--) {
    // Get call frame at index
    CallFrame *frame = &vm.frames[i];

    // Get the function being called in the current call frame
    ObjFunction *function = frame->closure->function;

    // -1 because the instruction pointer is sitting on the next instruction to
    // be executed
    size_t instruction = frame->ip - function->chunk.code - 1;

    fprintf(stderr, "[line %d] in ", function->chunk.lines[instruction]);

    if (function->name == NULL) {
      fprintf(stderr, "script\n");
    } else {
      fprintf(stderr, "%s()\n", function->name->chars);
    }
  }

  resetStack();
}

/// @brief Implements the native `print` function, which prints its argument to
/// the console.
/// @param argCount The number of arguments passed to the function
/// @param args A pointer to the first argument on the stack
/// @param value A pointer to a Value where the result of the function call
/// @return `true` if the function executed successfully, `false` if it
/// encountered an error
static bool printNative(int argCount, Value *args, Value *value) {
  printValue(args[0]);
  printf("\n");

  *value = NIL_VAL;
  return true;
}

/// @brief Implements the native `clock` function.
/// @param argCount The number of arguments passed to the function
/// @param args A pointer to the first argument on the stack
/// @param value A pointer to a Value where the result of the function call
/// @return `true` if the function executed successfully, `false` if it
/// encountered an error
static bool clockNative(int argCount, Value *args, Value *value) {
  *value = NUMBER_VAL((double)clock() / CLOCKS_PER_SEC);
  return true;
}

#define ASSERT_TYPE(type, value, message, ...)                                 \
  if (!type(value)) {                                                          \
    runtimeError(message, ##__VA_ARGS__);                                      \
    return false;                                                              \
  }

#define MIN_MAX_NATIVE(argCount, args, value, op)                              \
  if (argCount >= 1 && !IS_NUMBER(args[0])) {                                  \
    runtimeError("Arguments must be numbers.");                                \
    return false;                                                              \
  }                                                                            \
  double min = AS_NUMBER(args[0]);                                             \
  if (argCount == 1) {                                                         \
    *value = NUMBER_VAL(min);                                                  \
    return true;                                                               \
  }                                                                            \
  for (int i = 1; i < argCount; i++) {                                         \
    ASSERT_TYPE(IS_NUMBER, args[i], "Arguments must be numbers.")              \
    double number = AS_NUMBER(args[i]);                                        \
    if (number op min) {                                                       \
      min = number;                                                            \
    }                                                                          \
  }                                                                            \
  *value = NUMBER_VAL(min);                                                    \
  return true;

/// @brief Implements the native `min` function, which returns the minimum of
/// its arguments.
/// @param argCount The number of arguments passed to the function
/// @param args A pointer to the first argument on the stack
/// @param value A pointer to a Value where the result of the function call
/// @return `true` if the function executed successfully, `false` if it
/// encountered an error
static bool minNative(int argCount, Value *args, Value *value) {
  MIN_MAX_NATIVE(argCount, args, value, <);
}

/// @brief Implements the native `max` function, which returns the maximum of
/// its arguments.
/// @param argCount The number of arguments passed to the function
/// @param args A pointer to the first argument on the stack
/// @param value A pointer to a Value where the result of the function call
/// @return `true` if the function executed successfully, `false` if it
/// encountered an error
static bool maxNative(int argCount, Value *args, Value *value) {
  MIN_MAX_NATIVE(argCount, args, value, >);
}

#define UNARY_NATIVE(args, value, func)                                        \
  ASSERT_TYPE(IS_NUMBER, args[0], "Argument must be a number.")                \
  *value = NUMBER_VAL(func(AS_NUMBER(args[0])));                               \
  return true;

/// @brief Implements the native `abs` function, which returns the absolute
/// value of a number.
/// @param argCount The number of arguments passed to the function
/// @param args A pointer to the first argument on the stack
/// @param value A pointer to a Value where the result of the function call
/// @return `true` if the function executed successfully, `false` if it
/// encountered an error
static bool absNative(int argCount, Value *args, Value *value) {
  UNARY_NATIVE(args, value, fabs);
}

/// @brief Implements the native `sqrt` function, which returns the square root
/// of a number.
/// @param argCount The number of arguments passed to the function
/// @param args A pointer to the first argument on the stack
/// @param value A pointer to a Value where the result of the function call
/// @return `true` if the function executed successfully, `false` if it
/// encountered an error
static bool sqrtNative(int argCount, Value *args, Value *value) {
  UNARY_NATIVE(args, value, sqrt);
}

/// @brief Implements the native `ceiling` function, which returns the smallest
/// integer greater than or equal to a number.
/// @param argCount The number of arguments passed to the function
/// @param args A pointer to the first argument on the stack
/// @param value A pointer to a Value where the result of the function call
/// @return `true` if the function executed successfully, `false` if it
/// encountered an error
static bool ceilingNative(int argCount, Value *args, Value *value) {
  UNARY_NATIVE(args, value, ceil);
}

/// @brief Implements the native `floor` function, which returns the largest
/// integer less than or equal to a number.
/// @param argCount The number of arguments passed to the function
/// @param args A pointer to the first argument on the stack
/// @param value A pointer to a Value where the result of the function call
/// @return `true` if the function executed successfully, `false` if it
/// encountered an error
static bool floorNative(int argCount, Value *args, Value *value) {
  UNARY_NATIVE(args, value, floor);
}

/// @brief Implements the native `pow` function, which returns the result of
/// raising the first argument to the power of the second argument.
/// @param argCount The number of arguments passed to the function
/// @param args A pointer to the first argument on the stack
/// @param value A pointer to a Value where the result of the function call
/// @return `true` if the function executed successfully, `false` if it
/// encountered an error
static bool powNative(int argCount, Value *args, Value *value) {
  if (!IS_NUMBER(args[0]) || !IS_NUMBER(args[1])) {
    runtimeError("Operands must be a number.");
    return false;
  }

  *value = NUMBER_VAL(pow(AS_NUMBER(args[0]), AS_NUMBER(args[1])));
  return true;
}

/// @brief Implements the native `len` function, which returns the length of a
/// string.
/// @param argCount The number of arguments passed to the function
/// @param args A pointer to the first argument on the stack
/// @param value A pointer to a Value where the result of the function call
/// @return `true` if the function executed successfully, `false` if it
/// encountered an error
static bool lenNative(int argCount, Value *args, Value *value) {
  ASSERT_TYPE(IS_STRING, args[0], "Argument must be a string.");
  *value = NUMBER_VAL(AS_STRING(args[0])->length);
  return true;
}

/// @brief Implements the native `substring` function, which returns a substring
/// of a string given a starting index and length.
/// @param argCount The number of arguments passed to the function
/// @param args A pointer to the first argument on the stack
/// @param value A pointer to a Value where the result of the function call
/// @return `true` if the function executed successfully, `false` if it
/// encountered an error
static bool substringNative(int argCount, Value *args, Value *value) {
  ASSERT_TYPE(IS_STRING, args[0], "Argument 1 must be a string.");
  ASSERT_TYPE(IS_NUMBER, args[1], "Argument 2 must be a number.");
  ASSERT_TYPE(IS_NUMBER, args[2], "Argument 3 must be a number.");

  ObjString *string = AS_STRING(args[0]);

  int startIndex = (int)AS_NUMBER(args[1]);
  int length = (int)AS_NUMBER(args[2]);

  if (startIndex < 0 || length < 0) {
    runtimeError("Start index and length must be non-negative.");
    return false;
  }

  if (startIndex > string->length) {
    runtimeError("Start index out of bounds.");
    return false;
  }

  if (startIndex + length > string->length) {
    runtimeError("Substring length out of bounds.");
    return false;
  }

  char *chars = ALLOCATE(char, length + 1);
  memcpy(chars, string->chars + startIndex, length);
  chars[length] = '\0';

  *value = OBJ_VAL(takeString(chars, length));
  return true;
}

/// @brief Implements the native `concat` function, which returns the
/// concatenation of multiple strings.
/// @param argCount The number of arguments passed to the function
/// @param args A pointer to the first argument on the stack
/// @param value A pointer to a Value where the result of the function call
/// @return `true` if the function executed successfully, `false` if it
/// encountered an error
static bool concatNative(int argCount, Value *args, Value *value) {
  if (argCount == 1) {
    ASSERT_TYPE(IS_STRING, args[0], "Argument 1 must be a string.");
    *value = args[0];
    return true;
  }

  int length = 0;
  ObjString *strings[argCount];

  for (int i = 0; i < argCount; i++) {
    ASSERT_TYPE(IS_STRING, args[i], "Argument %d must be a string.", i + 1);

    strings[i] = AS_STRING(args[i]);

    if (length > INT_MAX - strings[i]->length) {
      runtimeError("Concatenated string is too long.");
      return false;
    }

    length += strings[i]->length;
  }

  char *chars = ALLOCATE(char, length + 1);
  char *cursor = chars;

  for (int i = 0; i < argCount; i++) {
    memcpy(cursor, strings[i]->chars, strings[i]->length);
    cursor += strings[i]->length;
  }

  chars[length] = '\0';

  *value = OBJ_VAL(takeString(chars, length));
  return true;
}

/// @brief Defines a native function in the global variables hash table.
/// @param name The name of the native function
/// @param arity The number of parameters the native function takes
/// @param isVariadic Whether the native function is variadic (takes a variable
/// number of arguments)
/// @param function A pointer to the native function
static void defineNative(const char *name, int arity, bool isVariadic,
                         NativeFn function) {
  // Push the native function's name and the native function itself onto the
  // stack
  push(OBJ_VAL(copyString(name, (int)strlen(name))));
  push(OBJ_VAL(newNative(arity, isVariadic, function)));

  // Define the native function in the global variables hash table
  tableSet(&vm.globals, AS_STRING(vm.stack[0]), vm.stack[1]);

  // Pop the native function and its name from the stack
  pop();
  pop();
}

/// @brief Returns a value from the stack without popping it.
/// @param distance How far down the stack to peek; 0 is the top)
/// @return The value at the given distance from the top of the stack
static Value peek(int distance) { return vm.stackTop[-1 - distance]; }

/// @brief Sets up a call frame to call the given closure.
/// @param function The closure to call
/// @param argCount The number of arguments being passed to the closure
/// @return `true` if the call was successful, `false` if it failed
static bool call(ObjClosure *closure, int argCount) {
  // Runtime arity check
  if (argCount != closure->function->arity) {
    runtimeError("Expected %d arguments but got %d.", closure->function->arity,
                 argCount);
    return false;
  }

  // Runtime stack overflow check
  if (vm.frameCount == FRAMES_MAX) {
    runtimeError("Stack overflow.");
    return false;
  }

  // Get call frame and set it up to call the given closure
  CallFrame *frame = &vm.frames[vm.frameCount++];
  frame->closure = closure;

  // Point the call frame's instruction pointer to the beginning of the
  // function's bytecode
  frame->ip = closure->function->chunk.code;

  // Line up the call frame's slots with the function's arguments on the stack
  // (-1 for the function itself)
  frame->slots = vm.stackTop - argCount - 1;

  return true;
}

/// @brief Calls a value with the given number of arguments.
/// @param callee The value to call
/// @param argCount The number of arguments being passed to the callee
/// @return `true` if the call was successful, `false` if it failed
static bool callValue(Value callee, int argCount) {
  if (IS_OBJ(callee)) {
    switch (OBJ_TYPE(callee)) {
    case OBJ_BOUND_METHOD: {
      ObjBoundMethod *bound = AS_BOUND_METHOD(callee);

      // Set the slot zero of the call frame (the "this" slot) to the method's
      // receiver
      vm.stackTop[-argCount - 1] = bound->receiver;

      return call(bound->method, argCount);
    }

    case OBJ_CLASS: {
      ObjClass *klass = AS_CLASS(callee);

      // Overwrite the callee on the stack with the new instance
      vm.stackTop[-argCount - 1] = OBJ_VAL(newInstance(klass));

      Value initializer;
      if (tableGet(&klass->methods, vm.initString, &initializer)) {
        // If the class has an initializer method, call it automatically after
        // creating the instance
        return call(AS_CLOSURE(initializer), argCount);
      } else if (argCount != 0) {
        // Otherswise, if the class doesn't have an initializer method, it
        // shouldn't be called with any arguments
        runtimeError("Expected 0 arguments but got %d.", argCount);
        return false;
      }

      return true;
    }

    case OBJ_CLOSURE:
      return call(AS_CLOSURE(callee), argCount);

    case OBJ_NATIVE: {
      // Cast the callee to a native function
      ObjNative *native = AS_NATIVE(callee);

      if ((native->isVariadic && argCount < native->arity) ||
          (!native->isVariadic && argCount != native->arity)) {
        runtimeError("Expected %s%d argument%s but got %d.",
                     native->isVariadic ? "at least " : "", native->arity,
                     native->arity != 1 ? "s" : "", argCount);
        return false;
      }

      // Call the native function
      Value result;
      bool success =
          native->function(argCount, vm.stackTop - argCount, &result);

      // Pop arguments and the callee from the stack
      vm.stackTop -= argCount + 1;

      if (!success) {
        return false;
      }

      // Push the result of the native function call onto the stack
      push(result);

      return true;
    }

    default:
      // Non-callable object type.
      break;
    }
  }

  runtimeError("Can only call functions and classes.");

  return false;
}

/// @brief Invokes a method on a class.
/// @param klass The class on which to invoke the method
/// @param name The name of the method to invoke
/// @param argCount The number of arguments to pass to the method
/// @return `true` if the invocation was successful, `false` otherwise
static bool invokeFromClass(ObjClass *klass, ObjString *name, int argCount) {
  Value method;
  if (!tableGet(&klass->methods, name, &method)) {
    runtimeError("Undefined property \"%s\".", name->chars);
    return false;
  }

  return call(AS_CLOSURE(method), argCount);
}

/// @brief Invokes a method on an instance.
/// @param name The name of the method to invoke
/// @param argCount The number of arguments to pass to the method
/// @return `true` if the invocation was successful, `false` otherwise
static bool invoke(ObjString *name, int argCount) {
  // The receiver of the method is below the arguments on the stack
  Value receiver = peek(argCount);

  ObjInstance *instance = AS_INSTANCE(receiver);

  // Look for the method in the instance's fields first, to allow for method
  // overrides on a per-instance basis
  Value value;
  if (tableGet(&instance->fields, name, &value)) {
    // Set the slot zero of the call frame (the "this" slot) to the instance
    vm.stackTop[-argCount - 1] = value;

    // Call the method closure
    return callValue(value, argCount);
  }

  return invokeFromClass(instance->klass, name, argCount);
}

/// @brief Binds a method to an instance.
/// @param klass The class to which the method belongs
/// @param name The name of the method
/// @return `true` if the method was found and bound, `false` otherwise
///
/// If a method with the given name is found in the class, it replaces the
/// instance on the stack with a new bound method object.
static bool bindMethod(ObjClass *klass, ObjString *name) {
  Value method;
  if (!tableGet(&klass->methods, name, &method)) {
    runtimeError("Undefined property \"%s\".", name->chars);
    return false;
  }

  // The instance is on top of the stack
  ObjBoundMethod *bound = newBoundMethod(peek(0), AS_CLOSURE(method));

  // Pop instance
  pop();

  // Push bound method
  push(OBJ_VAL(bound));

  return true;
}

/// @brief Captures an upvalue from the current function's stack.
/// @param local A pointer to the local variable to capture
/// @return A pointer to the newly created upvalue object
///
/// If an upvalue already exists for the given local variable, it is returned,
/// so that multiple closures capturing the same local variable share the same
/// upvalue object.
static ObjUpvalue *captureUpvalue(Value *local) {
  ObjUpvalue *prevUpvalue = NULL;

  // Start of the linked list of open upvalues
  ObjUpvalue *upvalue = vm.openUpvalues;

  // Traverse the linked list to find an existing upvalue for the given local
  // variable
  while (upvalue != NULL && upvalue->location > local) {
    prevUpvalue = upvalue;
    upvalue = upvalue->next;
  }

  // If an upvalue already exists for the given local variable, return it
  if (upvalue != NULL && upvalue->location == local) {
    return upvalue;
  }

  ObjUpvalue *createdUpvalue = newUpvalue(local);

  // Keep the list sorted by the local variable's address in descending order
  createdUpvalue->next = upvalue;

  if (prevUpvalue == NULL) {
    // When the new upvalue is the first in the list, update the head of the
    // list
    vm.openUpvalues = createdUpvalue;
  } else {
    // Otherwise, link the previous upvalue to the new upvalue
    prevUpvalue->next = createdUpvalue;
  }

  return createdUpvalue;
}

/// @brief Closes an upvalue.
/// @param last A pointer to the last stack slot to close
///
/// Close upvalue by copying the value from the stack to the upvalue object
/// and updating the upvalue's location to point to the closed-over value in
/// the upvalue object instead of the stack.
static void closeUpvalue(Value *last) {
  // Traverse the linked list of open upvalues from top to bottom
  while (vm.openUpvalues != NULL && vm.openUpvalues->location >= last) {
    ObjUpvalue *upvalue = vm.openUpvalues;
    upvalue->closed = *upvalue->location;
    upvalue->location = &upvalue->closed;
    vm.openUpvalues = upvalue->next;
  }
}

/// @brief Defines a method on a class.
/// @param name The name of the method
///
/// After adding the method to the class's methods hash table, the class object
/// is left on the stack as the receiver of the next method definition, so that
/// multiple methods can be defined in a row without having to reload the class
/// object onto the stack.
static void defineMethod(ObjString *name) {
  // The closure method is on top of the stack
  Value method = peek(0);

  // The class is below the method on the stack
  ObjClass *klass = AS_CLASS(peek(1));

  // Define the method in the class's methods hash table
  tableSet(&klass->methods, name, method);

  // Pop the closure method from the stack
  pop();
}

/// @brief Determines if a value is falsey.
/// @param value The value to check
/// @return `true` if the value is falsey, `false` otherwise
static bool isFalsey(Value value) {
  return IS_NIL(value) || (IS_BOOL(value) && !AS_BOOL(value));
}

static void concatenate() {
  // Get the two strings to concatenate from the top of the stack (without
  // popping them, to prevent them from being collected by GC during
  // concatenation)
  ObjString *b = AS_STRING(peek(0));
  ObjString *a = AS_STRING(peek(1));

  // Get length of concatenated string
  int length = a->length + b->length;

  // Allocate memory for it (+1 for null terminator)
  char *chars = ALLOCATE(char, length + 1);

  // Copy left string
  memcpy(chars, a->chars, a->length);

  // Copy right string
  memcpy(chars + a->length, b->chars, b->length);

  // Null-terminate
  chars[length] = '\0';

  // Create concatenated heap-allocated object
  ObjString *result = takeString(chars, length);

  // Pop the two strings from the stack
  pop();
  pop();

  // Push result
  push(OBJ_VAL(result));
}

void initVM() {
  resetStack();

  // When the VM starts, there are no heap-allocated objects
  vm.objects = NULL;

  vm.bytesAllocated = 0;
  // 1 MB initial threshold for GC
  vm.nextGC = 1024 * 1024;

  vm.grayCount = 0;
  vm.grayCapacity = 0;
  vm.grayStack = NULL;

  initTable(&vm.globals);
  initTable(&vm.strings);

  // Initialize it as `NULL` first to avoid marking a non-initialized pointer
  // during GC
  vm.initString = NULL;
  vm.initString = copyString("init", 4);

#define NATIVE_FIXED(name, arity, function)                                    \
  defineNative(name, arity, false, function)
#define NATIVE_VARIADIC(name, arity, function)                                 \
  defineNative(name, arity, true, function)

  NATIVE_FIXED("print", 1, printNative);
  NATIVE_FIXED("clock", 0, clockNative);

  // Math functions
  NATIVE_VARIADIC("min", 1, minNative);
  NATIVE_VARIADIC("max", 1, maxNative);
  NATIVE_FIXED("abs", 1, absNative);
  NATIVE_FIXED("sqrt", 1, sqrtNative);
  NATIVE_FIXED("ceiling", 1, ceilingNative);
  NATIVE_FIXED("floor", 1, floorNative);
  NATIVE_FIXED("pow", 2, powNative);

  // String functions
  NATIVE_FIXED("len", 1, lenNative);
  NATIVE_FIXED("substring", 3, substringNative);
  NATIVE_VARIADIC("concat", 1, concatNative);

#undef NATIVE_FIXED
#undef NATIVE_VARIADIC
}

void freeVM() {
  freeTable(&vm.globals);
  freeTable(&vm.strings);

  // Set copied string to `NULL` before freeing it below
  vm.initString = NULL;

  freeObjects();
}

static InterpretResult run() {
  // Current topmost call frame
  CallFrame *frame = &vm.frames[vm.frameCount - 1];

#define READ_BYTE() (*frame->ip++)
#define READ_SHORT()                                                           \
  (frame->ip += 2, (uint16_t)((frame->ip[-2] << 8) | frame->ip[-1]))
#define READ_CONSTANT()                                                        \
  (frame->closure->function->chunk.constants.values[READ_BYTE()])
#define READ_STRING() AS_STRING(READ_CONSTANT())

#define BINARY_OP(valueType, op)                                               \
  do {                                                                         \
    if (!IS_NUMBER(peek(0)) || !IS_NUMBER(peek(1))) {                          \
      runtimeError("Operands must be numbers.");                               \
      return INTERPRET_RUNTIME_ERROR;                                          \
    }                                                                          \
    double b = AS_NUMBER(pop());                                               \
    double a = AS_NUMBER(pop());                                               \
    push(valueType(a op b));                                                   \
  } while (false)

  for (;;) {
#ifdef DEBUG_TRACE_INSTRUCTION
    printf("          ");
    for (Value *slot = vm.stack; slot < vm.stackTop; slot++) {
      printf("[ ");
      printValue(*slot);
      printf(" ]");
    }
    printf("\n");
    disassembleInstruction(
        &frame->closure->function->chunk,
        (int)(frame->ip - frame->closure->function->chunk.code));
#endif

    uint8_t instruction;

    switch (instruction = READ_BYTE()) {
    case OP_CONSTANT: {
      Value constant = READ_CONSTANT();
      push(constant);
      break;
    }

    case OP_NIL:
      push(NIL_VAL);
      break;
    case OP_TRUE:
      push(BOOL_VAL(true));
      break;
    case OP_FALSE:
      push(BOOL_VAL(false));
      break;
    case OP_POP:
      pop();
      break;

    case OP_GET_LOCAL: {
      uint8_t slot = READ_BYTE();
      push(frame->slots[slot]);
      break;
    }

    case OP_SET_LOCAL: {
      uint8_t slot = READ_BYTE();
      frame->slots[slot] = peek(0);
      break;
    }

    case OP_GET_GLOBAL: {
      // Read variable name from the constant table
      ObjString *name = READ_STRING();

      Value value;

      // Get variable value from the global variables hash table
      if (!tableGet(&vm.globals, name, &value)) {
        runtimeError("Undefined variable \"%s\".", name->chars);
        return INTERPRET_RUNTIME_ERROR;
      }

      // Push variable value onto the stack
      push(value);
      break;
    }

    case OP_DEFINE_GLOBAL: {
      // Read variable name from the constant table
      ObjString *name = READ_STRING();

      // Look up variable value (the initializer) from the top of the stack
      tableSet(&vm.globals, name, peek(0));

      // Pop value from the stack
      pop();
      break;
    }

    case OP_SET_GLOBAL: {
      // Read variable name from the constant table
      ObjString *name = READ_STRING();

      // Set variable value in the global variables hash table
      if (tableSet(&vm.globals, name, peek(0))) {
        // If the variable was newly defined, delete it and report an error
        tableDelete(&vm.globals, name);

        runtimeError("Undefined variable \"%s\".", name->chars);
        return INTERPRET_RUNTIME_ERROR;
      }
      break;
    }

    case OP_GET_UPVALUE: {
      uint8_t slot = READ_BYTE();
      push(*frame->closure->upvalues[slot]->location);
      break;
    }

    case OP_SET_UPVALUE: {
      uint8_t slot = READ_BYTE();
      *frame->closure->upvalues[slot]->location = peek(0);
      break;
    }

    case OP_GET_PROPERTY: {
      if (!IS_INSTANCE(peek(0))) {
        runtimeError("Only instances have properties.");
        return INTERPRET_RUNTIME_ERROR;
      }

      // Look up instance on the stack (without popping it, to prevent it from
      // being collected by GC)
      ObjInstance *instance = AS_INSTANCE(peek(0));

      // Read property name from the constant table
      ObjString *name = READ_STRING();

      Value value;
      if (tableGet(&instance->fields, name, &value)) {
        // Pop instance
        pop();

        // Push property value onto the stack
        push(value);
        break;
      }

      // If the property is not found in the instance's fields, look for a
      // method with the same name in the instance's class
      if (!bindMethod(instance->klass, name)) {
        return INTERPRET_RUNTIME_ERROR;
      }
      break;
    }

    case OP_SET_PROPERTY: {
      if (!IS_INSTANCE(peek(1))) {
        runtimeError("Only instances have fields.");
        return INTERPRET_RUNTIME_ERROR;
      }

      // Look up instance on the stack (without popping it, to prevent it from
      // being collected by GC)
      ObjInstance *instance = AS_INSTANCE(peek(1));

      // Set property value in the instance's fields hash table
      tableSet(&instance->fields, READ_STRING(), peek(0));

      Value value = pop();

      // Pop instance
      pop();

      push(value);
      break;
    }

    case OP_GET_SUPER: {
      // Look up method name operand from the constant table
      ObjString *name = READ_STRING();

      // Pop and get superclass on the stack (leaving the receiver on the stack
      // for method binding)
      ObjClass *superclass = AS_CLASS(pop());

      // Try to bind the method to the superclass
      if (!bindMethod(superclass, name)) {
        return INTERPRET_RUNTIME_ERROR;
      }
      break;
    }

    case OP_EQUAL: {
      Value b = pop();
      Value a = pop();
      push(BOOL_VAL(valuesEqual(a, b)));
      break;
    }

    case OP_GREATER:
      BINARY_OP(BOOL_VAL, >);
      break;
    case OP_LESS:
      BINARY_OP(BOOL_VAL, <);
      break;

    case OP_ADD: {
      if (IS_STRING(peek(0)) && IS_STRING(peek(1))) {
        // If operands are strings, concatenate
        concatenate();
      } else if (IS_NUMBER(peek(0)) && IS_NUMBER(peek(1))) {
        // If operands are numbers, sum
        double b = AS_NUMBER(pop());
        double a = AS_NUMBER(pop());

        // Push result
        push(NUMBER_VAL(a + b));
      } else {
        // Otherwise, runtime error
        runtimeError("Operands must be two numbers or strings.");
        return INTERPRET_RUNTIME_ERROR;
      }
      break;
    }

    case OP_SUBTRACT:
      BINARY_OP(NUMBER_VAL, -);
      break;
    case OP_MULTIPLY:
      BINARY_OP(NUMBER_VAL, *);
      break;
    case OP_DIVIDE:
      BINARY_OP(NUMBER_VAL, /);
      break;
    case OP_NOT:
      push(BOOL_VAL(isFalsey(pop())));
      break;

    case OP_NEGATE: {
      // Look up last pushed value (the operand)
      if (!IS_NUMBER(peek(0))) {
        runtimeError("Operand must be a number.");
        return INTERPRET_RUNTIME_ERROR;
      }

      // Pop, negate, and push
      push(NUMBER_VAL(-AS_NUMBER(pop())));
      break;
    }

    case OP_JUMP: {
      uint16_t offset = READ_SHORT();
      frame->ip += offset;
      break;
    }

    case OP_LOOP: {
      uint16_t offset = READ_SHORT();
      frame->ip -= offset;
      break;
    }

    case OP_JUMP_IF_FALSE: {
      uint16_t offset = READ_SHORT();

      if (isFalsey(peek(0))) {
        // Jump by moving the instruction pointer forward by the jump offset
        frame->ip += offset;
      }
      break;
    }

    case OP_CALL: {
      // Read argument count (operand)
      int argCount = READ_BYTE();

      if (!callValue(peek(argCount), argCount)) {
        return INTERPRET_RUNTIME_ERROR;
      }

      // Update the current call frame to the callee's
      frame = &vm.frames[vm.frameCount - 1];
      break;
    }

    case OP_INVOKE: {
      // Read the operands: method name and argument count
      ObjString *method = READ_STRING();
      int argCount = READ_BYTE();

      if (!invoke(method, argCount)) {
        return INTERPRET_RUNTIME_ERROR;
      }

      // Update the current call frame to the callee's
      frame = &vm.frames[vm.frameCount - 1];
      break;
    }

    case OP_SUPER_INVOKE: {
      // Read the operands: method name and argument count
      ObjString *method = READ_STRING();
      int argCount = READ_BYTE();

      // Pop and get superclass on the stack
      ObjClass *superclass = AS_CLASS(pop());

      if (!invokeFromClass(superclass, method, argCount)) {
        return INTERPRET_RUNTIME_ERROR;
      }

      // Update the current call frame to the callee's
      frame = &vm.frames[vm.frameCount - 1];
      break;
    }

    case OP_CLOSURE: {
      // Read function from the constant table
      ObjFunction *function = AS_FUNCTION(READ_CONSTANT());

      // Create closure object for the function
      ObjClosure *closure = newClosure(function);

      // Push closure value onto the stack
      push(OBJ_VAL(closure));

      for (int i = 0; i < closure->upvalueCount; i++) {
        uint8_t isLocal = READ_BYTE();
        uint8_t index = READ_BYTE();

        // The current call frame's function is the surrounding function of the
        // closure being created
        if (isLocal) {
          // Capture the upvalue from the current function's stack
          // ('frame->slots' points to the first slot of the current call frame)
          closure->upvalues[i] = captureUpvalue(frame->slots + index);
        } else {
          // Capture the upvalue from the surrounding function's closure
          closure->upvalues[i] = frame->closure->upvalues[index];
        }
      }
      break;
    }

    case OP_CLOSE_UPVALUE: {
      // -1 because 'stackTop' points to the next available slot
      closeUpvalue(vm.stackTop - 1);

      // Discard stack slot of the closed-over variable
      pop();
      break;
    }

    case OP_RETURN: {
      // Get return value from the top of the stack
      Value result = pop();

      // Close any open upvalues pointing to variables in the returning
      // function's stack frame
      closeUpvalue(frame->slots);

      // Discard the call frame for the returning function
      vm.frameCount--;

      // If the call frame stack is empty, it means the VM has finished
      // executing the top level code
      if (vm.frameCount == 0) {
        // Pop the top level script function's value from the stack
        pop();

        return INTERPRET_OK;
      }

      // Move the stack top back to the caller's position (i.e. the function
      // value)
      vm.stackTop = frame->slots;

      // Push the return value onto the stack (replacing the function value)
      push(result);

      // Update the current call frame to the caller's
      frame = &vm.frames[vm.frameCount - 1];
      break;
    }

    case OP_CLASS:
      push(OBJ_VAL(newClass(READ_STRING())));
      break;

    case OP_INHERIT: {
      Value superclass = peek(1);

      if (!IS_CLASS(superclass)) {
        runtimeError("Superclass must be a class.");
        return INTERPRET_RUNTIME_ERROR;
      }

      ObjClass *subclass = AS_CLASS(peek(0));

      // Add superclass's methods to subclass's methods
      tableAddAll(&AS_CLASS(superclass)->methods, &subclass->methods);

      // Pop subclass
      pop();
      break;
    }

    case OP_METHOD:
      defineMethod(READ_STRING());
      break;

    case OP_ARRAY:
      break;
    }
  }

#undef READ_BYTE
#undef READ_SHORT
#undef READ_CONSTANT
#undef READ_STRING
#undef BINARY_OP
}

InterpretResult interpret(const char *source) {
  ObjFunction *function = compile(source);

  if (function == NULL) {
    return INTERPRET_COMPILE_ERROR;
  }

  // Push compiled function onto the stack (so it doesn't get collected)
  push(OBJ_VAL(function));

  ObjClosure *closure = newClosure(function);

  // Pop compiled function from the stack
  pop();

  // Push closure onto the stack
  push(OBJ_VAL(closure));

  // Call the top level script closure with no arguments
  callValue(OBJ_VAL(closure), 0);

  return run();
}

/// @brief Pushes a value onto the stack.
/// @param value The value to push
void push(Value value) {
  // Store the value at the current stack top
  *vm.stackTop = value;
  // Move the stack top up to the next slot
  vm.stackTop++;
}

/// @brief Pops the top value from the stack.
/// @return The value that was popped
Value pop() {
  // Move the stack top down to the most recently used slot
  vm.stackTop--;
  // Return value (no need to remove it; moving the stack top down marks it as
  // "unused")
  return *vm.stackTop;
}
