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
 * Hashtable Interface
 *
 * Some interface ideas from here: http://lwn.net/Articles/612100/
 */

#ifndef _HASHTABLE_H_
#define _HASHTABLE_H_

#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>

typedef uint32_t (*ht_hashfunc)(const void *key, size_t len);
typedef bool (*ht_cmpfunc)(const void *key1, size_t len1, const void *key2,
                           size_t len2);

/**
 * Single Linked Link hash table elements.
 *
 * This should be a member of the values stored in the hashtable. This is
 * preferred over the link pointing to the object stored to avoid an extra
 * pointer indirection.
 */
typedef struct ht_link {
    struct ht_link *next; /**< The next element */
} ht_link;

/**
 * Paramaters for initializing a hashtable
 */
typedef struct ht_params {
    size_t nelem_hint;    /**< Hint for number of elements */
    size_t key_len;       /**< Length of a key. 0 for unused */
    size_t key_offset;    /**< Offset of the key in the struct */
    size_t head_offset;   /**< Offset of the ht_link into the struct */
    ht_hashfunc hashfunc; /**< Hash function to use */
    ht_cmpfunc cmpfunc;   /**< Comparison function to use */
} ht_params;

/**
 * The hash table structure. Basic chained buckets.
 */
typedef struct ht_table {
    ht_params params; /**< Paramaters used to initialize the hashtable */
    ht_link *buckets; /**< The bucket array */
    size_t n_buckets; /**< Current number of buckets */
} ht_table;

/**
 * Initialize given hashtable with params
 *
 * @param ht The hashtable to initialize
 * @param params paramaters
 * @return CCC_OK on success, relevant error code on failure
 */
status_t ht_init(ht_table *ht, ht_params *params);

/**
 * Destroys given hashtable
 *
 * @param ht The hashtable to destroy
 */
void ht_destroy(ht_table *ht);

/**
 * Insert element with specified link into hashtable
 *
 * @param ht The hashtable to insert into
 * @param elem The element to insert
 * @return CCC_OK on success, relevant error code on failure
 */
status_t ht_insert(ht_table *ht, ht_link *elem);

/**
 * Remove specified element from hashtable
 *
 * @param ht The hashtable to remove from
 * @param key Key of element to remove
 * @return true if removed, false otherwise
 */
bool ht_remove(ht_table *ht, const void *key);

/**
 * Lookup specified element in hashtable
 *
 * @param ht The hashtable to retrieve from
 * @param key Key of element to retrieve
 * @return A pointer to the element in the hashtable. NULL otherwises
 */
void *ht_lookup(const ht_table, const void *key);

#endif /* _HASHTABLE_H_ */
