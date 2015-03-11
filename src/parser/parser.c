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

/*
status_t parser_parse(lexer_t *lexer, trans_unit_t **result) {
    assert(lexer != NULL);
    assert(result != NULL);
    status_t status = CCC_OK;

    lex_wrap_t lex;
    lex.lexer = lexer;
    LEX_ADVANCE(&lex);

    status = par_trans_unit(&lex, result);
fail:
    return status;
}

status_t par_trans_unit(lex_wrap_t *lex, trans_unit_t **result) {
    status_t status = CCC_OK;
    trans_unit_t *tunit = NULL;

    // Setup new translation unit
    ALLOC_NODE(tunit, trans_unit_t);
    tunit->path = lex->cur.mark.filename;
    if (!CCC_OK ==
        (status = sl_init(&tunit->gdecls, offsetof(gdecl_t, link)))) {
        goto fail;
    }

    while (true) {
        // We're done when we reach EOF
        if (lex->cur.type == TOKEN_EOF) {
            *result = tunit;
            return status;
        }

        gdecl_t *gdecl;
        if (CCC_OK != (status = par_gdecl(lex, &gdecl))) {
            goto fail;
        }
        sl_append(&tunit->gdecls, &gdecl->link);
    }

fail: // General failure
    trans_unit_destroy(tunit);
fail0: // Failed allocation
    free(tunit);
    return status;
}

void trans_unit_destroy(trans_unit *tunit) {
    sl_link_t *cur;
    SL_FOR_EACH(cur, &tunit->gdecls) {
        gdecl_t *gdecl = GET_ELEM(&tunit->gdecls, cur);
        gdecl_destroy(gdecl);
    }
    sl_destroy(&tunit->gdecls, NOFREE);
}

status_t par_gdecl(lex_wrap_t *lex, gdecl_t **result) {
    status_t status = CCC_OK;
    gdecl_t *gdecl = NULL;

    // Setup new gdecl
    ALLOC_NODE(gdecl, gdecl_t);
    gdecl->type = GDECL_NULL; // Unknown type of gdecl

    if (lex->cur.type == TYPEDEF) {
        gdecl->type = GDECL_TYPEDEF;
        LEX_ADVANCE(lex);
        type_t *type;
        if (CCC_OK != (status = par_type(lex, &gdecl->typedef_params.type))) {
            goto fail;
        }

        if (lex->cur.type != ID) {
            // TODO: Report error here
            assert(false);
            status = CCC_ESYNTAX;
            goto fail;
        }
        gdecl->typdef_params.name = &lex->cur.tab_entry.key;
        LEX_ADVANCE(lex);
        LEX_MATCH(lex, SEMI);

        return status;
    }

    type_t *type;
    if (CCC_OK != (status = par_type(lex, &type))) {
        goto fail;
    }

    if (type->type_mod & TMOD_TYPEDEF) {
    }

    if (lex->cur.type == SEMI) {
        gdecl->type = TYPE;
        gdecl->type.type = type;
        return status;
    }

    // TODO: This isn't correct because parens can wrap around identifiers
    if (lex->cur.type != ID) {
        // TODO: report
        assert(false);
    }
    len_str_t *id = &cur->type.tab_entry.key;
    LEX_ADVANCE(lex);
    if (lex->cur.type == EQ) { // match assignments
        // TODO: should match a statement
        return status;
    }

    // Match function decls and defns
    if (lex->cur.type != LPAREN) {
        // TODO: report
        assert(false);
    }

    slist_t list;
    LEX_ADVANCE(lex);
    while (lex->cur.type != RPAREN) {
        param_t *param;
        ALLOC_NODE(param, param_t);
        slist_append(&list, &param->link);
        if (CCC_OK != (status = par_type(lex, &param->type))) {
            // TODO: Destroy list here on failure
            goto fail;
        }
        switch (lex->cur.type) {
        case COMMA:  // No param name provided
            param->id = NULL;
            break;
        case ID:
            param->id = &lex->cur.tab_entry.key;
            break;
        default:
            // TODO: Handle error
            assert(false);
        }
    }
    LEX_ADVANCE(lex); // Advance past RPAREN

    if (lex->cur.type == SEMI) {
        gdecl->type = GDECL_FDECL;
        gdecl->fdecl.ret = type;
        gdecl->fdecl.id = id;
        memcpy(&gdecl->fdecl.params, &list);

        return status;
    }

    if (lex->cur.type != LBRACE) {
        // TODO: Handle this
        assert(false);
    }

    gdecl->type = GDECL_FDEFN;
    gdecl->fdefn.ret = type;
    gdecl->fdefn.id = id;
    memcpy(&gdecl->fdefn.params, &list);

    if (CCC_OK != (status = par_stmt(gdecl->fdefn.stmt))) {
        goto fail;
    }

    return status;

fail:
    gdecl_destroy(gdecl);
    return status;
}

void gdecl_destroy(gdecl_t *gdecl) {
    if (gdecl == NULL) {
        return;
    }
    sl_link_t cur;

    switch (gdecl->type) {
    case GDECL_NULL:
        break;
    case GDECL_FDEFN:
        type_destroy(gdecl->fdefn.ret);
        SL_FOREACH(cur, &gdecl->fdefn.params) {
            param_t *param = GET_ELEM(&gdecl->fdefn.params, cur);
            destroy_type(&param->type);
        }
        slist_destroy(&gdecl->fdefn.params, DOFREE);
        stmt_destroy(gdecl->fdefn.stmt);
        break;
    case GDECL_FDECL:
        type_destroy(gdecl->fdecl.ret);
        SL_FOREACH(cur, &gdecl->fdecl.params) {
            param_t *param = GET_ELEM(&gdecl->fdecl.params, cur);
            destroy_type(&param->type);
        }
        slist_destroy(&gdecl->fdecl.params, DOFREE);
        break;
    case GDECL_VDECL:
        destroy_stmt(&gdecl->vdecl);
        break;
    case GDECL_TYPE:
        destroy_type(&gdecl->type);
    case GDECL_TYPEDEF:
    }
}

status_t par_type(lex_wrap_t *lex, type_t **result) {
    status_t status = CCC_OK;
    // TODO: Implement
    (void)lex;
    (void)result;
    return status;
}

status_t par_expr(lex_wrap_t *lex, type_t **result) {
    status_t status = CCC_OK;
    // TODO: Implement
    (void)lex;
    (void)result;
    return status;
}

status_t par_stmt(lex_wrap_t *lex, type_t **result) {
    status_t status = CCC_OK;
    // TODO: Implement
    (void)lex;
    (void)result;
    return status;
}
*/

