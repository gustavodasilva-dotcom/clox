set -xe
mkdir -p ./build/

# Compile the main application
cc -g -std=c99 -Wall -I ./include/ \
  ./src/chunk.c ./src/compiler.c ./src/debug.c ./src/memory.c ./src/object.c ./src/scanner.c ./src/table.c ./src/value.c ./src/vm.c ./src/main.c \
  -o ./build/clox -lm
