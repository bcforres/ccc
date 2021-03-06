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
 * Vector implementation
 */

#include "vector.h"

#include "util/util.h"

#define MIN_SIZE 4

// 1.5 growth factor
#define NEW_SIZE(size) MAX((size) + ((size) >> 1), MIN_SIZE)

extern void *vec_get(vec_t *vec, size_t idx);
extern void vec_set(vec_t *vec, size_t idx, void *val);
extern void **vec_elems(vec_t *vec);
extern size_t vec_size(const vec_t *vec);
extern size_t vec_capacity(const vec_t *vec);
extern void *vec_front(vec_t *vec);
extern void *vec_back(vec_t *vec);
extern void *vec_pop_back(vec_t *vec);
extern bool vec_iter_has_next(vec_iter_t *iter);
extern void *vec_iter_get(vec_iter_t *iter);
extern void *vec_iter_advance(vec_iter_t *iter);
extern void *vec_iter_reverse(vec_iter_t *iter);

void vec_init(vec_t *vec, size_t capacity) {
    vec->elems = emalloc(capacity * sizeof(vec->elems[0]));
    vec->size = 0;
    vec->capacity = capacity;
}

void vec_move(vec_t *dest, vec_t *src, bool destroy_dest) {
    if (destroy_dest) {
        vec_destroy(dest);
    }
    memcpy(dest, src, sizeof(vec_t));
    src->elems = NULL;
    src->size = 0;
    src->capacity = 0;
}

void vec_destroy(vec_t *vec) {
    free(vec->elems);
    vec->size = 0;
    vec->capacity = 0;
}

void vec_reserve(vec_t *vec, size_t size) {
    if (vec->capacity > size) {
        return;
    }

    vec->capacity = size;
    vec->elems = erealloc(vec->elems, vec->capacity * sizeof(vec->elems[0]));
}

void vec_push_back(vec_t *vec, void *elem) {
    if (vec->size == vec->capacity) {
        vec->capacity = NEW_SIZE(vec->capacity);
        vec->elems =
            erealloc(vec->elems, vec->capacity * sizeof(vec->elems[0]));
    }
    vec->elems[vec->size++] = elem;
}

void vec_resize(vec_t *vec, size_t size) {
    vec_reserve(vec, size);
    vec->size = size;
}

void vec_append_vec(vec_t *dest, vec_t *vec2) {
    VEC_FOREACH(cur, vec2) {
        vec_push_back(dest, vec_get(vec2, cur));
    }
}

void vec_iter_init(vec_iter_t *iter, vec_t *vec) {
    iter->vec = vec;
    iter->off = 0;
}
