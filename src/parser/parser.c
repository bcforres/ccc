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
 * Parser implementation
 *
 * Recursive descent style parser
 */

#include "parser.h"
#include "parser_priv.h"

#include <assert.h>
#include <stdlib.h>

#include "util/logger.h"

status_t parser_parse(lexer_t *lexer, typetab_t *typetab,
                      len_str_t *file, trans_unit_t **result) {
    assert(lexer != NULL);
    assert(typetab != NULL);
    assert(file != NULL);
    assert(result != NULL);
    status_t status = CCC_OK;
    lex_wrap_t lex;
    lex.lexer = lexer;
    lex.typetab = typetab;
    LEX_ADVANCE(&lex);
    status = par_translation_unit(&lex, file, result);
fail:
    return status;
}

static inline int par_get_prec(token_t token) {
    switch (token) {
    case STAR:
    case DIV:
    case MOD:      return 10;

    case PLUS:
    case MINUS:    return 9;

    case LSHIFT:
    case RSHIFT:   return 8;

    case LT:
    case GT:
    case LE:
    case GE:       return 7;

    case EQ:
    case NE:       return 6;

    case BITAND:   return 5;
    case BITXOR:   return 4;
    case BITOR:    return 3;
    case LOGICAND: return 2;
    case LOGICOR:  return 1;
    default:
        assert(false);
    }

}

bool par_greater_or_equal_prec(token_t t1, token_t t2) {
    return par_get_prec(t1) >= par_get_prec(t2);
}

status_t par_translation_unit(lex_wrap_t *lex, len_str_t *file,
                              trans_unit_t **result) {
    status_t status = CCC_OK;
    trans_unit_t *tunit;
    ALLLC_NODE(tunit, trans_unit_t);
    sl_init(&tunit->gdecls, offsetof(gdecl_t, link));
    tunit->path = file;

    while (lex->cur.type != TOKEN_EOF) {
        gdecl_t *gdecl;
        if (CCC_OK != (status = par_external_declaration(lex, &gdecl))) {
            goto fail;
        }
        sl_append(&tunit->gdecls, &gdecl->link);
    }
    *result = tunit;
fail:
    return status;
}

status_t par_external_declaration(lex_wrap_t *lex, gdecl_t **result) {
    status_t status = CCC_OK;
    gdecl_t *gdecl = NULL;
    ALLOC_NODE(gdecl, gdecl_t);
    gdecl->decl = NULL;

    type_t *type = NULL;

    // Must match at least one declaration specifier
    if (CCC_OK != (status = par_declaration_specifier(lex, &type))) {
        goto fail;
    }
    // Match declaration specifers until they don't match anymore
    while (CCC_BACKTRACK != (status = par_declaration_specifier(lex, &type))) {
        if (status != CCC_OK) {
            goto fail;
        }
    }

    ALLOC_NODE(gdecl->decl, stmt_t);
    gdecl->decl.type = STMT_DECL;
    gdecl->decl.decl.type = type;
    sl_init(&gdecl->decl.decl.decls, offsetof(decl_node, link));

    decl_node_t *decl_node = NULL;
    if (CCC_BACKTRACK == (status = par_declarator(lex, type, &decl_node))) {
        // If the next character isn't a declarator, then its a type declaration
        gdecl->type = GDECL_DECL;
        ALLOC_NODE(gdecl->decl, stmt_t);
        gdecl->decl = STMT_DECL;
        gdecl->decl.type = type;
        if (CCC_OK != sl_init(&gdecl->decls, offsetof(decl_node_t, link))) {
            free(gdecl->decl);
            goto fail;
        }
        goto done;
    }
    status = CCC_OK;
    sl_append(&gdecl->decl.decl.decls, &decl_node->link);

    if (decl_node.type.type == TYPE_FUNC) {
        if (CCC_OK != (status = par_function_definition(lex, gdecl))) {
            goto fail;
        }
    } else {
        if (CCC_OK != (status = par_declaration(lex, &gdecl->decl))) {
            goto fail;
        }
    }

done:
    *result = gdecl;
    return status;

fail:
    // free(type); TODO: Call type destructor
    free(gdecl);
    return status;
}

/**
 * Continues parsing after type and declarator
 *
 * gdecl_t.decl holds type and declarator as a declaration stmt
 */
status_t par_function_definition(lex_wrap_t *lex, gdecl_t *gdecl) {
    status_t status = CCC_OK;
    gdecl->type = GDECL_FDEFN;

    /* TODO: Handle old style function signature
    while (lex->cur.type != RPAREN) {
        stmt_t *param = NULL;
        if (CCC_OK != (status = par_declaration(lex, &param))) {
            goto fail;
        }
        sl_append(&gdecl->fdefn.params, param->link);
    }
    */

    if (CCC_OK != (status = par_compound_statement(lex, &gdecl->fdefn.stmt))) {
        goto fail;
        // TODO: Destroy function definition
    }
fail:
    return status;
}

status_t par_declaration_specifier(lex_wrap_t *lex, type_t **type) {
    switch (lex->cur.type) {
        // Storage class specifiers
    case AUTO:
    case REGISTER:
    case STATIC:
    case EXTERN:
    case TYPEDEF:
        return par_storage_class_specifier(lex, type);

        // Type specifiers:
    case ID: {
        // Type specifier only if its a typedef name
        tt_key_t key = {{ lex->cur.tab_entry->key.str,
                           lex->cur.tab_entry->key.len }, TT_TYPEDEF };
        if (tt_lookup(lex->typetab, &key) == NULL) {
            return CCC_BACKTRACK;
        }
    case VOID:
    case CHAR:
    case SHORT:
    case INT:
    case LONG:
    case FLOAT:
    case DOUBLE:
    case SIGNED:
    case UNSIGNED:
    case STRUCT:
    case UNION:
    case ENUM:
        return par_type_specifier(lex, type);
    }

        // Type qualitifiers
    case CONST:
    case VOLATILE:
        return par_type_qualifier(lex, type);
    default:
        return CCC_BACKTRACK;
    }
}

