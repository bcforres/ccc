/*
 * Copyright (C) 2015 Bailey Forrest <baileycforrest@gmail.com>
 *
 * This file is part of CCC.
 *
 * CCC is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * CCC is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with CCC.  If not, see <http://www.gnu.org/licenses/>.
 */
/**
 * Token types
 */

#ifndef _TOKEN_H_
#define _TOKEN_H_

#include "util/file_directory.h"
#include "util/string_set.h"

typedef enum token_t {
    TOKEN_EOF,
    HASH,          // #
    HASHHASH,      // ##

    NEWLINE,       // '\n'
    BACKSLASH,     // '\\'

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

    // Built in
    OFFSETOF,      // __builtin_offsetof
    VA_LIST,       // __builtin_va_list
    VA_START,      // __builtin_va_start
    VA_ARG,        // __builtin_va_arg
    VA_END,        // __builtin_va_end
    VA_COPY,       // __builtin_va_copy

    // Types
    VOID,          // void

    CHAR,          // char
    SHORT,         // short
    INT,           // int
    LONG,          // long
    UNSIGNED,      // unsigned
    SIGNED,        // signed

    DOUBLE,        // double
    FLOAT,         // float

    // Other
    ID,            // identifier
    STRING,        // string
    INTLIT,        // Integral literal
    FLOATLIT,      // Float literal

    FUNC,          // __func__
} token_t;

/**
 * Structure representing a single lexeme
 *
 * Acts as a tagged union
 */
typedef struct lexeme_t {
    str_set_t hideset;
    fmark_t mark;                  /**< Location of lexeme */
    token_t type;                  /**< Type of lememe */

    union {
        char *id_name;
        char *str_val;
        struct {
            bool hasU;             /**< Has U suffix */
            bool hasL;             /**< Has L suffix */
            bool hasLL;            /**< Has LL suffix */
            long long int_val;     /**< For integral types */
        } int_params;
        struct {
            bool hasF;             /**< Has F suffix */
            bool hasL;             /**< Has L suffix */
            long double float_val; /**< For floating point types */
        } float_params;
    };
} lexeme_t;

typedef struct token_man_t {
    slist_t tokens;
} token_man_t;

void token_man_init(token_man_t *tm);
void token_man_destroy(token_man_t *tm);
lexeme_t *token_create(token_man_t *tm);

/**
 * Prints a token
 *
 * @param token The token to print
 */
void token_print(lexeme_t *token);

/**
 * Returns a pointer to a token's string representation
 */
const char *token_str(token_t token);


#endif /* _TOKEN_H_ */
