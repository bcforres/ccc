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
 * Type checker implementation
 */

#include "typecheck.h"
#include "typecheck_priv.h"

#include <assert.h>
#include <stdio.h>

#include "typecheck_init.h"
#include "util/logger.h"


void tc_state_init(tc_state_t *tcs) {
    sl_init(&tcs->etypes, offsetof(type_t, heap_link));
    tcs->tunit = NULL;
    tcs->typetab = NULL;
    tcs->func = NULL;
    tcs->last_switch = NULL;
    tcs->last_loop = NULL;
    tcs->last_break = NULL;
    tcs->ignore_undef = false;
}

void tc_state_destroy(tc_state_t *tcs) {
    // Use free rather than ast_type_destroy because we don't want to
    // recursively free other nodes
    SL_DESTROY_FUNC(&tcs->etypes, free);
}

bool typecheck_ast(trans_unit_t *ast) {
    tc_state_t tcs;
    tc_state_init(&tcs);
    tcs.tunit = ast;
    bool retval = typecheck_trans_unit(&tcs, ast);
    tc_state_destroy(&tcs);
    return retval;
}

bool typecheck_const_expr(expr_t *expr, long long *result, bool ignore_undef) {
    bool retval;
    tc_state_t tcs;
    tc_state_init(&tcs);
    tcs.ignore_undef = ignore_undef;
    if (typecheck_expr(&tcs, expr, TC_CONST)) {
        typecheck_const_expr_eval(tcs.typetab, expr, result);
        retval = true;
    } else {
        retval = false;
    }
    tc_state_destroy(&tcs);

    return retval;
}

void typecheck_const_expr_eval(typetab_t *typetab, expr_t *expr,
                               long long *result) {
    switch (expr->type) {
    case EXPR_PAREN:
        typecheck_const_expr_eval(typetab, expr->paren_base, result);
        return;
    case EXPR_VAR: {
        typetab_entry_t *entry = tt_lookup(typetab, expr->var_id);
        // If entry isn't defined, return 0
        if (entry == NULL) {
            *result = 0;
        } else if (entry->entry_type == TT_ENUM_ID) {
            *result = entry->enum_val;
            return;
        } else {
            assert(false);
        }
        return;
    }
    case EXPR_CONST_INT:
        *result = expr->const_val.int_val;
        return;
    case EXPR_BIN: {
        long long temp1;
        long long temp2;
        typecheck_const_expr_eval(typetab, expr->bin.expr1, &temp1);
        typecheck_const_expr_eval(typetab, expr->bin.expr2, &temp2);
        switch (expr->bin.op) {
        case OP_TIMES:    *result = temp1 *  temp2; break;
        case OP_DIV:      *result = temp1 /  temp2; break;
        case OP_MOD:      *result = temp1 %  temp2; break;
        case OP_PLUS:     *result = temp1 +  temp2; break;
        case OP_MINUS:    *result = temp1 -  temp2; break;
        case OP_LSHIFT:   *result = temp1 << temp2; break;
        case OP_RSHIFT:   *result = temp1 >> temp2; break;
        case OP_LT:       *result = temp1 <  temp2; break;
        case OP_GT:       *result = temp1 >  temp2; break;
        case OP_LE:       *result = temp1 <= temp2; break;
        case OP_GE:       *result = temp1 >= temp2; break;
        case OP_EQ:       *result = temp1 == temp2; break;
        case OP_NE:       *result = temp1 != temp2; break;
        case OP_BITAND:   *result = temp1 &  temp2; break;
        case OP_BITXOR:   *result = temp1 ^  temp2; break;
        case OP_BITOR:    *result = temp1 |  temp2; break;
        case OP_LOGICAND: *result = temp1 && temp2; break;
        case OP_LOGICOR:  *result = temp1 || temp2; break;
        default:
            assert(false);
        }
        return;
    }
    case EXPR_UNARY: {
        long long temp;
        typecheck_const_expr_eval(typetab, expr->unary.expr, &temp);
        switch (expr->unary.op) {
        case OP_UPLUS:    *result =  temp; break;
        case OP_UMINUS:   *result = -temp; break;
        case OP_BITNOT:   *result = ~temp; break;
        case OP_LOGICNOT: *result = !temp; break;
        default:
            assert(false);
        }
        return;
    }
    case EXPR_COND: {
        long long temp;
        typecheck_const_expr_eval(typetab, expr->cond.expr1, &temp);
        if (temp) {
            typecheck_const_expr_eval(typetab, expr->cond.expr2, result);
        } else {
            typecheck_const_expr_eval(typetab, expr->cond.expr3, result);
        }
        return;
    }
    case EXPR_CAST:
        typecheck_const_expr_eval(typetab, expr->cast.base, result);
        return;

    case EXPR_SIZEOF:
    case EXPR_ALIGNOF:
        if (expr->sizeof_params.type != NULL) {
            type_t *type;
            decl_node_t *node = sl_head(&expr->sizeof_params.type->decls);
            if (node == NULL) {
                type = expr->sizeof_params.type->type;
            } else {
                type = node->type;
            }
            if (expr->type == EXPR_SIZEOF) {
                *result = ast_type_size(type);
            } else { // expr->type == EXPR_ALIGNOF
                *result = ast_type_align(type);
            }
        } else {
            assert(expr->sizeof_params.expr != NULL);
            if (expr->type == EXPR_SIZEOF) {
                *result = ast_type_size(expr->sizeof_params.expr->etype);
            } else { // expr->type == EXPR_ALIGNOF
                *result = ast_type_align(expr->sizeof_params.expr->etype);
            }
        }
        return;
    case EXPR_OFFSETOF: {
        type_t *type = DECL_TYPE(expr->offsetof_params.type);
        *result = ast_type_offset(type, &expr->offsetof_params.list);
        return;
    }
    case EXPR_VOID:
    case EXPR_ASSIGN:
    case EXPR_CONST_FLOAT:
    case EXPR_CONST_STR:
    case EXPR_CALL:
    case EXPR_CMPD:
    case EXPR_MEM_ACC:
    case EXPR_ARR_IDX:
    case EXPR_INIT_LIST:
    case EXPR_DESIG_INIT:
    default:
        assert(false);
    }
}

bool typecheck_type_equal(type_t *t1, type_t *t2) {
    // Types which differ in these modifiers are still equal
#define IGNORE_MASK (~(TMOD_EXTERN | TMOD_TYPEDEF | TMOD_INLINE))

    t1 = ast_type_untypedef(t1);
    t2 = ast_type_untypedef(t2);

    // Remove any modifiers which do not effect equality
    while (t1->type == TYPE_MOD && (t1->mod.type_mod & IGNORE_MASK) == 0) {
        t1 = t1->mod.base;
        t1 = ast_type_untypedef(t1);
    }
    while (t2->type == TYPE_MOD && (t2->mod.type_mod & IGNORE_MASK) == 0) {
        t2 = t2->mod.base;
        t2 = ast_type_untypedef(t2);
    }

    if (t1 == t2) { // Pointers equal
        return true;
    }

    if (t1->type != t2->type) {
        return false;
    }

    switch (t1->type) {
    case TYPE_VOID:
    case TYPE_BOOL:
    case TYPE_CHAR:
    case TYPE_SHORT:
    case TYPE_INT:
    case TYPE_LONG:
    case TYPE_LONG_LONG:
    case TYPE_FLOAT:
    case TYPE_DOUBLE:
    case TYPE_LONG_DOUBLE:
    case TYPE_VA_LIST:
        assert(false && "Primitive types should have same adderss");
        return false;

    case TYPE_STRUCT:
    case TYPE_UNION:
    case TYPE_ENUM:
        // compound types which aren't the same address cannot be the same type
        return false;

    case TYPE_TYPEDEF:
        assert(false && "Should be untypedefed");
        return false;

    case TYPE_MOD: {
        return ((t1->mod.type_mod & IGNORE_MASK) ==
                (t2->mod.type_mod & IGNORE_MASK)) &&
            typecheck_type_equal(t1->mod.base, t2->mod.base);
    }

    case TYPE_PAREN:
        assert(false && "Parens should be removed");
        return false;

    case TYPE_FUNC: {
        if (!typecheck_type_equal(t1->func.type, t2->func.type)) {
            return false;
        }
        sl_link_t *ptype1 = t1->func.params.head;
        sl_link_t *ptype2 = t2->func.params.head;
        while (ptype1 != NULL && ptype2 != NULL) {
            decl_t *decl1 = GET_ELEM(&t1->func.params, ptype1);
            decl_t *decl2 = GET_ELEM(&t2->func.params, ptype2);

            type_t *decl_type1 = DECL_TYPE(decl1);
            type_t *decl_type2 = DECL_TYPE(decl2);
            if (!typecheck_type_equal(decl_type1, decl_type2)) {
                return false;
            }
            ptype1 = ptype1->next;
            ptype2 = ptype2->next;
        }
        if (ptype1 != NULL || ptype2 != NULL) {
            return false;
        }

        return true;
    }

    case TYPE_ARR: {
        return t1->arr.nelems == t2->arr.nelems &&
            typecheck_type_equal(t1->arr.base, t2->arr.base);
    }
    case TYPE_PTR:
        return (t1->ptr.type_mod == t2->ptr.type_mod) &&
            typecheck_type_equal(t1->ptr.base, t2->ptr.base);
    default:
        assert(false);
    }

    return true;
}