status_t par_translation_unit(lex_wrap_t *lex) {
    status_t status = CCC_OK;
    while (true) {

        // We're done when we reach EOF
        if (lex->cur.type == TOKEN_EOF) {
            return status;
        }

        if (CCC_OK != (status = par_external_declaration(lex))) {
            goto fail;
        }
    }
fail:
    return status;
}

status_t par_external_declaration(lex_wrap_t *lex) {
    status_t status = CCC_OK;
    if (CCC_OK != (status = par_declaration_specifier(lex))) {
        goto fail;
    }

    bool done = false;
    while (!done) {
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
            // case ID: ID needs to be handled separately

            // Type qualitifiers
        case CONST:
        case VOLATILE:
            if (CCC_OK != (status = par_declaration_specifier(lex))) {
                goto fail;
            }
            break;
        default:
            done = true;
        }

        // Must be declaration
        if (lex->cur.type == SEMI) {
            return par_declaration(lex);
        }

        // Must match declarator now
        par_declarator(lex);

        switch (lex->cur.type) {
        case EQ:
        case SEMI:
            return par_declaration(lex);
        case LPAREN:
            return par_function_definition(lex);
        }
    }
fail:
    return status;
}

status_t par_function_definition(lex_wrap_t *lex) {
    LEX_MATCH(lex, LPAREN);

    while (lex->cur.type != RPAREN) {
        par_declaration(lex);
    }

    return par_compound_statement;
}

