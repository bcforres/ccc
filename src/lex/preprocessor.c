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
 * Implementation for preprocessor/file reader
 */
// TODO2: Change preprocessor to only output one space per set of whitespace
// characters, this will simplify implementation greatly

#include "preprocessor.h"
#include "preprocessor_priv.h"

#include <stddef.h>

#include <assert.h>
#include <ctype.h>
#include <string.h>
#include <time.h>

#include "optman.h"

#include "parse/pp_directives.h"

#include "util/htable.h"
#include "util/slist.h"
#include "util/string_builder.h"
#include "util/text_stream.h"
#include "util/util.h"

#define VA_ARG_NAME "__VA_ARGS__"
#define VA_ARG_LEN (sizeof(VA_ARG_NAME) - 1)

#define PREDEF_MACRO_LIT(name, word, type)                              \
    { SL_LINK_LIT, LEN_STR_LIT(name),                                   \
      TSTREAM_LIT(word, NULL, BUILT_IN_FILENAME, BUILT_IN_FILENAME, 0, 0), \
      SLIST_LIT(offsetof(len_str_node_t, link)), -1, type }

static pp_macro_t s_predef_macros[] = {
    // Standard C required macros
    PREDEF_MACRO_LIT("__FILE__", "", MACRO_FILE),
    PREDEF_MACRO_LIT("__LINE__", "", MACRO_LINE),
    PREDEF_MACRO_LIT("__DATE__", "", MACRO_DATE),
    PREDEF_MACRO_LIT("__TIME__", "", MACRO_TIME),
    PREDEF_MACRO_LIT("defined", "",  MACRO_DEFINED),
    PREDEF_MACRO_LIT("_Pragma", "",  MACRO_PRAGMA),
    PREDEF_MACRO_LIT("__STDC__", "1", MACRO_BASIC), // ISO C
    PREDEF_MACRO_LIT("__STDC_VERSION__", "201112L", MACRO_BASIC), // C11
    PREDEF_MACRO_LIT("__STDC_HOSTED__", "1", MACRO_BASIC), // stdlib available

    PREDEF_MACRO_LIT("__STDC_UTF_16__", "1", MACRO_BASIC), // UTF16 supported
    PREDEF_MACRO_LIT("__STDC_UTF_32__", "1", MACRO_BASIC), // UTF32 supported

    // We don't support these C features
    PREDEF_MACRO_LIT("__STDC_NO_ATOMICS__", "1", MACRO_BASIC),
    PREDEF_MACRO_LIT("__STDC_NO_COMPLEX__", "1", MACRO_BASIC),
    PREDEF_MACRO_LIT("__STDC_NO_THREADS__", "1", MACRO_BASIC),
    PREDEF_MACRO_LIT("__STDC_NO_VLA__", "1", MACRO_BASIC),

    // Required for compatability
    PREDEF_MACRO_LIT("__alignof__", "_Alignof", MACRO_BASIC),
    PREDEF_MACRO_LIT("__FUNCTION__", "__func__", MACRO_BASIC),

#ifdef __x86_64__
    PREDEF_MACRO_LIT("__amd64", "1", MACRO_BASIC),
    PREDEF_MACRO_LIT("__amd64__", "1", MACRO_BASIC),
    PREDEF_MACRO_LIT("__x86_64", "1", MACRO_BASIC),
    PREDEF_MACRO_LIT("__x86_64__", "1", MACRO_BASIC),
#endif

#ifdef __linux
    PREDEF_MACRO_LIT("__linux", "1", MACRO_BASIC),
    PREDEF_MACRO_LIT("__linux__", "1", MACRO_BASIC),
    PREDEF_MACRO_LIT("__gnu_linux__", "1", MACRO_BASIC),
    PREDEF_MACRO_LIT("__unix", "1", MACRO_BASIC),
    PREDEF_MACRO_LIT("__unix__", "1", MACRO_BASIC),
    PREDEF_MACRO_LIT("_LP64", "1", MACRO_BASIC),
    PREDEF_MACRO_LIT("__LP64__", "1", MACRO_BASIC),
    PREDEF_MACRO_LIT("__ELF__", "1", MACRO_BASIC),
#endif

    // TODO1: Conditionally compile or handle these better
    PREDEF_MACRO_LIT("char16_t", "short", MACRO_BASIC),
    PREDEF_MACRO_LIT("char32_t", "int", MACRO_BASIC)
};

/**
 * Predefined macros that have parameters. The param parsing logic is too fancy
 * to have a literal for
 */
static len_str_t s_predef_param_macros[] = {
    LEN_STR_LIT("__attribute__(xyz) /* None */") // Pesky attribute
};