bool typecheck_expr_lvalue(tc_state_t *tcs, expr_t *expr) {
    switch (expr->type) {
    case EXPR_PAREN:
        return typecheck_expr_lvalue(tcs, expr->paren_base);

    case EXPR_MEM_ACC:
    case EXPR_ARR_IDX:
    case EXPR_VAR:
        return true;

    case EXPR_UNARY:
        switch (expr->unary.op) {
        case OP_PREINC:
        case OP_POSTINC:
        case OP_PREDEC:
        case OP_POSTDEC:
            return typecheck_expr_lvalue(tcs, expr->unary.expr);
        case OP_DEREF:
            return true;
        default:
            break;
        }
        break;
    case EXPR_CMPD: {
        expr_t *last = sl_tail(&expr->cmpd.exprs);
        return typecheck_expr_lvalue(tcs, last);
    }
    default:
        break;
    }
    logger_log(expr->mark, LOG_ERR,
               "lvalue required as left operand of assignment");
    return false;
}

bool typecheck_type_assignable(fmark_t *mark, type_t *to, type_t *from) {
    to = ast_type_untypedef(to);
    from = ast_type_untypedef(from);

    type_t *umod_to = ast_type_unmod(to);
    type_t *umod_from = ast_type_unmod(from);

    if (umod_to->type == TYPE_VOID) {
        if (mark != NULL) {
            logger_log(mark, LOG_ERR, "invalid use of void expression");
        }
        return false;
    }

    if (umod_from->type == TYPE_VOID) {
        if (mark != NULL) {
            logger_log(mark, LOG_ERR,
                       "void value not ignored as it ought to be");
        }
        return false;
    }

    // TODO1: This ignores const
    if (typecheck_type_equal(umod_to, umod_from)) {
        return true;
    }


    if (umod_from->type == TYPE_STRUCT || umod_from->type == TYPE_UNION) {
        goto fail;
    }

    bool is_num_from = TYPE_IS_NUMERIC(umod_from);
    bool is_int_from = TYPE_IS_INTEGRAL(umod_from);
    bool is_ptr_from = TYPE_IS_PTR(umod_from);

    switch (umod_to->type) {
    case TYPE_VOID:
        if (mark != NULL) {
            logger_log(mark, LOG_ERR, "can't assign to void");
        }
        return false;

    case TYPE_BOOL:
    case TYPE_CHAR:
    case TYPE_SHORT:
    case TYPE_INT:
    case TYPE_LONG:
    case TYPE_LONG_LONG:
    case TYPE_FLOAT:
    case TYPE_DOUBLE:
    case TYPE_LONG_DOUBLE:
        if (is_num_from || umod_from->type == TYPE_ENUM) {
            return true;
        }

        if (is_ptr_from) {
            if (mark != NULL) {
                logger_log(mark, LOG_WARN, "initialization makes integer from"
                           " pointer without a cast");
            }
            return true;
        }

        goto fail;

    case TYPE_STRUCT:
    case TYPE_UNION:
    case TYPE_VA_LIST:
        goto fail;

    case TYPE_ENUM:
        if (is_num_from) {
            return true;
        }
        goto fail;

    case TYPE_ARR:
        if (umod_from->type == TYPE_PTR &&
            typecheck_type_assignable(mark, umod_to->arr.base,
                                      umod_from->ptr.base)) {
            return true;
        }
        if (umod_from->type == TYPE_ARR &&
            typecheck_type_assignable(mark, umod_to->arr.base,
                                      umod_from->arr.base)) {
            type_t *umod_to_b = ast_type_unmod(umod_to->arr.base);
            type_t *umod_from_b = ast_type_unmod(umod_from->arr.base);

            if (umod_to_b->type != TYPE_ARR || umod_from_b->type != TYPE_ARR) {
                return true;
            } else {
                assert(umod_to_b->arr.len != NULL &&
                       umod_from_b->arr.len != NULL);
                if (umod_to_b->arr.nelems == umod_from_b->arr.nelems) {
                    return true;
                }
            }
        }
        if (mark != NULL) {
            logger_log(mark, LOG_ERR,
                       "assignment to expression with array type");
        }
        return false;
    case TYPE_PTR:
        if (is_int_from) {
            return true;
        }

        // Can assign any pointer type to void *
        if (umod_to->ptr.base->type == TYPE_VOID && is_ptr_from) {
            return true;
        }

        switch (umod_from->type) {
        case TYPE_FUNC:
            if (typecheck_type_equal(umod_to->ptr.base, umod_from)) {
                return true;
            }
            break;
        case TYPE_ARR:
            if (typecheck_type_assignable(mark, umod_to->ptr.base,
                                          umod_from->arr.base)) {
                return true;
            }
            break;
        case TYPE_PTR:
            // Can freely assign to/from void *
            if (ast_type_unmod(umod_to->ptr.base)->type == TYPE_VOID ||
                ast_type_unmod(umod_from->ptr.base)->type == TYPE_VOID) {
                return true;
            }
            if (typecheck_type_assignable(mark, umod_to->ptr.base,
                                          umod_from->ptr.base)) {
                return true;
            }
            break;
        default:
            break;
        }

        goto fail;

        // Can't assign to functions
    case TYPE_FUNC:
        break;
    default:
        assert(false);
    }

fail:
    if (mark != NULL) {
        logger_log(mark, LOG_ERR,
                   "incompatible types when assigning");
    }
    return false;
}

bool typecheck_types_binop(fmark_t *mark, oper_t op, type_t *t1, type_t *t2) {
    t1 = ast_type_untypedef(t1);
    t2 = ast_type_untypedef(t2);
    type_t *umod1 = ast_type_unmod(t1);
    type_t *umod2 = ast_type_unmod(t2);

    bool is_numeric1 = TYPE_IS_NUMERIC(umod1) || umod1->type == TYPE_ENUM;
    bool is_numeric2 = TYPE_IS_NUMERIC(umod2) || umod2->type == TYPE_ENUM;
    bool is_int1 = TYPE_IS_INTEGRAL(umod1) || umod1->type == TYPE_ENUM;
    bool is_int2 = TYPE_IS_INTEGRAL(umod2) || umod2->type == TYPE_ENUM;
    bool is_ptr1 = TYPE_IS_PTR(umod1);
    bool is_ptr2 = TYPE_IS_PTR(umod2);

    // If both are integer types, they can use any binary operator
    if (is_int1 && is_int2) {
        return true;
    }

    switch (op) {
    case OP_TIMES:
    case OP_DIV:
        // times, div, only allow numeric operands
        if (is_numeric1 && is_numeric2) {
            return true;
        }
        break;

    case OP_BITAND:
    case OP_BITXOR:
    case OP_BITOR:
    case OP_MOD:
    case OP_LSHIFT:
    case OP_RSHIFT:
        // Require both operands to be integers, which was already checked
        break;

    case OP_MINUS:
        if (is_ptr1 && is_ptr2) {
            type_t *umod_base1 = ast_type_unmod(ast_type_ptr_base(umod1));
            type_t *umod_base2 = ast_type_unmod(ast_type_ptr_base(umod2));
            if (typecheck_type_equal(umod_base1, umod_base2)) {
                return true;
            }
        }
        // FALL THROUGH

    case OP_PLUS:
        if (is_numeric1 && is_numeric2) {
            return true;
        }
        // Allow pointers to be added with integers
        if ((is_ptr1 && is_int2) || (is_int1 && is_ptr2)) {
            return true;
        }

        break;

    case OP_LT:
    case OP_GT:
    case OP_LE:
    case OP_GE:
    case OP_EQ:
    case OP_NE:
    case OP_LOGICAND:
    case OP_LOGICOR:
        // Two integral types allowed
        if (is_numeric1 && is_numeric2) {
            return true;
        }
        // Allow combinations of pointers and ints
        if((is_ptr1 && is_ptr2) || (is_ptr1 && is_int2) ||
           (is_int1 && is_ptr2)) {
            return true;
        }
        break;

    default:
        assert(false);
    }

    logger_log(mark, LOG_ERR, "invalid operands to binary %s",
               ast_oper_str(op));
    return false;
}

bool typecheck_type_unaryop(fmark_t *mark, oper_t op, type_t *type) {
    type = ast_type_unmod(type);
    bool is_numeric = TYPE_IS_NUMERIC(type);
    bool is_int = TYPE_IS_INTEGRAL(type);
    bool is_ptr = TYPE_IS_PTR(type);

    switch (op) {
    case OP_PREINC:
    case OP_POSTINC:
    case OP_PREDEC:
    case OP_POSTDEC:
        if (is_numeric || is_int || is_ptr) {
            return true;
        }
        break;

    case OP_ADDR:
        // Can take the address of anything
        return true;

    case OP_DEREF:
        if (is_ptr) {
            return true;
        }
        break;

    case OP_UPLUS:
    case OP_UMINUS:
        if (is_numeric) {
            return true;
        }
        break;

    case OP_BITNOT:
        if (is_int) {
            return true;
        }
        break;

    case OP_LOGICNOT:
        if (is_numeric || is_int || is_ptr || type->type == TYPE_ENUM) {
            return true;
        }
        break;
    default:
        assert(false);
    }

    logger_log(mark, LOG_ERR, "invalid operand to operator %s",
               ast_oper_str(op));
    return false;
}

