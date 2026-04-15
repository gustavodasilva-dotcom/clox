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

/// @brief Implements the native `clock` function.
/// @param argCount The number of arguments passed to the function
/// @param args A pointer to the first argument on the stack
/// @return The current CPU time in seconds
static Value clockNative(int argCount, Value *args) {
  return NUMBER_VAL((double)clock() / CLOCKS_PER_SEC);
}

static void resetStack() {
  // Set the stack top to the beginning of the stack (decays to a pointer to the
  // first element)
  vm.stackTop = vm.stack;

  // Empty call frame stack
  vm.frameCount = 0;

  vm.openUpvalues = NULL;
}

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

/// @brief Defines a native function in the global variables hash table.
/// @param name The name of the native function
/// @param function A pointer to the native function
static void defineNative(const char *name, NativeFn function) {
  // Push the native function's name and the native function itself onto the
  // stack
  push(OBJ_VAL(copyString(name, (int)strlen(name))));
  push(OBJ_VAL(newNative(function)));

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
      NativeFn native = AS_NATIVE(callee);

      // Call the native function
      Value result = native(argCount, vm.stackTop - argCount);

      // Pop arguments and the callee from the stack
      vm.stackTop -= argCount + 1;

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

  defineNative("clock", clockNative);
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

    case OP_PRINT: {
      printValue(pop());
      printf("\n");
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

    case OP_METHOD:
      defineMethod(READ_STRING());
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
