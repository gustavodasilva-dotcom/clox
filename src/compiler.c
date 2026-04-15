#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "common.h"
#include "compiler.h"
#include "memory.h"
#include "scanner.h"

typedef struct {
  Token current;
  Token previous;
  bool hadError;
  bool panicMode;
} Parser;

typedef enum {
  PREC_NONE,
  PREC_ASSIGNMENT, // = 		(lowest)
  PREC_OR,         // or
  PREC_AND,        // and
  PREC_EQUALITY,   // == !=
  PREC_COMPARISON, // < > <= >=
  PREC_TERM,       // + -
  PREC_FACTOR,     // * /
  PREC_UNARY,      // ! -
  PREC_CALL,       // . ()
  PREC_PRIMARY     // 			(highest)
} Precedence;

typedef void (*ParseFn)(bool canAssign);

typedef struct {
  ParseFn prefix;
  ParseFn infix;
  Precedence precedence;
} ParseRule;

// Represents a local variable in the current function's scope.
typedef struct {
  Token name;
  int depth;

  // Indicates whether the variable has been captured by any inner function's
  // closures
  bool isCaptured;
} Local;

typedef struct {
  uint8_t index;
  bool isLocal;
} Upvalue;

typedef enum {
  // A function defined by the user
  TYPE_FUNCTION,

  // The initializer method of a class, which is called when an instance of the
  // class is created
  TYPE_INITIALIZER,

  // A method defined in a class
  TYPE_METHOD,

  // The top-level code outside of any function is compiled as an anonymous
  // function
  TYPE_SCRIPT
} FunctionType;

// Represents a function being compiled, which is needed to compile nested
// functions.
typedef struct Compiler {
  struct Compiler *enclosing;
  ObjFunction *function;
  FunctionType type;

  // Local variables declared in the current function, including parameters
  Local locals[UINT8_COUNT];
  int localCount;

  // Upvalues for variables from enclosing functions that are captured by the
  // current function's closures
  Upvalue upvalues[UINT8_COUNT];

  int scopeDepth;
} Compiler;

// Represents a class being compiled, which is needed to compile method bodies.
typedef struct ClassCompiler {
  // The enclosing class, if any
  struct ClassCompiler *enclosing;

  // The name of the class being compiled
  Token name;
} ClassCompiler;

Parser parser;

Compiler *current = NULL;

// The current, innermost class being compiled, or `NULL` if not compiling a
// class
ClassCompiler *currentClass = NULL;

Chunk *compilingChunk;

/// @brief Returns a pointer to the chunk currently being compiled, which is the
/// chunk of the current function.
/// @return A pointer to the chunk currently being compiled
static Chunk *currentChunk() { return &current->function->chunk; }

static void errorAt(Token *token, const char *message) {
  // Don't report any errors while in panic mode
  if (parser.panicMode) {
    return;
  }

  // Enter panic mode to avoid reporting any further errors
  parser.panicMode = true;

  fprintf(stderr, "[line %d] Error", token->line);

  if (token->type == TOKEN_EOF) {
    fprintf(stderr, " at end");
  } else if (token->type == TOKEN_ERROR) {
    // Nothing
  } else {
    fprintf(stderr, " at '%.*s'", token->length, token->start);
  }

  fprintf(stderr, ": %s\n", message);

  // Indicate that an error has occurred
  parser.hadError = true;
}

static void error(const char *message) { errorAt(&parser.previous, message); }

static void errorAtCurrent(const char *message) {
  errorAt(&parser.current, message);
}

// Read tokens in a loop, and break when a non-error token is found. That way,
// only valid tokens are returned, and any errors are reported immediately.
static void advance() {
  parser.previous = parser.current;

  for (;;) {
    parser.current = scanToken();

    if (parser.current.type != TOKEN_ERROR) {
      break;
    }

    errorAtCurrent(parser.current.start);
  }
}

static void consume(TokenType type, const char *message) {
  if (parser.current.type == type) {
    advance();
    return;
  }

  errorAtCurrent(message);
}

static bool check(TokenType type) { return parser.current.type == type; }

static bool match(TokenType type) {
  if (!check(type))
    return false;

  advance();

  return true;
}

static void emitByte(uint8_t byte) {
  writeChunk(currentChunk(), byte, parser.previous.line);
}

static void emitBytes(uint8_t byte1, uint8_t byte2) {
  emitByte(byte1);
  emitByte(byte2);
}

