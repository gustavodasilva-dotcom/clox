#include <stdio.h>
#include <string.h>

#include "memory.h"
#include "object.h"
#include "table.h"
#include "value.h"
#include "vm.h"

#define ALLOCATE_OBJ(type, objectType)                                         \
  (type *)allocateObject(sizeof(type), objectType)

static Obj *allocateObject(size_t size, ObjType type) {
  // Heap-allocate object (size is determined by the caller)
  Obj *object = (Obj *)reallocate(NULL, 0, size);

  // Set type
  object->type = type;

  // Initialize as unmarked (not yet determined to be reachable)
  object->isMarked = false;

  // Point the new object to the current head of the list
  object->next = vm.objects;

  // Update the head to point to the new object
  vm.objects = object;

#ifdef DEBUG_LOG_GC
  printf("%p allocate %ld for %d\n", (void *)object, size, type);
#endif

  return object;
}

ObjClosure *newClosure(ObjFunction *function) {
  // Dynamically allocate the array of upvalue pointers on the heap
  ObjUpvalue **upvalues = ALLOCATE(ObjUpvalue *, function->upvalueCount);

  // Initialize the array to NULL
  for (int i = 0; i < function->upvalueCount; i++) {
    upvalues[i] = NULL;
  }

  ObjClosure *closure = ALLOCATE_OBJ(ObjClosure, OBJ_CLOSURE);
  closure->function = function;
  closure->upvalues = upvalues;
  closure->upvalueCount = function->upvalueCount;

  return closure;
}

ObjFunction *newFunction() {
  ObjFunction *function = ALLOCATE_OBJ(ObjFunction, OBJ_FUNCTION);

  function->arity = 0;
  function->upvalueCount = 0;
  function->name = NULL;
  initChunk(&function->chunk);

  return function;
}

ObjNative *newNative(NativeFn function) {
  ObjNative *native = ALLOCATE_OBJ(ObjNative, OBJ_NATIVE);
  native->function = function;

  return native;
}

static ObjString *allocateString(char *chars, int length, uint32_t hash) {
  // Heap-allocate the string object
  ObjString *string = ALLOCATE_OBJ(ObjString, OBJ_STRING);

  // Set properties
  string->length = length;
  string->chars = chars;
  string->hash = hash;

  // Push the string object onto the stack to ensure it's reachable during GC
  push(OBJ_VAL(string));

  // Intern string
  tableSet(&vm.strings, string, NIL_VAL);

  // Pop the string object from the stack after interning it
  pop();

  return string;
}

static uint32_t hashString(const char *key, int length) {
  uint32_t hash = 2166136261u;

  for (int i = 0; i < length; i++) {
    hash ^= key[i];
    hash *= 16777619;
  }

  return hash;
}

ObjString *takeString(char *chars, int length) {
  // Calculate hash
  uint32_t hash = hashString(chars, length);

  ObjString *interned = tableFindString(&vm.strings, chars, length, hash);

  if (interned != NULL) {
    // Free heap-allocated string
    FREE_ARRAY(char, chars, length + 1);

    // Return interned string
    return interned;
  }

  return allocateString(chars, length, hash);
}

ObjString *copyString(const char *chars, int length) {
  // Calculate hash
  uint32_t hash = hashString(chars, length);

  // Look up interned string
  ObjString *interned = tableFindString(&vm.strings, chars, length, hash);

  if (interned != NULL) {
    // Return interned string
    return interned;
  }

  // Allocate enough memory (+1 for the null terminator)
  char *heapChars = ALLOCATE(char, length + 1);

  // Copy the string into the heap-allocated memory
  memcpy(heapChars, chars, length);

  // Null-terminate
  heapChars[length] = '\0';

  return allocateString(heapChars, length, hash);
}

ObjUpvalue *newUpvalue(Value *slot) {
  ObjUpvalue *upvalue = ALLOCATE_OBJ(ObjUpvalue, OBJ_UPVALUE);
  upvalue->closed = NIL_VAL;
  upvalue->location = slot;
  upvalue->next = NULL;

  return upvalue;
}

static void printFunction(ObjFunction *function) {
  if (function->name == NULL) {
    printf("<script>");
    return;
  }

  printf("<fn %s>", function->name->chars);
}

void printObject(Value value) {
  switch (OBJ_TYPE(value)) {
  case OBJ_CLOSURE:
    printFunction(AS_CLOSURE(value)->function);
    break;
  case OBJ_FUNCTION:
    printFunction(AS_FUNCTION(value));
    break;
  case OBJ_NATIVE:
    printf("<native fn>");
    break;
  case OBJ_STRING:
    printf("%s", AS_CSTRING(value));
    break;
  case OBJ_UPVALUE:
    printf("upvalue");
    break;
  }
}