status_t par_storage_class_specifier(lex_wrap_t *lex, type_t **type) {
    // Allocate new type if one isn't assigned
    if (*type == NULL) {
        ALLOC_NODE(*type, type_t);
        *type->type = TYPE_MOD;
        *type->mod.base = NULL;
    }
    status_t status = CCC_OK;
    type_mod_t tmod;
    switch (lex->cur.type) {
        // Storage class specifiers
    case AUTO:     tmod = TMOD_AUTO;     break;
    case REGISTER: tmod = TMOD_REGISTER; break;
    case STATIC:   tmod = TMOD_STATIC;   break;
    case EXTERN:   tmod = TMOD_EXTERN;   break;
    case TYPEDEF:  tmod = TMOD_TYPEDEF;  break;
    default:
        return CCC_EYNTAX;
    }
    // Create a new type modifier on the front if there isn't one
    if (*type->type != TYPE_MOD) {
        type_t *new_type;
        ALLOC_NODE(new_type, type_t);
        new_type->type = TYPE_MOD;
        new_type->mod.base = *type;
        *type = new_type;
        new_type->size = new_type->mod.base.size;
        new_type->align = new_type->mod.base.align;
    }
    if (*type->mod.type_mod & tmod) {
        // TODO: Report duplicate storage class specifier
    }
    *type->mod.type |= tmod;
fail:
    // TODO: free mem on failure
    return status;
}

status_t par_type_specifier(lex_wrap_t *lex, type_t **type) {
    // Check first node for mod node
    type_t *mod_node;
    if (*type != NULL && *type->type == TYPE_MOD) {
        mod_node = *type;
    }

    // Find last node in chain
    type_t **end_node = NULL;
    if (*type != NULL) {
        bool done = false;
        for (end_node = type; !done && *end_node != NULL; ) {
            switch (*end_node->type) {
            case TYPE_ARR: end_node = &tmp->arr.base; break;
            case TYPE_PTR: end_node = &tmp->ptr.base; break;
            case TYPE_MOD: end_node = &tmp->mod.base; break;
            default:
                done = true;
            }
        }
    }

    // We found a node that is type, so we cannot have two
    if (*end_node != NULL) {
        // TODO: report duplicate type specifier
    }

    switch (lex->cur.type) {
    case ID: {
        // Type specifier only if its a typedef name
        tt_key_t key = {{ lex->cur.tab_entry->key.str,
                          lex->cur.tab_entry->key.len }, TT_TYPEDEF };
        typetab_entry_t *entry = tt_lookup(lex->typetab, &key);
        if (entry == NULL) {
            return CCC_ESYNTAX;
        }
        *end_node = entry->type;
        break;
    }
    case VOID:   *end_node = tt_void;   break;
    case CHAR:   *end_node = tt_char;   break;
    case SHORT:  *end_node = tt_short;  break;
    case INT:    *end_node = tt_int;    break;
    case LONG:   *end_node = tt_long;   break;
    case FLOAT:  *end_node = tt_float;  break;
    case DOUBLE: *end_node = tt_double; break;

        // Don't give a base type for signed/unsigned. No base type defaults to
        // int
    case SIGNED:
    case UNSIGNED:
        type_mod_t mod = lex->cur.type == SIGNED ? TMOD_SIGNED : TMOD_UNSIGNED;
        if (mod_node == NULL) {
            ALLOC_NODE(mod_node, type_t);
            mod_node->type = TYPE_MOD;
            mod_node->dealloc = true;
            mod_node->mod.base = *type;
            *type = mod_node;
            mod_node->size = mode_node->mod.base.size;
            mod_node->align = mode_node->mod.base.align;
        }

        if (mod_node->mod.type_mod & mod) {
            // TODO: Report duplicate type specifier
        }
        mod_node->mod.type_mod |= mod;

        break;
    case STRUCT:
    case UNION:
        return par_struct_or_union_specifier(lex, end_node);
    case ENUM:
        return par_enum_specifier(lex, end_node);
    default:
        return CCC_ESYNTAX;
    }
}

status_t par_struct_or_union_specifier(lex_wrap_t *lex, type_t **type) {
    assert(type != NULL);
    assert(*type == NULL);
    status_t status = CCC_OK;

    basic_type_t type;
    type_t new_type = NULL;

    switch (lex->cur.type) {
    case STRUCT:
        type = TYPE_STRUCT;
        break;
    case UNION:
        type = TYPE_UNION;
        break;
    default:
        status = CCC_ESYNTAX;
        goto fail;
    }
    LEX_ADVANCE(lex);

    len_str_t *name = NULL;
    typetab_entry_t *entry = NULL
    if (lex->cur.type == ID) {
        name = lex->cur.tab_entry->key;
        tt_key_t key = {{ key->str, key->len }, TT_COMPOUND };
        entry = tt_lookup(lex->typetab, &key);

        LEX_ADVANCE(lex);

        // Not a definition
        if (lex->cur.type != LBRACE && entry != NULL) {
            if (entry->type != type) {
                status = ESYNTAX;
                goto fail;
            }
            *type = entry->type;
            return CCC_OK;
        }
    }

    if (entry == NULL) { // Allocate a new type if it doesn't exist
        ALLOC_NODE(new_type, type_t);
        new_type->type = type;
        new_type->size = 0; // Mark as unitialized
        new_type->align = 0;
        new_type->dealloc = false;
        sl_init(&struct_decls, offsetof(struct_decl_t, link));
    }

    // Add a new named compound type into the type table
    if (name != NULL && entry == NULL) {
        if (CCC_OK !=
            (status = tt_insert(&lex->typetab, new_type, name, &entry))) {
            goto fail;
        }
    }

    if (lex->cur.type != LBRACE) {
        if (name != NULL) { // Create a new declaration in the table
            *type = new_type;
            return CCC_OK;
        } else { // Can't have a compound type without a name or definition
            status = CCC_ESYNTAX;
            goto fail;
        }
    }

    // Must match at least one struct declaration
    if (CCC_OK != (status = par_struct_declaration(lex, new_type))) {
        goto fail;
    }
    while (CCC_BACKTRACK != (status = par_struct_declaration(lex, new_type))) {
        if (status != CCC_OK) {
            goto fail;
        }
    }
    status = CCC_OK;
    LEX_MATCH(lex, RBRACE);

    *type = new_type;
    return status;

fail:
    // TODO: Handle freeing memory
    return status;
}

