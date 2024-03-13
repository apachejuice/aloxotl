// apachejuice, 27.02.2024
// See LICENSE for details.
#include "scanner.h"
#include "common.h"
#include "memory.h"

#include <complex.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <stdlib.h>

struct {
    const char *start;
    const char *current;
    size        line;
    size        column;
} scanner;

void init_scanner (const char *source) {
    scanner.start   = source;
    scanner.current = source;
    scanner.line    = 1;
    scanner.column  = 1;
}

static bool is_digit (char c) {
    return c >= '0' && c <= '9';
}

static bool is_alpha (char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

static bool is_at_end (void) {
    return *scanner.current == 0;
}

static token make_token (token_type type) {
    token tok = {
        .type   = type,
        .start  = scanner.start,
        .len    = (size) (scanner.current - scanner.start -
                       (type == TOKEN_STRING ? 1 : 0)),
        .line   = scanner.line,
        .column = scanner.column,
    };

    return tok;
}

// IF the token's type is TOKEN_ERROR, the lexeme needs to be freed.
static token error_token (const char *format, ...) {
    va_list ap;
    va_start (ap, format);

    size  len = vsnprintf (NULL, 0, format, ap);
    char *msg = reallocate (NULL, 0, len + 1);

    vsprintf (msg, format, ap);
    va_end (ap);

    token tok = {
        .type   = TOKEN_ERROR,
        .start  = msg,
        .len    = len,
        .line   = scanner.line,
        .column = scanner.column,
    };

    return tok;
}

static char advance (void) {
    char c = *scanner.current++;
    if (c == '\n') {
        scanner.line++;
        scanner.column = 1;
    } else {
        scanner.column++;
    }

    return scanner.current[-1];
}

static char peek (void) {
    return *scanner.current;
}

static bool match (char expected) {
    if (is_at_end ()) return false;
    if (*scanner.current != expected) return false;

    scanner.current++;
    return true;
}

static char peek_next (void) {
    if (is_at_end ()) return '\0';
    return scanner.current[1];
}

static void skip_whitespace (void) {
    while (1) {
        char c = peek ();
        switch (c) {
            case ' ':
            case '\r':
            case '\n':  // handled in advance ()
            case '\t': advance (); break;
            case '/':
                if (peek_next () == '/') {
                    // A comment goes until the end of the line.
                    while (peek () != '\n' && !is_at_end ()) advance ();
                } else {
                    return;
                }
                break;
            default: return;
        }
    }
}

static token string (void) {
    while (peek () != '"' && !is_at_end ()) {
        advance ();
    }

    if (is_at_end ()) return error_token ("Unterminated string");

    // The closing quote.
    advance ();
    return make_token (TOKEN_STRING);
}

static token number (void) {
    while (is_digit (peek ())) advance ();

    if (peek () == '.' && is_digit (peek_next ())) {
        advance ();

        while (is_digit (peek ())) advance ();
    }

    return make_token (TOKEN_NUMBER);
}

static token_type check_keyword (size start, size length, const char *remain,
                                 token_type type) {
    if ((size) (scanner.current - scanner.start) == start + length &&
        memcmp (scanner.start + start, remain, length) == 0) {
        return type;
    }

    return TOKEN_IDENTIFIER;
}

static token_type identifier_type (void) {
    switch (*scanner.start) {
        case 'a': return check_keyword (1, 2, "nd", TOKEN_AND);
        case 'c': return check_keyword (1, 4, "lass", TOKEN_CLASS);
        case 'e': return check_keyword (1, 3, "lse", TOKEN_ELSE);
        case 'f':
            if (scanner.current - scanner.start > 1) {
                switch (scanner.start[1]) {
                    case 'a': return check_keyword (2, 3, "lse", TOKEN_FALSE);
                    case 'o': return check_keyword (2, 1, "r", TOKEN_FOR);
                    case 'u': return check_keyword (2, 1, "n", TOKEN_FUN);
                }
            }

            break;
        case 'i': return check_keyword (1, 1, "f", TOKEN_IF);
        case 'n': return check_keyword (1, 2, "il", TOKEN_NIL);
        case 'o': return check_keyword (1, 1, "r", TOKEN_OR);
        case 'p': return check_keyword (1, 4, "rint", TOKEN_PRINT);
        case 'r': return check_keyword (1, 5, "eturn", TOKEN_RETURN);
        case 's': return check_keyword (1, 4, "uper", TOKEN_SUPER);
        case 't':
            if (scanner.current - scanner.start > 1) {
                switch (scanner.start[1]) {
                    case 'h': return check_keyword (2, 2, "is", TOKEN_THIS);
                    case 'r': return check_keyword (2, 2, "ue", TOKEN_TRUE);
                }
            }

            break;
        case 'v': return check_keyword (1, 2, "ar", TOKEN_VAR);
        case 'w': return check_keyword (1, 4, "hile", TOKEN_WHILE);
    }

    return TOKEN_IDENTIFIER;
}

static token identifier (void) {
    while (is_alpha (peek ()) || is_digit (peek ())) advance ();
    return make_token (identifier_type ());
}

token scan_token (void) {
    skip_whitespace ();
    scanner.start = scanner.current;
    if (is_at_end ()) return make_token (TOKEN_EOF);

    char c = advance ();
    if (is_alpha (c)) return identifier ();
    if (is_digit (c)) return number ();

    switch (c) {
        case '(': return make_token (TOKEN_LEFT_PAREN);
        case ')': return make_token (TOKEN_RIGHT_PAREN);
        case '{': return make_token (TOKEN_LEFT_BRACE);
        case '}': return make_token (TOKEN_RIGHT_BRACE);
        case ';': return make_token (TOKEN_SEMICOLON);
        case ',': return make_token (TOKEN_COMMA);
        case '.': return make_token (TOKEN_DOT);
        case '-': return make_token (TOKEN_MINUS);
        case '+': return make_token (TOKEN_PLUS);
        case '/': return make_token (TOKEN_SLASH);
        case '*': return make_token (TOKEN_STAR);
        case '!':
            return make_token (match ('=') ? TOKEN_BANG_EQUAL : TOKEN_BANG);
        case '=':
            return make_token (match ('=') ? TOKEN_EQUAL_EQUAL : TOKEN_EQUAL);
        case '<':
            return make_token (match ('=') ? TOKEN_LESS_EQUAL : TOKEN_LESS);
        case '>':
            return make_token (match ('=') ? TOKEN_GREATER_EQUAL
                                           : TOKEN_GREATER);
        case '"': return string ();
    }

    return error_token ("Unexpected character %c", *scanner.current);
}
