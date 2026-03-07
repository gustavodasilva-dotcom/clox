#include <stdio.h>

#include "chunk.h"
#include "debug.h"

static void testWriteReturnOpcode() {
  Chunk chunk;

  initChunk(&chunk);
  writeChunk(&chunk, OP_RETURN, 123);

  freeChunk(&chunk);
}

static void testDisassembleChunk() {
  Chunk chunk;

  initChunk(&chunk);
  writeChunk(&chunk, OP_RETURN, 123);

  disassembleChunk(&chunk, "testDisassembleChunk");

  freeChunk(&chunk);
}

static void testWriteConstantOpcode() {
  Chunk chunk;

  initChunk(&chunk);

  int constant = addConstant(&chunk, 1.2);
  writeChunk(&chunk, OP_CONSTANT, 123);
  writeChunk(&chunk, constant, 123);

  writeChunk(&chunk, OP_RETURN, 123);

  disassembleChunk(&chunk, "testWriteConstantOpcode");

  freeChunk(&chunk);
}

int main(int argc, char *argv[]) {
  printf("Executing tests.\n");

  putchar('\n');

  // Doesn't have any output
  testWriteReturnOpcode();

  testDisassembleChunk();
  putchar('\n');

  testWriteConstantOpcode();
  putchar('\n');

  printf("Tests executed.\n");
}