static void emitLoop(int loopStart) {
  emitByte(OP_LOOP);

  // Calculate the jump offset by subtracting the loop start's offset from the
  // current offset, and adding 2 to account for the jump instruction's own
  // bytecode
  int offset = currentChunk()->count - loopStart + 2;

  if (offset > UINT16_MAX) {
    error("Loop body too large.");
  }

  // A 16-bit operand is reserved for the jump offset
  emitByte((offset >> 8) & 0xff);
  emitByte(offset & 0xff);
}

static int emitJump(uint8_t instruction) {
  emitByte(instruction);

  // A 16-bit operand is reserved for the jump offset
  emitByte(0xff);
  emitByte(0xff);

  return currentChunk()->count - 2;
}

/// @brief Emits bytecode to return from the current function.
///
/// This function is called when a return statement without an
/// expression being returned is compiled.
static void emitReturn() {
  if (current->type == TYPE_INITIALIZER) {
    // Implicitly return `this` from initializer methods
    emitBytes(OP_GET_LOCAL, 0);
  } else {
    emitByte(OP_NIL);
  }

  emitByte(OP_RETURN);
}

static uint8_t makeConstant(Value value) {
  int constant = addConstant(currentChunk(), value);

  if (constant > UINT8_MAX) {
    error("Too many constants in one chunk.");
    return 0;
  }

  return (uint8_t)constant;
}

static void emitConstant(Value value) {
  emitBytes(OP_CONSTANT, makeConstant(value));
}

static void patchJump(int offset) {
  // -2 to adjust for the bytecode for the jump offset itself
  int jump = currentChunk()->count - offset - 2;

  if (jump > UINT16_MAX) {
    error("Too much code to jump over.");
  }

  // Write the jump offset into the control flow's instruction operand
  currentChunk()->code[offset] = (jump >> 8) & 0xff;
  currentChunk()->code[offset + 1] = jump & 0xff;
}

static void initCompiler(Compiler *compiler, FunctionType type) {
  compiler->enclosing = current;
  compiler->function = NULL;
  compiler->type = type;
  compiler->localCount = 0;
  compiler->scopeDepth = 0;
  compiler->function = newFunction();
  current = compiler;

  // Store the function's name
  if (type != TYPE_SCRIPT) {
    current->function->name =
        copyString(parser.previous.start, parser.previous.length);
  }

  // Reserve slot 0 for VM's internal use
  Local *local = &current->locals[current->localCount++];
  local->depth = 0;
  local->isCaptured = false;

  // Class methods have an implicit "this" variable in slot 0
  if (type != TYPE_FUNCTION) {
    local->name.start = "this";
    local->name.length = 4;
  } else {
    local->name.start = "";
    local->name.length = 0;
  }
}

static ObjFunction *endCompiler() {
  emitReturn();

  ObjFunction *function = current->function;

#ifdef DEBUG_PRINT_CODE
  if (!parser.hadError) {
    disassembleChunk(currentChunk(), function->name != NULL
                                         ? function->name->chars
                                         : "<script>");
  }
#endif

  current = current->enclosing;

  return function;
}

static void beginScope() { current->scopeDepth++; }

static void endScope() {
  current->scopeDepth--;

  // For each local variable in the scope being exited
  while (current->localCount > 0 &&
         current->locals[current->localCount - 1].depth > current->scopeDepth) {
    if (current->locals[current->localCount - 1].isCaptured) {
      // It doesn't require an operand, since the value to be closed is on top
      // of the stack
      emitByte(OP_CLOSE_UPVALUE);
    } else {
      // Pop from the stack
      emitByte(OP_POP);
    }

    current->localCount--;
  }
}

// Forward declarations
static void expression();
static void statement();
static void declaration();
static ParseRule *getRule(TokenType type);
static void parsePrecedence(Precedence precedence);

static uint8_t identifierConstant(Token *name) {
  return makeConstant(OBJ_VAL(copyString(name->start, name->length)));
}

static bool identifiersEqual(Token *a, Token *b) {
  if (a->length != b->length) {
    return false;
  }

  return memcmp(a->start, b->start, a->length) == 0;
}

static int resolveLocal(Compiler *compiler, Token *name) {
  // Current scope is at the end of the array, so iterate backwards
  for (int i = compiler->localCount - 1; i >= 0; i--) {
    Local *local = &compiler->locals[i];

    if (identifiersEqual(name, &local->name)) {
      // Check if the variable is uninitialized
      if (local->depth == -1) {
        error("Can't read local variable in its own initializer.");
      }

      return i;
    }
  }

  return -1;
}

