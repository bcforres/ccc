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
 * Preprocessor directive implementation
 */

#include "pp_directives.h"
#include "preprocessor_priv.h"

#include <assert.h>
#include <string.h>

#include "util/util.h"

static pp_directive_t s_directives[] = {
    { { NULL }, LEN_STR_LITERAL("define" ), pp_directive_define  },
    { { NULL }, LEN_STR_LITERAL("include"), pp_directive_include },
    { { NULL }, LEN_STR_LITERAL("ifndef" ), pp_directive_ifndef  },
    { { NULL }, LEN_STR_LITERAL("endif"),   pp_directive_endif   }
};

void pp_directives_register(htable_t *ht) {
    // Add directive handlers
    for (size_t i = 0; i < sizeof(s_directives) / sizeof(s_directives[0]);
         ++i) {
        ht_insert(ht, &s_directives[i].link);
    }

}

/**
 * Note that this function needs to allocate memory for the paramaters, macro
 * name and body. This is because the mmaped file will be unmapped when we
 * are done with the file.
 *
 * TODO: A possible optimization would be to read the whole macro twice. The
 * first time is to allocate the whole thing in one chunk, the second time is
 * to copy the macro into the newly allocated memory.
 */
void pp_directive_define(preprocessor_t *pp) {
    assert(NULL == sl_head(&pp->macro_insts) && "Define inside macro!");

    pp_file_t *file = sl_head(&pp->file_insts);
    char *cur = file->cur;
    char *end = file->end;
    char *lookahead = file->cur;

    // Skip whitespace before name
    SKIP_WS(lookahead, end);
    if (lookahead == end) {
        // TODO: Report/handle error
        assert(false && "Macro definition at end of file");
    }
    cur = lookahead;

    // Read the name of the macro
    ADVANCE_IDENTIFIER(lookahead, end);
    if (lookahead == end) {
        // TODO: Report/handle error
        assert(false && "Macro definition at end of file");
    }

    size_t name_len = lookahead - cur;

    len_str_node_t lookup = { { NULL }, { cur, name_len } };

    if (NULL != ht_lookup(&pp->macros, &lookup)) {
        // TODO: warn about redefined macro
    }

    // Create the macro object
    pp_macro_t *new_macro = malloc(sizeof(pp_macro_t));
    if (NULL == new_macro) {
        // TODO: Report/handle error
        assert(false && "Out of memory");
    }

    status_t status = pp_macro_init(new_macro);
    if (CCC_OK != status) {
        // TODO: Report/handle error
        assert(false && "Failed to create macro");
    }

    // Allocate the name
    new_macro->name.len = name_len;
    new_macro->name.str = malloc(name_len + 1);
    if (NULL == new_macro->name.str) {
        // TODO: Report/handle error
        assert(false && "Out of memory");
    }
    strncpy(new_macro->name.str, cur, new_macro->name.len);
    new_macro->name.str[new_macro->name.len] = '\0';

    cur = lookahead;

    // Process paramaters
    new_macro->num_params = 0;
    if ('(' == *lookahead) {
        while (lookahead != end) {
            new_macro->num_params++;

            ADVANCE_IDENTIFIER(lookahead, end);
            size_t param_len = lookahead - cur;

            if (param_len == 0) {
                // TODO: Handle correctly
                assert(false && "missing paramater name");
            }

            // Allocate paramaters
            len_str_node_t *string = malloc(sizeof(len_str_t));
            if (NULL == string) {
                // TODO: Report/handle error
                assert(false && "Out of memory");
            }
            string->str.len = param_len;
            string->str.str = malloc(param_len + 1);
            strncpy(string->str.str, cur, param_len);
            string->str.str[param_len] = '\0';

            sl_append(&new_macro->params, &string->link);

            cur = lookahead + 1;

            if (*lookahead == ',') {
                lookahead++;
            }

            if (*lookahead == ')') { // End of param list
                lookahead++;
                break;
            }
        }
        if (lookahead == end) {
            // TODO: Report/handle error
            assert(false && "Macro definition at end of file");
        }
    }

    // Skip whitespace after parameters
    SKIP_WS(lookahead, end);
    if (lookahead == end) {
        // TODO: Report/handle error
        assert(false && "Macro definition at end of file");
    }

    cur = lookahead;

    // Keep processing macro until we find a newline
    while (lookahead != end) {
        if ('\n' == *lookahead && '\\' != *(lookahead - 1)) {
            break;
        }
        lookahead++;
    }
    if (lookahead == end) {
        // TODO: Report/handle error
        assert(false && "Macro definition at end of file");
    }

    // Allocate the macro body
    size_t macro_len = lookahead - cur;
    new_macro->start = macro_len == 0 ? NULL : malloc(macro_len + 1);
    if (macro_len != 0 && NULL == new_macro->start) {
        // TODO: Report/handle error
        assert(false && "Out of memory");
    }
    strncpy(new_macro->start, cur, macro_len);

    new_macro->end = new_macro->start + macro_len;
    *new_macro->end = '\0';

    // Add it to the hashtable
    ht_insert(&pp->macros, &new_macro->link);
}

void pp_directive_include(preprocessor_t *pp) {
    //TODO: Implement this
    (void)pp;
}

void pp_directive_ifndef(preprocessor_t *pp) {
    //TODO: Implement this
    (void)pp;
}

void pp_directive_endif(preprocessor_t *pp) {
    //TODO: Implement this
    (void)pp;
}
