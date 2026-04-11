#ifndef clox_vm_h
#define clox_vm_h

#include "chunk.h"
#include "object.h"
#include "table.h"
#include "value.h"

#define FRAMES_MAX 64
#define STACK_MAX (FRAMES_MAX * UINT8_COUNT)

typedef struct {
  ObjClosure *closure;
  uint8_t *ip;
  Value *slots;
} CallFrame;

typedef struct {
  CallFrame frames[FRAMES_MAX];
  int frameCount;

  Value stack[STACK_MAX];
  Value *stackTop;

  // Global variables
  Table globals;

  // String interning
  Table strings;

  // Head of the linked list of open upvalues pointing to variables on the stack
  ObjUpvalue *openUpvalues;

  // Total number of bytes currently allocated
  size_t bytesAllocated;
  // Threshold to trigger the next GC cycle
  size_t nextGC;

  // Pointer to the head of the linked list
  Obj *objects;

  int grayCount;
  int grayCapacity;
  // Stack of gray objects to trace during GC
  Obj **grayStack;
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
