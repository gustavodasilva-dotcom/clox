#include <stdlib.h>

#include "memory.h"

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