status_t par_declaration_specifier(lex_wrap_t *lex) {
    switch (lex->cur.type) {
        // Storage class specifiers
    case AUTO:
    case REGISTER:
    case STATIC:
    case EXTERN:
    case TYPEDEF:
        return par_storage_class_specifier(lex);

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
    case ID:
        return par_type_specifier(lex);

        // Type qualitifiers
    case CONST:
    case VOLATILE:
        return par_type_qualifier(lex);
    default:
        return CCC_ESYNTAX;
    }
}

status_t parse_storage_class_specifier(lex_wrap_t *lex) {
}

status_t parse_type_specifier(lex_wrap_t *lex) {
    switch (lex->cur.type) {
    case VOID:
    case CHAR:
    case SHORT:
    case INT:
    case LONG:
    case FLOAT:
    case DOUBLE:
    case SIGNED:
    case UNSIGNED:
        return CCC_OK;
    case STRUCT:
    case UNION:
        return par_struct_or_union_specifier(lex);
    case ENUM:
        return par_enum_specifier(lex);
    case ID:
        return par_type_specifier(lex);
    default:
        return CCC_ESYNTAX;
    }
}

status_t par_struct_or_union_specifier(lex_wrap_t *lex) {
    switch (lex->cur.type) {
    case STRUCT:
    case UNION:
        break;
    default:
        return CCC_ESYNTAX;
    }
    LEX_ADVANCE(lex);

    if (lex->cur.type == LBRACE) {
        par_struct_declaration(lex);
        LEX_MATCH(lex, RBRACE);
        return CCC_OK;
    }

    if (lex->cur.type != ID) {
        return CCC_ESYNTAX;
    }
    LEX_ADVANCE(lex);

    if (lex->cur.type == LBRACE) {
        par_struct_declaration(lex);
        LEX_MATCH(lex, RBRACE);
        return CCC_OK;
    }

    return CCC_OK;
}

status_t par_struct_declaration(lex_wrap_t *lex) {
    bool done = false;
    while (!done) {
        switch (lex->cur.type) {
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
            // case ID: TODO: ID needs to be handled separately

            // Type qualitifiers
        case CONST:
        case VOLATILE:
            return par_specifier_qualifier(lex);
            break;
        default:
            done = true;
        }
    }

    return par_struct_declarator_list(lex);
}

status_t par_struct_declaration(lex_wrap_t *lex) {
    switch (lex.cur->type) {
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
    case ID:
        return par_type_specifier(lex);

        // Type qualitifiers
    case CONST:
    case VOLATILE:
        return par_type_qualifier(lex);
        break;
    default:
        return CCC_ESYNTAX;
    }

    return CCC_OK;
}

status_t par_struct_declarator_list(lex_wrap_t *lex) {
    par_struct_declarator();
    while (lex->cur.type == COMMA) {
        par_struct_declarator();
    }
    return CCC_OK;
}

status_t par_struct_declarator(lex_wrap_t *lex) {
    if (lex->cur.type == COLON) {
        LEX_ADVANCE(lex);
        return par_constant_expression(lex);
    }

    par_declarator(lex);

    if (lex->cur.type == COLON) {
        LEX_ADVANCE(lex);
        return par_constant_expression(lex);
    }

    return CCC_OK;
}

status_t par_declarator(lex_wrap_t *lex) {
    if (lex->cur.type == STAR) {
        par_pointer(lex);
    }
    return par_direct_declarator(lex);
}

status_t par_pointer(lex_wrap_t *lex) {
    LEX_MATCH(lex, STAR);

    bool done = false;
    while (!done) {
        switch (lex->cur.type) {
        case CONST:
        case VOLATILE:
            par_type_qualifier(lex);
            break;
        default:
            done = true;
        }
    }

    if (lex->cur.type == STAR) {
        par_pointer(lex);
    }

    return CCC_OK;
}

