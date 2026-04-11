#ifndef clox_compiler_h
#define clox_compiler_h

#include "object.h"
#include "vm.h"

ObjFunction *compile(const char *source);

/// @brief Marks the compiler's root objects as reachable during GC.
void markCompilerRoots();

#endif
