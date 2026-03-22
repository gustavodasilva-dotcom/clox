#ifndef clox_vm_h
#define clox_vm_h

#include "chunk.h"
#include "table.h"
#include "value.h"

#define STACK_MAX 256

typedef struct {
  Chunk *chunk;
  uint8_t
      *ip; // Instruction pointer (points to the next instruction to execute)
  Value stack[STACK_MAX];
  Value *stackTop;

  Table globals; // Global variables
  Table strings; // String interning

  Obj *objects; // Pointer to the head of the linked list
} VM;

typedef enum {
  INTERPRET_OK,
  INTERPRET_COMPILE_ERROR,
  INTERPRET_RUNTIME_ERROR
} InterpretResult;

// Expose global variable externally
extern VM vm;

void initVM();
void freeVM();
InterpretResult interpret(const char *source);
void push(Value value);
Value pop();

#endif