bool typecheck_type_max(trans_unit_t *tunit, fmark_t *mark, type_t *t1,
                        type_t *t2, type_t **result) {
    t1 = ast_type_untypedef(t1);
    t2 = ast_type_untypedef(t2);

    if (typecheck_type_equal(t1, t2)) {
        *result = t1;
        return true;
    }

    type_t *umod1 = ast_type_unmod(t1);
    type_t *umod2 = ast_type_unmod(t2);

    bool is_numeric1 = TYPE_IS_NUMERIC(umod1);
    bool is_numeric2 = TYPE_IS_NUMERIC(umod2);
    bool is_int2 = TYPE_IS_INTEGRAL(umod2);
    bool is_ptr2 = TYPE_IS_PTR(umod2);

    if (is_numeric1 && is_numeric2) {
        if (umod1->type > umod2->type) {
            *result = t1;
        } else if (umod2->type > umod1->type) {
            *result = t2;
        } else {
            // If they are the same magnitude of integer, then unsigned wins
            if (TYPE_IS_UNSIGNED(t1)) {
                *result = t1;
            } else if (TYPE_IS_UNSIGNED(t2)) {
                *result = t2;
            } else {
                *result = t1;
            }
        }
        return true;
    }

    switch (umod1->type) {
    case TYPE_VOID: // Void cannot be converted to anything
        break;

    case TYPE_BOOL:
    case TYPE_CHAR:
    case TYPE_SHORT:
    case TYPE_INT:
    case TYPE_LONG:
    case TYPE_LONG_LONG:
    case TYPE_FLOAT:
    case TYPE_DOUBLE:
    case TYPE_LONG_DOUBLE:
        if (umod2->type == TYPE_ENUM) {
            *result = t1;
            return true;
        }

        if (is_ptr2) {
            *result = t2;
            return true;
        }
        break;

    case TYPE_STRUCT:
    case TYPE_UNION:
    case TYPE_VA_LIST:
        // Compound types cannot be converted
        break;

    case TYPE_ENUM:
        if (umod2->type == TYPE_ENUM) {
            *result = t1;
            return true;
        }
        if (is_int2) {
            *result = t2;
            return true;
        }
        break;

        // Only allowed to combine with integral types
    case TYPE_FUNC:
    case TYPE_ARR:
    case TYPE_PTR:
        if (is_int2) {
            *result = t1;
            return true;
        }

        // If both are pointers, and one is a void *, return the other
        if (umod2->type == TYPE_PTR &&
            ast_type_unmod(umod2->ptr.base)->type == TYPE_VOID) {
            *result = t1;
            return true;
        }

        if (is_ptr2 && umod1->type == TYPE_PTR &&
            ast_type_unmod(umod1->ptr.base)->type == TYPE_VOID) {
            *result = t2;
            return true;
        }

        if (is_ptr2) {
            type_t *umod_base1 = ast_type_unmod(ast_type_ptr_base(umod1));
            type_t *umod_base2 = ast_type_unmod(ast_type_ptr_base(umod2));
            if (typecheck_type_equal(umod_base1, umod_base2)) {
                // If we're ever in a state where we need to "combine" two
                // types, then pointer types must actually be a pointer
                if (umod1->type == TYPE_PTR) {
                    *result = t1;
                } else if (umod2->type == TYPE_PTR) {
                    *result = t2;
                } else {
                    type_t *ptr_type = ast_type_create(tunit, umod_base1->mark,
                                                       TYPE_PTR);
                    ptr_type->ptr.base = umod_base1;
                    *result = ptr_type;
                }
                return true;
            }
        }
        break;
    default:
        assert(false);
    }

    logger_log(mark, LOG_ERR, "Incompatable types");
    return false;
}

bool typecheck_type_cast(fmark_t *mark, type_t *to, type_t *from) {
    to = ast_type_untypedef(to);
    from = ast_type_untypedef(from);

    if (typecheck_type_equal(to, from)) {
        return true;
    }

    // Anything can be casted to void
    if (to->type == TYPE_VOID) {
        return true;
    }

    type_t *umod_to = ast_type_unmod(to);
    type_t *umod_from = ast_type_unmod(from);

    // Can't cast to struct/union types
    if (umod_to->type == TYPE_STRUCT || umod_to->type == TYPE_UNION) {
        logger_log(mark, LOG_ERR, "conversion to non-scalar type requested");
        return false;
    }
    // Can't cast from struct/union types
    if (umod_from->type == TYPE_STRUCT || umod_from->type == TYPE_UNION) {
        logger_log(mark, LOG_ERR, "conversion from non-scalar type requested");
        return false;
    }

    return true;
}

bool typecheck_type_integral(fmark_t *mark, type_t *type) {
    switch (type->type) {
    case TYPE_BOOL:
    case TYPE_CHAR:
    case TYPE_SHORT:
    case TYPE_INT:
    case TYPE_LONG:
    case TYPE_LONG_LONG:
    case TYPE_FLOAT:
    case TYPE_DOUBLE:
    case TYPE_LONG_DOUBLE:
    case TYPE_ENUM:
        return true;

    case TYPE_TYPEDEF:
        return typecheck_type_integral(mark, type->typedef_params.base);
    case TYPE_MOD:
        return typecheck_type_integral(mark, type->mod.base);
    case TYPE_PAREN:
        return typecheck_type_integral(mark, type->paren_base);

    case TYPE_VOID:
    case TYPE_STRUCT:
    case TYPE_UNION:

    case TYPE_FUNC:
    case TYPE_ARR:
    case TYPE_PTR:

    case TYPE_VA_LIST:
        break;

    default:
        assert(false);
    }

    logger_log(mark, LOG_ERR, "integral type required");
    return false;
}

bool typecheck_type_conditional(fmark_t *mark, type_t *type) {
    switch (type->type) {
    case TYPE_BOOL:
    case TYPE_CHAR:
    case TYPE_SHORT:
    case TYPE_INT:
    case TYPE_LONG:
    case TYPE_LONG_LONG:
    case TYPE_FLOAT:
    case TYPE_DOUBLE:
    case TYPE_LONG_DOUBLE:

    case TYPE_ENUM:
    case TYPE_FUNC:
    case TYPE_ARR:
    case TYPE_PTR:
        return true;

    case TYPE_TYPEDEF:
        return typecheck_type_conditional(mark, type->typedef_params.base);
    case TYPE_MOD:
        return typecheck_type_conditional(mark, type->mod.base);
    case TYPE_PAREN:
        return typecheck_type_conditional(mark, type->paren_base);

    case TYPE_VOID:
    case TYPE_STRUCT:
    case TYPE_UNION:
    case TYPE_VA_LIST:
        break;

    default:
        assert(false);
    }

    logger_log(mark, LOG_ERR, "conditional type required");
    return false;
}

bool typecheck_designator_list(tc_state_t *tcs, type_t *type,
                            designator_list_t *list) {
    SL_FOREACH(cur, &list->list) {
        type = ast_type_unmod(type);
        expr_t *cur_expr = GET_ELEM(&list->list, cur);
        switch (cur_expr->type) {
        case EXPR_MEM_ACC:
            if (type->type != TYPE_STRUCT &&
                type->type != TYPE_UNION) {
                logger_log(cur_expr->mark, LOG_ERR,
                           "request for member '%s' in something not a "
                           "structure or union", cur_expr->mem_acc.name);
                return false;
            }
            decl_node_t *node = ast_type_find_member(type,
                                                     cur_expr->mem_acc.name,
                                                     NULL);
            if (node == NULL) {
                logger_log(cur_expr->mark, LOG_ERR,
                           "type type has no member '%s'",
                           cur_expr->mem_acc.name);
                return false;
            }
            type = node->type;
            break;
        case EXPR_ARR_IDX:
            if (type->type != TYPE_ARR) {
                logger_log(cur_expr->mark, LOG_ERR,
                           "subscripted value is not an array");
                return false;
            }
            if (!typecheck_expr(tcs, cur_expr->arr_idx.index, TC_CONST)) {
                logger_log(cur_expr->arr_idx.index->mark, LOG_ERR,
                           "cannot apply 'offsetof' to a non constant "
                           "address");
                return false;
            }
            long long val;
            typecheck_const_expr_eval(tcs->typetab, cur_expr->arr_idx.index,
                                      &val);
            cur_expr->arr_idx.const_idx = val;
            type = type->arr.base;
            break;
        default:
            assert(false);
        }
    }

    return true;
}

bool typecheck_expr_integral(tc_state_t *tcs, expr_t *expr) {
    if (!typecheck_expr(tcs, expr, TC_NOCONST)) {
        return false;
    }
    return typecheck_type_integral(expr->mark, expr->etype);
}