status_t par_type_qualifier(lex_wrap_t *lex) {
}

status_t par_direct_declaration(lex_wrap_t *lex) {
    if (cur->lex.type == LPAREN) {
        LEX_ADVANCE(lex);
        par_direct_declaration(lex);
        LEX_MATCH(lex, RPAREN);
    } else if (cur->lex.type == ID) {
        LEX_ADVANCE(lex);
    }

    if (cur->lex.type == LBRACK) {
        LEX_ADVANCE(lex);
        if (cur->lex.type == RBRACK) {
            LEX_ADVANCE(lex);
        } else {
            par_const_expression(lex);
            LEX_MATCH(lex, RBRACK);
        }
    } else if (cur->lex.type == LPAREN) {
        LEX_ADVANCE(lex);

        if (cur->lex.type == ID) {
            while (cur->lex.type == ID) {
                LEX_ADVANCE(lex);
            }
            LEX_MATCH(lex, RPAREN);
        } else {
            par_parameter_type_list(lex);
        }
    }

    return CCC_OK;
}

/*
status_t par_constant_expression(lex_wrap_t *lex) {
    return par_conditional_expression(lex_wrap_t *lex);
}

status_t par_conditional_expression(lex_wrap_t *lex) {
    par_logical_expression(lex);

    if (lex->cur.type != COND) {
        return CCC_OK;
    }
    LEX_ADVANCE(lex);

    par_expression(lex);

    if (lex->cur.type != COLON) {
        return CCC_ESYNTAX;
    }
    LEX_ADVANCE(lex);
    return par_conditional_expression(lex);
}
*/

status_t par_expression(lex_wrap_t *lex) {
    bool primary1 = false;

    switch (lex->cur.type) {
        // Unary expressions
    case INC:
    case DEC:
    case SIZEOF:
    case BITAND:
    case STAR:
    case PLUS:
    case MINUS:
    case BITNOT:
    case LOGICNOT:
        unary = true;
        par_unary_expression(lex);
        break;

        // Primary expressions
    case ID:
    case STRING:
    case INTCONST:
    case FLOATCONST:
        primary = true;
        break;

        // Casts and parens around expressions
    case LPAREN:
        LEX_ADVANCE(lex);
        switch (lex->cur.type) {
            // Cases for casts
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
            // case ID: TODO: ID needs to be handled separately

            // Type qualitifiers
        case CONST:
        case VOLATILE:
            par_cast_expression(lex);
            break;

            // Parens
        default:
            primary = true;
            par_expression(lex);
            break;
        }
    }

    if (primary) {
        switch (lex->cur.next) {
        case DEREF:
        case INC:
        case DEC:
        case DOT:
        case LBRACK:
        case LPAREN:
            par_postfix_expression(lex);
        }

        switch (lex->cur.next) {
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
        case BINOREQ:
            par_assignment_expression(lex);
        }
    }
    token_t op1;

    switch (lex->cur.next) {
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
        op1 = lex->cur.next;
        LEX_ADVANCE(lex);
        break;

    case COND:
        LEX_ADVANCE(lex);
        par_expression(lex);
        LEX_MATCH(COLON);
        return par_expression(lex);
    }

    // Parse next nonbinary expression
    switch (lex->cur.type) {
        // Unary expressions
    case INC:
    case DEC:
    case SIZEOF:
    case BITAND:
    case STAR:
    case PLUS:
    case MINUS:
    case BITNOT:
    case LOGICNOT:
        par_unary_expression(lex);
        break;

        // Primary expressions
    case ID:
    case STRING:
    case INTCONST:
    case FLOATCONST:
        break;

        // Casts and parens around expressions
    case LPAREN:
        LEX_ADVANCE(lex);
        switch (lex->cur.type) {
            // Cases for casts
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
            // case ID: TODO: ID needs to be handled separately

            // Type qualitifiers
        case CONST:
        case VOLATILE:
            par_cast_expression(lex);
            break;

            // Parens
        default:
            par_expression(lex);
            break;
        }
    }

    bool binary2 = false;
    token_t op2;
    switch (lex->cur.next) {
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
        op2 = lex->cur.next;
        break;

    case COND:
        LEX_ADVANCE(lex);
        par_expression(lex);
        LEX_MATCH(COLON);
        par_expression(lex);
        // Handle binary operators
        return;
    default:
        // Return expression combinaned with last op
        return;
    }

    if (greater_or_equal_prec(op1, op2)) {
        // Combine op1
        // TODO: loop back to above with combined first two as the left
    } else {
        par_expresson(lex);
        // recursive call with 2nd expression as current op
    }

    return CCC_OK;
}