/// @brief Adds an upvalue for a variable with the given index and local status.
/// @param compiler The compiler for the function currently being compiled
/// @param index The index of the variable
/// @param isLocal A flag indicating whether the variable is local
/// @return The index of the upvalue, or -1 if not found
static int addUpvalue(Compiler *compiler, uint8_t index, bool isLocal) {
  int upvalueCount = compiler->function->upvalueCount;

  // Check if the upvalue already exists for the given variable
  for (int i = 0; i < upvalueCount; i++) {
    Upvalue *upvalue = &compiler->upvalues[i];

    if (upvalue->index == index && upvalue->isLocal == isLocal) {
      return i;
    }
  }

  // Check if there is room to add a new upvalue
  if (upvalueCount == UINT8_COUNT) {
    error("Too many closure variables in function.");
    return 0;
  }

  // Add a new upvalue for the given variable
  compiler->upvalues[upvalueCount].isLocal = isLocal;
  compiler->upvalues[upvalueCount].index = index;

  // Return the index of the new upvalue
  return compiler->function->upvalueCount++;
}

/// @brief Resolves an upvalue for a variable with the given name in an
/// enclosing function.
/// @param compiler The compiler for the function currently being compiled
/// @param name The token representing the variable name
/// @return The index of the upvalue, or -1 if not found
static int resolveUpvalue(Compiler *compiler, Token *name) {
  if (compiler->enclosing == NULL) {
    return -1;
  }

  // Try to resolve the variable as a local in the immediately enclosing
  // function
  int local = resolveLocal(compiler->enclosing, name);

  if (local != -1) {
    // Mark the local variable from the immediately enclosing function as
    // captured
    compiler->enclosing->locals[local].isCaptured = true;
    return addUpvalue(compiler, (uint8_t)local, true);
  }

  // Try to resolve the variable as an upvalue in the immediately function
  // closure
  int upvalue = resolveUpvalue(compiler->enclosing, name);

  if (upvalue != -1) {
    return addUpvalue(compiler, (uint8_t)upvalue, false);
  }

  return -1;
}

static void addLocal(Token name) {
  if (current->localCount == UINT8_COUNT) {
    error("Too many local variables in function.");
    return;
  }

  Local *local = &current->locals[current->localCount++];
  local->name = name;

  // Mark the variable as uninitialized
  local->depth = -1;
  local->isCaptured = false;
}

static void declareVariable() {
  // Global variables are implicitly declared
  if (current->scopeDepth == 0) {
    return;
  }

  Token *name = &parser.previous;

  // Current scope is at the end of the array, so iterate backwards
  for (int i = current->localCount - 1; i >= 0; i--) {
    Local *local = &current->locals[i];

    // Stop if we reach a variable from an outer scope, since it's not in scope
    if (local->depth != -1 && local->depth < current->scopeDepth) {
      break;
    }

    if (identifiersEqual(name, &local->name)) {
      error("Already variable with this name in this scope.");
    }
  }

  addLocal(*name);
}

static uint8_t parseVariable(const char *errorMessage) {
  consume(TOKEN_IDENTIFIER, errorMessage);

  declareVariable();

  if (current->scopeDepth > 0) {
    // Return a dummy index for local variables (since they don't have an index
    // in the constant table)
    return 0;
  }

  return identifierConstant(&parser.previous);
}

static void markInitialized() {
  // Prevent global variables from interfering with local variable resolution
  if (current->scopeDepth == 0) {
    return;
  }

  current->locals[current->localCount - 1].depth = current->scopeDepth;
}

static void defineVariable(uint8_t global) {
  // Local variables are implicitly defined when they are declared
  if (current->scopeDepth > 0) {
    markInitialized();
    return;
  }

  emitBytes(OP_DEFINE_GLOBAL, global);
}

/// @brief Compiles a list of function arguments.
/// @return The number of arguments compiled.
static uint8_t argumentList() {
  uint8_t argCount = 0;

  if (!check(TOKEN_RIGHT_PAREN)) {
    do {
      expression();

      if (argCount == 255) {
        error("Can't have more than 255 arguments.");
      }

      argCount++;
    } while (match(TOKEN_COMMA));
  }

  consume(TOKEN_RIGHT_PAREN, "Expect \")\" after arguments.");

  return argCount;
}

/// @brief Compiles the right operand of a logical expression, e.g., in `a and
/// b`, compiles `b` and emits a jump to skip it if `a` is falsy.
static void and_(bool canAssign) {
  // Jump the right operand if the left one is falsy
  int endJump = emitJump(OP_JUMP_IF_FALSE);

  // Pop the left operand's value from the stack
  emitByte(OP_POP);

  // Compile the right operand
  parsePrecedence(PREC_AND);

  patchJump(endJump);
}

