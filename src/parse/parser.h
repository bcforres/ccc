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
 * Parser Interface
 */

#ifndef _PARSER_H_
#define _PARSER_H_

#include "parse/ast.h"
#include "parse/lexer.h"
#include "parse/type_table.h"

/**
 * Parses input from a lexer into an AST
 *
 * @param lexer Lexer to parse tokens from
 * @param result The parsed AST
 * @return CCC_OK on success, error code on error
 */
status_t parser_parse(lexer_t *lexer, trans_unit_t **result);

/**
 * Parses input from a lexer into an expression
 *
 * @param lexer Lexer to parse tokens from
 * @param result The parsed expression
 * @return CCC_OK on success, error code on error
 */
status_t parser_parse_expr(lexer_t *lexer, expr_t **result);

#endif /* _PARSER_H_ */