status_t par_unary_expression(lex_wrap_t *lex) {
    case (lex->cur.type) {
        // Primary expressions
    case ID:
    case STRING:
    case INTCONST:
    case FLOATCONST:
        return par_postfix_expression(lex);
    case INC:
    case DEC:
        LEX_ADVANCE(lex);
        par_unary_expression(lex);
        break;
    case SIZEOF:
        LEX_ADVANCE(lex);
        switch (lex->cur.type) {
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
            // case ID: TODO: ID needs to be handled separately

            // Type qualitifiers
        case CONST:
        case VOLATILE:
            par_type_name(lex);
            break;
        default:
            par_unary_expression(lex);
            break;
        }
        break;
    case BITAND:
    case STAR:
    case PLUS:
    case MINUS:
    case BITNOT:
    case LOGICNOT:
        par_cast_expression(lex);
        break;
    }
}
status_t par_cast_expression(lex_wrap_t *lex) {
    if (lex->cur.next != LPAREN) {
        return par_unary_expression(lex);

    }
    LEX_ADVANCE(lex);
    par_type_name(lex);
    LEX_MATCH(lex, RPAREN);
    return par_cast_expression(lex);
}
status_t par_postfix_expression(lex_wrap_t *lex) {
    switch (lex->cur.next) {
    case LBRACK:
        LEX_ADVANCE(lex);
        par_expression(lex);
        LEX_MATCH(lex, RBRACK);
        return CCC_OK;

    case LPAREN:
        LEX_ADVANCE(lex);
        while (lex->cur.next != RPAREN) {
            bool primary = false;
            bool parsed = false;
            switch (lex->cur.next) {
            case ID:
            case STRING:
            case INTCONST:
            case FLOATCONST:
                primary = true;
            default:
                primary = false;
            }
            if (primary) {
                switch (lex->cur.next) {
                case DEREF:
                case INC:
                case DEC:
                case DOT:
                case LBRACK:
                case LPAREN:
                    par_postfix_expression(lex);
                }

                switch (lex->cur.next) {
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
                case BINOREQ:
                    par_assignment_expression(lex);
                    bool parsed = true;
                }
            }
            if (!parsed) {
                // TODO: Need to continue with primary if found
                par_expression(lex);
            }
        }
        LEX_ADVANCE(lex);
        break;

    case DOT:
        LEX_MATCH(lex, ID);
        break;

    case DEREF:
        LEX_MATCH(lex, ID);
        break;

    case INC:
    case DEC:
        LEX_ADVANCE(lex);
        break;
    }

    return CCC_OK;
}
status_t par_assignment_expression(lex_wrap_t *lex) {
    switch (lex->cur.next) {
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
    case BINOREQ:
    default:
        return CCC_ESYNTAX;
    }
    ADVANCE_LEX(lex);

    bool primary = false;
    bool parsed = false;
    switch (lex->cur.next) {
    case ID:
    case STRING:
    case INTCONST:
    case FLOATCONST:
        primary = true;
    default:
        primary = false;
    }
    if (primary) {
        switch (lex->cur.next) {
        case DEREF:
        case INC:
        case DEC:
        case DOT:
        case LBRACK:
        case LPAREN:
            par_postfix_expression(lex);
        }

        switch (lex->cur.next) {
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
        case BINOREQ:
            par_assignment_expression(lex);
            bool parsed = true;
        }
    }
    if (!parsed) {
        // TODO: Need to continue with primary if found
        par_expression(lex);
    }
}