/// @brief Compiles the right operand of a binary expression.
static void binary(bool canAssign) {
  // Remember the (infix) operator
  TokenType operatorType = parser.previous.type;

  // Compile the right operand
  ParseRule *rule = getRule(operatorType);

  // Use one level higher because binary operators are left-associative
  parsePrecedence((Precedence)(rule->precedence + 1));

  // Emit the operator instruction
  switch (operatorType) {
  case TOKEN_BANG_EQUAL:
    // !(a == b)
    emitBytes(OP_EQUAL, OP_NOT);
    break;
  case TOKEN_EQUAL_EQUAL:
    emitByte(OP_EQUAL);
    break;
  case TOKEN_GREATER:
    emitByte(OP_GREATER);
    break;
  case TOKEN_GREATER_EQUAL:
    // !(a < b)
    emitBytes(OP_LESS, OP_NOT);
    break;
  case TOKEN_LESS:
    emitByte(OP_LESS);
    break;
  case TOKEN_LESS_EQUAL:
    // !(a > b)
    emitBytes(OP_GREATER, OP_NOT);
    break;
  case TOKEN_PLUS:
    emitByte(OP_ADD);
    break;
  case TOKEN_MINUS:
    emitByte(OP_SUBTRACT);
    break;
  case TOKEN_STAR:
    emitByte(OP_MULTIPLY);
    break;
  case TOKEN_SLASH:
    emitByte(OP_DIVIDE);
    break;
  default:
    // Unreachable
    return;
  }
}

/// @brief Compiles a function call expression.
static void call(bool canAssign) {
  // Compile argument list (push them onto the stack)
  uint8_t argCount = argumentList();

  // The instruction operand is the argument count
  emitBytes(OP_CALL, argCount);
}

/// @brief Compiles get and set expressions.
static void dot(bool canAssign) {
  consume(TOKEN_IDENTIFIER, "Expect property name after \".\".");

  uint8_t name = identifierConstant(&parser.previous);

  if (canAssign && match(TOKEN_EQUAL)) {
    // Compile the assigned expression and push its value onto the stack
    expression();

    // Emit the setter instruction with the property name operand
    emitBytes(OP_SET_PROPERTY, name);
  } else if (match(TOKEN_LEFT_PAREN)) {
    // Compile the argument list (push them onto the stack)
    uint8_t argCount = argumentList();

    // Emit the method call superinstruction with the property name and
    // argument count operands
    emitBytes(OP_INVOKE, name);
    emitByte(argCount);
  } else {
    // Emit the getter instruction with the property name operand
    emitBytes(OP_GET_PROPERTY, name);
  }
}

/// @brief Compiles a literal expression, which can be `true`, `false`, or
/// `nil`.
static void literal(bool canAssign) {
  switch (parser.previous.type) {
  case TOKEN_FALSE:
    emitByte(OP_FALSE);
    break;
  case TOKEN_NIL:
    emitByte(OP_NIL);
    break;
  case TOKEN_TRUE:
    emitByte(OP_TRUE);
    break;
  default:
    // Unreachable
    return;
  }
}

/// @brief Compiles a grouping expression.
///
/// Does not emit any bytecode. Its sole purpose is to recursively call
/// `expression()` (which emits bytecode) and consume the closing parenthesis.
static void grouping(bool canAssign) {
  expression();
  consume(TOKEN_RIGHT_PAREN, "Expect ')' after expression.");
}

/// @brief Compiles a number literal expression.
static void number(bool canAssign) {
  double value = strtod(parser.previous.start, NULL);
  emitConstant(NUMBER_VAL(value));
}

/// @brief Compiles the right operand of a logical expression, e.g., in `a or
/// b`, compiles `b` and emits a jump to skip it if `a` is truthy.
static void or_(bool canAssign) {
  // Jump the right operand if the left one is truthy
  int elseJump = emitJump(OP_JUMP_IF_FALSE);
  int endJump = emitJump(OP_JUMP);

  patchJump(elseJump);
  emitByte(OP_POP);

  parsePrecedence(PREC_OR);
  patchJump(endJump);
}

/// @brief Compiles a string literal expression.
static void string(bool canAssign) {
  // -1 to exclude the opening quote; -2 to exclude the closing quote
  emitConstant(OBJ_VAL(
      copyString(parser.previous.start + 1, parser.previous.length - 2)));
}

