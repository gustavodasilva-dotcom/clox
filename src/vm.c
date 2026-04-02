#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "common.h"
#include "compiler.h"
#include "debug.h"
#include "memory.h"
#include "object.h"
#include "vm.h"

VM vm;

static void resetStack() {
  // Set the stack top to the beginning of the stack (decays to a pointer to the
  // first element)
  vm.stackTop = vm.stack;

  // Empty call frame stack
  vm.frameCount = 0;
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
    ObjFunction *function = frame->function;

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

/// @brief Returns a value from the stack without popping it.
/// @param distance How far down the stack to peek; 0 is the top)
/// @return The value at the given distance from the top of the stack
static Value peek(int distance) { return vm.stackTop[-1 - distance]; }

/// @brief Sets up a call frame to call the given function.
/// @param function The function to call
/// @param argCount The number of arguments being passed to the function
/// @return `true` if the call was successful, `false` if it failed
static bool call(ObjFunction *function, int argCount) {
  // Runtime arity check
  if (argCount != function->arity) {
    runtimeError("Expected %d arguments but got %d.", function->arity,
                 argCount);
    return false;
  }

  // Runtime stack overflow check
  if (vm.frameCount == FRAMES_MAX) {
    runtimeError("Stack overflow.");
    return false;
  }

  // Get call frame and set it up to call the given function
  CallFrame *frame = &vm.frames[vm.frameCount++];
  frame->function = function;

  // Point the call frame's instruction pointer to the beginning of the
  // function's bytecode
  frame->ip = function->chunk.code;

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
    case OBJ_FUNCTION:
      return call(AS_FUNCTION(callee), argCount);
    default:
      // Non-callable object type.
      break;
    }
  }

  runtimeError("Can only call functions and classes.");

  return false;
}

static bool isFalsey(Value value) {
  return IS_NIL(value) || (IS_BOOL(value) && !AS_BOOL(value));
}

static void concatenate() {
  // Pop operands
  ObjString *b = AS_STRING(pop());
  ObjString *a = AS_STRING(pop());

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

  // Push result
  push(OBJ_VAL(result));
}

void initVM() {
  resetStack();

  // When the VM starts, there are no heap-allocated objects
  vm.objects = NULL;

  initTable(&vm.globals);
  initTable(&vm.strings);
}

void freeVM() {
  freeTable(&vm.globals);
  freeTable(&vm.strings);
  freeObjects();
}

static InterpretResult run() {
  // Current topmost call frame
  CallFrame *frame = &vm.frames[vm.frameCount - 1];

#define READ_BYTE() (*frame->ip++)
#define READ_SHORT()                                                           \
  (frame->ip += 2, (uint16_t)((frame->ip[-2] << 8) | frame->ip[-1]))
#define READ_CONSTANT() (frame->function->chunk.constants.values[READ_BYTE()])
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
    disassembleInstruction(&frame->function->chunk,
                           (int)(frame->ip - frame->function->chunk.code));
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
      // Read variable name from the constant pool
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
      // Read variable name from the constant pool
      ObjString *name = READ_STRING();

      // Look up variable value (the initializer) from the top of the stack
      tableSet(&vm.globals, name, peek(0));

      // Pop value from the stack
      pop();
      break;
    }

    case OP_SET_GLOBAL: {
      // Read variable name from the constant pool
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

    case OP_RETURN: {
      // Get return value from the top of the stack
      Value result = pop();

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

  // Push compiled function onto the stack so it can be called by the VM's main
  // loop
  push(OBJ_VAL(function));

  // Call the top level script function with no arguments
  callValue(OBJ_VAL(function), 0);

  return run();
}

void push(Value value) {
  // Store the value at the current stack top
  *vm.stackTop = value;
  // Move the stack top up to the next slot
  vm.stackTop++;
}

Value pop() {
  // Move the stack top down to the most recently used slot
  vm.stackTop--;
  // Return value (no need to remove it; moving the stack top down marks it as
  // "unused")
  return *vm.stackTop;
}