status_t par_type_name(lex_wrap_t *lex) {
    par_specifier_qualifier(lex);
    bool done = false;
    while (!done) {
        switch (lex->cur.type) {
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
            // case ID: TODO: ID needs to be handled separately

            // Type qualitifiers
        case CONST:
        case VOLATILE:
            par_specifier_qualifier(lex);
            break;
        default:
            done = true;
        }
    }
    switch (lex->cur.next) {
    case STAR:
    case LPAREN:
    case LBRACE:
        par_abstract_declarator(lex);
    }

    return CCC_OK;
}

status_t par_parameter_type_list(lex_wrap_t *lex) {
    par_parameter_list(lex);
    if (lex->cur.next != ELIPSE) {
        return CCC_OK;
    }
    LEX_ADVANCE(lex);

    return CCC_OK;
}

status_t par_parameter_list(lex_wrap_t *lex) {
    par_parameter_declaration(lex);
    bool done = false;
    while (!done && lex->cur.next == COMMA) {
        LEX_ADVANCE(lex);
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
            // case ID: ID needs to be handled separately

            // Type qualitifiers
        case CONST:
        case VOLATILE:
            par_parameter_declaration(lex);
            break;
        default:
            done = true;
        }
    }
}

status_t par_parameter_declaration(lex_wrap_t *lex) {
    par_declaration_specifier(lex);
    bool done = false;
    while (!done && lex->cur.next == COMMA) {
        LEX_ADVANCE(lex);
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
            // case ID: ID needs to be handled separately

            // Type qualitifiers
        case CONST:
        case VOLATILE:
            par_parameter_declaration(lex);
            break;
        default:
            done = true;
        }
    }

    switch (lex->cur.type) {
    case ID:
    case STAR:
    case LPAREN:
        return par_declarator(lex);
    }
}

status_t par_enum_specifier(lex_wrap_t *lex) {
    LEX_MATCH(lex, ENUM);
    if (lex->cur.type == ID) {
        LEX_ADVANCE(lex);
    }

    if (lex->cur.type == LBRACE) {
        LEX_ADVANCE(lex);
        par_enumerator_list(lex);
        LEX_MATCH(lex, RBRACE);
    }

    return CCC_OK;
}

status_t par_enumerator_list(lex_wrap_t *lex) {
    par_enumerator(lex);
    while (lex->cur.next == COMMA) {
        par_enumerator(lex);
    }

    return CCC_OK;
}

status_t par_enumerator(lex_wrap_t *lex) {
    LEX_MATCH(lex, ID);
    if (lex->cur.type == EQ) {
        LEX_ADVANCE(lex, ID);
        LEX_MATCH(lex, INTLIT);
    }
    return CCC_OK;
}

status_t par_declaration(lex_wrap_t *lex) {
    par_declaration_specifier(lex);
    bool done = false;
    while (!done && lex->cur.next == COMMA) {
        LEX_ADVANCE(lex);
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
            // case ID: ID needs to be handled separately

            // Type qualitifiers
        case CONST:
        case VOLATILE:
            par_parameter_declaration(lex);
            break;
        default:
            done = true;
        }
    }

    done = false;
    while (!done) {
        switch (lex->cur.type) {
        case ID:
        case LPAREN:
        case STAR:
            par_init_declarator(lex);
            break;
        default:
            done = true;
        }
    }
}

status_t par_init_declarator(lex_wrap_t *lex) {
    par_declarator(lex);

    if (lex->cur.type == ASSIGN) {
        LEX_ADVANCE(lex);
        par_initializer(lex);
    }

    return CCC_OK;
}