status_t par_struct_declaration(lex_wrap_t *lex, type_t *type) {
    status_t status = CCC_OK;
    type_t *decl_type = NULL;
    if (CCC_OK != (status = par_specifier_qualifier(lex, &type))) {
        goto fail;
    }
    while (CCC_BACKTRACK != (status = par_specifier_qualifier(lex, &type))) {
        if (status != CCC_OK) {
            goto fail;
        }
    }

    if (CCC_OK != par_struct_declarator_list(lex, type, decl_type)) {
        goto fail;
    }
    LEX_MATCH(lex, SEMI);
fail:
    return status;
}

status_t par_specifier_qualifier(lex_wrap_t *lex, type_t **type) {
    switch (lex->cur.type) {
        // Type specifiers:
    case ID: {
        // Type specifier only if its a typedef name
        tt_key_t key = {{ lex->cur.tab_entry->key.str,
                          lex->cur.tab_entry->key.len }, TT_TYPEDEF };
        if (tt_lookup(lex->typetab, &key) == NULL) {
            return CCC_BACKTRACK;
        }
    }
    case VOID:
    case CHAR:
    case SHORT:
    case INT:
    case LONG:
    case FLOAT:
    case DOUBLE:
    case SIGNED:
    case UNSIGNED:
    case STRUCT:
    case UNION:
    case ENUM:
        return par_type_specifier(lex, type);

        // Type qualitifiers
    case CONST:
    case VOLATILE:
        return par_type_qualifier(lex, type);
    default:
        return CCC_BACKTRACK;
    }

    return CCC_OK;
}

status_t par_struct_declarator_list(lex_wrap_t *lex, type_t *base,
                                    type_t *decl_type) {
    status_t status = CCC_OK;
    if (CCC_OK != (status = par_struct_declarator(lex, base, decl_type))) {
        goto fail;
    }
    while (lex->cur.type == COMMA) {
        if (CCC_OK != (status = par_struct_declarator(lex, base_decl_type))) {
            goto fail;
        }
    }

fail:
    return status;
}