void pp_init(preprocessor_t *pp, htable_t *macros) {
    static const ht_params_t macro_params = {
        0,                                // No Size estimate
        offsetof(pp_macro_t, name),       // Offset of key
        offsetof(pp_macro_t, link),       // Offset of ht link
        len_str_hash,                     // Hash function
        len_str_eq,                       // void string compare
    };

    static const ht_params_t directive_params = {
        0,                                // No Size estimate
        offsetof(pp_directive_t, key),    // Offset of key
        offsetof(pp_directive_t, link),   // Offset of ht link
        len_str_hash,                          // Hash function
        len_str_eq,                          // void string compare
    };

    sl_init(&pp->file_insts, offsetof(pp_file_t, link));
    sl_init(&pp->macro_insts, offsetof(pp_macro_inst_t, link));
    sl_init(&pp->search_path, offsetof(len_str_node_t, link));
    sl_init(&pp->fmarks, offsetof(fmark_node_t, link));

    ht_init(&pp->directives, &directive_params);

    if (macros == NULL) {
        ht_init(&pp->macros, &macro_params);

        // Register directive handlers
        pp_directives_init(pp);

        // Load predefined macros
        for (size_t i = 0; i < STATIC_ARRAY_LEN(s_predef_macros); ++i) {
            status_t status = ht_insert(&pp->macros, &s_predef_macros[i].link);
            assert(status == CCC_OK);
        }

        static bool predef_loaded = false;
        if (!predef_loaded) {
            // Load these only once
            // They are stored on the option manager's macros so they persist
            // between preprocessor instantiations
            predef_loaded = true;

            for (size_t i = 0; i < STATIC_ARRAY_LEN(s_predef_param_macros);
                 ++i) {
                macro_node_t *node = emalloc(sizeof(macro_node_t));
                len_str_t *str = &s_predef_param_macros[i];
                tstream_t stream;
                ts_init(&stream, str->str, str->str + str->len,
                        BUILT_IN_FILENAME, BUILT_IN_FILENAME, NULL, 0, 0);

                if (CCC_OK != pp_directive_define_helper(&stream, &node->macro,
                                                         false, NULL)) {
                    free(node);
                    continue;
                }
                node->macro->type = MACRO_CLI_OPT;
                sl_append(&optman.macros, &node->link);
            }
        }

        // Load command line parameter macros
        SL_FOREACH(cur, &optman.macros) {
            macro_node_t *macro_node = GET_ELEM(&optman.macros, cur);
            status_t status = ht_insert(&pp->macros, &macro_node->macro->link);
            assert(status == CCC_OK);
        }

        pp->pp_if = false;
    } else {
        ht_create_handle(&pp->macros, macros);
        pp->pp_if = true;
    }

    // Initialize state
    pp->block_comment = false;
    pp->line_comment = false;
    pp->string = false;
    pp->char_string = false;
    pp->stringify_esc = false;
    pp->ignore_escape = false;
    pp->char_line = false;
    pp->ignore = false;
    pp->in_directive = false;
}

/**
 * Release all resources in pp
 */
void pp_destroy(preprocessor_t *pp) {
    SL_DESTROY_FUNC(&pp->file_insts, pp_file_destroy);
    SL_DESTROY_FUNC(&pp->macro_insts, pp_macro_inst_destroy);
    SL_DESTROY_FUNC(&pp->fmarks, free);

    if (!pp->pp_if) {
        HT_DESTROY_FUNC(&pp->macros, pp_macro_destroy);
    }
    pp_directives_destroy(pp);
    ht_destroy(&pp->directives);
}

void pp_close(preprocessor_t *pp) {
    pp_destroy(pp);
}

/**
 * Map the file and push it onto the file_insts stack
 */
status_t pp_open(preprocessor_t *pp, const char *filename) {
    status_t status = CCC_OK;
    pp_file_t *pp_file;
    if (CCC_OK !=
        (status = pp_map_file(filename, &pp_file))) {
        goto fail;
    }

    sl_prepend(&pp->file_insts, &pp_file->link);

    fdir_entry_t *file = fdir_lookup(filename);
    assert(file != NULL); // pp_map_file should add the file to the directory

fail:
    return status;
}

void pp_last_mark(preprocessor_t *pp, fmark_t *result) {
    assert(result != NULL);
    memcpy(result, &pp->last_mark, sizeof(fmark_t));
}

status_t pp_nextchar(preprocessor_t *pp, int *nextchar) {
    status_t status = CCC_OK;

    while (!pp->ignore) {
        if (CCC_RETRY != (status = pp_nextchar_helper(pp, nextchar))) {
            return status;
        }
    }

    while (pp->ignore || status == CCC_RETRY) {
        // Fetch characters until another directive is run to tell us to stop
        // ignoring
        status = pp_nextchar_helper(pp, nextchar);
        if (pp->ignore && *nextchar == PP_EOF) {
            // Only go to end of current file
            logger_log(&pp->last_mark, LOG_ERR, "Unexpected EOF");
            return CCC_ESYNTAX;
        }
    }

    return status;
}

pp_file_t *pp_file_create(void) {
    pp_file_t *pp_file = emalloc(sizeof(pp_file_t));

    sl_init(&pp_file->cond_insts, offsetof(pp_cond_inst_t, link));
    pp_file->if_count = 0;

    return pp_file;
}

void pp_file_destroy(pp_file_t *pp_file) {
    SL_DESTROY_FUNC(&pp_file->cond_insts, free);
    free(pp_file);
}

