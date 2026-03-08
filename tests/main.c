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

  freeVM();

  freeChunk(&chunk);
}

static void testNegateArithmeticOperator() {
  initVM();

  Chunk chunk;

  initChunk(&chunk);

  int constant = addConstant(&chunk, 1.2);
  writeChunk(&chunk, OP_CONSTANT, 123);
  writeChunk(&chunk, constant, 123);
  writeChunk(&chunk, OP_NEGATE, 123);

  writeChunk(&chunk, OP_RETURN, 123);

  disassembleChunk(&chunk, "testNegateArithmeticOperator");

  freeVM();

  freeChunk(&chunk);
}

static void testBinaryArithmeticOperators() {
  initVM();

  Chunk chunk;

  initChunk(&chunk);

  int constant = addConstant(&chunk, 1.2);
  writeChunk(&chunk, OP_CONSTANT, 123);
  writeChunk(&chunk, constant, 123);

  constant = addConstant(&chunk, 3.4);
  writeChunk(&chunk, OP_CONSTANT, 123);
  writeChunk(&chunk, constant, 123);

  writeChunk(&chunk, OP_ADD, 123);

  constant = addConstant(&chunk, 5.6);
  writeChunk(&chunk, OP_CONSTANT, 123);
  writeChunk(&chunk, constant, 123);

  writeChunk(&chunk, OP_DIVIDE, 123);
  writeChunk(&chunk, OP_NEGATE, 123);

  writeChunk(&chunk, OP_RETURN, 123);

  disassembleChunk(&chunk, "testBinaryArithmeticOperators");

  freeVM();

  freeChunk(&chunk);
}

static void testOtherBinaryArithmeticOperators() {
  initVM();

  Chunk chunk;

  initChunk(&chunk);

  // 1 * 2 + 3 = 5
  int constant = addConstant(&chunk, 1);
  writeChunk(&chunk, OP_CONSTANT, 123);
  writeChunk(&chunk, constant, 123);

  constant = addConstant(&chunk, 2);
  writeChunk(&chunk, OP_CONSTANT, 123);
  writeChunk(&chunk, constant, 123);

  writeChunk(&chunk, OP_MULTIPLY, 123);

  constant = addConstant(&chunk, 3);
  writeChunk(&chunk, OP_CONSTANT, 123);
  writeChunk(&chunk, constant, 123);

  writeChunk(&chunk, OP_ADD, 123);

  writeChunk(&chunk, OP_RETURN, 123);

  disassembleChunk(&chunk, "testOtherBinaryArithmeticOperators");

  freeChunk(&chunk);

  putchar('\n');

  // 1 + 2 * 3 = 7
  constant = addConstant(&chunk, 1);
  writeChunk(&chunk, OP_CONSTANT, 123);
  writeChunk(&chunk, constant, 123);

  constant = addConstant(&chunk, 2);
  writeChunk(&chunk, OP_CONSTANT, 123);
  writeChunk(&chunk, constant, 123);

  constant = addConstant(&chunk, 3);
  writeChunk(&chunk, OP_CONSTANT, 123);
  writeChunk(&chunk, constant, 123);

  writeChunk(&chunk, OP_MULTIPLY, 123);

  writeChunk(&chunk, OP_ADD, 123);

  writeChunk(&chunk, OP_RETURN, 123);

  disassembleChunk(&chunk, "testOtherBinaryArithmeticOperators");

  freeChunk(&chunk);

  putchar('\n');

  // 3 - 2 - 1 = 0
  constant = addConstant(&chunk, 3);
  writeChunk(&chunk, OP_CONSTANT, 123);
  writeChunk(&chunk, constant, 123);

  constant = addConstant(&chunk, 2);
  writeChunk(&chunk, OP_CONSTANT, 123);
  writeChunk(&chunk, constant, 123);

  writeChunk(&chunk, OP_SUBTRACT, 123);

  constant = addConstant(&chunk, 1);
  writeChunk(&chunk, OP_CONSTANT, 123);
  writeChunk(&chunk, constant, 123);

  writeChunk(&chunk, OP_SUBTRACT, 123);

  writeChunk(&chunk, OP_RETURN, 123);

  disassembleChunk(&chunk, "testOtherBinaryArithmeticOperators");

  freeChunk(&chunk);

  putchar('\n');

  // 1 + 2 * 3 - 4 / -5 = 7.8
  constant = addConstant(&chunk, 1);
  writeChunk(&chunk, OP_CONSTANT, 123);
  writeChunk(&chunk, constant, 123);

  constant = addConstant(&chunk, 2);
  writeChunk(&chunk, OP_CONSTANT, 123);
  writeChunk(&chunk, constant, 123);

  constant = addConstant(&chunk, 3);
  writeChunk(&chunk, OP_CONSTANT, 123);
  writeChunk(&chunk, constant, 123);

  writeChunk(&chunk, OP_MULTIPLY, 123);

  writeChunk(&chunk, OP_ADD, 123);

  constant = addConstant(&chunk, 4);
  writeChunk(&chunk, OP_CONSTANT, 123);
  writeChunk(&chunk, constant, 123);

  constant = addConstant(&chunk, 5);
  writeChunk(&chunk, OP_CONSTANT, 123);
  writeChunk(&chunk, constant, 123);

  writeChunk(&chunk, OP_NEGATE, 123);

  writeChunk(&chunk, OP_DIVIDE, 123);

  writeChunk(&chunk, OP_SUBTRACT, 123);

  writeChunk(&chunk, OP_RETURN, 123);

  disassembleChunk(&chunk, "testOtherBinaryArithmeticOperators");

  freeVM();

  freeChunk(&chunk);
}

static void run_vm_tests() {
  printf("Executing VM tests.\n");

  putchar('\n');

  // Doesn't have any output
  testInitFreeVM();

  testInterpretChunkVM();
  putchar('\n');

  testNegateArithmeticOperator();
  putchar('\n');

  testBinaryArithmeticOperators();
  putchar('\n');

  testOtherBinaryArithmeticOperators();
  putchar('\n');

  printf("VM tests executed.\n");
}

int main(int argc, char *argv[]) {
  run_chunk_tests();

  putchar('\n');
  printf("==========================================\n");
  putchar('\n');

  run_vm_tests();

  return 0;
}
