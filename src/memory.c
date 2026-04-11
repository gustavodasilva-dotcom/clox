#include <stdlib.h>

#include "compiler.h"
#include "memory.h"
#include "vm.h"

#ifdef DEBUG_LOG_GC
#include "debug.h"
#include <stdio.h>
#endif

#define GC_HEAD_GROW_FACTOR 2

void *reallocate(void *pointer, size_t oldSize, size_t newSize) {
  // Update the total bytes allocated
  vm.bytesAllocated += newSize - oldSize;

  // Run GC when more memory is requested
  if (newSize > oldSize) {
#ifdef DEBUG_STRESS_GC
    collectGarbage();
#endif

    // If the total bytes allocated exceeds the threshold, trigger a GC cycle
    if (vm.bytesAllocated > vm.nextGC) {
      collectGarbage();
    }
  }

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

void markObject(Obj *object) {
  // Make sure the object points to a valid heap-allocated object
  if (object == NULL) {
    return;
  }

  // If the object is already marked, it's already been determined to be
  // reachable, so skip it
  if (object->isMarked) {
    return;
  }

#ifdef DEBUG_LOG_GC
  printf("%p mark ", (void *)object);
  printValue(OBJ_VAL(object));
  printf("\n");
#endif

  object->isMarked = true;

  if (vm.grayCapacity < vm.grayCount + 1) {
    vm.grayCapacity = GROW_CAPACITY(vm.grayCapacity);

    // The gray stack it's not managed by the GC, so use 'realloc' directly on
    // it
    vm.grayStack = realloc(vm.grayStack, sizeof(Obj *) * vm.grayCapacity);

    // Exit if allocation fails (out of memory)
    if (vm.grayStack == NULL) {
      exit(1);
    }
  }

  // Add the object to the gray stack for later traversal of its references
  vm.grayStack[vm.grayCount++] = object;
}

void markValue(Value value) {
  // Numbers, Booleans, and nil values are not heap-allocated objects, so skip
  // them
  if (!IS_OBJ(value)) {
    return;
  }

  markObject(AS_OBJ(value));
}

/// @brief Marks all values in an array as reachable.
/// @param array The array of values to mark
static void markArray(ValueArray *array) {
  for (int i = 0; i < array->count; i++) {
    markValue(array->values[i]);
  }
}

/// @brief Marks the references of a gray object as reachable, and adds them to
/// the gray stack if they are not already marked. This is called "blackening"
/// the object.
/// @param object The object to be blackened
///
/// A black object is one that has been marked as reachable and that is no
/// longer on the gray stack.
static void blackenObject(Obj *object) {
#ifdef DEBUG_LOG_GC
  printf("%p blacken ", (void *)object);
  printValue(OBJ_VAL(object));
  printf("\n");
#endif

  switch (object->type) {
  case OBJ_CLOSURE: {
    ObjClosure *closure = (ObjClosure *)object;

    // Mark the function wrapped by the closure as a GC root
    markObject((Obj *)closure->function);

    // Mark the closure's upvalues as GC roots
    for (int i = 0; i < closure->upvalueCount; i++) {
      markObject((Obj *)closure->upvalues[i]);
    }
    break;
  }

  case OBJ_FUNCTION: {
    ObjFunction *function = (ObjFunction *)object;

    // Mark the function's name as a GC root
    markObject((Obj *)function->name);

    // Mark the function's chunk constants as GC roots
    markArray(&function->chunk.constants);
    break;
  }

  case OBJ_UPVALUE:
    markValue(((ObjUpvalue *)object)->closed);
    break;

  case OBJ_NATIVE:
  case OBJ_STRING:
    break;
  }
}

static void freeObject(Obj *object) {
#ifdef DEBUG_LOG_GC
  printf("%p free type %d\n", (void *)object, object->type);
#endif

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

/// @brief Marks the root objects as reachable.
///
/// A root object is one that is directly accessible by the program without
/// following any references.
static void markRoots() {
  // Mark stack values (local variables or temporaries)
  for (Value *slot = vm.stack; slot < vm.stackTop; slot++) {
    markValue(*slot);
  }

  // Mark call frames (closures) that point to constants and upvalues
  for (int i = 0; i < vm.frameCount; i++) {
    markObject((Obj *)vm.frames[i].closure);
  }

  // Mark open upvalues that point to variables on the stack
  for (ObjUpvalue *upvalue = vm.openUpvalues; upvalue != NULL;
       upvalue = upvalue->next) {
    markObject((Obj *)upvalue);
  }

  // Mark global variables
  markTable(&vm.globals);

  // Mark values directly referenced by the compiler
  markCompilerRoots();
}

/// @brief Traces the references of gray objects, marking any reachable objects
/// as gray as well.
static void traceReferences() {
  while (vm.grayCount > 0) {
    // Get the next gray object and remove it from the gray stack
    Obj *object = vm.grayStack[--vm.grayCount];

    blackenObject(object);
  }
}

/// @brief Frees any heap-allocated objects that were not marked as reachable,
/// and unmarks the reachable objects for the next GC cycle.
static void sweep() {
  Obj *previous = NULL;
  Obj *object = vm.objects;

  while (object != NULL) {
    if (object->isMarked) {
      // Unmark the black (or gray) object for the next GC cycle
      object->isMarked = false;

      previous = object;

      // Unlink it from the list and move to the next one
      object = object->next;
    } else {
      // Store the white object to be freed
      Obj *unreached = object;

      // Unlink it from the list and move to the next one
      object = object->next;

      if (previous != NULL) {
        // Point the previous object's 'next' to the next object, skipping over
        // the white object
        previous->next = object;
      } else {
        // Update the head of the list
        vm.objects = object;
      }

      freeObject(unreached);
    }
  }
}

void collectGarbage() {
#ifdef DEBUG_LOG_GC
  printf("-- gc begin\n");

  size_t before = vm.bytesAllocated;
#endif

  markRoots();

  traceReferences();

  tableRemoveWhite(&vm.strings);

  sweep();

  // Update the threshold for the next GC cycle
  vm.nextGC = vm.bytesAllocated * GC_HEAD_GROW_FACTOR;

#ifdef DEBUG_LOG_GC
  printf("-- gc end\n");

  printf("   collected %ld bytes (from %ld to %ld) next at %ld\n",
         before - vm.bytesAllocated, before, vm.bytesAllocated, vm.nextGC);
#endif
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

  free(vm.grayStack);
}