status_t pp_map_file(const char *filename, pp_file_t **result) {
    status_t status = CCC_OK;
    pp_file_t *pp_file = pp_file_create();

    fdir_entry_t *entry;
    if (CCC_OK != (status = fdir_insert(filename, &entry))) {
        goto fail;
    }

    ts_init(&pp_file->stream, entry->buf, entry->end, entry->filename,
            entry->buf, NULL, 1, 1);

    *result = pp_file;
    return status;

fail:
    pp_file_destroy(pp_file);
    return status;
}

void pp_map_stream(preprocessor_t *pp, tstream_t *src, pp_macro_t *macro,
                   char *buf, pp_macro_inst_flag_t flags) {
    pp_macro_inst_t *macro_inst = pp_macro_inst_create(macro, flags);
    macro_inst->buf = buf;
    memcpy(&macro_inst->stream, src, sizeof(tstream_t));
    sl_prepend(&pp->macro_insts, &macro_inst->link);
}

pp_macro_t *pp_macro_create(char *name, size_t len) {
    // Allocate macro and name in one chunk
    pp_macro_t *macro = emalloc(sizeof(pp_macro_t) + len + 1);
    sl_init(&macro->params, offsetof(len_str_node_t, link));

    // Set to safe value for destruction
    ((tstream_t *)&macro->stream)->mark.last = NULL;

    macro->name.str = (char *)macro + sizeof(pp_macro_t);
    macro->name.len = len;
    strncpy(macro->name.str, name, len);
    macro->name.str[len] = '\0';

    macro->type = MACRO_BASIC;

    return macro;
}

void pp_macro_destroy(pp_macro_t *macro) {
    if (macro == NULL ||
        macro->type == MACRO_CLI_OPT || // Don't destroy CLI option macros
        (macro >= s_predef_macros &&
         macro < s_predef_macros + STATIC_ARRAY_LEN(s_predef_macros))) {
        // Don't do anything when destroying predefined macros
        return;
    }
    SL_DESTROY_FUNC(&macro->params, free);
    free(macro);
}

pp_macro_inst_t *pp_macro_inst_create(pp_macro_t *macro,
                                      pp_macro_inst_flag_t flags) {
    pp_macro_inst_t *macro_inst = emalloc(sizeof(pp_macro_inst_t));
    sl_init(&macro_inst->param_insts, offsetof(pp_param_inst_t, link));

    static const ht_params_t pp_param_map_params = {
        0,                                   // No size hint
        offsetof(pp_param_map_elem_t, key),  // Key offset
        offsetof(pp_param_map_elem_t, link), // HT link offset
        len_str_hash,                        // String Hash function
        len_str_eq                           // void string compare
    };

    ht_init(&macro_inst->param_map, &pp_param_map_params);

    // Shallow copy because macro already has a copy of its fmarks
    if (macro != NULL) {
        memcpy(&macro_inst->stream, &macro->stream, sizeof(tstream_t));
    }
    macro_inst->macro = macro;
    macro_inst->buf = NULL;
    macro_inst->flags = flags;

    return macro_inst;
}

static void pp_param_map_elem_destroy(pp_param_map_elem_t *elem) {
    if (elem->expand_val.len != 0) {
        free(elem->expand_val.str);
    }
    free(elem);
}

void pp_macro_inst_destroy(pp_macro_inst_t *macro_inst) {
    if (macro_inst == NULL) {
        return;
    }
    SL_DESTROY_FUNC(&macro_inst->param_insts, free);
    HT_DESTROY_FUNC(&macro_inst->param_map, pp_param_map_elem_destroy);
    free(macro_inst->buf);
    free(macro_inst);
}

tstream_t *pp_get_stream(preprocessor_t *pp, bool *stringify,
                         bool *noexpand) {
    assert(pp != NULL);
    assert(stringify != NULL);
    assert(noexpand != NULL);

    tstream_t *stream = NULL; // Stream to work on

    *stringify = false;
    *noexpand = false;

    // Try to find an incomplete macro on the stack
    pp_macro_inst_t *macro_inst = sl_head(&pp->macro_insts);
    while (macro_inst != NULL) {

        // Try to find in complete macro parameter
        pp_param_inst_t *param_inst = sl_head(&macro_inst->param_insts);
        while (param_inst != NULL) {
            stream = &param_inst->stream;

            if (!ts_end(stream)) { // Found an unfinished parameter
                *noexpand = true;
                if (param_inst->stringify) {
                    *stringify = true;
                }
                return stream;
            }

            // Mark stringification of paramaters
            // Return the string in case both the stringification and the
            // end of a mapped stream happen at the same time (we want the
            // stringification sent before the EOF)
            if (stringify != NULL && param_inst->stringify) {
                *stringify = true;
                param_inst->stringify = false; // Avoid infinite loop
                return stream;
            }

            param_inst = sl_pop_front(&macro_inst->param_insts);
            free(param_inst);
            param_inst = sl_head(&macro_inst->param_insts);
        }

        stream = &macro_inst->stream;

        if (!ts_end(stream)) { // Found an unfinished macro
            if (macro_inst->flags & MACRO_INST_NOEXPAND) {
                *noexpand = true;
            }
            return stream;
        }

        macro_inst = sl_pop_front(&pp->macro_insts);

        // For mapped streams, we need to signal an EOF
        if (macro_inst->flags & MACRO_INST_MAPPED) {
            pp_macro_inst_destroy(macro_inst);
            return NULL;
        }
        pp_macro_inst_destroy(macro_inst);
        macro_inst = sl_head(&pp->macro_insts);
    }

    // if we're done with macros, try to find an incomplete file
    pp_file_t *file = sl_head(&pp->file_insts);
    while (file != NULL) {
        stream = &file->stream;

        if (!ts_end(stream)) { // Found an unfinished file
            return stream;
        }

        file = sl_pop_front(&pp->file_insts);
        pp_file_destroy(file);
        file = sl_head(&pp->file_insts);
    }

    return NULL;
}

