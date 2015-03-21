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
 * Private Interface for preprocessor/file reader
 */

#ifndef _PREPROCESSOR_PRIV_H_
#define _PREPROCESSOR_PRIV_H_

#include "util/slist.h"
#include "util/util.h"
#include "util/logger.h"

/**
 * An instance of an open file on the preprocessor
 */
typedef struct pp_file_t {
    sl_link_t link;   /**< List link */
    tstream_t stream; /**< Text stream */
    int if_count;     /**< Number of instances of active if directive */
} pp_file_t;

/**
 * Struct for macro definition
 */
typedef struct pp_macro_t {
    sl_link_t link;         /**< List link */
    len_str_t name;         /**< Macro name, hashtable key */
    const tstream_t stream; /**< Text stream template */
    slist_t params;         /**< Macro paramaters, list of len_str */
    int num_params;         /**< Number of paramaters */
} pp_macro_t;

/**
 * Mapping from macro paramater to value
 */
typedef struct pp_param_map_elem_t {
    sl_link_t link; /**< List link */
    len_str_t key;  /**< Macro paramater being mappend */
    len_str_t val;  /**< Macro paramater value */
} pp_param_map_elem_t;

/**
 * Represents the macro invocation
 */
typedef struct pp_macro_inst_t {
    sl_link_t link;     /**< List link */
    htable_t param_map; /**< Mapping of strings to paramater values */
    tstream_t stream;   /**< Text stream of macro instance */
} pp_macro_inst_t;

/**
 * Helper function to fetch characters with macro substitution
 *
 * @param pp The preprocessor te fetch characters from
 * @param ignore_directive if true, directives are not processed
 */
int pp_nextchar_helper(preprocessor_t *pp, bool ignore_directive);

/**
 * Maps the specified file. Gives result as a pp_file_t
 *
 * @param filename Filename to open.
 * @param len Length of filename
 * @param result Location to store result. NULL if failed
 * @param last_file The file which included one being mapped. NULL if none.
 * @return CCC_OK on success, error code otherwise
 */
status_t pp_file_map(const char *filename, size_t len, pp_file_t *last_file,
                     pp_file_t **result);

/**
 * Unmaps and given pp_file_t. Does free pp_file.
 *
 * @param filename Filename to open.
 * @param result Location to store result. NULL if failed
 */
void pp_file_destroy(pp_file_t *pp_file);

/**
 * Creates a macro
 *
 * @param macro Points to new macro on success
 * @return CCC_OK on success, error code otherwise
 */
status_t pp_macro_create(char *name, size_t len, pp_macro_t **result);

/**
 * Destroys a macro definition. Does free macro
 *
 * @param macro definition to destroy
 */
void pp_macro_destroy(pp_macro_t *macro);

/**
 * Creates a macro instance
 *
 * @param macro to create instace of
 * @param result Location to store result. NULL if failed
 * @return CCC_OK on success, error code otherwise
 */
status_t pp_macro_inst_create(pp_macro_t *macro, pp_macro_inst_t **result);

/**
 * Destroys a macro instance. Does free macro_inst.
 *
 * @param macro intance to destroy
 */
void pp_macro_inst_destroy(pp_macro_inst_t *macro_inst);

#endif /* _PREPROCESSOR_PRIV_H_ */