#include <stdio.h>
#include <string.h>

#include "memory.h"
#include "object.h"
#include "value.h"
#include "vm.h"

#define ALLOCATE_OBJ(type, objectType)                                         \
  (type *)allocateObject(sizeof(type), objectType)

static Obj *allocateObject(size_t size, ObjType type) {
  // Heap-allocate object (size is determined by the caller)
  Obj *object = (Obj *)reallocate(NULL, 0, size);

  // Set type
  object->type = type;

  // Point the new object to the current head of the list
  object->next = vm.objects;

  // Update the head to point to the new object
  vm.objects = object;

  return object;
}

static ObjString *allocateString(char *chars, int length) {
  // Heap-allocate the string object
  ObjString *string = ALLOCATE_OBJ(ObjString, OBJ_STRING);

  // Set properties
  string->length = length;
  string->chars = chars;

  return string;
}

ObjString *takeString(char *chars, int length) {
  return allocateString(chars, length);
}

ObjString *copyString(const char *chars, int length) {
  // Allocate enough memory (+1 for the null terminator)
  char *heapChars = ALLOCATE(char, length + 1);

  // Copy the string into the heap-allocated memory
  memcpy(heapChars, chars, length);

  // Null-terminate
  heapChars[length] = '\0';

  return allocateString(heapChars, length);
}

void printObject(Value value) {
  switch (OBJ_TYPE(value)) {
  case OBJ_STRING:
    printf("%s", AS_CSTRING(value));
    break;
  }
}
