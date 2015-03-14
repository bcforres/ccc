/*
  Copyright (C) 2015 Bailey Forrest <baileycforrest@gmail.com>

  This file is part of CCC.

  CCC is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  CCC is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with CCC.  If not, see <http://www.gnu.org/licenses/>.
*/
/**
 * Token types
 */

#ifndef _TOKEN_H_
#define _TOKEN_H_

#include "util/file_directory.h"

typedef enum token_t {
    TOKEN_EOF,           // end of file
    // Delimiters
    LBRACE,        // {
    RBRACE,        // }
    LPAREN,        // (
    RPAREN,        // )
    SEMI,          // ;
    COMMA,         // ,
    LBRACK,        // [
    RBRACK,        // ]
    DEREF,         // ->
    DOT,           // .
    ELIPSE,        // ...

    COND,          // ?
    COLON,         // :

    // Assignment operators
    ASSIGN,        // =
    PLUSEQ,        // +=
    MINUSEQ,       // -=
    STAREQ,        // *=
    DIVEQ,         // /=
    MODEQ,         // %=
    BITXOREQ,      // ^=
    BITOREQ,       // |=
    BITANDEQ,      // &=
    RSHIFTEQ,      // >>=
    LSHIFTEQ,      // <<=

    // Comparison operators
    EQ,            // ==
    NE,            // !=
    LT,            // <
    GT,            // >
    LE,            // <=
    GE,            // >=

    // Arithmetic
    RSHIFT,        // >>
    LSHIFT,        // <<

    LOGICAND,      // &&
    LOGICOR,       // ||
    LOGICNOT,      // !

    PLUS,          // +
    MINUS,         // -
    STAR,          // *
    DIV,           // /
    MOD,           // %

    BITAND,        // &
    BITOR,         // |
    BITXOR,        // ^
    BITNOT,        // ~

    INC,           // ++
    DEC,           // --

    // Keywords
    AUTO,          // auto
    BREAK,         // break
    CASE,          // case
    CONST,         // const
    CONTINUE,      // continue
    DEFAULT,       // default
    DO,            // do
    ELSE,          // else
    ENUM,          // enum
    EXTERN,        // extern
    FOR,           // for
    GOTO,          // goto
    IF,            // if
    INLINE,        // inline
    REGISTER,      // register
    RESTRICT,      // restrict
    RETURN,        // return
    SIZEOF,        // sizeof
    STATIC,        // static
    STRUCT,        // struct
    SWITCH,        // switch
    TYPEDEF,       // typedef
    UNION,         // union
    VOLATILE,      // volatile
    WHILE,         // while

    // Underscore keywords
    ALIGNAS,       // _Alignas
    ALIGNOF,       // _Alignof
    BOOL,          // _Bool
    COMPLEX,       // _Complex
    GENERIC,       // _Generic
    IMAGINARY,     // _Imaginary
    NORETURN,      // _Noreturn
    STATIC_ASSERT, // _Static_assert
    THREAD_LOCAL,  // _Thread_local

    // Types
    VOID,     // void

    CHAR,     // char
    SHORT,    // short
    INT,      // int
    LONG,     // long
    UNSIGNED, // unsigned
    SIGNED,   // signed

    DOUBLE,   // double
    FLOAT,    // float

    // Other
    ID,       // identifier
    STRING,   // string
    INTLIT,   // Integral literal
    FLOATLIT, // Float literal
} token_t;

typedef struct symtab_entry_t symtab_entry_t; // Forward definition

/**
 * Structure representing a single lexeme
 *
 * Acts as a tagged union
 */
typedef struct lexeme_t {
    token_t type;                  /**< Type of lememe */
    fmark_t mark;                  /**< Location of lexeme */

    union {
        symtab_entry_t *tab_entry; /**< For string/id types */
        struct {
            long long int_val;     /**< For integral types */
            bool hasU;             /**< Has U suffix */
            bool hasL;             /**< Has L suffix */
            bool hasLL;            /**< Has LL suffix */
        } int_params;
        struct {
            double float_val;      /**< For floating point types */
            bool hasF;             /**< Has F suffix */
        } float_params;
    };
} lexeme_t;

/**
 * Prints a token
 *
 * @param token The token to print
 */
void token_print(lexeme_t *token);


#endif /* _TOKEN_H_ */