bool typecheck_expr_conditional(tc_state_t *tcs, expr_t *expr) {
    if (!typecheck_expr(tcs, expr, TC_NOCONST)) {
        return false;
    }
    return typecheck_type_conditional(expr->mark, expr->etype);
}

bool typecheck_expr_va_list(tc_state_t *tcs, expr_t *expr) {
    if (!typecheck_expr(tcs, expr, TC_NOCONST)) {
        return false;
    }
    type_t *type = ast_type_unmod(expr->etype);
    if (type->type != TYPE_VA_LIST) {
        logger_log(expr->mark, LOG_ERR, "Expected __builtin_va_list");
        return false;
    }
    return true;
}

bool typecheck_trans_unit(tc_state_t *tcs, trans_unit_t *trans_unit) {
    typetab_t *save_tab = tcs->typetab;
    tcs->typetab = &trans_unit->typetab;
    bool retval = true;

    SL_FOREACH(cur, &trans_unit->gdecls) {
        retval &= typecheck_gdecl(tcs, GET_ELEM(&trans_unit->gdecls, cur));
    }

    tcs->typetab = save_tab;
    return retval;
}

bool typecheck_gdecl(tc_state_t *tcs, gdecl_t *gdecl) {
    bool retval = true;

    switch (gdecl->type) {
    case GDECL_FDEFN: {
        gdecl_t *func_save = tcs->func;
        assert(func_save == NULL); // Can't have nested functions in C
        tcs->func = gdecl;

        decl_node_t *node = sl_head(&gdecl->decl->decls);
        assert(node != NULL && node->id != NULL);

        // Set the current function we're in
        log_function = node->id;

        retval &= typecheck_decl(tcs, gdecl->decl, TYPE_VOID);
        retval &= typecheck_stmt(tcs, gdecl->fdefn.stmt);
        SL_FOREACH(cur, &gdecl->fdefn.gotos) {
            stmt_t *goto_stmt = GET_ELEM(&gdecl->fdefn.gotos, cur);
            stmt_t *label = ht_lookup(&tcs->func->fdefn.labels,
                                      &goto_stmt->goto_params.label);
            if (label == NULL) {
                logger_log(goto_stmt->mark, LOG_ERR,
                           "label %s used but not defined",
                           goto_stmt->goto_params.label);
                retval = false;
            }
        }

        // Restore old state
        log_function = NULL;
        tcs->func = func_save;
        break;
    }
    case GDECL_DECL:
        retval &= typecheck_decl(tcs, gdecl->decl, TYPE_VOID);
        break;
    default:
        assert(false);
        retval = false;
    }

    return retval;
}

bool typecheck_stmt(tc_state_t *tcs, stmt_t *stmt) {
    status_t status;
    bool retval = true;
    switch (stmt->type) {
    case STMT_NOP:
        return true;

    case STMT_DECL:
        return typecheck_decl(tcs, stmt->decl, TYPE_VOID);

    case STMT_LABEL:
        retval &= typecheck_stmt(tcs, stmt->label.stmt);

        assert(tcs->func != NULL);
        if (CCC_OK != (status = ht_insert(&tcs->func->fdefn.labels,
                                          &stmt->label.link))) {
            goto fail;
        }
        return retval;
    case STMT_CASE:
        if (tcs->last_switch == NULL) {
            logger_log(stmt->mark, LOG_ERR,
                       "'case' label not within a switch statement");
            retval = false;
        } else {
            sl_append(&tcs->last_switch->switch_params.cases,
                      &stmt->case_params.link);
        }
        retval &= typecheck_expr_integral(tcs, stmt->case_params.val);
        retval &= typecheck_stmt(tcs, stmt->case_params.stmt);
        return retval;
    case STMT_DEFAULT:
        if (tcs->last_switch == NULL) {
            logger_log(stmt->mark, LOG_ERR,
                       "'default' label not within a switch statement");
            retval = false;
        } else {
            tcs->last_switch->switch_params.default_stmt = stmt;
        }
        retval &= typecheck_stmt(tcs, stmt->default_params.stmt);
        return retval;

    case STMT_IF:
        retval &= typecheck_expr_conditional(tcs, stmt->if_params.expr);
        retval &= typecheck_stmt(tcs, stmt->if_params.true_stmt);
        if (stmt->if_params.false_stmt != NULL) {
            retval &= typecheck_stmt(tcs, stmt->if_params.false_stmt);
        }
        return retval;
    case STMT_SWITCH: {
        retval &= typecheck_expr_integral(tcs, stmt->switch_params.expr);

        stmt_t *switch_save = tcs->last_switch;
        stmt_t *break_save = tcs->last_break;
        tcs->last_switch = stmt;
        tcs->last_break = stmt;

        retval &= typecheck_stmt(tcs, stmt->switch_params.stmt);

        tcs->last_switch = switch_save;
        tcs->last_break = break_save;
        return retval;
    }

    case STMT_DO: {
        stmt_t *loop_save = tcs->last_loop;
        stmt_t *break_save = tcs->last_break;
        tcs->last_loop = stmt;
        tcs->last_break = stmt;

        retval &= typecheck_stmt(tcs, stmt->do_params.stmt);
        retval &= typecheck_expr_conditional(tcs, stmt->do_params.expr);

        tcs->last_loop = loop_save;
        tcs->last_break = break_save;
        return retval;
    }
    case STMT_WHILE: {
        retval &= typecheck_expr_conditional(tcs, stmt->while_params.expr);

        stmt_t *loop_save = tcs->last_loop;
        stmt_t *break_save = tcs->last_break;
        tcs->last_loop = stmt;
        tcs->last_break = stmt;

        retval &= typecheck_stmt(tcs, stmt->while_params.stmt);

        tcs->last_loop = loop_save;
        tcs->last_break = break_save;
        return retval;
    }
    case STMT_FOR: {

        stmt_t *loop_save = tcs->last_loop;
        stmt_t *break_save = tcs->last_break;
        typetab_t *last_tab = tcs->typetab;
        tcs->last_loop = stmt;
        tcs->last_break = stmt;
        if (stmt->for_params.typetab != NULL) {
            tcs->typetab = stmt->for_params.typetab;
        }

        if (stmt->for_params.expr1 != NULL) {
            retval &= typecheck_expr(tcs, stmt->for_params.expr1, TC_NOCONST);
        }
        if (stmt->for_params.decl1 != NULL) {
            retval &= typecheck_decl(tcs, stmt->for_params.decl1, TYPE_VOID);
        }
        if (stmt->for_params.expr2 != NULL) {
            retval &= typecheck_expr_conditional(tcs, stmt->for_params.expr2);
        }
        if (stmt->for_params.expr3 != NULL) {
            retval &= typecheck_expr(tcs, stmt->for_params.expr3, TC_NOCONST);
        }

        retval &= typecheck_stmt(tcs, stmt->for_params.stmt);

        // Restore state
        tcs->typetab = last_tab;
        tcs->last_loop = loop_save;
        tcs->last_break = break_save;
        return retval;
    }

    case STMT_GOTO:
        assert(tcs->func != NULL);
        sl_append(&tcs->func->fdefn.gotos, &stmt->goto_params.link);
        return retval;
    case STMT_CONTINUE:
        if (tcs->last_loop == NULL) {
            logger_log(stmt->mark, LOG_ERR,
                       "continue statement not within a loop");
            retval = false;
        } else {
            stmt->continue_params.parent = tcs->last_loop;
        }
        return retval;
    case STMT_BREAK:
        if (tcs->last_break == NULL) {
            logger_log(stmt->mark, LOG_ERR,
                       "break statement not within loop or switch");
            retval = false;
        } else {
            stmt->break_params.parent = tcs->last_loop;
        }
        return retval;
    case STMT_RETURN: {
        decl_node_t *func_sig = sl_head(&tcs->func->decl->decls);
        assert(func_sig->type->type == TYPE_FUNC);

        if (stmt->return_params.expr == NULL) {
            if (ast_type_unmod(func_sig->type->func.type)->type != TYPE_VOID) {
                logger_log(stmt->mark, LOG_WARN,
                           "'return' with no value, in function returning"
                           " non-void");
            }
        } else {
            if (!typecheck_expr(tcs, stmt->return_params.expr, TC_NOCONST)) {
                return false;
            }
            retval &=
                typecheck_type_assignable(stmt->mark,
                                          func_sig->type->func.type,
                                          stmt->return_params.expr->etype);
            stmt->return_params.type = func_sig->type->func.type;
        }
        return retval;
    }

    case STMT_COMPOUND: {
        // Enter new scope
        typetab_t *save_tab = tcs->typetab;
        tcs->typetab = &stmt->compound.typetab;

        SL_FOREACH(cur, &stmt->compound.stmts) {
            retval &= typecheck_stmt(tcs, GET_ELEM(&stmt->compound.stmts, cur));
        }

        // Restore scope
        tcs->typetab = save_tab;
        return retval;
    }

    case STMT_EXPR:
        return typecheck_expr(tcs, stmt->expr.expr, TC_NOCONST);

    default:
        assert(false);
    }

fail:
    return retval;
}

