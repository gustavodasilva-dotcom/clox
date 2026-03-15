set -xe
clang-format -i ./include/chunk.h ./include/common.h ./include/compiler.h ./include/debug.h ./include/memory.h ./include/object.h ./include/scanner.h ./include/value.h ./include/vm.h ./src/chunk.c ./src/compiler.c ./src/debug.c ./src/main.c ./src/memory.c ./src/object.c ./src/scanner.c ./src/value.c ./src/vm.c ./tests/main.c
