#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "chunk.h"
#include "common.h"
#include "debug.h"
#include "vm.h"

static void repl() {
  char line[1024];

  for (;;) {
    // Prompt user
    printf("> ");

    // If user input is EOF, exit the REPL
    if (!fgets(line, sizeof(line), stdin)) {
      printf("\n");
      break;
    }

    // Otherwise, interpret it
    interpret(line);
  }
}

static char *readFile(const char *path) {
  // Open the file in binary mode
  FILE *file = fopen(path, "rb");
  if (file == NULL) {
    fprintf(stderr, "Could not open file \"%s\".\n", path);
    // Exit with code 74 (EX_IOERR)
    exit(74);
  }

  // Seek to the end
  fseek(file, 0L, SEEK_END);

  // Get the number of bytes from the current position (which is the end) to the
  // beginning (i.e. the file size)
  size_t fileSize = ftell(file);

  // Rewind to the beginning
  rewind(file);

  // Allocate a buffer to hold the file contents, plus one byte for the null
  // terminator
  char *buffer = (char *)malloc(fileSize + 1);
  if (buffer == NULL) {
    fprintf(stderr, "Not enough memory to read \"%s\".\n", path);
    // Exit with code 74 (EX_IOERR)
    exit(74);
  }

  size_t bytesRead = fread(buffer, sizeof(char), fileSize, file);
  if (bytesRead < fileSize) {
    fprintf(stderr, "Could not read file \"%s\".\n", path);
    // Exit with code 74 (EX_IOERR)
    exit(74);
  }

  // Null-terminate the buffer (offset by the number of bytes read)
  buffer[bytesRead] = '\0';

  // Close the file
  fclose(file);

  return buffer;
}

static void runFile(const char *path) {
  char *source = readFile(path);
  InterpretResult result = interpret(source);
  free(source);

  if (result == INTERPRET_COMPILE_ERROR) {
    // Exit with code 65 (EX_DATAERR)
    exit(65);
  }

  if (result == INTERPRET_RUNTIME_ERROR) {
    // Exit with code 70 (EX_SOFTWARE)
    exit(70);
  }
}

int main(int argc, char *argv[]) {
  initVM();

  // First argument is the executable name
  if (argc == 1) {
    // No arguments, start the REPL
    repl();
  } else if (argc == 2) {
    // One argument, treat it as a file path and execute the file
    runFile(argv[1]);
  } else {
    fprintf(stderr, "Usage: clox [path]\n");
    exit(64);
  }

  freeVM();

  return 0;
}
