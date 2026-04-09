#include <stdlib.h>

#include "memory.h"
#include "vm.h"

void *reallocate(void *pointer, size_t oldSize, size_t newSize) {
  // Free allocation
  if (newSize == 0) {
    free(pointer);
    return NULL;
  }

  // Allocate new block; shrink or grow existing one
  void *result = realloc(pointer, newSize);

  // Exit if allocation fails (out of memory)
  if (result == NULL) {
    exit(1);
  }

  return result;
}

static void freeObject(Obj *object) {
  switch (object->type) {
  case OBJ_CLOSURE: {
    // Free the upvalue array of the closure object
    ObjClosure *closure = (ObjClosure *)object;
    FREE_ARRAY(ObjUpvalue *, closure->upvalues, closure->upvalueCount);

    // Free the closure object itself, but not the function it wraps
    FREE(ObjClosure, object);
    break;
  }

  case OBJ_FUNCTION: {
    ObjFunction *function = (ObjFunction *)object;

    // Free the function's chunk
    freeChunk(&function->chunk);

    // Free the function object itself
    FREE(ObjFunction, object);
    break;
  }

  case OBJ_NATIVE: {
    FREE(ObjNative, object);
    break;
  }

  case OBJ_STRING: {
    ObjString *string = (ObjString *)object;

    // Free the string's character data
    FREE_ARRAY(char, string->chars, string->length + 1);

    // Free the string object itself
    FREE(ObjString, object);
    break;
  }

  case OBJ_UPVALUE:
    // Free the upvalue object itself, but not the closed-over variable it
    // references
    FREE(ObjUpvalue, object);
    break;
  }
}

void freeObjects() {
  // Point to the head of the list
  Obj *object = vm.objects;

  // Traverse it
  while (object != NULL) {
    Obj *next = object->next;
    freeObject(object);
    object = next;
  }
}