/// @brief Compiles a named variable expression.
/// @param name The token representing the variable name
/// @param canAssign Whether the variable is being assigned to
static void namedVariable(Token name, bool canAssign) {
  uint8_t getOp, setOp;

  int arg = resolveLocal(current, &name);

  if (arg != -1) {
    getOp = OP_GET_LOCAL;
    setOp = OP_SET_LOCAL;
  } else if ((arg = resolveUpvalue(current, &name)) != -1) {
    getOp = OP_GET_UPVALUE;
    setOp = OP_SET_UPVALUE;
  } else {
    arg = identifierConstant(&name);

    getOp = OP_GET_GLOBAL;
    setOp = OP_SET_GLOBAL;
  }

  if (canAssign && match(TOKEN_EQUAL)) {
    // Assignment
    expression();

    emitBytes(setOp, (uint8_t)arg);
  } else {
    // Variable access
    emitBytes(getOp, (uint8_t)arg);
  }
}

/// @brief Compiles a variable expression.
static void variable(bool canAssign) {
  namedVariable(parser.previous, canAssign);
}

/// @brief Compiles a `this` expression.
static void this_(bool canAssign) {
  // Prevent using `this` outside of class methods
  if (currentClass == NULL) {
    error("Can't call \"this\" outside of a class.");
    return;
  }

  variable(false);
}

// The order of stack operands is reversed (Polish notation). So, first the
// operand is compiled. Then, if the operator (which was previously consumed) is
// a minus sign, the `OP_NEGATE` bytecode is emitted.
static void unary(bool canAssign) {
  // Remember the (prefix) operator
  TokenType operatorType = parser.previous.type;

  // Compile the operand
  parsePrecedence(PREC_UNARY);

  switch (operatorType) {
  case TOKEN_BANG:
    emitByte(OP_NOT);
    break;
  case TOKEN_MINUS:
    emitByte(OP_NEGATE);
    break;
  default:
    // Unreachable
    return;
  }
}

ParseRule rules[] = {
    [TOKEN_LEFT_PAREN] = {grouping, call, PREC_CALL},
    [TOKEN_RIGHT_PAREN] = {NULL, NULL, PREC_NONE},
    [TOKEN_LEFT_BRACE] = {NULL, NULL, PREC_NONE},
    [TOKEN_RIGHT_BRACE] = {NULL, NULL, PREC_NONE},
    [TOKEN_COMMA] = {NULL, NULL, PREC_NONE},
    [TOKEN_DOT] = {NULL, dot, PREC_CALL},
    [TOKEN_MINUS] = {unary, binary, PREC_TERM},
    [TOKEN_PLUS] = {NULL, binary, PREC_TERM},
    [TOKEN_SEMICOLON] = {NULL, NULL, PREC_NONE},
    [TOKEN_SLASH] = {NULL, binary, PREC_FACTOR},
    [TOKEN_STAR] = {NULL, binary, PREC_FACTOR},
    [TOKEN_BANG] = {unary, NULL, PREC_NONE},
    [TOKEN_BANG_EQUAL] = {NULL, binary, PREC_EQUALITY},
    [TOKEN_EQUAL] = {NULL, NULL, PREC_NONE},
    [TOKEN_EQUAL_EQUAL] = {NULL, binary, PREC_EQUALITY},
    [TOKEN_GREATER] = {NULL, binary, PREC_COMPARISON},
    [TOKEN_GREATER_EQUAL] = {NULL, binary, PREC_COMPARISON},
    [TOKEN_LESS] = {NULL, binary, PREC_COMPARISON},
    [TOKEN_LESS_EQUAL] = {NULL, binary, PREC_COMPARISON},
    [TOKEN_IDENTIFIER] = {variable, NULL, PREC_NONE},
    [TOKEN_STRING] = {string, NULL, PREC_NONE},
    [TOKEN_NUMBER] = {number, NULL, PREC_NONE},
    [TOKEN_AND] = {NULL, and_, PREC_AND},
    [TOKEN_CLASS] = {NULL, NULL, PREC_NONE},
    [TOKEN_ELSE] = {NULL, NULL, PREC_NONE},
    [TOKEN_FALSE] = {literal, NULL, PREC_NONE},
    [TOKEN_FOR] = {NULL, NULL, PREC_NONE},
    [TOKEN_FUN] = {NULL, NULL, PREC_NONE},
    [TOKEN_IF] = {NULL, NULL, PREC_NONE},
    [TOKEN_NIL] = {literal, NULL, PREC_NONE},
    [TOKEN_OR] = {NULL, or_, PREC_OR},
    [TOKEN_PRINT] = {NULL, NULL, PREC_NONE},
    [TOKEN_RETURN] = {NULL, NULL, PREC_NONE},
    [TOKEN_SUPER] = {NULL, NULL, PREC_NONE},
    [TOKEN_THIS] = {this_, NULL, PREC_NONE},
    [TOKEN_TRUE] = {literal, NULL, PREC_NONE},
    [TOKEN_VAR] = {NULL, NULL, PREC_NONE},
    [TOKEN_WHILE] = {NULL, NULL, PREC_NONE},
    [TOKEN_ERROR] = {NULL, NULL, PREC_NONE},
    [TOKEN_EOF] = {NULL, NULL, PREC_NONE},
};