bool typecheck_decl(tc_state_t *tcs, decl_t *decl, type_type_t type) {
    bool retval = true;

    retval &= typecheck_type(tcs, decl->type);

    if (decl->type->type == TYPE_MOD &&
        decl->type->mod.type_mod & TMOD_TYPEDEF) {
        // Don't need to typecheck decl nodes if its a typedef
        return retval;
    }
    SL_FOREACH(cur, &decl->decls) {
        retval &= typecheck_decl_node(tcs, GET_ELEM(&decl->decls, cur), type);
    }

    return retval;
}

bool typecheck_decl_node(tc_state_t *tcs, decl_node_t *decl_node,
                         type_type_t type) {
    bool retval = true;
    retval &= typecheck_type(tcs, decl_node->type);
    type_t *node_type = ast_type_untypedef(decl_node->type);
    type_t *unmod = ast_type_unmod(node_type);

    // TODO1: Incomplete types are allowed for function declarations
    if ((unmod->type == TYPE_STRUCT || unmod->type == TYPE_UNION) &&
        unmod->struct_params.esize == (size_t)-1 &&
        !(node_type->type == TYPE_MOD &&
          node_type->mod.type_mod & TMOD_EXTERN)) {
        logger_log(decl_node->mark, LOG_ERR,
                   "storage size of '%s' isn't known", decl_node->id);
        return false;
    }

    if (unmod->type == TYPE_VOID) {
        logger_log(decl_node->mark, LOG_ERR,
                   "variable or field '%s' declared void", decl_node->id);
        return false;
    }
    if ((type == TYPE_STRUCT || type == TYPE_UNION) &&
        (unmod->type == TYPE_STRUCT || unmod->type == TYPE_UNION) &&
        unmod->struct_params.esize == (size_t)-1) {
        logger_log(decl_node->mark, LOG_ERR, "field '%s' has incomplete type",
                   decl_node->id);
        return false;
    }

    if (type == TYPE_VOID && decl_node->id != NULL) {
        status_t status;
        typetab_entry_t *entry = NULL;
        type_t *type_base = node_type;
        while (type_base->type == TYPE_PTR || type_base->type == TYPE_ARR) {
            type_base = ast_type_ptr_base(type_base);
        }

        // Not a definiton if its a variable with extern, or a function
        // declaration not equal to the function we're currently in
        bool is_decl = ((type_base->type == TYPE_MOD &&
                         type_base->mod.type_mod & TMOD_EXTERN) ||
                        (node_type->type == TYPE_FUNC &&
                         (tcs->func == NULL ||
                          decl_node != sl_head(&tcs->func->decl->decls))));

        if (CCC_OK !=
            (status =
             tt_insert(tcs->typetab, node_type, TT_VAR, decl_node->id,
                       &entry))) {
            if (status != CCC_DUPLICATE) {
                logger_log(decl_node->mark, LOG_ERR,
                           "Failure inserting to type table");
                return false;
            } else {
                entry = tt_lookup(tcs->typetab, decl_node->id);

                // If existing type is extern, compare with the type
                // without extern
                type_t *cmp_type;
                if (entry->type->type == TYPE_MOD &&
                    entry->type->mod.type_mod == TMOD_EXTERN) {
                    cmp_type = entry->type->mod.base;
                } else {
                    cmp_type = entry->type;
                }

                if (cmp_type == tt_implicit_func) {
                    if (node_type->type != TYPE_FUNC ||
                        node_type->func.type->type != TYPE_INT) {
                        logger_log(decl_node->mark, LOG_ERR,
                                   "conflicting types for '%s'", decl_node->id);
                        return false;
                    }
                    entry->type = node_type;
                } else {

                    // Redefined if old entry is not a variable, last variable
                    // was already defined, and this is a definiton, or if the
                    // types are not equal
                    if (entry->entry_type != TT_VAR ||
                        (entry->var.var_defined && !is_decl) ||
                        !typecheck_type_equal(cmp_type, node_type)) {
                        logger_log(decl_node->mark, LOG_ERR,
                                   "Redefined symbol %s", decl_node->id);
                        return false;
                    }
                }
            }
        } else {
            entry->var.var_defined = !is_decl;
        }
    }
    if (decl_node->expr != NULL) {
        switch (type) {
        case TYPE_VOID:
            if (!retval) {
                return false;
            }

            switch (decl_node->expr->type) {
            case EXPR_DESIG_INIT: // This should not parse
                assert(false);
                return false;
            case EXPR_INIT_LIST:
                retval &= typecheck_init_list(tcs, node_type,
                                              decl_node->expr);
                break;
            case EXPR_CONST_STR:
                // If we're assigning to an array without a size, set the
                // array's size to string's length
                if (node_type->type == TYPE_ARR &&
                    node_type->arr.len == NULL) {
                    node_type->arr.nelems =
                        strlen(decl_node->expr->const_val.str_val) + 1;
                }
                // FALL THROUGH
            default:
                retval &= typecheck_expr(tcs, decl_node->expr, TC_NOCONST);
                retval &= typecheck_type_assignable(decl_node->mark, node_type,
                                                    decl_node->expr->etype);
            }
            break;
        case TYPE_STRUCT:
        case TYPE_UNION: {
            retval &= typecheck_expr(tcs, decl_node->expr, TC_CONST);
            if (!retval) {
                return false;
            }

            type_t *type = decl_node->expr->etype;
            type = ast_type_unmod(type);
            if (!TYPE_IS_INTEGRAL(type)) {
                logger_log(decl_node->mark, LOG_ERR,
                           "bit-field '%s' width not an integer constant",
                           decl_node->id);
                return false;
            }

            // Evaluate the size
            if (decl_node->expr->type != EXPR_CONST_INT) {
                long long size;
                typecheck_const_expr_eval(tcs->typetab, decl_node->expr, &size);
                expr_t *new_size = ast_expr_create(tcs->tunit,
                                                   decl_node->expr->mark,
                                                   EXPR_CONST_INT);
                new_size->etype = tt_int;
                new_size->const_val.type = tt_int;
                new_size->const_val.int_val = size;
                decl_node->expr = new_size;
            }

            break;
        }
        case TYPE_ENUM: {
            retval &= typecheck_expr(tcs, decl_node->expr, TC_CONST);
            if (!retval) {
                return false;
            }

            type_t *type = decl_node->expr->etype;
            type = ast_type_unmod(type);
            if (!TYPE_IS_INTEGRAL(type)) {
                logger_log(decl_node->mark, LOG_ERR,
                           "enumerator value for '%s' is not an integer "
                           "constant", decl_node->id);
                return false;
            }
            break;
        }
        default:
            assert(false);
            return false;
        }
    }
    return retval;
}