pp_param_map_elem_t *pp_lookup_macro_param(preprocessor_t *pp,
                                           len_str_t *lookup) {
    SL_FOREACH(cur, &pp->macro_insts) {
        pp_macro_inst_t *cur_macro_inst = GET_ELEM(&pp->macro_insts, cur);

        // Skip mapped macros
        if (cur_macro_inst->macro == NULL) {
            continue;
        }
        pp_param_map_elem_t *param =
            ht_lookup(&cur_macro_inst->param_map, lookup);

        // Only search up to the first not mapped macro
        return param;
    }

    return NULL;
}

status_t pp_nextchar_helper(preprocessor_t *pp, int *nextchar) {
    status_t status = CCC_OK;

    bool stringify, noexpand;
    tstream_t *stream = pp_get_stream(pp, &stringify, &noexpand);

    // Macros already have parameters evaluated, just continue
    if (noexpand) {
        // If we're stringifying, and current character is a \ or "
        // we need to escape
        if (stringify) {
            if(ts_cur(stream) == '"' || ts_cur(stream) == '\\') {
                if (!pp->stringify_esc) {
                    pp->stringify_esc = true;
                    *nextchar = '\\';
                    return status;
                } else {
                    pp->stringify_esc = false;
                }
            } else if (ts_skip_ws_and_comment(stream, true) > 0) {
                *nextchar = ' ';
                return status;
            }
        }
        *nextchar = ts_advance(stream);
        return status;
    }

    // Handle closing quote of stringification
    if (stringify) {
        *nextchar = '"';
        return status;
    }
    pp_macro_inst_t *macro_inst = sl_head(&pp->macro_insts);

    // Finished processing all files
    if (stream == NULL) {
        if (pp->block_comment) {
            logger_log(NULL, LOG_ERR, "unterminated comment");
        }
        *nextchar = PP_EOF;
        return status;
    }

    // Get copy of current location, the last mark
    memcpy(&pp->last_mark, &stream->mark, sizeof(fmark_t));

    int cur_char = ts_cur(stream);

    // Handle %: = # digraph
    if (cur_char == '%' && ts_next(stream) == ':') {
        cur_char = '#';
        ts_advance(stream);
    }

    int next_char = ts_next(stream);
    int last_char = ts_last(stream);

    // Handle comments
    if (cur_char == '/' && !pp->line_comment && !pp->block_comment &&
        !pp->string && !pp->char_string) {
        if (next_char == '/') {
            pp->line_comment = true;
        } else if (next_char == '*') {
            pp->block_comment = true;
            // Advance stream to prevent /*/ from opening/closing
            ts_advance(stream);
            ts_advance(stream);
        }
    }

    // If we're in a comment, return whitespace
    if (pp->line_comment) {
        if (cur_char == '\n') {
            pp->line_comment = false;
        }
        ts_advance(stream);
        *nextchar = ' ';
        return status;
    }

    if (pp->block_comment) {
        if (last_char == '*' && cur_char == '/') {
            pp->block_comment = false;
        }
        ts_advance(stream);
        *nextchar = ' ';
        return status;
    }

    // Found a character on line, don't process new directives
    if (!pp->char_line && cur_char != '#' && !isspace(cur_char)) {
        pp->char_line = true;
    }

    if (!pp->string && !pp->char_string && cur_char == '\'') {
        pp->char_string = true;
        *nextchar = ts_advance(stream);
        return status;
    }

    if (pp->char_string && cur_char == '\'' &&
        (last_char != '\\' || pp->ignore_escape)) {
        pp->char_string = false;
    }

    // Handle strings
    if (!pp->string && !pp->char_string && cur_char == '"') {
        pp->string = true;
        *nextchar = ts_advance(stream);
        return status;
    }

    if (pp->string && cur_char == '"' &&
        (last_char != '\\' || pp->ignore_escape)) {
        pp->string = false;
    }

    if (cur_char == '\n') {
        pp->char_line = false;
    }

    if (pp->string || pp->char_string) {
        if (cur_char == '\\' && last_char == '\\') {
            pp->ignore_escape = true;
        } else {
            pp->ignore_escape = false;
        }
        *nextchar = ts_advance(stream);
        return status;
    }


    tstream_t lookahead;
    memcpy(&lookahead, stream, sizeof(tstream_t));

    bool concat = false;
    // Handle concatenation
    if (macro_inst != NULL && !isspace(last_char) &&
        (cur_char == ' ' || cur_char == '\t' || cur_char == '\\' ||
         cur_char == '#')) {
        ts_skip_ws_and_comment(&lookahead, false);

        bool first = true;

        // Skip multiple ## with only white space around them
        while (!ts_end(&lookahead)) {
            if (ts_cur(&lookahead) == '#' && ts_next(&lookahead) == '#') {
                concat = true;
                ts_advance(&lookahead);
                ts_advance(&lookahead);

                // Handle %:%: digraph
            } else if (first && ts_next(&lookahead) == '%') {
                // If first, and we encountered the digraph, then ts_cur
                // points to # and ts_next points to %
                ts_advance(&lookahead);
                ts_advance(&lookahead);
                if (ts_cur(&lookahead) == ':') {
                    concat = true;
                    ts_advance(&lookahead);
                }
            } else if (ts_cur(&lookahead) == '%' &&
                       ts_next(&lookahead) == ':') {
                ts_advance(&lookahead);
                ts_advance(&lookahead);
                if (ts_cur(&lookahead) == '%' && ts_next(&lookahead) == ':') {
                    concat = true;
                    ts_advance(&lookahead);
                    ts_advance(&lookahead);
                }
            } else {
                break;
            }
            first = false;
            ts_skip_ws_and_comment(&lookahead, false);
        }
        if (concat) {
            // Set stream to new location
            memcpy(stream, &lookahead, sizeof(tstream_t));
            cur_char = ts_cur(stream);
            last_char = ts_last(stream);
            next_char = ts_next(stream);
        } else {
            // Need to reset lookahad
            memcpy(&lookahead, stream, sizeof(tstream_t));
        }
    }

    if (cur_char == '#') {
        // Check for preprocessor directive if we're not in a macro
        if (macro_inst == NULL) {
            // If we found a character before the #, just ignore it
            if (pp->char_line) {
                logger_log(&stream->mark, LOG_ERR, "Stray '#' in program");
                ts_advance(stream);
                return CCC_ESYNTAX;
            }

            if (pp->in_directive) {
                logger_log(&stream->mark, LOG_ERR,
                           "Unexpected '#' in directive");
            }
            ts_advance(stream);
            ts_skip_ws_and_comment(stream, false);
            char *start = ts_location(stream);
            size_t len = ts_advance_identifier(stream);

            // Single # isn't an error
            if (len == 0) {
                return CCC_RETRY;
            }
            len_str_t lookup = { start, len };
            pp_directive_t *directive = ht_lookup(&pp->directives, &lookup);

            if (directive == NULL) {
                logger_log(&stream->mark, LOG_ERR,
                           "Invalid preprocessing directive %.*s", len, start);
                ts_skip_line(stream, &pp->block_comment); // Skip rest of line
                return CCC_ESYNTAX;
            }

            // Perform directive action
            pp->in_directive = true;
            status = directive->action(pp);
            pp->in_directive = false;

            if (directive->skip_line) {
                ts_skip_line(stream, &pp->block_comment); // Skip rest of line
            }

            if (status != CCC_OK) {
                return status;
            }
            // Tell caller to fetch another character
            return CCC_RETRY;
        } else {
            // In macro, must be stringification, concatenation handled abave
            ts_advance(stream);

            // If we're in a macro without paramaters, just return #
            pp_macro_t *macro = macro_inst->macro;
            if (macro != NULL && sl_head(&macro->params) == NULL) {
                *nextchar = '#';
                return status;
            }
            ts_skip_ws_and_comment(stream, false);
            char *start = ts_location(stream);
            size_t len = ts_advance_identifier(stream);
            len_str_t lookup = { start, len };
            pp_param_map_elem_t *param =
                pp_lookup_macro_param(pp, &lookup);

            if (param == NULL) {
                // If this is a mapped stream, just return the #
                if (macro_inst->macro == NULL) {
                    ts_advance(stream);
                    *nextchar = '#';
                    return status;
                }
                logger_log(&stream->mark, LOG_ERR,
                           "'#' Is not followed by a macro paramater");
                ts_advance(stream);
                return CCC_ESYNTAX;
            }

            pp_param_inst_t *param_inst = emalloc(sizeof(pp_param_inst_t));
            memcpy(&param_inst->stream, stream, sizeof(tstream_t));
            param_inst->stream.cur = param->raw_val.str;
            param_inst->stream.end = param->raw_val.str + param->raw_val.len;
            param_inst->stringify = true;
            sl_prepend(&macro_inst->param_insts, &param_inst->link);

            // We are stringifying, so return a double quote
            *nextchar = '"';
            return status;
        }
    }

    switch (last_char) {
        // Cases which cannot be before a macro. Macros are identifiers, so if
        // we are already in an identifier, cur char cannot be a macro
    case ASCII_LOWER:
    case ASCII_UPPER:
    case ASCII_DIGIT:
    case '_':
        *nextchar = ts_advance(stream);
        return status;
    default:
        break;
        // Fall through, need to look for macros parameters
    }

    // if cur character cannot start an identifier, we know not to check for
    // a macro
    switch (cur_char) {
    case ASCII_LOWER:
    case ASCII_UPPER:
    case '_':
        break;
    default:
        *nextchar = ts_advance(stream);
        return status;
    }

    char *start = ts_location(&lookahead);
    size_t len = ts_advance_identifier(&lookahead);
    len_str_t lookup = { start, len };

    // Macro paramaters take precidence, look them up first
    if (macro_inst != NULL) {
        pp_param_map_elem_t *param =
            pp_lookup_macro_param(pp, &lookup);

        // Found a parameter
        if (param != NULL) {
            // Skip over parameter name
            memcpy(stream, &lookahead, sizeof(tstream_t));

            pp_param_inst_t *param_inst = emalloc(sizeof(pp_param_inst_t));
            memcpy(&param_inst->stream, &lookahead, sizeof(tstream_t));

            // Check if there's a concatenation after the parameter
            if (!concat) {
                ts_skip_ws_and_comment(&lookahead, false);
                if (ts_cur(&lookahead) == '#' && ts_cur(&lookahead) == '#') {
                    concat = true;
                }

                // Handle %:%: = ## digraph
                if (ts_cur(&lookahead) == '%' && ts_next(&lookahead) == ':') {
                    ts_advance(&lookahead);
                    ts_advance(&lookahead);
                    if (ts_cur(&lookahead) == '%' &&
                        ts_next(&lookahead) == ':') {
                        concat = true;
                    }
                }
            }

            // If we're concatenating, use raw macro value, not expanded value
            if (concat) {
                param_inst->stream.cur = param->raw_val.str;
                param_inst->stream.end = param->raw_val.str +
                    param->raw_val.len;
            } else {
                param_inst->stream.cur = param->expand_val.str;
                param_inst->stream.end = param->expand_val.str +
                    param->expand_val.len;
            }

            param_inst->stream.last = ' ';
            param_inst->stringify = false;

            // Add to stack of upper most macro, not cur_macro_inst
            sl_prepend(&macro_inst->param_insts, &param_inst->link);
            return CCC_RETRY;
        }

    }

    // Don't expand macros if we're concatenating
    if (concat) {
        *nextchar = ts_advance(stream);
        return status;
    }

    // Look up in the macro table
    pp_macro_t *macro = ht_lookup(&pp->macros, &lookup);

    if (macro == NULL) { // No macro found
        if (pp->pp_if) { // In preprecessor conditional
            // Skip past end of parameter
            memcpy(stream, &lookahead, sizeof(tstream_t));

            // Replace undefined macros with 0
            *nextchar = '0';
            return status;
        } else {
            *nextchar = ts_advance(stream);
            return status;
        }
    }

    bool in_macro = false;
    bool recursive = false;

    SL_FOREACH(cur, &pp->macro_insts) {
        pp_macro_inst_t *macro_inst = GET_ELEM(&pp->macro_insts, cur);
        if (macro_inst->macro != NULL) {
            in_macro = true;

            if (macro_inst->macro == macro) {
                recursive = true;
            }
        }
    }

    // Protect against recursive macros
    if (recursive) {
        *nextchar = ts_advance(stream);
        return status;
    }

    switch (macro->type) {
        // For basic macros, just keep going
    case MACRO_BASIC:
    case MACRO_CLI_OPT:
        break;
    case MACRO_FILE:
    case MACRO_LINE:
    case MACRO_DATE:
    case MACRO_TIME:
        memcpy(stream, &lookahead, sizeof(tstream_t));
        return pp_handle_special_macro(pp, macro);
    case MACRO_DEFINED:
        if (!pp->pp_if) {
            *nextchar = ts_advance(stream);
            return status;
        }
        *nextchar = pp_handle_defined(pp, &lookahead, stream);
        return status;
    case MACRO_PRAGMA:
        *nextchar = pp_directive_pragma_helper(pp, PRAGMA_UNDER);
        return status;
    default:
        assert(false);
    }

    if (macro->num_params >= 0) {
        ts_skip_ws_and_comment(&lookahead, true);
        if (ts_cur(&lookahead) != '(') {
            // If macro requires params, but none are provided, this is just
            // treated as identifier
            *nextchar = ts_advance(stream);
            return status;
        }
    }

    // Create macro as mapped so we get PP_EOF after evaluating macro's stream
    pp_macro_inst_t *new_macro_inst = pp_macro_inst_create(macro,
                                                           MACRO_INST_MAPPED);

    // -1 means non function style macro
    if (macro->num_params >= 0) {
        ts_advance(&lookahead); // Skip the paren

        if (macro->num_params == 0) {
            ts_skip_ws_and_comment(&lookahead, false);
            if (ts_cur(&lookahead) != ')') {
                logger_log(&stream->mark, LOG_ERR,
                           "unterminated argument list invoking macro \"%.*s\"",
                           lookup.len, lookup.str);
                status = CCC_ESYNTAX;
                goto fail;
            }
            ts_advance(&lookahead); // Skip the rparen
        } else {// Need to create param map
            int num_params = 0;

            bool done = false;

            SL_FOREACH(cur_link, &macro->params) {
                // Get current paramater in macro
                len_str_node_t *param_str = GET_ELEM(&macro->params, cur_link);

                bool vararg = param_str->str.str == NULL;
                assert(vararg ? param_str == sl_tail(&macro->params) : true);

                ts_skip_ws_and_comment(&lookahead, false);
                num_params++;
                tstream_t cur_param;
                memcpy(&cur_param, &lookahead, sizeof(tstream_t));
                int num_parens = 0;
                char *space_start = NULL;
                while (!ts_end(&lookahead)) {
                    if (ts_cur(&lookahead) == '"' ||
                        ts_cur(&lookahead) == '\'') {
                        ts_skip_string(&lookahead);
                        space_start = NULL;
                        continue;
                    }
                    if (ts_cur(&lookahead) == '/' &&
                        ts_next(&lookahead) == '*') {
                        space_start = ts_location(&lookahead);
                        ts_skip_ws_and_comment(&lookahead, false);
                        continue;
                    }
                    if (ts_cur(&lookahead) == '(') {
                        num_parens++;
                    } else if (num_parens > 0 && ts_cur(&lookahead) == ')') {
                        num_parens--;
                    } else if (num_parens == 0) {
                        // end of current param
                        if (ts_cur(&lookahead) == ',' && !vararg) {
                            break;
                        }
                        if (ts_cur(&lookahead) == ')') { // end of all params
                            done = true;
                            break;
                        }
                    }
                    if (!isspace(ts_cur(&lookahead))) {
                        space_start = NULL;
                    } else if (space_start == NULL) {
                        space_start = ts_location(&lookahead);
                    }
                    ts_advance(&lookahead);
                }

                if (ts_end(&lookahead)
                    && (num_params != macro->num_params || !done)) {
                    logger_log(&stream->mark, LOG_ERR,
                               "Unexpected EOF while scanning macro paramaters");
                    status = CCC_ESYNTAX;
                    goto fail;
                }

                char *end = space_start == NULL ?
                    ts_location(&lookahead) : space_start;
                size_t cur_len = end - ts_location(&cur_param);
                cur_param.end = end;

                size_t buf_len = cur_len + 1;
                pp_param_map_elem_t *param_elem =
                    emalloc(sizeof(pp_param_map_elem_t));
                string_builder_t sb;

                if (cur_len == 0) {
                    // If there is nothing in the paramater, set it to the
                    // empty string
                    param_elem->expand_val.str = "";
                    param_elem->expand_val.len = 0;
                } else {
                    sb_init(&sb, buf_len);
                    pp_map_stream(pp, &cur_param, NULL, NULL,
                                  MACRO_INST_MAPPED);

                    while (true) {
                        int nextchar = 0;
                        status_t status = pp_nextchar_helper(pp, &nextchar);
                        if (status == CCC_RETRY) {
                            continue;
                        }
                        if (nextchar == PP_EOF) {
                            break;
                        }
                        sb_append_char(&sb, nextchar);
                    }
                    if (sb_len(&sb) == 0) {
                        // If there is nothing in the paramater, set it to the
                        // empty string
                        param_elem->expand_val.str = "";
                        param_elem->expand_val.len = 0;
                        sb_destroy(&sb);
                    } else {
                        param_elem->expand_val.str = sb_buf(&sb);
                        param_elem->expand_val.len = sb_len(&sb);
                    }
                }

                // Insert the paramater mapping into the instance's hash table
                if (vararg) {
                    param_elem->key.str = VA_ARG_NAME;
                    param_elem->key.len = VA_ARG_LEN;
                } else {
                    param_elem->key.str = param_str->str.str;
                    param_elem->key.len = param_str->str.len;
                }

                // If we're in a macro, then the macro's parameters need to be
                // expanded. Otherwise, they are not
                if (!in_macro) {
                    param_elem->raw_val.str = cur_param.cur;
                    param_elem->raw_val.len = cur_len;
                } else {
                    param_elem->raw_val.str = param_elem->expand_val.str;
                    param_elem->raw_val.len = param_elem->expand_val.len;
                }

                ht_insert(&new_macro_inst->param_map, &param_elem->link);

                ts_advance(&lookahead);

                if (done) {
                    break;
                }
            }

            if ((done && num_params != macro->num_params) || !done) {
                logger_log(&stream->mark, LOG_ERR,
                           "Incorrect number of macro paramaters");
                status = CCC_ESYNTAX;
                goto fail;
            }
        }
    }
    fmark_node_t *mark_node = emalloc(sizeof(fmark_node_t));
    memcpy(&mark_node->mark, &stream->mark, sizeof(fmark_t));
    new_macro_inst->stream.mark.last = &mark_node->mark;
    sl_append(&pp->fmarks, &mark_node->link);

    // Add new macro instance to the stack
    sl_prepend(&pp->macro_insts, &new_macro_inst->link);

    tstream_t evaluated;
    memcpy(&evaluated, &new_macro_inst->stream, sizeof(tstream_t));

    // Evaluate the stream and map it
    string_builder_t sb;
    sb_init(&sb, 0);

    while (true) {
        int nextchar = 0;
        status_t status = pp_nextchar_helper(pp, &nextchar);
        if (status == CCC_RETRY) {
            continue;
        }
        if (nextchar == PP_EOF) {
            break;
        }
        sb_append_char(&sb, nextchar);
    }
    evaluated.cur = sb_buf(&sb);
    evaluated.end = sb_buf(&sb) + sb_len(&sb);
    pp_map_stream(pp, &evaluated, macro, sb_buf(&sb), MACRO_INST_NONE);

    // Set current to the end of the macro and params
    memcpy(stream, &lookahead, sizeof(tstream_t));
    return CCC_RETRY;

fail:
    ts_advance(stream); // Skip character to prevent infinite loop
    pp_macro_inst_destroy(new_macro_inst);
    return status;
}