// The first token always belongs to a prefix expression.
static void parsePrecedence(Precedence precedence) {
  // Scan the next token
  advance();

  // Look up prefix parser
  ParseFn prefixRule = getRule(parser.previous.type)->prefix;

  if (prefixRule == NULL) {
    error("Expect expression.");
    return;
  }

  // Only allow assignment if the current precedence level is not higher than
  // the assignment operator's precedence
  bool canAssign = precedence <= PREC_ASSIGNMENT;

  // Compiles prefix expression
  prefixRule(canAssign);

  while (precedence <= getRule(parser.current.type)->precedence) {
    // Scan the infix operator
    advance();

    // Get infix rule
    ParseFn infixRule = getRule(parser.previous.type)->infix;

    // Compiles infix expression
    infixRule(canAssign);
  }

  if (canAssign && match(TOKEN_EQUAL)) {
    error("Invalid assignment target.");
  }
}

static ParseRule *getRule(TokenType type) { return &rules[type]; };

static void expression() { parsePrecedence(PREC_ASSIGNMENT); }

/// @brief Compiles a block of code, which is a sequence of declarations and
/// statements surrounded by curly braces.
static void block() {
  while (!check(TOKEN_RIGHT_BRACE) && !check(TOKEN_EOF)) {
    declaration();
  }

  consume(TOKEN_RIGHT_BRACE, "Expect \"}\" after block.");
}

/// @brief Compiles a function (closure), which has the following syntax:
/// ```lox
/// fun functionName(parameters) body
/// ```
/// @param type The type of closure being compiled
///
/// The resulting closure object is left on top of the stack as a value.
static void function(FunctionType type) {
  // Create a new compiler for the function being compiled
  Compiler compiler;
  initCompiler(&compiler, type);

  beginScope();

  // Compile the parameter list
  consume(TOKEN_LEFT_PAREN, "Expect \"(\" after function name.");

  if (!check(TOKEN_RIGHT_PAREN)) {
    do {
      current->function->arity++;

      if (current->function->arity > 255) {
        errorAtCurrent("Can't have more than 255 parameters.");
      }

      // Compile and, subsequently, define, since parameters are not initialized
      uint8_t paramConstant = parseVariable("Expect parameter name.");
      defineVariable(paramConstant);
    } while (match(TOKEN_COMMA));
  }

  consume(TOKEN_RIGHT_PAREN, "Expect \")\" after parameters.");

  // The body
  consume(TOKEN_LEFT_BRACE, "Expect \"{\" before function body.");
  block();

  // Create the function object
  ObjFunction *function = endCompiler();
  emitBytes(OP_CLOSURE, makeConstant(OBJ_VAL(function)));

  // Emit captured upvalues as operands to the closure instruction
  for (int i = 0; i < function->upvalueCount; i++) {
    emitByte(compiler.upvalues[i].isLocal ? 1 : 0);
    emitByte(compiler.upvalues[i].index);
  }
}

/// @brief Compiles a method declaration, which has the following syntax:
/// ```lox
/// methodName(parameters) body
/// ```
static void method() {
  consume(TOKEN_IDENTIFIER, "Expect method name.");

  // Add the method name to the constant table
  uint8_t constant = identifierConstant(&parser.previous);

  FunctionType type = TYPE_METHOD;

  // Check if the method is an class initializer, which has the special name
  // `init`
  if (parser.previous.length == 4 &&
      memcmp(parser.previous.start, "init", 4) == 0) {
    type = TYPE_INITIALIZER;
  }

  // Compile the method body as a function, and leave the resulting closure on
  // top of the stack
  function(type);

  // Emit the method instruction with the method name operand
  emitBytes(OP_METHOD, constant);
}