bool typecheck_expr(tc_state_t *tcs, expr_t *expr, bool constant) {
    bool retval = true;
    if (expr->etype != NULL) {
        return retval;
    }
    expr->etype = NULL;

    switch(expr->type) {
    case EXPR_VOID:
        expr->etype = tt_void;
        return retval;

    case EXPR_PAREN:
        retval &= typecheck_expr(tcs, expr->paren_base, constant);
        expr->etype = expr->paren_base->etype;
        return retval;

    case EXPR_VAR: {
        // Do nothing if we're ignoring undefined variables
        if (tcs->ignore_undef) {
            expr->etype = tt_int;
            return retval;
        }
        typetab_entry_t *entry = tt_lookup(tcs->typetab, expr->var_id);
        if (entry == NULL ||
            (entry->entry_type != TT_VAR && entry->entry_type != TT_ENUM_ID)) {
            logger_log(expr->mark, LOG_ERR, "'%s' undeclared.", expr->var_id);
            return false;
        }
        if (constant == TC_CONST && entry->entry_type == TT_VAR) {
            logger_log(expr->mark, LOG_ERR, "Expected constant value");
            return false;
        }

        if (entry->type->type == TYPE_FUNC) {
            // If we're using a function as a variable, etype is function
            // pointer
            type_t *ptr_type = ast_type_create(tcs->tunit, expr->mark,
                                               TYPE_PTR);
            ptr_type->ptr.base = entry->type;
            expr->etype = ptr_type;
        } else {
            expr->etype = entry->type;
        }
        return retval;
    }

    case EXPR_ASSIGN:
        retval &= typecheck_expr(tcs, expr->assign.dest, TC_NOCONST);
        retval &= typecheck_expr(tcs, expr->assign.expr, TC_NOCONST);
        if (!retval) {
            return false;
        }
        retval &= typecheck_expr_lvalue(tcs, expr->assign.dest);
        retval &= typecheck_type_assignable(expr->assign.dest->mark,
                                            expr->assign.dest->etype,
                                            expr->assign.expr->etype);
        if (expr->assign.op != OP_NOP) {
            retval &= typecheck_types_binop(expr->mark, expr->assign.op,
                                            expr->assign.dest->etype,
                                            expr->assign.expr->etype);
        }
        expr->etype = expr->assign.dest->etype;
        return retval;

    case EXPR_CONST_INT:
    case EXPR_CONST_FLOAT:
    case EXPR_CONST_STR:
        expr->etype = expr->const_val.type;
        return retval;

    case EXPR_BIN:
        retval &= typecheck_expr(tcs, expr->bin.expr1, TC_NOCONST);
        retval &= typecheck_expr(tcs, expr->bin.expr2, TC_NOCONST);
        if (!retval) {
            return false;
        }
        retval &= typecheck_types_binop(expr->mark, expr->bin.op,
                                        expr->bin.expr1->etype,
                                        expr->bin.expr2->etype);
        type_t *umod1 = ast_type_unmod(expr->bin.expr1->etype);
        type_t *umod2 = ast_type_unmod(expr->bin.expr2->etype);
        switch (expr->bin.op) {
        case OP_LT:
        case OP_GT:
        case OP_LE:
        case OP_GE:
        case OP_EQ:
        case OP_NE:
        case OP_LOGICAND:
        case OP_LOGICOR:
            // C11 S 6.8.5
            expr->etype = tt_int;
            break;
        case OP_MINUS:
            // subtracting pointers evaluates to size_t
            if (umod1->type == TYPE_PTR && umod2->type == TYPE_PTR) {
                expr->etype = tt_size_t;
                break;
            }
            break;
        case OP_PLUS: {
            type_t *ptr_type = NULL;
            if (TYPE_IS_PTR(umod1) && TYPE_IS_INTEGRAL(umod2)) {
                ptr_type = umod1;
            } else if (TYPE_IS_PTR(umod2) && TYPE_IS_INTEGRAL(umod1)) {
                ptr_type = umod2;
            }
            if (ptr_type != NULL) {
                if (ptr_type->type == TYPE_PTR) {
                    expr->etype = ptr_type;
                } else {
                    type_t *new_ptr = ast_type_create(tcs->tunit,
                                                      ptr_type->mark,
                                                      TYPE_PTR);
                    new_ptr->ptr.base = ast_type_ptr_base(ptr_type);
                    expr->etype = new_ptr;
                }
            }
            break;
        }
        default:
            break;
        }
        if (expr->etype == NULL) {
            type_t *etype;
            retval &= typecheck_type_max(tcs->tunit, expr->mark,
                                         expr->bin.expr1->etype,
                                         expr->bin.expr2->etype,
                                         &etype);
            // Integers are the minimum size for arithmetic operators
            if (TYPE_IS_INTEGRAL(etype) && etype->type < TYPE_INT) {
                etype = tt_int;
            }
            expr->etype = etype;
        }
        return retval;

    case EXPR_UNARY:
        if (!typecheck_expr(tcs, expr->unary.expr, TC_NOCONST)) {
            return false;
        }
        if (!typecheck_type_unaryop(expr->mark, expr->unary.op,
                                    expr->unary.expr->etype)) {
            return false;
        }
        switch (expr->unary.op) {
        case OP_ADDR:
            // We can take address of compound literals
            if (!(expr->unary.expr->type == EXPR_CAST &&
                  expr->unary.expr->cast.base->type == EXPR_INIT_LIST) &&
                !typecheck_expr_lvalue(tcs, expr->unary.expr)) {
                return false;
            }

            // Address of function pointer is the same thing
            if (expr->unary.expr->type == EXPR_VAR &&
                expr->unary.expr->etype->type == TYPE_PTR &&
                expr->unary.expr->etype->ptr.base->type == TYPE_FUNC) {
                expr->etype = expr->unary.expr->etype;
                break;
            }

            expr->etype = emalloc(sizeof(type_t));
            // If we have a translation unit, save etypes on it because we'll
            // need them later
            if (tcs->tunit != NULL) {
                sl_append(&tcs->tunit->types, &expr->etype->heap_link);
            } else {
                sl_append(&tcs->etypes, &expr->etype->heap_link);
            }
            expr->etype->mark = expr->mark;

            expr->etype->type = TYPE_PTR;
            expr->etype->ptr.type_mod = TMOD_NONE;
            expr->etype->ptr.base = expr->unary.expr->etype;
            break;
        case OP_DEREF: {
            type_t *ptr_type = ast_type_unmod(expr->unary.expr->etype);
            if (!TYPE_IS_PTR(ptr_type)) {
                logger_log(expr->unary.expr->mark, LOG_ERR,
                           "invalid type argument of unary '*'");
                return false;
            }
            type_t *unmod = ast_type_unmod(ast_type_ptr_base(ptr_type));
            if (unmod->type == TYPE_VOID) {
                logger_log(expr->mark, LOG_WARN,
                           "dereferencing a 'void *' pointer");
            }
            if ((unmod->type == TYPE_STRUCT || unmod->type == TYPE_VOID) &&
                unmod->struct_params.esize == (size_t)-1) {
                logger_log(expr->mark, LOG_ERR,
                           "dereferencing pointer to incomplete type");
                retval = false;
            }
            expr->etype = unmod;
            break;
        }
        case OP_LOGICNOT:
            expr->etype = tt_bool;
            break;
        case OP_UMINUS:
        case OP_UPLUS:
        case OP_BITNOT:
            expr->etype = expr->unary.expr->etype;
            // Integers are the minimum size for arithmetic operators
            if (TYPE_IS_INTEGRAL(expr->etype) && expr->etype->type < TYPE_INT) {
                expr->etype = tt_int;
            }
            break;
        default:
            expr->etype = expr->unary.expr->etype;
        }
        return retval;

    case EXPR_COND:
        retval &= typecheck_expr_conditional(tcs, expr->cond.expr1);
        retval &= typecheck_expr(tcs, expr->cond.expr2, TC_NOCONST);
        retval &= typecheck_expr(tcs, expr->cond.expr3, TC_NOCONST);
        if (!retval) {
            return false;
        }
        if (ast_type_unmod(expr->cond.expr2->etype)->type == TYPE_VOID ||
            ast_type_unmod(expr->cond.expr3->etype)->type == TYPE_VOID) {
            expr->etype = tt_void;
        } else {
            retval &= typecheck_type_max(tcs->tunit, expr->mark,
                                         expr->cond.expr2->etype,
                                         expr->cond.expr3->etype,
                                         &expr->etype);
        }
        return retval;

    case EXPR_CAST: {
        type_t *cast_type = DECL_TYPE(expr->cast.cast);

        if (expr->cast.base->type == EXPR_INIT_LIST) {
            // Compound literal
            retval &= typecheck_init_list(tcs, cast_type, expr->cast.base);
        } else {
            if (!typecheck_expr(tcs, expr->cast.base, TC_NOCONST)) {
                return false;
            }
            retval &= typecheck_type_cast(DECL_MARK(expr->cast.cast), cast_type,
                                          expr->cast.base->etype);
        }
        expr->etype = cast_type;
        return retval;
    }

    case EXPR_CALL: {
        expr_t *func_expr = expr->call.func;

        if (func_expr->type == EXPR_VAR &&
            tt_lookup(tcs->typetab, func_expr->var_id) == NULL) {
            logger_log(expr->mark, LOG_WARN,
                       "implicit declaration of function '%s'",
                       func_expr->var_id);

            typetab_t *tt_save = tcs->typetab;
            tcs->typetab = &tcs->tunit->typetab;
            status_t status = tt_insert(tcs->typetab, tt_implicit_func, TT_VAR,
                                        func_expr->var_id, NULL);
            assert(status == CCC_OK);
            tcs->typetab = tt_save;
            func_expr->etype = tt_implicit_func_ptr;
        } else if (!(retval &= typecheck_expr(tcs, func_expr, TC_NOCONST))) {
            return false;
        }

        type_t *func_sig = ast_type_unmod(func_expr->etype);
        if (func_sig->type == TYPE_PTR) {
            func_sig = ast_type_unmod(func_sig->ptr.base);
        }
        if (func_sig->type != TYPE_FUNC) {
            logger_log(expr->mark, LOG_ERR,
                       "called object is not a function or function pointer");
            return false;
        }
        int arg_num = 1;
        sl_link_t *cur_sig, *cur_arg;
        cur_sig = func_sig->func.params.head;
        cur_arg = expr->call.params.head;
        while (cur_sig != NULL && cur_arg != NULL) {
            decl_t *decl = GET_ELEM(&func_sig->func.params, cur_sig);
            decl_node_t *param = sl_head(&decl->decls);
            expr_t *arg = GET_ELEM(&expr->call.params, cur_arg);
            retval &= typecheck_expr(tcs, arg, TC_NOCONST);
            type_t *param_type = param == NULL ? decl->type : param->type;
            if (arg->etype != NULL &&
                !typecheck_type_assignable(NULL, param_type,
                                           arg->etype)) {
                logger_log(arg->mark, LOG_ERR,
                           "incompatible type for argument %d of function",
                           arg_num);
                return false;
            }

            ++arg_num;
            cur_sig = cur_sig->next;
            cur_arg = cur_arg->next;
        }
        if (cur_sig != NULL) {
            decl_t *decl = GET_ELEM(&func_sig->func.params, cur_sig);
            decl_node_t *param = sl_head(&decl->decls);

            // Only report error if parameter isn't (void)
            if (!(arg_num == 1 && param == NULL &&
                  decl->type->type == TYPE_VOID)) {
                logger_log(expr->mark, LOG_ERR,
                           "too few arguments to function");
                retval = false;
            }
        }
        if (cur_arg != NULL) {
            if (func_sig->func.varargs) {
                while (cur_arg != NULL) {
                    expr_t *arg = GET_ELEM(&expr->call.params, cur_arg);
                    retval &= typecheck_expr(tcs, arg, TC_NOCONST);
                    cur_arg = cur_arg->next;
                }
            } else if (sl_head(&func_sig->func.params) != NULL) {
                // Don't report error for () K & R style decls
                logger_log(expr->mark, LOG_ERR,
                           "too many arguments to function");
                retval = false;
            }
        }
        expr->etype = func_sig->func.type;
        return retval;
    }

    case EXPR_CMPD: {
        SL_FOREACH(cur, &expr->cmpd.exprs) {
            retval &= typecheck_expr(tcs, GET_ELEM(&expr->cmpd.exprs, cur),
                                     TC_NOCONST);
        }
        expr_t *tail = sl_tail(&expr->cmpd.exprs);
        expr->etype = tail->etype;
        return retval;
    }

    case EXPR_SIZEOF:
    case EXPR_ALIGNOF: {
        type_t *type = expr->sizeof_params.type == NULL ? NULL :
            DECL_TYPE(expr->sizeof_params.type);

        if (type != NULL && type->type == TYPE_TYPEDEF &&
            type->typedef_params.type == TYPE_VOID) {
            // If type went out of scope, replace it with variable
            typetab_entry_t *entry =
                tt_lookup(tcs->typetab, type->typedef_params.name);
            if (entry->entry_type != TT_TYPEDEF) {
                expr_t *var_expr = ast_expr_create(tcs->tunit, expr->mark,
                                                   EXPR_VAR);
                var_expr->var_id = type->typedef_params.name;
                expr->sizeof_params.type = NULL;
                expr->sizeof_params.expr = var_expr;
                type = NULL;
            }
        }
        if (type != NULL) {
            type_t *old_type = type;
            type = ast_type_unmod(type);
            // If there are no typedefs/modifiers, must be a raw type in
            // the expression, so typecheck it
            if (old_type == type) {
                if (!typecheck_type(tcs, type)) {
                    return false;
                }
            }
            if (type->type == TYPE_STRUCT || type->type == TYPE_UNION) {
                // If the type hasn't been defined yet, then return an error
                if (type->struct_params.esize == (size_t)-1) {
                    logger_log(expr->mark, LOG_ERR,
                               "invalid application to incomplete type");
                    return false;
                }
            }
            retval &= typecheck_decl(tcs, expr->sizeof_params.type, TYPE_VOID);
        }
        if (expr->sizeof_params.expr != NULL) {
            retval &= typecheck_expr(tcs, expr->sizeof_params.expr, TC_NOCONST);
        }
        expr->etype = tt_size_t;
        return retval;
    }

    case EXPR_OFFSETOF:
        retval &= typecheck_decl(tcs, expr->offsetof_params.type, TYPE_VOID);
        decl_node_t *decl = sl_head(&expr->offsetof_params.type->decls);
        type_t *compound = decl == NULL ?
            expr->offsetof_params.type->type : decl->type;
        retval &= typecheck_designator_list(tcs, compound,
                                            &expr->offsetof_params.list);
        expr->etype = tt_size_t;
        return true;

    case EXPR_MEM_ACC: {
        if (!typecheck_expr(tcs, expr->mem_acc.base, TC_NOCONST)) {
            return false;
        }
        type_t *compound = ast_type_unmod(expr->mem_acc.base->etype);
        switch (compound->type) {
        case TYPE_STRUCT:
        case TYPE_UNION:
            if (expr->mem_acc.op != OP_DOT) {
                logger_log(expr->mark, LOG_ERR,
                           "invalid type argument of '->'");
                return false;
            }
            break;
        case TYPE_PTR:
            if (expr->mem_acc.op == OP_ARROW) {
                compound = ast_type_unmod(compound->ptr.base);
                if (compound->type == TYPE_STRUCT ||
                    compound->type == TYPE_UNION) {
                    break;
                }
            }
            // FALL THROUGH
        default:
            logger_log(expr->mark, LOG_ERR,
                       "request for member '%s' in something not a structure "
                       "or union", expr->mem_acc.name);
            return false;
        }
        if (compound->struct_params.esize == (size_t)-1) {
            logger_log(expr->mark, LOG_ERR,
                       "dereferencing pointer to incomplete type");
            return false;
        }
        decl_node_t *mem_node = ast_type_find_member(compound,
                                                     expr->mem_acc.name, NULL);

        if (mem_node != NULL) {
            ast_canonicalize_mem_acc(tcs->tunit, expr,
                                     expr->mem_acc.base->etype, NULL);
            expr->etype = mem_node->type;
            return true;
        }
        logger_log(expr->mark, LOG_ERR, "compound type has no member '%s'",
                   expr->mem_acc.name);
        return false;
    }

    case EXPR_ARR_IDX: {
        retval &= typecheck_expr(tcs, expr->arr_idx.array, TC_NOCONST);
        retval &= typecheck_expr(tcs, expr->arr_idx.index, TC_NOCONST);
        if (!retval) {
            return false;
        }
        type_t *umod_arr = ast_type_unmod(expr->arr_idx.array->etype);
        type_t *umod_index = ast_type_unmod(expr->arr_idx.index->etype);

        if (umod_arr->type != TYPE_PTR && umod_arr->type != TYPE_ARR) {
            logger_log(expr->arr_idx.array->mark, LOG_ERR,
                       "subscripted value is neither array nor pointer nor"
                       " vector");
            retval = false;
        }
        if (!TYPE_IS_INTEGRAL(umod_index)) {
            logger_log(expr->arr_idx.index->mark, LOG_ERR,
                       "array subscript is not an integer");
            retval = false;
        }

        if (umod_arr->type == TYPE_PTR) {
            expr->etype = umod_arr->ptr.base;
        } else { // umod_arr->type == TYPE_ARR
            expr->etype = umod_arr->arr.base;
        }
        return retval;
    }

    case EXPR_INIT_LIST: {
        VEC_FOREACH(cur, &expr->init_list.exprs) {
            retval &= typecheck_expr(tcs, vec_get(&expr->init_list.exprs, cur),
                                     TC_NOCONST);
        }
        // Don't know what etype is
        return retval;
    }

    case EXPR_DESIG_INIT:
        retval &= typecheck_expr(tcs, expr->desig_init.val, TC_NOCONST);
        // Don't know what etype is
        return retval;

    case EXPR_VA_START: {
        retval &= typecheck_expr_va_list(tcs, expr->vastart.ap);
        gdecl_t *func = tcs->func;
        bool failed = true;
        if (func != NULL && expr->vastart.last->type == EXPR_VAR) {
            decl_node_t *fun_decl = sl_head(&func->decl->decls);
            assert(fun_decl != NULL);
            type_t *fun_type = fun_decl->type;
            assert(fun_type->type == TYPE_FUNC);
            decl_t *last_param = sl_tail(&fun_type->func.params);
            if (last_param != NULL) {
                decl_node_t *last_param_node = sl_tail(&last_param->decls);
                assert(last_param_node != NULL);
                if (strcmp(expr->vastart.last->var_id, last_param_node->id) ==
                    0) {
                    failed = false;
                }
            }
        }
        if (failed) {
            logger_log(expr->vastart.last->mark, LOG_ERR,
                       "Expected function parameter name");
            retval = false;
        }
        expr->etype = tt_void;
        return retval;
    }
    case EXPR_VA_ARG: {
        retval &= typecheck_expr_va_list(tcs, expr->vaarg.ap);
        retval &= typecheck_decl(tcs, expr->vaarg.type, TC_NOCONST);
        decl_node_t *node = sl_head(&expr->vaarg.type->decls);
        if (node != NULL) {
            expr->etype = node->type;
        } else {
            expr->etype = expr->vaarg.type->type;
        }
        return retval;
    }
    case EXPR_VA_END:
        retval &= typecheck_expr_va_list(tcs, expr->vaend.ap);
        expr->etype = tt_void;
        return retval;
    case EXPR_VA_COPY:
        retval &= typecheck_expr_va_list(tcs, expr->vacopy.dest);
        retval &= typecheck_expr_va_list(tcs, expr->vacopy.src);
        expr->etype = tt_void;
        return retval;
    default:
        retval = false;
        assert(false);
    }

    return retval;
}

