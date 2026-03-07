#include <stdio.h>

#include "chunk.h"
#include "debug.h"
#include "vm.h"

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

static void run_chunk_tests() {
  printf("Executing Chunk tests.\n");

  putchar('\n');

  // Doesn't have any output
  testWriteReturnOpcode();

  testDisassembleChunk();
  putchar('\n');

  testWriteConstantOpcode();
  putchar('\n');

  printf("Chunk tests executed.\n");
}

static void testInitFreeVM() {
  initVM();

  freeVM();
}

static void testInterpretChunkVM() {
  initVM();

  Chunk chunk;

  initChunk(&chunk);

  int constant = addConstant(&chunk, 1.2);
  writeChunk(&chunk, OP_CONSTANT, 123);
  writeChunk(&chunk, constant, 123);

  writeChunk(&chunk, OP_RETURN, 123);

  disassembleChunk(&chunk, "testInterpretChunkVM");

  interpret(&chunk);

  freeVM();

  freeChunk(&chunk);
}

static void run_vm_tests() {
  printf("Executing VM tests.\n");

  putchar('\n');

  testInitFreeVM();

  testInterpretChunkVM();

  putchar('\n');

  printf("VM tests executed.\n");
}

int main(int argc, char *argv[]) {
  run_chunk_tests();

  putchar('\n');
  printf("=====================\n");
  putchar('\n');

  run_vm_tests();

  return 0;
}
