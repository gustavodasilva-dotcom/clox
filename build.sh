set -xe
mkdir -p ./build/

# Compile the main application
cc -g -std=c99 -Wall -I ./include/ \
  ./src/chunk.c ./src/debug.c ./src/memory.c ./src/value.c ./src/vm.c ./src/main.c \
  -o ./build/clox

# Compile the tests
cc -g -std=c99 -Wall -I ./include/ \
  ./src/chunk.c ./src/debug.c ./src/memory.c ./src/value.c ./src/vm.c ./tests/main.c \
  -o ./build/tests