bool typecheck_type(tc_state_t *tcs, type_t *type) {
    if (type->typechecked) {
        return true;
    }
    type->typechecked = true;
    bool retval = true;

    switch(type->type) {
    case TYPE_VOID:
    case TYPE_BOOL:
    case TYPE_CHAR:
    case TYPE_SHORT:
    case TYPE_INT:
    case TYPE_LONG:
    case TYPE_LONG_LONG:
    case TYPE_FLOAT:
    case TYPE_DOUBLE:
    case TYPE_LONG_DOUBLE:
    case TYPE_VA_LIST:
        // Primitive types always type check
        return retval;

    case TYPE_STRUCT:
    case TYPE_UNION: {
        // If size exists, already typechecked
        if (type->struct_params.esize != (size_t)-1) {
            return true;
        }
        SL_FOREACH(cur, &type->struct_params.decls) {
            retval &= typecheck_decl(tcs,
                                     GET_ELEM(&type->struct_params.decls, cur),
                                     type->type);
        }

        if (!retval) {
            return false;
        }

        // Make sure there are no duplicate names
        struct_iter_t cur, check;
        struct_iter_init(type, &cur);

        do {
            // Skip anonymous members
            if (cur.node == NULL || cur.node->id == NULL) {
                continue;
            }

            memcpy(&check, &cur, sizeof(cur));

            while (struct_iter_advance(&check)) {
                if (check.node != NULL) {
                    if (check.node->id != NULL &&
                        strcmp(cur.node->id, check.node->id) == 0) {
                        logger_log(check.node->mark, LOG_ERR,
                                   "duplicate member '%s'", check.node->id);
                        retval = false;
                    }
                } else {
                    // Check anonymous struct/unions
                    assert(check.decl != NULL);
                    if (ast_type_find_member(check.decl->type, cur.node->id,
                                             NULL)) {
                        logger_log(cur.node->mark, LOG_ERR,
                                   "duplicate member '%s'", cur.node->id);
                        retval = false;
                    }
                }
            }
        } while (struct_iter_advance(&cur));

        ast_type_size(type); // Take size to mark as being a complete type
        return retval;
    }
    case TYPE_ENUM: {
        retval &= typecheck_type(tcs, type->enum_params.type);
        long long next_val = 0;
        SL_FOREACH(cur, &type->enum_params.ids) {
            decl_node_t *node = GET_ELEM(&type->enum_params.ids, cur);
            retval &= typecheck_decl_node(tcs, node, TYPE_ENUM);
            typetab_entry_t *entry;
            status_t status;
            if (CCC_OK !=
                (status =
                 tt_insert(tcs->typetab, type->enum_params.type, TT_ENUM_ID,
                           node->id, &entry))) {
                if (status == CCC_DUPLICATE) {
                    logger_log(node->mark, LOG_ERR, "Redefined symbol %s",
                               node->id);
                }
                return false;
            }
            long long cur_val;
            if (node->expr != NULL) {
                typecheck_const_expr_eval(tcs->typetab, node->expr, &cur_val);
                entry->enum_val = cur_val;
                next_val = cur_val + 1;
            } else {
                entry->enum_val = next_val++;
            }
        }
        return retval;
    }

    case TYPE_TYPEDEF:
        if (type->typedef_params.type == TYPE_VOID) {
            // Make sure the typedef is still in scope
            typetab_entry_t *entry =
                tt_lookup(tcs->typetab, type->typedef_params.name);
            if (entry == NULL || entry->entry_type != TT_TYPEDEF) {
                logger_log(type->mark, LOG_ERR, "unexpected identifier '%s'",
                           type->typedef_params.name);
                return false;
            }
        }

        if (type->typedef_params.type == TYPE_VOID) {
            retval &= typecheck_type(tcs, type->typedef_params.base);
        }
        return retval;

    case TYPE_MOD:
        // If there's a modifier without a base, then assume its int
        if (type->mod.base == NULL) {
            type->mod.base = tt_int;
        }
        retval &= typecheck_type(tcs, type->mod.base);
        if (type->mod.type_mod & TMOD_SIGNED &&
            type->mod.type_mod & TMOD_UNSIGNED) {
            logger_log(type->mark, LOG_ERR,
                       "both 'signed' and 'unsigned' in declaration"
                       " specifiers");
            retval = false;
        }
        switch (type->mod.type_mod &
                (TMOD_AUTO | TMOD_REGISTER | TMOD_STATIC | TMOD_EXTERN)) {
        case TMOD_NONE:
        case TMOD_AUTO:
        case TMOD_REGISTER:
        case TMOD_STATIC:
        case TMOD_EXTERN:
            break;
        default:
            logger_log(type->mark, LOG_ERR,
                       "multiple storage classes in declaration specifiers");
            retval = false;
        }

        if (type->mod.type_mod & TMOD_ALIGNAS) {
            if (type->mod.alignas_type != NULL) {
                assert(type->mod.alignas_expr == NULL);
                type_t *align_type = DECL_TYPE(type->mod.alignas_type);
                type->mod.alignas_align = ast_type_align(align_type);
            } else {
                if (!typecheck_expr(tcs, type->mod.alignas_expr, TC_CONST)) {
                    logger_log(type->mod.alignas_expr->mark, LOG_ERR,
                               "requested alignment is not an integer"
                               " constant");
                    return false;
                }
                long long val;
                typecheck_const_expr_eval(tcs->typetab, type->mod.alignas_expr,
                                          &val);
                if (val < 0 || (val & (val - 1))) {
                    logger_log(type->mod.alignas_expr->mark, LOG_ERR,
                               "requested alignment is not a positive power of"
                               " 2");
                    return false;
                }
                type->mod.alignas_align = val;
            }
        }

        return retval;

    case TYPE_PAREN:
        return typecheck_type(tcs, type->paren_base);
    case TYPE_FUNC:
        retval &= typecheck_type(tcs, type->func.type);

        typetab_t *save_tab = NULL;
        if (tcs->func != NULL) {
            decl_node_t *func_node = sl_head(&tcs->func->decl->decls);
            if (func_node->type == type) {
                // Make sure to enter the scope of the function's body before
                // typechecking the decl so that the variables are added to the
                // correct scope
                save_tab = tcs->typetab;
                assert(tcs->func->fdefn.stmt->type = STMT_COMPOUND);
                tcs->typetab = &tcs->func->fdefn.stmt->compound.typetab;
            }
        }

        bool void_typed = false;

        // If this is only a function declaration, we don't want to add the
        // parameters to any symbol table
        type_type_t decl_type = save_tab == NULL ? TYPE_FUNC : TYPE_VOID;
        SL_FOREACH(cur, &type->func.params) {
            decl_t *decl = GET_ELEM(&type->func.params, cur);
            decl_node_t *node = sl_head(&decl->decls);
            assert(node == sl_tail(&decl->decls));

            if (node == NULL && decl->type->type == TYPE_VOID) {
                void_typed = true;
                break;
            }

            // Make sure param names are not duplicated
            if (node != NULL && node->id != NULL) {
                SL_FOREACH(cur, &type->func.params) {
                    decl_t *cur_decl = GET_ELEM(&type->func.params, cur);
                    decl_node_t *cur_node = sl_head(&cur_decl->decls);
                    if (cur_node == node) {
                        break;
                    }
                    if (cur_node == NULL || cur_node->id == NULL) {
                        continue;
                    }
                    if (strcmp(node->id, cur_node->id) == 0) {
                        logger_log(node->mark, LOG_ERR,
                                   "redefinition of parameter '%s'", node->id);
                        logger_log(cur_node->mark, LOG_NOTE,
                                   "previous definition of '%s' was here",
                                   node->id);
                        retval = false;
                    }
                }
            }

            // If a paramater is a function, convert it to a function pointer
            if (node != NULL && node->type->type == TYPE_FUNC) {
                type_t *ptr_type = ast_type_create(tcs->tunit,
                                                   node->type->mark, TYPE_PTR);
                ptr_type->ptr.base = node->type;
                node->type = ptr_type;
            }

            retval &= typecheck_decl(tcs, decl, decl_type);
        }

        if (void_typed) {
            if (sl_head(&type->func.params) != sl_tail(&type->func.params)) {
                logger_log(type->mark, LOG_ERR,
                           "'void' must be the only parameter");
                retval = false;
            } else {
                // If void typed, remove the paramaters
                sl_clear(&type->func.params);
            }
        }

        if (save_tab != NULL) {
            tcs->typetab = save_tab;
        }
        return retval;
    case TYPE_ARR:
        retval &= typecheck_type(tcs, type->arr.base);
        if (type->arr.len != NULL) {
            retval &= typecheck_expr(tcs, type->arr.len, TC_CONST);
            long long nelems;
            typecheck_const_expr_eval(tcs->typetab, type->arr.len,
                                      &nelems);
            if (nelems < 0) {
                logger_log(type->arr.len->mark, LOG_ERR,
                           "size of array is negative");
                retval = false;
            }
            type->arr.nelems = nelems;
        }
        return retval;
    case TYPE_PTR:
        return typecheck_type(tcs, type->ptr.base);

    case TYPE_STATIC_ASSERT: {
        if (!typecheck_expr(tcs, type->sa_params.expr, TC_CONST)) {
            logger_log(type->sa_params.expr->mark, LOG_ERR,
                       "expression in static assertion is not constant");
            return false;
        }
        long long value;
        typecheck_const_expr_eval(tcs->typetab, type->sa_params.expr, &value);
        if (value == 0) {
            logger_log(type->mark, LOG_ERR,
                       "static assertion failed: \"%s\"", type->sa_params.msg);
            return false;
        }
        return retval;
    }

    default:
        assert(false);
    }

    return retval;
}
