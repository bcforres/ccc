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
 * Hashtable implementation
 */
#include "hashtable.h"

#include <string.h>
#include <stdlib.h>

#include "util/util.h"

/** .75 load factor */
#define MAX_LOAD(ht) (((ht->nbuckets) >> 1) + ((ht->nbuckets) >> 2))

/** 1.5 growth rate */
#define NEW_SIZE(ht) ((ht->nbuckets) + (ht->nbuckets >> 2))

/** Minimum number of hash buckets */
#define MIN_BUCKETS ((size_t)16)

/** Pointer to element represented by head */
#define GET_ELEM(ht, head) \
    ((void *)(head) - ht->params.head_offset)

/** Pointer to key of element represented by head */
#define GET_KEY(ht, head) \
    (GET_ELEM(ht, head) + ht->params.key_offset)

/** Gets hash of a key */
#define GET_HASH(ht, key) \
    (ht->params.hashfunc((key), ht->params.key_len))


status_t ht_init(ht_table *ht, ht_params *params) {
    memcpy(&ht->params, params, sizeof(ht_params));
    ht->nbuckets = MAX(params->nelems, MIN_BUCKETS);
    ht->params.nelems = 0;

    // Initialize to NULL
    ht->buckets = calloc(ht->nbuckets, sizeof(ht_params *));
    return ht->buckets == NULL ? CCC_NOMEM : CCC_OK;
}

void ht_destroy(ht_table *ht) {
    // Free each chained list
    for (size_t i = 0; i < ht->nbuckets; ++i) {
        ht_link *cur = ht->buckets[i];
        ht_link *next = NULL;

        while(cur != NULL) {
            next = cur->next;
            free(GET_ELEM(ht, cur));
            cur = next;
        }
    }

    // Free the list of buckets
    free(ht->buckets);
}

/**
 * Grows the specified hashtable
 *
 * @param ht The hashtable to grow
 */
static status_t grow_ht(ht_table *ht) {
    size_t new_nbuckets = NEW_SIZE(ht);
    ht_link **new_buckets = calloc(new_nbuckets, sizeof(ht_link *));

    // Iterate through current hashtable and add all elems to new bucketlist
    for (size_t i = 0; i < ht->nbuckets; ++i) {

        ht_link *next = NULL;
        for (ht_link *cur = ht->buckets[i]; cur != NULL; cur = next) {
            next = cur->next;

            // Add cur to new bucket list
            void *key = GET_KEY(ht, cur);
            uint32_t bucket = GET_HASH(ht, key) % new_nbuckets;

            ht_link **new_cur = &new_buckets[bucket];
            for(; *new_cur != NULL; cur = (*new_cur)->next)
                continue;

            cur->next = NULL;
            *new_cur = cur;
        }
    }

    // Free old bucket list and update params
    free(ht->buckets);
    ht->buckets = new_buckets;
    ht->nbuckets = new_nbuckets;

    return CCC_OK;
}

status_t ht_insert(ht_table *ht, ht_link *elem) {
    // Grow the HT if necessary
    if (ht->params.nelems >= MAX_LOAD(ht)) {
        status_t status = grow_ht(ht);
        if (CCC_OK != status) {
            return status;
        }
    }

    ++ht->params.nelems;
    void *key = GET_KEY(ht, elem);
    uint32_t bucket = GET_HASH(ht, key) % ht->nbuckets;

    ht_link **cur = &ht->buckets[bucket];
    for(; *cur != NULL; *cur = (*cur)->next) {
        void *key2 = GET_KEY(ht, *cur);

        if (!ht->params.cmpfunc(key, key2, ht->params.key_len)) {
            // Different keys
            continue;
        }

        // Found a match
        elem->next = (*cur)->next;
        free(GET_ELEM(ht, *cur));
        *cur = elem;
        return CCC_OK;
    }

    elem->next = NULL;
    *cur = elem;
    return CCC_OK;
}

/**
 * Lookup helper function
 *
 * @param ht Hashtable to lookup in
 * @param key Key to lookup
 * @return Returns pointer to pointer of found element in hashtable chain,
 * NULL if doesn't exist
 */
static ht_link **ht_lookup_helper(ht_table *ht, const void *key) {
    uint32_t bucket = GET_HASH(ht, key) % ht->nbuckets;

    ht_link **cur = &ht->buckets[bucket];
    for (; *cur != NULL; *cur = (*cur)->next) {
        void *key2 = GET_KEY(ht, *cur);

        if (ht->params.cmpfunc(key, key2, ht->params.key_len)) {
            return cur;
        }
    }
    return NULL;
}

bool ht_remove(ht_table *ht, const void *key) {
    ht_link **pp_link = ht_lookup_helper(ht, key);
    if (pp_link == NULL) {
        return false;
    }

    ht_link *link = *pp_link;
    *pp_link = (*pp_link)->next;
    free(GET_ELEM(ht, link));
    return true;
}

void *ht_lookup(const ht_table *ht, const void *key) {
    ht_link **pp_link = ht_lookup_helper((ht_table *)ht, key);
    if (pp_link == NULL) {
        return NULL;
    }

    ht_link *link = *pp_link;
    return GET_ELEM(ht, link);
}