status_t par_struct_declarator(lex_wrap_t *lex, type_t *base,
                               type_t *decl_type) {
    status_t status = CCC_OK;
    struct_decl_t *node;
    ALLOC_NODE(node, struct_decl_t);
    node->bf_bits = NULL;
    ALLOC_NODE(node->decl, stmt_t);
    node->decl.type = STMT_DECL;
    node->decl.decl.type = decl_type;
    sl_init(&node->decl.decl.decls, offsetof(decl_node, link);

    decl_node_t *decl_node = NULL;

    if (CCC_OK != (status = par_declarator(lex, decl_type, &decl_node))) {
        goto fail;
    }
    sl_append(&node->decl.decl.decls, &decl_node->link);

    if (lex->cur.type == COLON) {
        LEX_ADVANCE(lex);
        if (CCC_OK != (status = par_expression(lex, NULL, &node->bf_bits))) {
            goto fail;
        }
    }

    // Update base type
    sl_append(&base->struct_decls, node->link);
    base->align = MAX(base->align, node->decl.type.align);

    // Update size and alignment.
    // TODO: Handle bitfields
    if (base->type == TYPE_STRUCT) {
        base->size += node->decl.type.size;
    } else { // base->type == TYPE_UNION
        base->size = MAX(base->size, node->decl.type.size);
    }

fail:
    return status;
}

// TODO: Handle abstract-declarator
status_t par_declarator(lex_wrap_t *lex, decl_node_t **decl_node,
                        type_t *base) {
    status_t status = CCC_OK;
    switch (lex->cur.type) {
    case STAR: {
        if (*decl_node == NULL) {
            ALLOC_NODE(*decl_node, decl_node_t);
            decl_node->type = base;
            decl_node->id = NULL;
            decl_node->expr = NULL;
        }

        while (lex->cur.type == STAR) {
            if (CCC_OK != (status = par_pointer(lex, &decl_node->type))) {
                goto fail;
            }
        }
    }
    case ID:
    case LPAREN:

        if (*decl_node == NULL) {
            ALLOC_NODE(*decl_node, decl_node_t);
            decl_node->type = base;
            decl_node->id = NULL;
            decl_node->expr = NULL;
        }

        if (CCC_OK != (status = par_direct_declarator(lex, *decl_node, base))) {
            goto fail;
        }
        break;
    default:
        return CCC_BACKTRACK;
    }

fail:
    return status;
}

status_t par_pointer(lex_wrap_t *lex, type_t **mod) {
    status_t status = CCC_OK;
    LEX_MATCH(lex, STAR);

    type_t *new_type = NULL;
    while (CCC_BACKTRACK != (status = par_type_qualifier(lex, &new_type))) {
        if (status != CCC_OK) {
            goto fail;
        }
    }
    status = CCC_OK;
    type_mod_t mods = new_type->mod.typemod;
    new_type->type = TYPE_PTR;
    new_type->size = PTR_SIZE;
    new_type->align = PTR_ALIGN;
    new_type->dealloc = true;
    new_type->ptr.base = *mod;
    new_type->ptr.mod = mods;

fail:
    return status;
}

status_t par_type_qualifier(lex_wrap_t *lex, type_t **type) {
    type_mod_t mod;
    switch (lex->cur.type) {
    case CONST:    mod = TMOD_CONST;    break;
    case VOLATILE: mod = TMOD_VOLATILE; break;
    default:
        return CCC_BACKTRACK;
    }

    // Check first node for mod node
    type_t *mod_node;
    if (*type != NULL && *type->type == TYPE_MOD) {
        mod_node = *type;
    } else {
        ALLOC_NODE(mod_node, type_t);
        mod_node->type = TYPE_MOD;
        mod_node->dealloc = true;
        mod_node->mod.base = *type;
        *type = mod_node;
        if (mod_node->mod.base != NULL) {
            mod_node->size = mode_node->mod.base.size;
            mod_node->align = mode_node->mod.base.align;
        } else {
            mod_node->size = 0;
            mod_node->align = 0;
        }
    }
    mod_node->mod.type_mod |= mod;
fail:
    return CCC_OK;
}

status_t par_direct_declarator(lex_wrap_t *lex, decl_node_t *node,
                               type_t *base) {
    status_t status = CCC_OK;

    if (lex->cur.type == LPAREN) {
        LEX_ADVANCE(lex);
        par_declarator(lex, base, &node);
        LEX_MATCH(lex, RPAREN);
    } else if (lex->cur.type == ID) {
        node->id = lex->cur.tab_entry.key;
        LEX_ADVANCE(lex);
    }

    bool done = false;
    while (!done) {
        switch (lex->cur.type) {
        case LBRACK:
            LEX_ADVANCE(lex);
            type_t *arr_type;
            ALLOC_NODE(arr_type, type_t);
            arr_type->type = TYPE_ARR;
            arr_type->align = node->type.align;
            arr_type->size = 0;
            arr_type->dealloc = true;
            arr_type->arr.base = node->type;
            arr_type->arr.len = NULL;
            if (lex->cur.type == RBRACK) {
                LEX_ADVANCE(lex);
            } else {
                if (CCC_OK !=
                    (status = par_expression(lex, NULL, &arr_type->arr.len))) {
                    goto fail;
                }
                LEX_MATCH(lex, RBRACK);
            }
            node->type = arr_type;
            break;
        case LPAREN:
            LEX_ADVANCE(lex);
            type_t *func_type;
            ALLOC_NODE(func_type, type_t);
            func_type->type = TYPE_FUNC;
            func_type->align = PTR_ALIGN;
            func_type->size = PTR_SIZE;
            func_type->dealloc = true;
            func_type->func.type = node->type;
            func_type->func.params = NULL;

            /* TODO: Support old decl syntax
            if (lex->cur.type == ID) {
                while (lex->cur.type == ID) {
                    LEX_ADVANCE(lex);
                }
                LEX_MATCH(lex, RPAREN);
            } else {
            */
            if (CCC_OK != (status = par_parameter_type_list(
                               lex, &func_type->func.params))) {
                goto fail;
            }
            //}
            node->type = func_type;
            break;
        default:
            done = true;
        }
    }

fail:
    return status;
}

status_t par_non_binary_expression(lex_wrap_t *lex, bool *is_unary,
                                   expr_t **result) {
    status_t status = CCC_OK;

    bool primary = false;
    bool unary = false;

    switch (lex->cur.type) {
        // Unary expressions
    case INC:
    case DEC:
    case SIZEOF:

        // Unary operators
    case BITAND:
    case STAR:
    case PLUS:
    case MINUS:
    case BITNOT:
    case LOGICNOT:
        if (CCC_OK != (status = par_unary_expression(lex, result))) {
            goto fail;
        }
        unary = true;
        break;

        // Primary expressions
    case ID:
    case STRING:
    case INTLIT:
    case FLOATLIT:
        if (CCC_OK != (status = par_primary_expression(lex, result))) {
            goto fail;
        }
        unary = true;
        primary = true;
        break;

        // Casts and parens around expressions
    case LPAREN:
        LEX_ADVANCE(lex);
        switch (lex->cur.type) {
            // Cases for casts

        case ID: {
            // Type specifier only if its a typedef name
            tt_key_t key = {{ lex->cur.tab_entry->key.str,
                              lex->cur.tab_entry->key.len }, TT_TYPEDEF };
            if (tt_lookup(lex->typetab, &key) == NULL) {
                break;
            }
        }
        case VOID:
        case CHAR:
        case SHORT:
        case INT:
        case LONG:
        case FLOAT:
        case DOUBLE:
        case SIGNED:
        case UNSIGNED:
        case STRUCT:
        case UNION:
        case ENUM:

            // Type qualitifiers
        case CONST:
        case VOLATILE:
            if (CCC_OK != (status = par_cast_expression(lex, true, result))) {
                goto fail;
            }
            break;

            // Parens
        default:
            if (CCC_OK != (status = par_expression(lex, NULL, result))) {
                goto fail;
            }
            primary = true;
            unary = true;
            LEX_MATCH(lex, RPAREN);
            break;
        }
    default:
        return CCC_ESYNTAX;
    }

    if (primary) {
        // If we found a primary expression, need to search for postfix
        switch (lex->cur.type) {
        case DEREF:
        case INC:
        case DEC:
        case DOT:
        case LBRACK:
        case LPAREN:
            expr_t *base = *result;
            if (CCC_OK != (status = par_postfix_expression(lex, base,
                                                           &result))) {
                goto fail;
            }
            break;
        default:
            break;
        }
    }

    *is_unary = unary;
fail:
    return status;
}

status_t par_expression(lex_wrap_t *lex, expr_t *left, expr_t **result) {
    status_t status = CCC_OK;
    bool unary1;

    if (left == NULL) { // Only search for first operand if not provided
        if (CCC_OK !=
            (status = par_non_binary_expression(lex, &unary1, &left))) {
            goto fail;
        }

        if (unary1) {
            // Search for assignment operators
            switch (lex->cur.type) {
            case EQ:
            case STAREQ:
            case DIVEQ:
            case MODEQ:
            case PLUSEQ:
            case MINUSEQ:
            case LSHIFTEQ:
            case RSHIFTEQ:
            case BITANDEQ:
            case BITXOREQ:
            case BITOREQ:
                if (CCC_OK !=
                    (status = par_assignment_expression(lex, left, result))) {
                    goto fail;
                }

                return CCC_OK;
            default:
                break;
            }
        }
    }

    bool new_left = true;
    while (new_left) {
        new_left = false;
        token_t op1;

        switch (lex->cur.type) {
            // Binary operators
        case STAR:
        case DIV:
        case MOD:
        case PLUS:
        case MINUS:
        case LSHIFT:
        case RSHIFT:
        case LT:
        case GT:
        case LE:
        case GE:
        case EQ:
        case NE:
        case BITAND:
        case BITXOR:
        case BITOR:
        case LOGICAND:
        case LOGICOR:
            op1 = lex->cur.type;
            LEX_ADVANCE(lex);
            break;

        case COND: { // Conditional operator
            expr_t *new_left;
            ALLOC_NODE(new_left, expr_t);
            new_left->type = EXPR_COND;
            new_left->cond.expr1 = left;

            LEX_ADVANCE(lex);
            if (CCC_OK != par_expression(lex, NULL, &new_left->cond.expr2)) {
                goto fail;
            }
            LEX_MATCH(lex, COLON);
            if (CCC_OK != par_expression(lex, NULL, &new_left->cond.expr3)) {
                goto fail;
            }
            left = new_left;
            new_left = true;
            break;
        }
        default:
            *result = left;
            return CCC_OK;
        }

        if (new_left) {
            continue;
        }

        expr_t *right;
        bool unary2;
        if (CCC_OK !=
            (status = par_non_binary_expression(lex, &unary2, &right))) {
            goto fail;
        }

        token_t op2;
        switch (lex->cur.type) {
            // Binary operators
        case STAR:
        case DIV:
        case MOD:
        case PLUS:
        case MINUS:
        case LSHIFT:
        case RSHIFT:
        case LT:
        case GT:
        case LE:
        case GE:
        case EQ:
        case NE:
        case BITAND:
        case BITXOR:
        case BITOR:
        case LOGICAND:
        case LOGICOR:
            op2 = lex->cur.type;
            break;

        case COND: { // Cond has lowest precedence
            LEX_ADVANCE(lex);
            expr_t *new_node;
            ALLOC_NODE(new_node, expr_t);
            new_node->type = EXPR_BIN;
            new_node->bin.expr1 = left;
            new_node->bin.expr2 = right;
            expr_t *cond_node;
            ALLOC_NODE(cond_node, expr_t);
            cond_node->type = EXPR_COND;
            cond_node->cond.expr1 = new_node;
            if (CCC_OK != par_expression(lex, NULL, &cond_node->cond.expr2)) {
                goto fail;
            }
            LEX_MATCH(lex, COLON);
            if (CCC_OK != par_expression(lex, NULL, &cond_node->cond.expr3)) {
                goto fail;
            }
            *result = new_node;
            return CCC_OK;
        }
        default: { // Not binary 2
            expr_t *new_node;
            ALLOC_NODE(new_node, expr_t);
            new_node->type = EXPR_BIN;
            new_node->bin.expr1 = left;
            new_node->bin.expr2 = right;
            *result = new_node;
            return CCC_OK;
        }
        }

        if (par_greater_or_equal_prec(op1, op2)) {
            expr_t *new_node;
            ALLOC_NODE(new_node, expr_t);
            new_node->type = EXPR_BIN;
            new_node->bin.expr1 = left;
            new_node->bin.expr2 = right;
            new_left = true;
            left = new_node;
        } else {
            expr_t *new_node;
            ALLOC_NODE(new_node, expr_t);
            new_node->type = EXPR_BIN;
            new_node->bin.expr1 = left;

            if (CCC_OK !=
                (status = par_expression(lex, right, &new_node->bin.right))) {
                goto fail;
            }

            *result = new_node;
            return CCC_OK;
        }
    }

fail:
    return status;
}

status_t par_unary_expression(lex_wrap_t *lex, expr_t **result) {
    status_t status = CCC_OK;
    expr_t *base;

    switch (lex->cur.type) {
        // Primary expressions
    case ID:
    case STRING:
    case INTLIT:
    case FLOATLIT:
        if (CCC_OK != (status = par_primary_expression(lex, &base))) {
            goto fail;
        }
        if (CCC_OK != (status = par_postfix_expression(lex, base, result))) {
            goto fail;
        }
        break;

    case INC:
    case DEC: {
        ALLOC_NODE(base, expr_t);
        oper_t op = lex->cur.type == INC ? OP_PREINC : OP_PREDEC;
        LEX_ADVANCE(lex);
        base->type = EXPR_UNARY;
        base->unary.op = op;
        if (CCC_OK != (status = par_unary_expression(lex, &base->expr))) {
            goto fail;
        }
        *result = base;
        return CCC_OK;
    }

    case SIZEOF:
        ALLOC_NODE(base, expr_t);
        LEX_ADVANCE(lex);
        base->type = EXPR_SIZEOF;
        if (CCC_BACKTRACK != (status = par_unary_expression(lex, result))) {
            if (status != CCC_OK) {
                goto fail;
            }
            base->sizeof_params.type = NULL;
            base->sizeof_params.expr = *result;
            *result = base;
            return CCC_OK;
        }

        base->sizeof_params.expr = NULL;
        if (CCC_OK !=
            (status = par_type_name(lex, result, &base->sizeof_params.type))) {
            goto fail;
        }

        *result = base;
        return CCC_OK;

    case BITAND:
    case STAR:
    case PLUS:
    case MINUS:
    case BITNOT:
    case LOGICNOT: {
        ALLOC_NODE(base, expr_t);
        oper_t op;
        switch (lex->cur.type) {
        case BITAND:   op = OP_ADDR;     break;
        case STAR:     op = OP_DEREF;    break;
        case PLUS:     op = OP_UPLUS;    break;
        case MINUS:    op = OP_UMINUS;   break;
        case BITNOT:   op = OP_BITNOT;   break;
        case LOGICNOT: op = OP_LOGICNOT; break;
        default:
            assert(false);
        }
        base->type = EXPR_UNARY;
        base->unary.op = op;
        if (CCC_OK !=
            (status = par_cast_expression(lex, false, &base->unary.expr))) {
            goto fail;
        }

        *result = base;
        return CCC_OK;
    }
    default:
        return CCC_BACKTRACK;
    }

fail:
    free(base);
    return status;
}

status_t par_cast_expression(lex_wrap_t *lex, bool skip_paren,
                             expr_t **result) {
    status_t status = CCC_OK;
    if (!skip_paren && lex->cur.type != LPAREN) {
        return par_unary_expression(lex, result);
    }
    if (!skip_paren) {
        LEX_ADVANCE(lex);
    }
    decl_t *type = NULL;
    if (CCC_OK != (status = par_type_name(lex, &type))) {
        goto fail;
    }
    LEX_MATCH(lex, RPAREN);

    expr_t *expr;
    ALLOC_NODE(expr, expr_t);
    expr->type = TYPE_CAST;
    expr->cast.type = type;
    expr->cast.base = NULL;
    if (CCC_OK !=
        (status = par_cast_expression(lex, false, &expr->cast.base))) {
        goto fail;
    }
    *result = expr;
fail:
    return status;
}

/**
 * Parses postfix expression after the primary expression part
 */
status_t par_postfix_expression(lex_wrap_t *lex, expr_t *base,
                                expr_t **result) {
    status_t status = CCC_OK;
    expr_t *expr = NULL;

    while (base != NULL) {
        switch (lex->cur.type) {
        case LBRACK:
            LEX_ADVANCE(lex);
            ALLOC_NODE(expr, expr_t);
            expr->type = EXPR_BINOP;
            expr->binop.op = ARR_ACC;
            expr->binop.expr1 = base;

            if (CCC_OK !=
                (status = par_expression(lex, NULL, expr->binop.expr2))) {
                goto fail;
            }
            LEX_MATCH(lex, RBRACK);
            base = expr;
            break;

        case LPAREN:
            LEX_ADVANCE(lex);
            ALLOC_NODE(expr, expr_t);
            expr->type = EXPR_CALL;
            expr->call.func = base;
            sl_init(&expr->call.params, offsetof(expr_t, link));

            while (lex->cur.type != RPAREN) {
                expr_t *param;
                if (CCC_OK != (status = par_expression(lex, NULL, &param))) {
                    goto fail;
                }
                sl_append(&expr->call.params, param);
                LEX_MATCH(lex, COMMA);
            }
            LEX_ADVANCE(lex);
            base = expr;
            break;

        case DOT:
        case DEREF: {
            oper_t op = lex->cur.type == DOT ? OP_DOT : OP_ARROW;
            LEX_ADVANCE(lex);
            if (lex->cur.type != ID) {
                goto fail;
            }
            ALLOC_NODE(expr, expr_t);
            expr->type = EXPR_MEM_ACC;
            expr->mem_acc.op = op;
            expr->mem_acc.name = &lex->cur.tab_entry.key;
            LEX_ADVANCE(lex);
            base = expr;
            break;
        }

        case INC:
        case DEC:
            oper_t op = lex->cur.type == INC ? OP_POSTINC : OP_POSTDEC;
            LEX_ADVANCE(lex);
            ALLOC_NODE(expr, expr_t);
            expr->type = EXPR_UNARY;
            expr->unary.op = op;
            expr->unary.expr = base;

            base = expr;
            break;
        default: // Found a terminating character
            base = NULL;
        }
    }

    *result = expr;
fail:
    return status;
}

/**
 * Parses assignment after the assignment operator
 */
status_t par_assignment_expression(lex_wrap_t *lex, expr_t *left,
                                   expr_t **result) {
    status_t status = CCC_OK;
    oper_t op;
    switch (lex->cur.type) {
    case EQ:       op = OP_NOP;    break;
    case STAREQ:   op = OP_TIMES;  break;
    case DIVEQ:    op = OP_DIV;    break;
    case MODEQ:    op = OP_MOD;    break;
    case PLUSEQ:   op = OP_PLUS;   break;
    case MINUSEQ:  op = OP_MINUS;  break;
    case LSHIFTEQ: op = OP_LSHIFT; break;
    case RSHIFTEQ: op = OP_RSHIFT; break;
    case BITANDEQ: op = OP_BITAND; break;
    case BITXOREQ: op = OP_BITXOR; break;
    case BITOREQ:  op = OP_BITOR;  break;
    default:
        return CCC_ESYNTAX;
    }
    LEX_ADVANCE(lex);
    expr_t *expr;
    ALLOC_NODE(expr, expr_t);
    expr->type = EXPR_ASSIGN;
    expr->assign.dest = left;
    expr->assign.op = op;

    if (CCC_OK != (status = par_expression(lex, NULL, &expr->assign.expr))) {
        goto fail;
    }

    *result = expr;
    return status;

fail:
    free(expr);
    return status;
}

// Excludes parens because they need to be diferrentiated from casts
status_t par_primary_expression(lex_wrap_t *lex, expr_t **result) {
    expr_t *base;
    ALLOC_NODE(base, expr_t);

    switch (lex->cur.type) {
    case ID: {
        base->type = EXPR_VAR;
        base->var_id = &lex->cur.tab_entry.key;
        LEX_ADVANCE(lex);
        break;
    }
    case STRING: {
        // TODO: Think about how pointers should be represented.
        // Probably a pointer to the appropriate declaration
        // So strings need a fake declaration
        LEX_ADVANCE(lex);
        break;
    }
    case INTLIT: {
        base->type = EXPR_CONST_INT;
        base->const_val.int_val = lex->cur.int_params.int_val;
        type_t *type;
        if (lex->cur.int_params.hasLL) {
            type = tt_long; // TODO: Add long long support
        } else if (lex->cur.int_params.hasL) {
            type = tt_long;
        } else {
            type = tt_int;
        }

        if (lex->cur.int_params.hasU) {
            type_t *type_mod;
            alloc_node(type_mod, type_t);
            type_mod->type = TYPE_MOD;
            type_mod->size = type->size;
            type_mod->align = type->align;
            type_mod->dealloc = true;
            type_mod->mod.type_mod = TMOD_UNSIGNED;
            type_mod->mod.base = type;
            type = type_mod;
        }
        base->const_val.type = type;
        LEX_ADVANCE(lex);
        break;
    }
    case FLOATLIT: {
        base->type = EXPR_CONST_FLOAT;
        base->const_val.float_val = lex->cur.float_params.float_val;
        base->const_val.type =
            lex->cur.float_params.hasF ? tt_float : tt_double;
        LEX_ADVANCE(lex);
        break;
    }
    default:
        return CCC_ESYNTAX;
    }

    *result = base;
fail:
    return status;
}

status_t par_type_name(lex_wrap_t *lex, stmt_t **result) {
    status_t status = CCC_OK;
    type_t base = NULL;
    if (CCC_OK != (status = par_specifier_qualifier(lex, &base))) {
        goto fail;
    }
    while (CCC_BACKTRACK != (status = par_specifier_qualifier(lex, &base))) {
        if (status != CCC_OK) {
            goto fail;
        }
    }
    stmt_t *decl;
    ALLOC_NODE(gdecl->decl, stmt_t);
    gdecl->decl.type = STMT_DECL;
    gdecl->decl.decl.type = base;
    sl_init(&gdecl->decl.decl.decls, offsetof(decl_node, link));

    decl_node_t *decl_node = NULL;
    if (CCC_BACKTRACK != (status = par_declarator(lex, base, &decl_node))) {
        if (status != CCC_OK) {
            goto fail;
        }
    }
    status = CCC_OK;
    sl_append(&gdecl->decl.decl.decls, &decl_node->link);

    *result = decl;
fail:
    return status;
}

status_t par_parameter_type_list(lex_wrap_t *lex) {
    status_t status = CCC_OK;
    par_parameter_list(lex);
    if (lex->cur.type != ELIPSE) {
        return CCC_OK;
    }
    LEX_ADVANCE(lex);

fail:
    return status;
}

status_t par_parameter_list(lex_wrap_t *lex) {
    status_t status = CCC_OK;
    if (CCC_OK != (status = par_parameter_declaration(lex))) {
        goto fail;
    }
    while (CCC_BACKTRACK != (status = par_parameter_declaration(lex))) {
        if (status != CCC_OK) {
            goto fail;
        }
    }
    status = CCC_OK;
fail:
    return status;
}

status_t par_parameter_declaration(lex_wrap_t *lex) {
    status_t status = CCC_OK;
    if (CCC_OK != (status = par_declaration_specifier(lex))) {
        goto fail;
    }
    while (CCC_BACKTRACK != (status = par_declaration_specifier(lex))) {
        if (status != CCC_OK) {
            goto fail;
        }
    }
    status = CCC_OK;

    if (CCC_BACKTRACK != (status = par_declarator(lex))) {
        if (status != CCC_OK) {
            goto fail;
        }
    }
fail:
    return status;
}

status_t par_enum_specifier(lex_wrap_t *lex) {
    status_t status = CCC_OK;
    LEX_MATCH(lex, ENUM);
    if (lex->cur.type == ID) {
        LEX_ADVANCE(lex);
    }

    if (lex->cur.type == LBRACE) {
        LEX_ADVANCE(lex);
        if (CCC_OK != (status = par_enumerator_list(lex))) {
            goto fail;
        }
        LEX_MATCH(lex, RBRACE);
    }

fail:
    return status;
}

status_t par_enumerator_list(lex_wrap_t *lex) {
    status_t status = CCC_OK;
    if (CCC_OK != (status = par_enumerator(lex))) {
        goto fail;
    }
    while (lex->cur.type == COMMA) {
        if (CCC_OK != (status = par_enumerator(lex))) {
            goto fail;
        }
    }

fail:
    return status;
}

status_t par_enumerator(lex_wrap_t *lex) {
    status_t status = CCC_OK;
    LEX_MATCH(lex, ID);
    if (lex->cur.type == EQ) {
        LEX_ADVANCE(lex);
        LEX_MATCH(lex, INTLIT);
    }
fail:
    return status;
}

status_t par_declaration(lex_wrap_t *lex) {
    status_t status = CCC_OK;

    // Must match at least one declaration specifier
    if (CCC_OK != (status = par_declaration_specifier(lex))) {
        goto fail;
    }
    // Match declaration specifers until they don't match anymore
    while (CCC_BACKTRACK != (status = par_declaration_specifier(lex))) {
        if (status != CCC_OK) {
            goto fail;
        }
    }
    while (CCC_BACKTRACK != (status = par_init_declarator(lex))) {
        if (status != CCC_OK) {
            goto fail;
        }
    }
    status = CCC_OK;

fail:
    return status;
}

status_t par_init_declarator(lex_wrap_t *lex) {
    status_t status = CCC_OK;
    if (CCC_OK != (status = par_declarator(lex))) {
        goto fail;
    }

    if (lex->cur.type == ASSIGN) {
        LEX_ADVANCE(lex);
        if (CCC_OK != (status = par_initializer(lex))) {
            goto fail;
        }
    }

fail:
    return status;
}

status_t par_initializer(lex_wrap_t *lex) {
    status_t status = CCC_OK;
    if (lex->cur.type != LBRACK) {
        return par_assignment_expression(lex);
    }
    LEX_ADVANCE(lex);
    if (CCC_OK != (status = par_initializer_list(lex))) {
        goto fail;
    }
    LEX_MATCH(lex, RBRACK);
fail:
    return status;
}

status_t par_initializer_list(lex_wrap_t *lex) {
    status_t status = CCC_OK;
    if (CCC_OK != (status = par_initializer(lex))) {
        goto fail;
    }
    while (lex->cur.type == COMMA) {
        LEX_ADVANCE(lex);
        if (lex->cur.type == RBRACK) {
            break;
        }
        if (CCC_OK != (status = par_initializer_list(lex))) {
            goto fail;
        }
    }
fail:
    return status;
}


status_t par_compound_statement(lex_wrap_t *lex) {
    status_t status = CCC_OK;
    LEX_MATCH(lex, LBRACK);
    while (lex->cur.type != RBRACK) {
        if (CCC_OK != (status =par_statement(lex))) {
            goto fail;
        }
    }
    LEX_ADVANCE(lex);
fail:
    return status;
}

status_t par_statement(lex_wrap_t *lex) {
    switch (lex->cur.type) {
        // Cases for declaration specifier
        // Storage class specifiers
    case AUTO:
    case REGISTER:
    case STATIC:
    case EXTERN:
    case TYPEDEF:

        // Type specifiers:
    case VOID:
    case CHAR:
    case SHORT:
    case INT:
    case LONG:
    case FLOAT:
    case DOUBLE:
    case SIGNED:
    case UNSIGNED:
    case STRUCT:
    case UNION:
    case ENUM:

        // Type qualitifiers
    case CONST:
    case VOLATILE:
        return par_declaration(lex);

    case ID: {
        // Type specifier only if its a typedef name
        tt_key_t key = {{ lex->cur.tab_entry->key.str,
                           lex->cur.tab_entry->key.len }, TT_TYPEDEF };
        if (tt_lookup(lex->typetab, &key) != NULL) {
            return par_declaration(lex);
        }
    }
    case CASE:
    case DEFAULT:
        return par_labeled_statement(lex);

    case IF:
    case SWITCH:
        return par_selection_statement(lex);

    case DO:
    case WHILE:
    case FOR:
        return par_iteration_statement(lex);

    case GOTO:
    case CONTINUE:
    case BREAK:
    case RETURN:
        return par_iteration_statement(lex);

    case SEMI: // noop
        return CCC_OK;
    default:
        return par_expression_statement(lex);
    }
}

status_t par_labeled_statement(lex_wrap_t *lex) {
    status_t status = CCC_OK;
    switch (lex->cur.type) {
    case ID:
        LEX_ADVANCE(lex);
        LEX_MATCH(lex, COLON);
        if (CCC_OK != (status = par_statement(lex))) {
            goto fail;
        }
        break;
    case CASE:
        LEX_ADVANCE(lex);
        if (CCC_OK != (status = par_expression(lex, false))) {
            goto fail;
        }
        LEX_MATCH(lex, COLON);
        if (CCC_OK != (status = par_statement(lex))) {
            goto fail;
        }
        break;
    case DEFAULT:
        LEX_ADVANCE(lex);
        if (CCC_OK != (status = par_statement(lex))) {
            goto fail;
        }
        break;
    default:
        return CCC_ESYNTAX;
    }

fail:
    return status;
}

status_t par_expression_statement(lex_wrap_t *lex) {
    status_t status = CCC_OK;
    if (lex->cur.type != SEMI) {
        if (CCC_OK != (status = par_expression(lex, false))) {
            goto fail;
        }
    }

    LEX_MATCH(lex, SEMI);
fail:
    return status;
}

status_t par_selection_statement(lex_wrap_t *lex) {
    status_t status = CCC_OK;
    switch (lex->cur.type) {
    case IF:
        LEX_ADVANCE(lex);
        LEX_MATCH(lex, LPAREN);
        if (CCC_OK != (status = par_expression(lex, false))) {
            goto fail;
        }
        LEX_MATCH(lex, RPAREN);
        if (CCC_OK != (status = par_statement(lex))) {
            goto fail;
        }
        if (lex->cur.type == ELSE) {
            LEX_ADVANCE(lex);
            if (CCC_OK != (status = par_statement(lex))) {
                goto fail;
            }
        }
        break;
    case SWITCH:
        LEX_ADVANCE(lex);
        LEX_MATCH(lex, LPAREN);
        if (CCC_OK != (status = par_expression(lex, false))) {
            goto fail;
        }
        LEX_MATCH(lex, RPAREN);
        if (CCC_OK != (status = par_statement(lex))) {
            goto fail;
        }
        break;
    default:
        return CCC_ESYNTAX;
    }
fail:
    return status;
}

status_t par_iteration_statement(lex_wrap_t *lex) {
    status_t status = CCC_OK;
    switch (lex->cur.type) {
    case DO:
        LEX_ADVANCE(lex);
        if (CCC_OK != (status = par_statement(lex))) {
            goto fail;
        }
        LEX_MATCH(lex, WHILE);
        LEX_MATCH(lex, LPAREN);
        if (CCC_OK != (status = par_expression(lex, false))) {
            goto fail;
        }
        LEX_MATCH(lex, RPAREN);
        LEX_MATCH(lex, SEMI);
        break;
    case WHILE:
        LEX_ADVANCE(lex);
        LEX_MATCH(lex, LPAREN);
        if (CCC_OK != (status = par_expression(lex, false))) {
            goto fail;
        }
        LEX_MATCH(lex, RPAREN);
        if (CCC_OK != (status = par_statement(lex))) {
            goto fail;
        }
        break;
    case FOR:
        LEX_ADVANCE(lex);
        LEX_MATCH(lex, LPAREN);
        if (lex->cur.type != SEMI) {
            if (CCC_OK != (status = par_expression(lex, false))) {
                goto fail;
            }
        }
        LEX_MATCH(lex, SEMI);
        if (lex->cur.type != SEMI) {
            if (CCC_OK != (status = par_expression(lex, false))) {
                goto fail;
            }
        }
        LEX_MATCH(lex, SEMI);
        if (lex->cur.type != SEMI) {
            if (CCC_OK != (status = par_expression(lex, false))) {
                goto fail;
            }
        }
        LEX_MATCH(lex, SEMI);
        LEX_MATCH(lex, RPAREN);
        if (CCC_OK != (status = par_statement(lex))) {
            goto fail;
        }
        break;
    default:
        return CCC_ESYNTAX;
    }
fail:
    return status;
}

status_t par_jump_statement(lex_wrap_t *lex) {
    status_t status = CCC_OK;
    switch (lex->cur.type) {
    case GOTO:
        LEX_ADVANCE(lex);
        LEX_MATCH(lex, ID);
        LEX_MATCH(lex, SEMI);
        break;
    case CONTINUE:
    case BREAK:
        LEX_ADVANCE(lex);
        LEX_MATCH(lex, SEMI);
        break;
    case RETURN:
        LEX_ADVANCE(lex);
        if (CCC_OK != (status = par_expression(lex, false))) {
            goto fail;
        }
        LEX_MATCH(lex, SEMI);
        break;
    default:
        return CCC_ESYNTAX;
    }
fail:
    return status;
}