/// @brief Compiles a class declaration, which has the following syntax:
/// ```lox
/// class ClassName { body }
/// ```
static void classDeclaration() {
  consume(TOKEN_IDENTIFIER, "Expect class name.");

  // Store the class name token for later use
  Token className = parser.previous;

  uint8_t nameConstant = identifierConstant(&parser.previous);
  declareVariable();

  // Create a new compiler for the class being compiled
  ClassCompiler classCompiler;
  classCompiler.name = parser.previous;
  classCompiler.enclosing = currentClass;
  currentClass = &classCompiler;

  emitBytes(OP_CLASS, nameConstant);

  // Define the class name as a variable so that it can be referenced in its
  // methods
  defineVariable(nameConstant);

  // Load the class name onto the stack to be used as the receiver of method
  // definitions
  namedVariable(className, false);

  consume(TOKEN_LEFT_BRACE, "Expect \"{\" before class body.");

  // Compile methods
  while (!check(TOKEN_RIGHT_BRACE) && !check(TOKEN_EOF)) {
    method();
  }

  consume(TOKEN_RIGHT_BRACE, "Expect \"}\" after class body.");

  // Pop the class name from the stack, since it's no longer needed
  emitByte(OP_POP);

  // Restore the enclosing class, if any (otherwise, set to `NULL`)
  currentClass = currentClass->enclosing;
}

/// @brief Compiles a function declaration, which has the following syntax:
/// ```lox
/// fun functionName(parameters) body
/// ```
static void funDeclaration() {
  // Declare the variable
  uint8_t global = parseVariable("Expect function name.");

  // Allow, at compile time, the function to refer to itself in its body by
  // marking it as initialized, making recursive references possible
  markInitialized();

  // Parse the function
  function(TYPE_FUNCTION);

  // Bind the function to the variable
  defineVariable(global);
}

/// @brief Compiles a variable declaration, which has the following syntax:
/// ```lox
/// var variableName = initializer;
/// ```
static void varDeclaration() {
  uint8_t global = parseVariable("Expect variable name.");

  if (match(TOKEN_EQUAL)) {
    // Compile initializer
    expression();
  } else {
    // Default initializer is nil
    emitByte(OP_NIL);
  }

  consume(TOKEN_SEMICOLON, "Expect \";\" after variable declaration.");

  defineVariable(global);
}

/// @brief Compiles a print statement, which has the following syntax:
/// ```lox
/// print expression;
/// ```
static void printStatement() {
  // Compile expression (push onto stack)
  expression();

  consume(TOKEN_SEMICOLON, "Expect \";\" after value.");

  // Emit instruction to print
  emitByte(OP_PRINT);
}

/// @brief Compiles a return statement, which has the following syntax:
/// ```lox
/// return value;
/// ```
static void returnStatement() {
  // Return statements are only valid non-top-level functions
  if (current->type == TYPE_SCRIPT) {
    error("Can't return from top-level code.");
  }

  if (match(TOKEN_SEMICOLON)) {
    // No return value, so emit bytecode to return nil
    emitReturn();
  } else {
    if (current->type == TYPE_INITIALIZER) {
      error("Can't return a value from an initializer.");
    }

    // Compile return value (push onto stack)
    expression();

    consume(TOKEN_SEMICOLON, "Expect \";\" after return value.");

    emitByte(OP_RETURN);
  }
}

/// @brief Compiles a while statement, which has the following syntax:
/// ```lox
/// while (condition) body
/// ```
static void whileStatement() {
  // Remember the offset of the beginning of the loop
  int loopStart = currentChunk()->count;

  consume(TOKEN_LEFT_PAREN, "Expect \"(\" after \"while\".");

  expression();

  consume(TOKEN_RIGHT_PAREN, "Expect \")\" after condition.");

  // Skip the body if the condition is falsy
  int exitJump = emitJump(OP_JUMP_IF_FALSE);

  // If the condition is truthy, pop its value from the stack
  emitByte(OP_POP);

  statement();

  // Loop back to the beginning of the loop to re-evaluate the condition
  emitLoop(loopStart);

  patchJump(exitJump);

  // If the condition is falsy, pop its value from the stack
  emitByte(OP_POP);
}

static void synchronize() {
  // Reset panic mode
  parser.panicMode = false;

  // Discard tokens until a statement boundary is found
  while (parser.current.type != TOKEN_EOF) {
    if (parser.previous.type == TOKEN_SEMICOLON) {
      // Return if the previous token was a statement terminator
      return;
    }

    // Return if the current token is a keyword that can start a statement
    switch (parser.current.type) {
    case TOKEN_CLASS:
    case TOKEN_FUN:
    case TOKEN_VAR:
    case TOKEN_FOR:
    case TOKEN_IF:
    case TOKEN_WHILE:
    case TOKEN_PRINT:
    case TOKEN_RETURN:
      return;

    default:
        // Do nothing
        ;
    }

    // Discard the current token
    advance();
  }
}