status_t par_initializer(lex_wrap_t *lex) {
    if (lex->cur.type != LBRACK) {
        return par_assignment_expression(lex);
    }
    LEX_ADVANCE(lex);
    par_initializer_list(lex);
    LEX_MATCH(lex, RBRACK);
    return CCC_OK;
}

status_t par_initializer_list(lex_wrap_t *lex) {
    par_initializer(lex);
    while (lex->cur.type == COMMA) {
        LEX_ADVANCE(lex);
        if (lex->cur.type == RBRACK) {
            break;
        }
        par_initializer_list(lex);
    }
}


status_t par_compound_statement(lex_wrap_t *lex) {
    LEX_MATCH(lex, LBRACK);
    while (lex->cur.next != RBRACK) {
        par_statement(lex);
    }
    LEX_ADVANCE(lex);
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
        // case ID: ID needs to be handled separately

        // Type qualitifiers
    case CONST:
    case VOLATILE:
        return par_declaration(lex);
        break;

    case ID: // TODO: Id can be in expression. need to have backtracking
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
        return;
    default:
        return par_expression_statement(lex);
    }
}

status_t par_labeled_statement(lex_wrap_t *lex) {
    switch (lex->cur.type) {
    case ID:
        LEX_ADVANCE(lex);
        LEX_MATCH(lex, COLON);
        par_statement(lex);
        break;
    case CASE:
        LEX_ADVANCE(lex);
        par_constant_expression(lex);
        LEX_MATCH(lex, COLON);
        par_statement(lex);
        break;
    case DEFAULT:
        LEX_ADVANCE(lex);
        par_statement(lex);
        break;
    default:
        return CCC_ESYNTAX;
    }

    return CCC_OK;
}

status_t par_expression_statement(lex_wrap_t *lex) {
    if (lex->cur.type != SEMI) {
        par_expression(lex);
    }

    LEX_MATCH(lex, SEMI);
}

status_t par_selection_statement(lex_wrap_t *lex) {
    switch (lex->cur.type) {
    case IF:
        LEX_ADVANCE(lex);
        LEX_MATCH(lex, LPAREN);
        par_expression(lex);
        LEX_MATCH(lex, RPAREN);
        par_statement(lex);
        if (lex->cur.type == ELSE) {
            LEX_ADVANCE(lex);
        }
        par_statement(lex);
        break;
    case SWITCH:
        LEX_ADVANCE(lex);
        LEX_MATCH(lex, LPAREN);
        par_expression(lex);
        LEX_MATCH(lex, RPAREN);
        par_statement(lex);
        break;
    default:
        return CCC_ESYNTAX;
    }
}

status_t par_iteration_statement(lex_wrap_t *lex) {
    switch (lex->cur.type) {
    case DO:
        LEX_ADVANCE(lex);
        par_statement(lex);
        LEX_MATCH(lex, WHILE);
        LEX_MATCH(lex, LPAREN);
        par_expression(lex);
        LEX_MATCH(lex, RPAREN);
        LEX_MATCH(lex, SEMI);
        break;
    case WHILE:
        LEX_ADVANCE(lex);
        LEX_MATCH(lex, LPAREN);
        par_expression(lex);
        LEX_MATCH(lex, RPAREN);
        par_statement(lex);
        break;
    case FOR:
        LEX_ADVANCE(lex);
        LEX_MATCH(lex, LPAREN);
        if (lex->cur.type != SEMI) {
            par_expression(lex);
        }
        LEX_MATCH(lex, SEMI);
        if (lex->cur.type != SEMI) {
            par_expression(lex);
        }
        LEX_MATCH(lex, SEMI);
        if (lex->cur.type != SEMI) {
            par_expression(lex);
        }
        LEX_MATCH(lex, SEMI);
        LEX_MATCH(lex, RPAREN);
        par_statement(lex);
        break;
    default:
        return CCC_ESYNTAX;
    }
}
status_t par_jump_statement(lex_wrap_t *lex) {
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
        par_expression(lex);
        LEX_MATCH(lex, SEMI);
        break;
    default:
        return CCC_ESYNTAX;
    }
}
