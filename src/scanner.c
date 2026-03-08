#include <stdio.h>
#include <string.h>

#include "common.h"
#include "scanner.h"

typedef struct {
  const char *start;
  const char *current;
  int line;
} Scanner;

Scanner scanner;

void initScanner(const char *source) {
  // Point both the start and current pointers to the beginning of the source
  // string
  scanner.start = source;
  scanner.current = source;
  scanner.line = 1;
}

static bool isAlpha(char c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

static bool isDigit(char c) { return c >= '0' && c <= '9'; }

static bool isAtEnd() { return *scanner.current == '\0'; }

static char advance() {
  // Consume current character
  scanner.current++;

  // Return it
  return scanner.current[-1];
}

static char peek() { return *scanner.current; }

static char peekNext() {
  if (isAtEnd()) {
    return '\0';
  }

  // Return the character after the current pointer
  return scanner.current[1];
}

static bool match(char expected) {
  // Return false if we've reached the end of the source string
  if (isAtEnd()) {
    return false;
  }

  // Return false if the current character doesn't match the expected character
  if (*scanner.current != expected) {
    return false;
  }

  // Consume the current character
  scanner.current++;

  return true;
}

static Token makeToken(TokenType type) {
  Token token;
  token.type = type;

  // Use the scanner's pointers to capture the token's lexeme
  token.start = scanner.start;
  token.length = (int)(scanner.current - scanner.start);

  token.line = scanner.line;

  return token;
}

static Token errorToken(const char *message) {
  Token token;
  token.type = TOKEN_ERROR;
  token.start = message;
  token.length = (int)strlen(message);
  token.line = scanner.line;

  return token;
}

static void skipWhitespace() {
  for (;;) {
    char c = peek();

    switch (c) {
    case ' ':
    case '\r':
    case '\t':
      advance();
      break;

    case '\n':
      // Increment line counter and consume newline character
      scanner.line++;
      advance();
      break;

    case '/':
      if (peekNext() == '/') {
        // A comment goes until the end of the line
        while (peek() != '\n' && !isAtEnd()) {
          advance();
        }
      } else {
        return;
      }
      break;

    default:
      return;
    }
  }
}

/// To better understand this function, let's use the keyword "super" as an
/// example. First, calculate if the length of the scanned lexeme matches the
/// expected keyword length (which is the sum of the arguments "start", 1, and
/// "length", 4, i.e., 5). Then, check if the current scanned lexeme starting
/// index plus the "start" offset (which is 1), resulting in "uper", matches the
/// rest of the expected keyword string ("uper"). If so, it's the "super"
/// keyword.
static TokenType checkKeyword(int start, int length, const char *rest,
                              TokenType type) {
  if (scanner.current - scanner.start == start + length &&
      memcmp(scanner.start + start, rest, length) == 0) {
    return type;
  }

  return TOKEN_IDENTIFIER;
}

static TokenType identifierType() {
  switch (scanner.start[0]) {
  case 'a':
    return checkKeyword(1, 2, "nd", TOKEN_AND);
  case 'c':
    return checkKeyword(1, 4, "lass", TOKEN_CLASS);
  case 'e':
    return checkKeyword(1, 3, "lse", TOKEN_ELSE);
  case 'f':
    // Check if the lexeme is just "f"
    if (scanner.current - scanner.start > 1) {
      switch (scanner.start[1]) {
      case 'a':
        return checkKeyword(2, 3, "lse", TOKEN_FALSE);
      case 'o':
        return checkKeyword(2, 1, "r", TOKEN_FOR);
      case 'u':
        return checkKeyword(2, 1, "n", TOKEN_FUN);
      }
    }
    break;
  case 'i':
    return checkKeyword(1, 1, "f", TOKEN_IF);
  case 'n':
    return checkKeyword(1, 2, "il", TOKEN_NIL);
  case 'o':
    return checkKeyword(1, 1, "r", TOKEN_OR);
  case 'p':
    return checkKeyword(1, 4, "rint", TOKEN_PRINT);
  case 'r':
    return checkKeyword(1, 5, "eturn", TOKEN_RETURN);
  case 's':
    return checkKeyword(1, 4, "uper", TOKEN_SUPER);
  case 't':
    // Check if the lexeme is just "t"
    if (scanner.current - scanner.start > 1) {
      switch (scanner.start[1]) {
      case 'h':
        return checkKeyword(2, 2, "is", TOKEN_THIS);
      case 'r':
        return checkKeyword(2, 2, "ue", TOKEN_TRUE);
      }
    }
    break;
  case 'v':
    return checkKeyword(1, 2, "ar", TOKEN_VAR);
  case 'w':
    return checkKeyword(1, 4, "hile", TOKEN_WHILE);
  }

  return TOKEN_IDENTIFIER;
}

static Token identifier() {
  // Consume alphanumerics
  while (isAlpha(peek()) || isDigit(peek())) {
    advance();
  }

  return makeToken(identifierType());
}

static Token number() {
  // Consume the integer part
  while (isDigit(peek())) {
    advance();
  }

  // Look for a fractional part
  if (peek() == '.' && isDigit(peekNext())) {
    // Consume the "."
    advance();

    // Consume the fractional part
    while (isDigit(peek())) {
      advance();
    }
  }

  return makeToken(TOKEN_NUMBER);
}

static Token string() {
  while (peek() != '"' && !isAtEnd()) {
    // Increment line counter for multi-line strings
    if (peek() == '\n') {
      scanner.line++;
    }

    advance();
  }

  if (isAtEnd()) {
    return errorToken("Unterminated string.");
  }

  // The closing quote
  advance();

  return makeToken(TOKEN_STRING);
}

/// During the scanning process, the start pointer is statically positioned at
/// the end of the last scanned token (or the beginning of the source string).
Token scanToken() {
  skipWhitespace();

  scanner.start = scanner.current;

  if (isAtEnd()) {
    return makeToken(TOKEN_EOF);
  }

  char c = advance();

  if (isAlpha(c)) {
    return identifier();
  }
  if (isDigit(c)) {
    return number();
  }

  switch (c) {
  // One-character lexemes
  case '(':
    return makeToken(TOKEN_LEFT_PAREN);
  case ')':
    return makeToken(TOKEN_RIGHT_PAREN);
  case '{':
    return makeToken(TOKEN_LEFT_BRACE);
  case '}':
    return makeToken(TOKEN_RIGHT_BRACE);
  case ';':
    return makeToken(TOKEN_SEMICOLON);
  case ',':
    return makeToken(TOKEN_COMMA);
  case '.':
    return makeToken(TOKEN_DOT);
  case '-':
    return makeToken(TOKEN_MINUS);
  case '+':
    return makeToken(TOKEN_PLUS);
  case '/':
    return makeToken(TOKEN_SLASH);
  case '*':
    return makeToken(TOKEN_STAR);

  // Two-character lexemes
  case '!':
    return makeToken(match('=') ? TOKEN_BANG_EQUAL : TOKEN_BANG);
  case '=':
    return makeToken(match('=') ? TOKEN_EQUAL_EQUAL : TOKEN_EQUAL);
  case '<':
    return makeToken(match('=') ? TOKEN_LESS_EQUAL : TOKEN_LESS);
  case '>':
    return makeToken(match('=') ? TOKEN_GREATER_EQUAL : TOKEN_GREATER);

  // Literal tokens
  case '"':
    return string();
  }

  return errorToken("Unexpected character.");
}