static void expressionStatement() {
  // Compile expression (produces a value on the stack)
  expression();

  consume(TOKEN_SEMICOLON, "Expect \";\" after expression.");

  // Discard the value left on the stack
  emitByte(OP_POP);
}

/// @brief Compiles a for statement, which has the following syntax:
/// ```lox
/// for (initializer; condition; increment) body
/// ```
static void forStatement() {
  // Create a new scope for the loop's initializer and control variables
  beginScope();

  consume(TOKEN_LEFT_PAREN, "Expect \"(\" after \"for\".");

  // Compile initializer (if any)
  if (match(TOKEN_SEMICOLON)) {
    // No initializer
  } else if (match(TOKEN_VAR)) {
    varDeclaration();
  } else {
    expressionStatement();
  }

  int loopStart = currentChunk()->count;

  // Compile condition clause
  int exitJump = -1;
  if (!match(TOKEN_SEMICOLON)) {
    expression();
    consume(TOKEN_SEMICOLON, "Expect \";\" after loop condition.");

    // Jump out of the loop if the condition is false
    exitJump = emitJump(OP_JUMP_IF_FALSE);

    // Condition
    emitByte(OP_POP);
  }

  // Compile increment clause
  if (!match(TOKEN_RIGHT_PAREN)) {
    // Since the increment clause is executed after the body, jump over it
    int bodyJump = emitJump(OP_JUMP);

    // Point in which the increment clause starts
    int incrementStart = currentChunk()->count;

    // Compile increment and pop its value from the stack
    expression();
    emitByte(OP_POP);

    consume(TOKEN_RIGHT_PAREN, "Expect \")\" after for clauses.");

    // Jump back to re-evaluate the condition after executing the increment
    // clause
    emitLoop(loopStart);

    // Update loop start to the beginning of the increment clause, so that the
    // loop will jump to the increment after executing the body
    loopStart = incrementStart;

    patchJump(bodyJump);
  }

  statement();

  emitLoop(loopStart);

  // Patch the jump to exit the loop if there was a condition clause
  if (exitJump != -1) {
    patchJump(exitJump);

    // Condition
    emitByte(OP_POP);
  }

  // End the loop's scope to discard any control variables
  endScope();
}

/// @brief Compiles an if statement, which has the following syntax:
/// ```lox
/// if (condition) thenBranch else elseBranch
/// ```
static void ifStatement() {
  consume(TOKEN_LEFT_PAREN, "Expect \"(\" after \"if\".");

  expression();

  consume(TOKEN_RIGHT_PAREN, "Expect \")\" after condition.");

  int thenJump = emitJump(OP_JUMP_IF_FALSE);

  // If the condition is truthy, pop its value from the stack
  emitByte(OP_POP);
  statement();

  int elseJump = emitJump(OP_JUMP);

  patchJump(thenJump);

  // If the condition is falsy, pop its value from the stack
  emitByte(OP_POP);

  if (match(TOKEN_ELSE)) {
    statement();
  }

  patchJump(elseJump);
}

static void statement() {
  if (match(TOKEN_PRINT)) {
    printStatement();
  } else if (match(TOKEN_FOR)) {
    forStatement();
  } else if (match(TOKEN_IF)) {
    ifStatement();
  } else if (match(TOKEN_RETURN)) {
    returnStatement();
  } else if (match(TOKEN_WHILE)) {
    whileStatement();
  } else if (match(TOKEN_LEFT_BRACE)) {
    beginScope();
    block();
    endScope();
  } else {
    expressionStatement();
  }
}

static void declaration() {
  if (match(TOKEN_CLASS)) {
    classDeclaration();
  } else if (match(TOKEN_FUN)) {
    funDeclaration();
  } else if (match(TOKEN_VAR)) {
    varDeclaration();
  } else {
    statement();
  }

  if (parser.panicMode) {
    // Error recovery at statement boundary
    synchronize();
  }
}

ObjFunction *compile(const char *source) {
  initScanner(source);

  Compiler compiler;
  initCompiler(&compiler, TYPE_SCRIPT);

  // Initialize error state
  parser.hadError = false;
  parser.panicMode = false;

  advance();

  while (!match(TOKEN_EOF)) {
    declaration();
  }

  ObjFunction *function = endCompiler();

  return parser.hadError ? NULL : function;
}

void markCompilerRoots() {
  Compiler *compiler = current;

  while (compiler != NULL) {
    // Mark the function object being compiled into as a GC root
    markObject((Obj *)compiler->function);

    // Mark any nested enclosing functions as GC roots
    compiler = compiler->enclosing;
  }
}
