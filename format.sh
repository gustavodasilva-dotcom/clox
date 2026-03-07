set -xe
clang-format -i ./include/chunk.h ./include/common.h ./include/debug.h ./include/memory.h ./include/value.h ./src/chunk.c ./src/debug.c ./src/main.c ./src/memory.c ./src/value.c ./tests/chunk_tests.c
