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
 * Implementation for compliation manager
 */

#include "manager.h"

#include <assert.h>

#include "parse/ast.h"
#include "parse/lexer.h"
#include "parse/parser.h"
#include "parse/preprocessor.h"
#include "parse/symtab.h"

status_t man_init(manager_t *manager, htable_t *macros) {
    assert(manager != NULL);
    status_t status = CCC_OK;

    if (CCC_OK != (status = pp_init(&manager->pp, macros))) {
        goto fail0;
    }

    if (CCC_OK != (status = st_init(&manager->symtab, IS_SYM))) {
        goto fail1;
    }
    if (CCC_OK != (status = st_init(&manager->string_tab, NOT_SYM))) {
        goto fail2;
    }
    if (CCC_OK !=
        (status = lexer_init(&manager->lexer, &manager->pp, &manager->symtab,
                             &manager->string_tab))) {
        goto fail3;
    }

    return status;

fail3:
    st_destroy(&manager->string_tab);
fail2:
    st_destroy(&manager->symtab);
fail1:
    pp_destroy(&manager->pp);
fail0:
    return status;
}

void man_destroy(manager_t *manager) {
    if (manager == NULL) {
        return;
    }

    lexer_destroy(&manager->lexer);
    st_destroy(&manager->string_tab);
    st_destroy(&manager->symtab);
    pp_destroy(&manager->pp);
}

status_t man_parse(manager_t *manager, trans_unit_t **ast) {
    assert(manager != NULL);
    assert(ast != NULL);
    return parser_parse(&manager->lexer, ast);
}

status_t man_parse_expr(manager_t *manager, expr_t **expr) {
    assert(manager != NULL);
    assert(expr != NULL);
    return parser_parse_expr(&manager->lexer, expr);
}