status_t pp_handle_special_macro(preprocessor_t *pp, pp_macro_t *macro) {
    static bool date_err = false;
    static bool time_err = false;

    pp_file_t *file = sl_tail(&pp->file_insts);
    assert(file != NULL);
    tstream_t *stream = &file->stream;

    // Found a parameter, set param state in pp
    pp_macro_inst_t *macro_inst = pp_macro_inst_create(macro,
                                                       MACRO_INST_NOEXPAND);

    time_t t;
    struct tm *tm;

    pp->macro_buf[0] = '"';
    char *buf = pp->macro_buf + 1;
    size_t buf_size = sizeof(pp->macro_buf) - 1;
    bool quotes = true;

    size_t len = 0;
    switch (macro->type) {
    case MACRO_FILE:
        strncpy(buf, stream->mark.filename, buf_size);
        len = strlen(stream->mark.filename);
        break;
    case MACRO_LINE:
        quotes = false; // Line number is an integer
        len = snprintf(buf, buf_size, "%d", stream->mark.line);
        break;
    case MACRO_DATE: {
        if (-1 == (t = time(NULL)) || NULL == (tm = localtime(&t))) {
            if (!date_err) {
                date_err = true;
                logger_log(&stream->mark, LOG_WARN, "Failed to get Date!");
            }
            len = snprintf(buf, buf_size, "??? ?? ????");
        } else {
            len = strftime(buf, buf_size, "%b %d %Y", tm);
        }
        break;
    }
    case MACRO_TIME: {
        if (-1 == (t = time(NULL)) || NULL == (tm = localtime(&t))) {
            if (!time_err) {
                time_err = true;
                logger_log(&stream->mark, LOG_WARN, "Failed to get Time!");
            }
            len = snprintf(buf, buf_size, "??:??:??");
        } else {
            len = strftime(buf, buf_size, "%T", tm);
        }
        break;
    }
    default:
        assert(false);
    }
    len = MIN(len, buf_size - 2);
    if (quotes) {
        buf[len++] = '"';
    }
    buf[len] = '\0';

    memcpy(&macro_inst->stream, stream, sizeof(tstream_t));
    if (quotes) {
        macro_inst->stream.cur = pp->macro_buf;
    } else {
        macro_inst->stream.cur = pp->macro_buf + 1; // Skip the quote
    }
    macro_inst->stream.end = pp->macro_buf + len + 1; // +1 for leading quote
    sl_prepend(&pp->macro_insts, &macro_inst->link);

    return CCC_RETRY;
}

int pp_handle_defined(preprocessor_t *pp, tstream_t *lookahead,
                      tstream_t *stream) {
    ts_skip_ws_and_comment(lookahead, false);
    bool paren = false;
    if (ts_cur(lookahead) == '(') {
        ts_advance(lookahead);
        paren = true;
    }

    char *start = ts_location(lookahead);
    size_t len = ts_advance_identifier(lookahead);
    if (len == 0) {
        logger_log(&stream->mark, LOG_ERR,
                   "operator \"defined\" requires an identifier");
        memcpy(stream, lookahead, sizeof(tstream_t));
        return -(int)CCC_ESYNTAX;
    }
    len_str_t lookup = { start, len };
    pp_macro_t *macro = ht_lookup(&pp->macros, &lookup);
    if (paren) {
        if (ts_cur(lookahead) != ')') {
            logger_log(&stream->mark, LOG_ERR,
                       "missing ')' after \"defined\"");
            memcpy(stream, lookahead, sizeof(tstream_t));
            return -(int)CCC_ESYNTAX;
        }
        ts_advance(lookahead);
    }

    memcpy(stream, lookahead, sizeof(tstream_t));

    return macro == NULL ? '0' : '1';
}