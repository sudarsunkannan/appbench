/* Hash Tables Implementation.
 *
 * This file implements in memory hash tables with insert/del/replace/find/
 * get-random-element operations. Hash tables will auto resize if needed
 * tables of power of two in size are used, collisions are handled by
 * chaining. See the source code for more information... :)
 *
 * Copyright (c) 2006-Present, Redis Ltd.
 * All rights reserved.
 *
 * Licensed under your choice of (a) the Redis Source Available License 2.0
 * (RSALv2); or (b) the Server Side Public License v1 (SSPLv1); or (c) the
 * GNU Affero General Public License v3 (AGPLv3).
 */

#include "fmacros.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdarg.h>
#include <limits.h>
#include <sys/time.h>
#include <stddef.h>

#include "dict.h"
#include "zmalloc.h"
#include "redisassert.h"
#include "monotonic.h"
#include "util.h"

/* Using dictSetResizeEnabled() we make possible to disable
 * resizing and rehashing of the hash table as needed. This is very important
 * for Redis, as we use copy-on-write and don't want to move too much memory
 * around when there is a child performing saving operations.
 *
 * Note that even when dict_can_resize is set to DICT_RESIZE_AVOID, not all
 * resizes are prevented:
 *  - A hash table is still allowed to expand if the ratio between the number
 *    of elements and the buckets >= dict_force_resize_ratio.
 *  - A hash table is still allowed to shrink if the ratio between the number
 *    of elements and the buckets <= 1 / (HASHTABLE_MIN_FILL * dict_force_resize_ratio). */
static dictResizeEnable dict_can_resize = DICT_RESIZE_ENABLE;
static unsigned int dict_force_resize_ratio = 4;

/* -------------------------- types ----------------------------------------- */
struct dictEntry {
    struct dictEntry *next;  /* Must be first */
    void *key;               /* Must be second */
    union {
        void *val;
        uint64_t u64;
        int64_t s64;
        double d;
    } v;
};

typedef struct dictEntryNoValue {
    dictEntry *next; /* Must be first */
    void *key;       /* Must be second */
} dictEntryNoValue;

static_assert(offsetof(dictEntry, next) == offsetof(dictEntryNoValue, next), "dictEntry & dictEntryNoValue next not aligned");
static_assert(offsetof(dictEntry, key) == offsetof(dictEntryNoValue, key), "dictEntry & dictEntryNoValue key not aligned");

/* -------------------------- private prototypes ---------------------------- */

static int _dictExpandIfNeeded(dict *d);
static void _dictShrinkIfNeeded(dict *d);
static void _dictRehashStepIfNeeded(dict *d, uint64_t visitedIdx);
static signed char _dictNextExp(unsigned long size);
static int _dictInit(dict *d, dictType *type);
static dictEntryLink dictGetNextLink(dictEntry *de);
static void dictSetNext(dictEntry *de, dictEntry *next);
static int dictDefaultCompare(dictCmpCache *cache, const void *key1, const void *key2);
static dictEntryLink dictFindLinkInternal(dict *d, const void *key, dictEntryLink *bucket);
dictEntryLink dictFindLinkForInsert(dict *d, const void *key, dictEntry **existing);
static dictEntry *dictInsertKeyAtLink(dict *d, void *key, dictEntryLink link);

/* -------------------------- unused  --------------------------- */
void dictSetSignedIntegerVal(dictEntry *de, int64_t val);
int64_t dictGetSignedIntegerVal(const dictEntry *de);
double dictIncrDoubleVal(dictEntry *de, double val);
void *dictEntryMetadata(dictEntry *de);
int64_t dictIncrSignedIntegerVal(dictEntry *de, int64_t val);

/* -------------------------- misc inline functions -------------------------------- */

typedef int (*keyCmpFunc)(dictCmpCache *cache, const void *key1, const void *key2);
static inline keyCmpFunc dictGetCmpFunc(dict *d) {
    if (d->useStoredKeyApi && d->type->storedKeyCompare)
        return d->type->storedKeyCompare;
    if (d->type->keyCompare)
        return d->type->keyCompare;
    return dictDefaultCompare;
}

static inline uint64_t dictHashKey(dict *d, const void *key, int isStoredKey) {
    if (isStoredKey && d->type->storedHashFunction)
        return d->type->storedHashFunction(key);
    else
        return d->type->hashFunction(key);
}

/* -------------------------- hash functions -------------------------------- */

static uint8_t dict_hash_function_seed[16];

void dictSetHashFunctionSeed(uint8_t *seed) {
    memcpy(dict_hash_function_seed,seed,sizeof(dict_hash_function_seed));
}

/* The default hashing function uses SipHash implementation
 * in siphash.c. */

uint64_t siphash(const uint8_t *in, const size_t inlen, const uint8_t *k);
uint64_t siphash_nocase(const uint8_t *in, const size_t inlen, const uint8_t *k);

uint64_t dictGenHashFunction(const void *key, size_t len) {
    return siphash(key,len,dict_hash_function_seed);
}

uint64_t dictGenCaseHashFunction(const unsigned char *buf, size_t len) {
    return siphash_nocase(buf,len,dict_hash_function_seed);
}

/* --------------------- dictEntry pointer bit tricks ----------------------  */

/* The 3 least significant bits in a pointer to a dictEntry determines what the
 * pointer actually points to. If the least bit is set, it's a key. Otherwise,
 * the bit pattern of the least 3 significant bits mark the kind of entry. */

#define ENTRY_PTR_MASK        7 /* 111 */
#define ENTRY_PTR_NORMAL      0 /* 000 : If a pointer to an entry with value. */
#define ENTRY_PTR_IS_ODD_KEY  1 /* XX1 : If a pointer to odd key address (must be 1). */
#define ENTRY_PTR_IS_EVEN_KEY 2 /* 010 : If a pointer to even key address. (must be 2 or 4). */
#define ENTRY_PTR_UNUSED      4 /* 100 : Unused. */

/* Returns 1 if the entry pointer is a pointer to a key, rather than to an
 * allocated entry. Returns 0 otherwise. */
static inline int entryIsKey(const dictEntry *de) {
    return ((uintptr_t)de & (ENTRY_PTR_IS_ODD_KEY | ENTRY_PTR_IS_EVEN_KEY));
}

/* Returns 1 if the pointer is actually a pointer to a dictEntry struct. Returns
 * 0 otherwise. */
static inline int entryIsNormal(const dictEntry *de) {
    return ((uintptr_t)(void *)de & ENTRY_PTR_MASK) == ENTRY_PTR_NORMAL;
}

/* Creates an entry without a value field. */
static inline dictEntry *createEntryNoValue(void *key, dictEntry *next) {
    dictEntryNoValue *entry = zmalloc(sizeof(*entry));
    entry->key = key;
    entry->next = next;
    return (dictEntry *) entry;
}

static inline dictEntry *encodeMaskedPtr(const void *ptr, unsigned int bits) {
    assert(((uintptr_t)ptr & ENTRY_PTR_MASK) == 0);
    return (dictEntry *)(void *)((uintptr_t)ptr | bits);
}

static inline void *decodeMaskedPtr(const dictEntry *de) {
    return (void *)((uintptr_t)(void *)de & ~ENTRY_PTR_MASK);
}

/* Decodes the pointer to an entry without value, when you know it is an entry
 * without value. Hint: Use entryIsNoValue to check. */
static inline dictEntryNoValue *decodeEntryNoValue(const dictEntry *de) {
    return decodeMaskedPtr(de);
}

/* Returns 1 if the entry has a value field and 0 otherwise. */
static inline int entryHasValue(const dictEntry *de) {
    return entryIsNormal(de);
}

/* ----------------------------- API implementation ------------------------- */

/* Reset hash table parameters already initialized with _dictInit()*/
static void _dictReset(dict *d, int htidx)
{
    d->ht_table[htidx] = NULL;
    d->ht_size_exp[htidx] = -1;
    d->ht_used[htidx] = 0;
}

/* Create a new hash table */
dict *dictCreate(dictType *type)
{
    size_t metasize = type->dictMetadataBytes ? type->dictMetadataBytes(NULL) : 0;
    dict *d = zmalloc(sizeof(*d)+metasize);
    if (metasize > 0) {
        memset(dictMetadata(d), 0, metasize);
    }
    _dictInit(d,type);
    return d;
}

/* Change dictType of dict to another one with metadata support
 * Rest of dictType's values must stay the same */
void dictTypeAddMeta(dict **d, dictType *typeWithMeta) {
    /* Verify new dictType is compatible with the old one */
    dictType toCmp = *typeWithMeta;
    toCmp.dictMetadataBytes = NULL;                            /* Expected old one not to have metadata */
    toCmp.onDictRelease = (*d)->type->onDictRelease;           /* Ignore 'onDictRelease' in comparison */
    assert(memcmp((*d)->type, &toCmp, sizeof(dictType)) == 0); /* The rest of the dictType fields must be the same */

    *d = zrealloc(*d, sizeof(dict) + typeWithMeta->dictMetadataBytes(*d));
    (*d)->type = typeWithMeta;
}

/* Initialize the hash table */
int _dictInit(dict *d, dictType *type)
{
    _dictReset(d, 0);
    _dictReset(d, 1);
    d->type = type;
    d->rehashidx = -1;
    d->pauserehash = 0;
    d->pauseAutoResize = 0;
    d->useStoredKeyApi = 0;
    return DICT_OK;
}

/* Resize or create the hash table,
 * when malloc_failed is non-NULL, it'll avoid panic if malloc fails (in which case it'll be set to 1).
 * Returns DICT_OK if resize was performed, and DICT_ERR if skipped. */
int _dictResize(dict *d, unsigned long size, int* malloc_failed)
{
    if (malloc_failed) *malloc_failed = 0;

    /* We can't rehash twice if rehashing is ongoing. */
    assert(!dictIsRehashing(d));

    /* the new hash table */
    dictEntry **new_ht_table;
    unsigned long new_ht_used;
    signed char new_ht_size_exp = _dictNextExp(size);

    /* Detect overflows */
    size_t newsize = DICTHT_SIZE(new_ht_size_exp);
    if (newsize < size || newsize * sizeof(dictEntry*) < newsize)
        return DICT_ERR;

    /* Rehashing to the same table size is not useful. */
    if (new_ht_size_exp == d->ht_size_exp[0]) return DICT_ERR;

    /* Allocate the new hash table and initialize all pointers to NULL */
    if (malloc_failed) {
        new_ht_table = ztrycalloc(newsize*sizeof(dictEntry*));
        *malloc_failed = new_ht_table == NULL;
        if (*malloc_failed)
            return DICT_ERR;
    } else
        new_ht_table = zcalloc(newsize*sizeof(dictEntry*));

    new_ht_used = 0;

    /* Prepare a second hash table for incremental rehashing.
     * We do this even for the first initialization, so that we can trigger the
     * rehashingStarted more conveniently, we will clean it up right after. */
    d->ht_size_exp[1] = new_ht_size_exp;
    d->ht_used[1] = new_ht_used;
    d->ht_table[1] = new_ht_table;
    d->rehashidx = 0;
    if (d->type->rehashingStarted) d->type->rehashingStarted(d);
    if (d->type->bucketChanged)
        d->type->bucketChanged(d, DICTHT_SIZE(d->ht_size_exp[1]));

    /* Is this the first initialization or is the first hash table empty? If so
     * it's not really a rehashing, we can just set the first hash table so that
     * it can accept keys. */
    if (d->ht_table[0] == NULL || d->ht_used[0] == 0) {
        if (d->type->rehashingCompleted) d->type->rehashingCompleted(d);
        if (d->type->bucketChanged)
            d->type->bucketChanged(d, -(long long)DICTHT_SIZE(d->ht_size_exp[0]));
        if (d->ht_table[0]) zfree(d->ht_table[0]);
        d->ht_size_exp[0] = new_ht_size_exp;
        d->ht_used[0] = new_ht_used;
        d->ht_table[0] = new_ht_table;
        _dictReset(d, 1);
        d->rehashidx = -1;
        return DICT_OK;
    }

    /* Force a full rehashing of the dictionary */
    if (d->type->force_full_rehash) {
        while (dictRehash(d, 1000)) {
            /* Continue rehashing */
        }
    }
    return DICT_OK;
}

int _dictExpand(dict *d, unsigned long size, int* malloc_failed) {
    /* the size is invalid if it is smaller than the size of the hash table 
     * or smaller than the number of elements already inside the hash table */
    if (dictIsRehashing(d) || d->ht_used[0] > size || DICTHT_SIZE(d->ht_size_exp[0]) >= size)
        return DICT_ERR;
    return _dictResize(d, size, malloc_failed);
}

/* return DICT_ERR if expand was not performed */
int dictExpand(dict *d, unsigned long size) {
    return _dictExpand(d, size, NULL);
}

/* return DICT_ERR if expand failed due to memory allocation failure */
int dictTryExpand(dict *d, unsigned long size) {
    int malloc_failed = 0;
    _dictExpand(d, size, &malloc_failed);
    return malloc_failed? DICT_ERR : DICT_OK;
}

/* return DICT_ERR if shrink was not performed */
int dictShrink(dict *d, unsigned long size) {
    /* the size is invalid if it is bigger than the size of the hash table
     * or smaller than the number of elements already inside the hash table */
    if (dictIsRehashing(d) || d->ht_used[0] > size || DICTHT_SIZE(d->ht_size_exp[0]) <= size)
        return DICT_ERR;
    return _dictResize(d, size, NULL);
}

/* Helper function for `dictRehash` and `dictBucketRehash` which rehashes all the keys
 * in a bucket at index `idx` from the old to the new hash HT. */
static void rehashEntriesInBucketAtIndex(dict *d, uint64_t idx) {
    dictEntry *de = d->ht_table[0][idx];
    uint64_t h;
    dictEntry *nextde;
    while (de) {
        nextde = dictGetNext(de);
        void *key = dictGetKey(de);
        /* Get the index in the new hash table */
        if (d->ht_size_exp[1] > d->ht_size_exp[0]) {
            h = dictHashKey(d, key, 1) & DICTHT_SIZE_MASK(d->ht_size_exp[1]);
        } else {
            /* We're shrinking the table. The tables sizes are powers of
             * two, so we simply mask the bucket index in the larger table
             * to get the bucket index in the smaller table. */
            h = idx & DICTHT_SIZE_MASK(d->ht_size_exp[1]);
        }
        if (d->type->no_value) {
            if (!d->ht_table[1][h]) {
                /* The destination bucket is empty, allowing the key to be stored 
                 * directly without allocating a dictEntry. If an old entry was 
                 * previously allocated, free its memory. */                
                if (!entryIsKey(de)) zfree(decodeMaskedPtr(de));
                
                if (d->type->keys_are_odd)
                    de = key; /* ENTRY_PTR_IS_ODD_KEY trivially set by the odd key. */
                else
                    de = encodeMaskedPtr(key, ENTRY_PTR_IS_EVEN_KEY);
                
            } else if (entryIsKey(de)) {
                /* We don't have an allocated entry but we need one. */
                de = createEntryNoValue(key, d->ht_table[1][h]);
            } else {
                dictSetNext(de, d->ht_table[1][h]);
            }
        } else {
            dictSetNext(de, d->ht_table[1][h]);
        }
        d->ht_table[1][h] = de;
        d->ht_used[0]--;
        d->ht_used[1]++;
        de = nextde;
    }
    d->ht_table[0][idx] = NULL;
}

/* This checks if we already rehashed the whole table and if more rehashing is required */
static int dictCheckRehashingCompleted(dict *d) {
    if (d->ht_used[0] != 0) return 0;
    
    if (d->type->rehashingCompleted) d->type->rehashingCompleted(d);
    if (d->type->bucketChanged)
        d->type->bucketChanged(d, -(long long)DICTHT_SIZE(d->ht_size_exp[0]));
    zfree(d->ht_table[0]);
    /* Copy the new ht onto the old one */
    d->ht_table[0] = d->ht_table[1];
    d->ht_used[0] = d->ht_used[1];
    d->ht_size_exp[0] = d->ht_size_exp[1];
    _dictReset(d, 1);
    d->rehashidx = -1;
    return 1;
}

/* Performs N steps of incremental rehashing. Returns 1 if there are still
 * keys to move from the old to the new hash table, otherwise 0 is returned.
 *
 * Note that a rehashing step consists in moving a bucket (that may have more
 * than one key as we use chaining) from the old to the new hash table, however
 * since part of the hash table may be composed of empty spaces, it is not
 * guaranteed that this function will rehash even a single bucket, since it
 * will visit at max N*10 empty buckets in total, otherwise the amount of
 * work it does would be unbound and the function may block for a long time. */
int dictRehash(dict *d, int n) {
    int empty_visits = n*10; /* Max number of empty buckets to visit. */
    unsigned long s0 = DICTHT_SIZE(d->ht_size_exp[0]);
    unsigned long s1 = DICTHT_SIZE(d->ht_size_exp[1]);
    if (dict_can_resize == DICT_RESIZE_FORBID || !dictIsRehashing(d)) return 0;
    /* If dict_can_resize is DICT_RESIZE_AVOID, we want to avoid rehashing. 
     * - If expanding, the threshold is dict_force_resize_ratio which is 4.
     * - If shrinking, the threshold is 1 / (HASHTABLE_MIN_FILL * dict_force_resize_ratio) which is 1/32. */
    if (dict_can_resize == DICT_RESIZE_AVOID && 
        ((s1 > s0 && s1 < dict_force_resize_ratio * s0) ||
         (s1 < s0 && s0 < HASHTABLE_MIN_FILL * dict_force_resize_ratio * s1)))
    {
        return 0;
    }

    while(n-- && d->ht_used[0] != 0) {
        /* Note that rehashidx can't overflow as we are sure there are more
         * elements because ht[0].used != 0 */
        assert(DICTHT_SIZE(d->ht_size_exp[0]) > (unsigned long)d->rehashidx);
        while(d->ht_table[0][d->rehashidx] == NULL) {
            d->rehashidx++;
            if (--empty_visits == 0) return 1;
        }
        /* Move all the keys in this bucket from the old to the new hash HT */
        rehashEntriesInBucketAtIndex(d, d->rehashidx);
        d->rehashidx++;
    }

    return !dictCheckRehashingCompleted(d);
}

long long timeInMilliseconds(void) {
    struct timeval tv;

    gettimeofday(&tv,NULL);
    return (((long long)tv.tv_sec)*1000)+(tv.tv_usec/1000);
}

/* Rehash in us+"delta" microseconds. The value of "delta" is larger
 * than 0, and is smaller than 1000 in most cases. The exact upper bound
 * depends on the running time of dictRehash(d,100).*/
int dictRehashMicroseconds(dict *d, uint64_t us) {
    if (d->pauserehash > 0) return 0;

    monotime timer;
    elapsedStart(&timer);
    int rehashes = 0;

    while(dictRehash(d,100)) {
        rehashes += 100;
        if (elapsedUs(timer) >= us) break;
    }
    return rehashes;
}

/* This function performs just a step of rehashing, and only if hashing has
 * not been paused for our hash table. When we have iterators in the
 * middle of a rehashing we can't mess with the two hash tables otherwise
 * some elements can be missed or duplicated.
 *
 * This function is called by common lookup or update operations in the
 * dictionary so that the hash table automatically migrates from H1 to H2
 * while it is actively used. */
static void _dictRehashStep(dict *d) {
    if (d->pauserehash == 0) dictRehash(d,1);
}

/* Performs rehashing on a single bucket. */
int _dictBucketRehash(dict *d, uint64_t idx) {
    if (d->pauserehash != 0) return 0;
    unsigned long s0 = DICTHT_SIZE(d->ht_size_exp[0]);
    unsigned long s1 = DICTHT_SIZE(d->ht_size_exp[1]);
    if (dict_can_resize == DICT_RESIZE_FORBID || !dictIsRehashing(d)) return 0;
    /* If dict_can_resize is DICT_RESIZE_AVOID, we want to avoid rehashing. 
     * - If expanding, the threshold is dict_force_resize_ratio which is 4.
     * - If shrinking, the threshold is 1 / (HASHTABLE_MIN_FILL * dict_force_resize_ratio) which is 1/32. */
    if (dict_can_resize == DICT_RESIZE_AVOID && 
        ((s1 > s0 && s1 < dict_force_resize_ratio * s0) ||
         (s1 < s0 && s0 < HASHTABLE_MIN_FILL * dict_force_resize_ratio * s1)))
    {
        return 0;
    }
    rehashEntriesInBucketAtIndex(d, idx);
    dictCheckRehashingCompleted(d);
    return 1;
}

/* Add an element to the target hash table */
int dictAdd(dict *d, void *key, void *val)
{
    dictEntry *entry = dictAddRaw(d,key,NULL);

    if (!entry) return DICT_ERR;
    if (!d->type->no_value) dictSetVal(d, entry, val);
    return DICT_OK;
}

int dictCompareKeys(dict *d, const void *key1, const void *key2) {
    dictCmpCache cache = {0};
    keyCmpFunc cmpFunc = dictGetCmpFunc(d);
    return cmpFunc(&cache, key1, key2);
}

/* Low level add or find:
 * This function adds the entry but instead of setting a value returns the
 * dictEntry structure to the user, that will make sure to fill the value
 * field as they wish.
 *
 * This function is also directly exposed to the user API to be called
 * mainly in order to store non-pointers inside the hash value, example:
 *
 * entry = dictAddRaw(dict,mykey,NULL);
 * if (entry != NULL) dictSetSignedIntegerVal(entry,1000);
 *
 * Return values:
 *
 * If key already exists NULL is returned, and "*existing" is populated
 * with the existing entry if existing is not NULL.
 *
 * If key was added, the hash entry is returned to be manipulated by the caller.
 */
dictEntry *dictAddRaw(dict *d, void *key, dictEntry **existing)
{
    /* Get the position for the new key or NULL if the key already exists. */
    void *position = dictFindLinkForInsert(d, key, existing);
    if (!position) return NULL;

    /* Dup the key if necessary. */
    if (d->type->keyDup) key = d->type->keyDup(d, key);

    return dictInsertKeyAtLink(d, key, position);
}

/* Adds a key in the dict's hashtable at the link returned by a preceding
 * call to dictFindLinkForInsert(). This is a low level function which allows
 * splitting dictAddRaw in two parts. Normally, dictAddRaw or dictAdd should be
 * used instead. It assumes that dictExpandIfNeeded() was called before. */
dictEntry *dictInsertKeyAtLink(dict *d, void *key, dictEntryLink link) {
    dictEntryLink bucket = link; /* It's a bucket, but the API hides that. */
    dictEntry *entry;
    /* If rehashing is ongoing, we insert in table 1, otherwise in table 0.
     * Assert that the provided bucket is the right table. */
    int htidx = dictIsRehashing(d) ? 1 : 0;
    assert(bucket >= &d->ht_table[htidx][0] &&
           bucket <= &d->ht_table[htidx][DICTHT_SIZE_MASK(d->ht_size_exp[htidx])]);
    if (d->type->no_value) {
        if (!*bucket) {
            /* We can store the key directly in the destination bucket without 
             * allocating dictEntry.
             */
            if (d->type->keys_are_odd) {
                entry = key;
                assert(entryIsKey(entry));
                /* The flag ENTRY_PTR_IS_ODD_KEY (=0x1) is already aligned with LSB bit  */
            } else {
                entry = encodeMaskedPtr(key, ENTRY_PTR_IS_EVEN_KEY);
            }
        } else {
            /* Allocate an entry without value. */
            entry = createEntryNoValue(key, *bucket);
        }
    } else {
        /* Allocate the memory and store the new entry.
         * Insert the element in top, with the assumption that in a database
         * system it is more likely that recently added entries are accessed
         * more frequently. */
        entry = zmalloc(sizeof(*entry));
        assert(entryIsNormal(entry)); /* Check alignment of allocation */
        entry->key = key;
        entry->next = *bucket;
    }
    *bucket = entry;
    d->ht_used[htidx]++;

    return entry;
}

/* Add or Overwrite:
 * Add an element, discarding the old value if the key already exists.
 * Return 1 if the key was added from scratch, 0 if there was already an
 * element with such key and dictReplace() just performed a value update
 * operation. */
int dictReplace(dict *d, void *key, void *val)
{
    dictEntry *entry, *existing;

    /* Try to add the element. If the key
     * does not exists dictAdd will succeed. */
    entry = dictAddRaw(d,key,&existing);
    if (entry) {
        dictSetVal(d, entry, val);
        return 1;
    }

    /* Set the new value and free the old one. Note that it is important
     * to do that in this order, as the value may just be exactly the same
     * as the previous one. In this context, think to reference counting,
     * you want to increment (set), and then decrement (free), and not the
     * reverse. */
    void *oldval = dictGetVal(existing);
    dictSetVal(d, existing, val);
    if (d->type->valDestructor)
        d->type->valDestructor(d, oldval);
    return 0;
}

/* Add or Find:
 * dictAddOrFind() is simply a version of dictAddRaw() that always
 * returns the hash entry of the specified key, even if the key already
 * exists and can't be added (in that case the entry of the already
 * existing key is returned.)
 *
 * See dictAddRaw() for more information. */
dictEntry *dictAddOrFind(dict *d, void *key) {
    dictEntry *entry, *existing;
    entry = dictAddRaw(d,key,&existing);
    return entry ? entry : existing;
}

/* Search and remove an element. This is a helper function for
 * dictDelete() and dictUnlink(), please check the top comment
 * of those functions. */
static dictEntry *dictGenericDelete(dict *d, const void *key, int nofree) {
    dictCmpCache cmpCache = {0};
    uint64_t h, idx;
    dictEntry *he, *prevHe;
    int table;

    /* dict is empty */
    if (dictSize(d) == 0) return NULL;

    h = dictHashKey(d, key, d->useStoredKeyApi);
    idx = h & DICTHT_SIZE_MASK(d->ht_size_exp[0]);

    /* Rehash the hash table if needed */
    _dictRehashStepIfNeeded(d,idx);

    keyCmpFunc cmpFunc = dictGetCmpFunc(d);

    for (table = 0; table <= 1; table++) {
        if (table == 0 && (long)idx < d->rehashidx) continue;
        idx = h & DICTHT_SIZE_MASK(d->ht_size_exp[table]);
        he = d->ht_table[table][idx];
        prevHe = NULL;
        while(he) {
            void *he_key = dictGetKey(he);
            if (key == he_key || cmpFunc(&cmpCache, key, he_key)) {
                /* Unlink the element from the list */
                if (prevHe)
                    dictSetNext(prevHe, dictGetNext(he));
                else
                    d->ht_table[table][idx] = dictGetNext(he);
                if (!nofree) {
                    dictFreeUnlinkedEntry(d, he);
                }
                d->ht_used[table]--;
                _dictShrinkIfNeeded(d);
                return he;
            }
            prevHe = he;
            he = dictGetNext(he);
        }
        if (!dictIsRehashing(d)) break;
    }
    return NULL; /* not found */
}

/* Remove an element, returning DICT_OK on success or DICT_ERR if the
 * element was not found. */
int dictDelete(dict *ht, const void *key) {
    return dictGenericDelete(ht,key,0) ? DICT_OK : DICT_ERR;
}

/* Remove an element from the table, but without actually releasing
 * the key, value and dictionary entry. The dictionary entry is returned
 * if the element was found (and unlinked from the table), and the user
 * should later call `dictFreeUnlinkedEntry()` with it in order to release it.
 * Otherwise if the key is not found, NULL is returned.
 *
 * This function is useful when we want to remove something from the hash
 * table but want to use its value before actually deleting the entry.
 * Without this function the pattern would require two lookups:
 *
 *  entry = dictFind(...);
 *  // Do something with entry
 *  dictDelete(dictionary,entry);
 *
 * Thanks to this function it is possible to avoid this, and use
 * instead:
 *
 * entry = dictUnlink(dictionary,entry);
 * // Do something with entry
 * dictFreeUnlinkedEntry(entry); // <- This does not need to lookup again.
 */
dictEntry *dictUnlink(dict *d, const void *key) {
    return dictGenericDelete(d,key,1);
}

/* You need to call this function to really free the entry after a call
 * to dictUnlink(). It's safe to call this function with 'he' = NULL. */
void dictFreeUnlinkedEntry(dict *d, dictEntry *he) {
    if (he == NULL) return;
    dictFreeKey(d, he);
    dictFreeVal(d, he);
    if (!entryIsKey(he)) zfree(decodeMaskedPtr(he));
}

/* Destroy an entire dictionary */
int _dictClear(dict *d, int htidx, void(callback)(dict*)) {
    unsigned long i;

    /* Free all the elements */
    for (i = 0; i < DICTHT_SIZE(d->ht_size_exp[htidx]) && d->ht_used[htidx] > 0; i++) {
        dictEntry *he, *nextHe;
        /* Callback will be called once for every 65535 deletions. Beware,
         * if dict has less than 65535 items, it will not be called at all.*/
        if (callback && i != 0 && (i & 65535) == 0) callback(d);

        if ((he = d->ht_table[htidx][i]) == NULL) continue;
        while(he) {
            nextHe = dictGetNext(he);
            dictFreeKey(d, he);
            dictFreeVal(d, he);
            if (!entryIsKey(he)) zfree(decodeMaskedPtr(he));
            d->ht_used[htidx]--;
            he = nextHe;
        }
    }
    /* Free the table and the allocated cache structure */
    zfree(d->ht_table[htidx]);
    /* Re-initialize the table */
    _dictReset(d, htidx);
    return DICT_OK; /* never fails */
}

/* Clear & Release the hash table */
void dictRelease(dict *d)
{
    /* Someone may be monitoring a dict that started rehashing, before
     * destroying the dict fake completion. */
    if (dictIsRehashing(d) && d->type->rehashingCompleted)
        d->type->rehashingCompleted(d);

    /* Subtract the size of all buckets. */
    if (d->type->bucketChanged)
        d->type->bucketChanged(d, -(long long)dictBuckets(d));

    if (d->type->onDictRelease)
        d->type->onDictRelease(d);

    _dictClear(d,0,NULL);
    _dictClear(d,1,NULL);
    zfree(d);
}

/* Finds a given key. Like dictFindLink(), yet search bucket even if dict is empty. 
 * 
 * Returns dictEntryLink reference if found. Otherwise, return NULL.
 * 
 * bucket - return pointer to bucket that the key was mapped. unless dict is empty.
 */
static dictEntryLink dictFindLinkInternal(dict *d, const void *key, dictEntryLink *bucket) {
    dictCmpCache cmpCache = {0};
    dictEntryLink link;
    uint64_t idx;
    int table;
    
    if (bucket) {
        *bucket = NULL;
    } else {
        /* If dict is empty and no need to find bucket, return NULL */
        if (dictSize(d) == 0) return NULL; 
    }

    const uint64_t hash = dictHashKey(d, key, d->useStoredKeyApi);
    idx = hash & DICTHT_SIZE_MASK(d->ht_size_exp[0]);
    keyCmpFunc cmpFunc = dictGetCmpFunc(d);

    /* Rehash the hash table if needed */
    _dictRehashStepIfNeeded(d,idx);

    int tables = (dictIsRehashing(d)) ? 2 : 1;
    for (table = 0; table < tables; table++) {
        if (table == 0 && (long)idx < d->rehashidx) continue;
        idx = hash & DICTHT_SIZE_MASK(d->ht_size_exp[table]);

        /* Prefetch the bucket at the calculated index */
        redis_prefetch_read(&d->ht_table[table][idx]);

        link = &(d->ht_table[table][idx]);
        if (bucket) *bucket = link;
        while(link && *link) {
            void *visitedKey = dictGetKey(*link);

            /* Prefetch the next entry to improve cache efficiency */
            redis_prefetch_read(dictGetNext(*link));

            if (key == visitedKey || cmpFunc( &cmpCache, key, visitedKey))                
                return link;

            link = dictGetNextLink(*link);
        }
    }
    return NULL;
}

dictEntry *dictFind(dict *d, const void *key)
{
    dictEntryLink link = dictFindLink(d, key, NULL);
    return (link) ? *link : NULL;
}

/* Finds the dictEntry using pointer and pre-calculated hash.
 * oldkey is a dead pointer and should not be accessed.
 * the hash value should be provided using dictGetHash.
 * no string / key comparison is performed.
 * return value is a pointer to the dictEntry if found, or NULL if not found. */
dictEntry *dictFindByHashAndPtr(dict *d, const void *oldptr, const uint64_t hash) {
    dictEntry *he;
    unsigned long idx, table;

    if (dictSize(d) == 0) return NULL; /* dict is empty */
    for (table = 0; table <= 1; table++) {
        idx = hash & DICTHT_SIZE_MASK(d->ht_size_exp[table]);
        if (table == 0 && (long)idx < d->rehashidx) continue;
        he = d->ht_table[table][idx];
        while(he) {
            if (oldptr == dictGetKey(he))
                return he;
            he = dictGetNext(he);
        }
        if (!dictIsRehashing(d)) return NULL;
    }
    return NULL;
}

/* Find a key and return its dictEntryLink reference. Otherwise, return NULL
 * 
 * A dictEntryLink pointer being used to find preceding dictEntry of searched item. 
 * It is Useful for deletion, addition, unlinking and updating, especially for 
 * dict configured with 'no_value'. In such cases returning only `dictEntry` from 
 * a lookup may be insufficient since it might be opt-out to be the object itself. 
 * By locating preceding dictEntry (dictEntryLink) these ops can be properly handled. 
 * 
 * After calling link = dictFindLink(...), any necessary updates based on returned 
 * link or bucket must be performed immediately after by calling dictSetKeyAtLink() 
 * without any intervening operations on given dict. Otherwise, `dictEntryLink` may 
 * become invalid. Example with kvobj of replacing key with new key:
 * 
 *      link = dictFindLink(d, key, &bucket, 0);
 *      ... Do something, but don't modify the dict ...
 *      // assert(link != NULL);
 *      dictSetKeyAtLink(d, kv, &link, 0);
 *      
 * To add new value (If no space for the new key, dict will be expanded by
 * dictSetKeyAtLink() and bucket will be looked up again.):
 *   
 *      link = dictFindLink(d, key, &bucket);
 *      ... Do something, but don't modify the dict ...
 *      // assert(link == NULL);
 *      dictSetKeyAtLink(d, kv, &bucket, 1);
 *  
 *  bucket - return link to bucket that the key was mapped. unless dict is empty.
 */
dictEntryLink dictFindLink(dict *d, const void *key, dictEntryLink *bucket) {
    if (bucket) *bucket = NULL;
    if (unlikely(dictSize(d) == 0))
        return NULL;
    
    return dictFindLinkInternal(d, key, bucket);
}

/* Set the key with link 
 *
 * link:    - When `newItem` is set, `link` points to the bucket of the key.
 *          - When `newItem` is not set, `link` points to the link of the key.
 *          - If *link is NULL, dictFindLink() will be called to locate the key.
 *          - On return, get updated, by need, to the inserted key. 
 *
 * newItem: 1 = Add a key with a new dictEntry.
 *          0 = Set a key to an existing dictEntry. 
 */
void dictSetKeyAtLink(dict *d, void *key, dictEntryLink *link, int newItem) {
    dictEntryLink dummy = NULL;
    if (link == NULL) link = &dummy;
    void *addedKey = (d->type->keyDup) ? d->type->keyDup(d, key) : key;
    
    if (newItem) {
        signed char snap[2] = {d->ht_size_exp[0], d->ht_size_exp[1] };

        /* Make room if needed for the new key */
        dictExpandIfNeeded(d);
        
        /* Lookup key's link if tables reallocated or if given link is set to NULL */
        if (snap[0] != d->ht_size_exp[0] || snap[1] != d->ht_size_exp[1] || *link == NULL) {
            dictEntryLink bucket;
            /* Bypass dictFindLink() to search bucket even if dict is empty!!! */
            dictUseStoredKeyApi(d, 1);
            *link = dictFindLinkInternal(d, key, &bucket);
            dictUseStoredKeyApi(d, 0);
            assert(bucket != NULL);
            assert(*link == NULL);
            *link = bucket; /* On newItem the link should be the bucket */
        }
        dictInsertKeyAtLink(d, addedKey, *link);
        return;
    } 
    
    /* Setting key of existing dictEntry (newItem == 0)*/
    
    if (*link == NULL) {
        *link = dictFindLink(d, key, NULL);
        assert(*link != NULL);
    }
    
    dictEntry **de = *link;
    if (entryIsKey(*de)) {
        /* `de` opt-out to be actually a key. Replace key but keep the lsb flags */
        int mask = ((uintptr_t) *de) & ENTRY_PTR_MASK;
        *de = encodeMaskedPtr(addedKey, mask);
    } else {
        /* either dictEntry or dictEntryNoValue */
        (*de)->key = addedKey;
    }
}

void *dictFetchValue(dict *d, const void *key) {
    dictEntry *he;

    he = dictFind(d,key);
    return he ? dictGetVal(he) : NULL;
}

/* Find an element from the table. A link is returned if the element is found, and
 * the user should later call `dictTwoPhaseUnlinkFree` with it in order to unlink
 * and release it. Otherwise if the key is not found, NULL is returned. These two
 * functions should be used in pair.
 * `dictTwoPhaseUnlinkFind` pauses rehash and `dictTwoPhaseUnlinkFree` resumes rehash.
 *
 * We can use like this:
 *
 * dictEntryLink link = dictTwoPhaseUnlinkFind(db->dict,key->ptr, &table);
 * // Do something, but we can't modify the dict
 * dictTwoPhaseUnlinkFree(db->dict, link, table); // We don't need to lookup again
 *
 * If we want to find an entry before delete this entry, this an optimization to avoid
 * dictFind followed by dictDelete. i.e. the first API is a find, and it gives some info
 * to the second one to avoid repeating the lookup
 */
dictEntryLink dictTwoPhaseUnlinkFind(dict *d, const void *key, int *table_index) {
    dictCmpCache cmpCache = {0};
    uint64_t h, idx, table;

    if (dictSize(d) == 0) return NULL; /* dict is empty */
    if (dictIsRehashing(d)) _dictRehashStep(d);

    h = dictHashKey(d, key, d->useStoredKeyApi);    
    keyCmpFunc cmpFunc = dictGetCmpFunc(d);

    for (table = 0; table <= 1; table++) {
        idx = h & DICTHT_SIZE_MASK(d->ht_size_exp[table]);
        if (table == 0 && (long)idx < d->rehashidx) continue;
        dictEntry **ref = &d->ht_table[table][idx];
        while (ref && *ref) {
            void *de_key = dictGetKey(*ref);
            if (key == de_key || cmpFunc(&cmpCache, key, de_key)) {
                *table_index = table;
                dictPauseRehashing(d);
                return ref;
            }
            ref = dictGetNextLink(*ref);
        }
        if (!dictIsRehashing(d)) return NULL;
    }
    return NULL;
}

void dictTwoPhaseUnlinkFree(dict *d, dictEntryLink plink, int table_index) {
    if (plink == NULL || *plink == NULL) return;
    dictEntry *de = *plink;
    d->ht_used[table_index]--;

    *plink = dictGetNext(de);
    dictFreeKey(d, de);
    dictFreeVal(d, de);
    if (!entryIsKey(de)) zfree(decodeMaskedPtr(de));
    _dictShrinkIfNeeded(d);
    dictResumeRehashing(d);
}

void dictSetKey(dict *d, dictEntry* de, void *key) {
    assert(!d->type->no_value);
    if (d->type->keyDup)
        de->key = d->type->keyDup(d, key);
    else
        de->key = key;
}

void dictSetVal(dict *d, dictEntry *de, void *val) {
    assert(entryHasValue(de));
    de->v.val = d->type->valDup ? d->type->valDup(d, val) : val;
}

void dictSetSignedIntegerVal(dictEntry *de, int64_t val) {
    assert(entryHasValue(de));
    de->v.s64 = val;
}

void dictSetUnsignedIntegerVal(dictEntry *de, uint64_t val) {
    assert(entryHasValue(de));
    de->v.u64 = val;
}

void dictSetDoubleVal(dictEntry *de, double val) {
    assert(entryHasValue(de));
    de->v.d = val;
}

int64_t dictIncrSignedIntegerVal(dictEntry *de, int64_t val) {
    assert(entryHasValue(de));
    return de->v.s64 += val;
}

uint64_t dictIncrUnsignedIntegerVal(dictEntry *de, uint64_t val) {
    assert(entryHasValue(de));
    return de->v.u64 += val;
}

double dictIncrDoubleVal(dictEntry *de, double val) {
    assert(entryHasValue(de));
    return de->v.d += val;
}

int dictEntryIsKey(const dictEntry *de) {
    return entryIsKey(de);
}

void *dictGetKey(const dictEntry *de) {
    /* if entryIsKey() */
    if ((uintptr_t)de & ENTRY_PTR_IS_ODD_KEY) return (void *) de;
    if ((uintptr_t)de & ENTRY_PTR_IS_EVEN_KEY) return decodeMaskedPtr(de);    
    /* Regular entry */
    return de->key;
}

void *dictGetVal(const dictEntry *de) {
    assert(entryHasValue(de));
    return de->v.val;
}

int64_t dictGetSignedIntegerVal(const dictEntry *de) {
    assert(entryHasValue(de));
    return de->v.s64;
}

uint64_t dictGetUnsignedIntegerVal(const dictEntry *de) {
    assert(entryHasValue(de));
    return de->v.u64;
}

double dictGetDoubleVal(const dictEntry *de) {
    assert(entryHasValue(de));
    return de->v.d;
}

/* Returns a mutable reference to the value as a double within the entry. */
double *dictGetDoubleValPtr(dictEntry *de) {
    assert(entryHasValue(de));
    return &de->v.d;
}

/* Returns the 'next' field of the entry or NULL if the entry doesn't have a
 * 'next' field. */
dictEntry *dictGetNext(const dictEntry *de) {
    if (entryIsKey(de)) return NULL; /* there's no next */
    /* Must come after entryIsKey() check */
    return de->next;
}

/* Returns a pointer to the 'next' field in the entry or NULL if the entry
 * doesn't have a next field. */
static dictEntryLink dictGetNextLink(dictEntry *de) {
    if (entryIsKey(de)) return NULL;
    /* Must come after entryIsKey() check */
    return &de->next;
}

static void dictSetNext(dictEntry *de, dictEntry *next) {
    assert(!entryIsKey(de));
    /* dictEntryNoValue & dictEntry are layout-compatible */
    de->next = next;
}

/* Returns the memory usage in bytes of the dict, excluding the size of the keys
 * and values. */
size_t dictMemUsage(const dict *d) {
    return dictSize(d) * sizeof(dictEntry) +
        dictBuckets(d) * sizeof(dictEntry*);
}

size_t dictEntryMemUsage(int noValueDict) {
    return (noValueDict) ? sizeof(dictEntryNoValue) :sizeof(dictEntry);
}

/* A fingerprint is a 64 bit number that represents the state of the dictionary
 * at a given time, it's just a few dict properties xored together.
 * When an unsafe iterator is initialized, we get the dict fingerprint, and check
 * the fingerprint again when the iterator is released.
 * If the two fingerprints are different it means that the user of the iterator
 * performed forbidden operations against the dictionary while iterating. */
unsigned long long dictFingerprint(dict *d) {
    unsigned long long integers[6], hash = 0;
    int j;

    integers[0] = (long) d->ht_table[0];
    integers[1] = d->ht_size_exp[0];
    integers[2] = d->ht_used[0];
    integers[3] = (long) d->ht_table[1];
    integers[4] = d->ht_size_exp[1];
    integers[5] = d->ht_used[1];

    /* We hash N integers by summing every successive integer with the integer
     * hashing of the previous sum. Basically:
     *
     * Result = hash(hash(hash(int1)+int2)+int3) ...
     *
     * This way the same set of integers in a different order will (likely) hash
     * to a different number. */
    for (j = 0; j < 6; j++) {
        hash += integers[j];
        /* For the hashing step we use Tomas Wang's 64 bit integer hash. */
        hash = (~hash) + (hash << 21); // hash = (hash << 21) - hash - 1;
        hash = hash ^ (hash >> 24);
        hash = (hash + (hash << 3)) + (hash << 8); // hash * 265
        hash = hash ^ (hash >> 14);
        hash = (hash + (hash << 2)) + (hash << 4); // hash * 21
        hash = hash ^ (hash >> 28);
        hash = hash + (hash << 31);
    }
    return hash;
}

void dictInitIterator(dictIterator *iter, dict *d)
{
    iter->d = d;
    iter->table = 0;
    iter->index = -1;
    iter->safe = 0;
    iter->entry = NULL;
    iter->nextEntry = NULL;
}

void dictInitSafeIterator(dictIterator *iter, dict *d)
{
    dictInitIterator(iter, d);
    iter->safe = 1;
}

void dictResetIterator(dictIterator *iter)
{
    if (!(iter->index == -1 && iter->table == 0)) {
        if (iter->safe)
            dictResumeRehashing(iter->d);
        else
            assert(iter->fingerprint == dictFingerprint(iter->d));
    }
}

dictIterator *dictGetIterator(dict *d)
{
    dictIterator *iter = zmalloc(sizeof(*iter));
    dictInitIterator(iter, d);
    return iter;
}

dictIterator *dictGetSafeIterator(dict *d) {
    dictIterator *i = dictGetIterator(d);

    i->safe = 1;
    return i;
}

dictEntry *dictNext(dictIterator *iter)
{
    while (1) {
        if (iter->entry == NULL) {
            if (iter->index == -1 && iter->table == 0) {
                if (iter->safe)
                    dictPauseRehashing(iter->d);
                else
                    iter->fingerprint = dictFingerprint(iter->d);

                /* skip the rehashed slots in table[0] */
                if (dictIsRehashing(iter->d)) {
                    iter->index = iter->d->rehashidx - 1;
                }
            }
            iter->index++;
            if (iter->index >= (long) DICTHT_SIZE(iter->d->ht_size_exp[iter->table])) {
                if (dictIsRehashing(iter->d) && iter->table == 0) {
                    iter->table++;
                    iter->index = 0;
                } else {
                    break;
                }
            }
            iter->entry = iter->d->ht_table[iter->table][iter->index];
        } else {
            iter->entry = iter->nextEntry;
        }
        if (iter->entry) {
            /* We need to save the 'next' here, the iterator user
             * may delete the entry we are returning. */
            iter->nextEntry = dictGetNext(iter->entry);
            return iter->entry;
        }
    }
    return NULL;
}

void dictReleaseIterator(dictIterator *iter)
{
    dictResetIterator(iter);
    zfree(iter);
}

/* Return a random entry from the hash table. Useful to
 * implement randomized algorithms */
dictEntry *dictGetRandomKey(dict *d)
{
    dictEntry *he, *orighe;
    unsigned long h;
    int listlen, listele;

    if (dictSize(d) == 0) return NULL;
    if (dictIsRehashing(d)) _dictRehashStep(d);
    if (dictIsRehashing(d)) {
        unsigned long s0 = DICTHT_SIZE(d->ht_size_exp[0]);
        do {
            /* We are sure there are no elements in indexes from 0
             * to rehashidx-1 */
            h = d->rehashidx + (randomULong() % (dictBuckets(d) - d->rehashidx));
            he = (h >= s0) ? d->ht_table[1][h - s0] : d->ht_table[0][h];
        } while(he == NULL);
    } else {
        unsigned long m = DICTHT_SIZE_MASK(d->ht_size_exp[0]);
        do {
            h = randomULong() & m;
            he = d->ht_table[0][h];
        } while(he == NULL);
    }

    /* Now we found a non empty bucket, but it is a linked
     * list and we need to get a random element from the list.
     * The only sane way to do so is counting the elements and
     * select a random index. */
    listlen = 0;
    orighe = he;
    while(he) {
        he = dictGetNext(he);
        listlen++;
    }
    listele = random() % listlen;
    he = orighe;
    while(listele--) he = dictGetNext(he);
    return he;
}

/* This function samples the dictionary to return a few keys from random
 * locations.
 *
 * It does not guarantee to return all the keys specified in 'count', nor
 * it does guarantee to return non-duplicated elements, however it will make
 * some effort to do both things.
 *
 * Returned pointers to hash table entries are stored into 'des' that
 * points to an array of dictEntry pointers. The array must have room for
 * at least 'count' elements, that is the argument we pass to the function
 * to tell how many random elements we need.
 *
 * The function returns the number of items stored into 'des', that may
 * be less than 'count' if the hash table has less than 'count' elements
 * inside, or if not enough elements were found in a reasonable amount of
 * steps.
 *
 * Note that this function is not suitable when you need a good distribution
 * of the returned items, but only when you need to "sample" a given number
 * of continuous elements to run some kind of algorithm or to produce
 * statistics. However the function is much faster than dictGetRandomKey()
 * at producing N elements. */
unsigned int dictGetSomeKeys(dict *d, dictEntry **des, unsigned int count) {
    unsigned long j; /* internal hash table id, 0 or 1. */
    unsigned long tables; /* 1 or 2 tables? */
    unsigned long stored = 0, maxsizemask;
    unsigned long maxsteps;

    if (dictSize(d) < count) count = dictSize(d);
    maxsteps = count*10;

    /* Try to do a rehashing work proportional to 'count'. */
    for (j = 0; j < count; j++) {
        if (dictIsRehashing(d))
            _dictRehashStep(d);
        else
            break;
    }

    tables = dictIsRehashing(d) ? 2 : 1;
    maxsizemask = DICTHT_SIZE_MASK(d->ht_size_exp[0]);
    if (tables > 1 && maxsizemask < DICTHT_SIZE_MASK(d->ht_size_exp[1]))
        maxsizemask = DICTHT_SIZE_MASK(d->ht_size_exp[1]);

    /* Pick a random point inside the larger table. */
    unsigned long i = randomULong() & maxsizemask;
    unsigned long emptylen = 0; /* Continuous empty entries so far. */
    while(stored < count && maxsteps--) {
        for (j = 0; j < tables; j++) {
            /* Invariant of the dict.c rehashing: up to the indexes already
             * visited in ht[0] during the rehashing, there are no populated
             * buckets, so we can skip ht[0] for indexes between 0 and idx-1. */
            if (tables == 2 && j == 0 && i < (unsigned long) d->rehashidx) {
                /* Moreover, if we are currently out of range in the second
                 * table, there will be no elements in both tables up to
                 * the current rehashing index, so we jump if possible.
                 * (this happens when going from big to small table). */
                if (i >= DICTHT_SIZE(d->ht_size_exp[1]))
                    i = d->rehashidx;
                else
                    continue;
            }
            if (i >= DICTHT_SIZE(d->ht_size_exp[j])) continue; /* Out of range for this table. */
            dictEntry *he = d->ht_table[j][i];

            /* Count contiguous empty buckets, and jump to other
             * locations if they reach 'count' (with a minimum of 5). */
            if (he == NULL) {
                emptylen++;
                if (emptylen >= 5 && emptylen > count) {
                    i = randomULong() & maxsizemask;
                    emptylen = 0;
                }
            } else {
                emptylen = 0;
                while (he) {
                    /* Collect all the elements of the buckets found non empty while iterating.
                     * To avoid the issue of being unable to sample the end of a long chain,
                     * we utilize the Reservoir Sampling algorithm to optimize the sampling process.
                     * This means that even when the maximum number of samples has been reached,
                     * we continue sampling until we reach the end of the chain.
                     * See https://en.wikipedia.org/wiki/Reservoir_sampling. */
                    if (stored < count) {
                        des[stored] = he;
                    } else {
                        unsigned long r = randomULong() % (stored + 1);
                        if (r < count) des[r] = he;
                    }

                    he = dictGetNext(he);
                    stored++;
                }
                if (stored >= count) goto end;
            }
        }
        i = (i+1) & maxsizemask;
    }

end:
    return stored > count ? count : stored;
}


/* Reallocate the dictEntry, key and value allocations in a bucket using the
 * provided allocation functions in order to defrag them. */
static void dictDefragBucket(dict *d, dictEntry **bucketref, dictDefragFunctions *defragfns) {
    dictDefragAllocFunction *defragalloc = defragfns->defragAlloc;
    dictDefragAllocFunction *defragkey = defragfns->defragKey;
    dictDefragAllocFunction *defragval = defragfns->defragVal;
    while (bucketref && *bucketref) {
        dictEntry *de = *bucketref, *newde = NULL;
        void *newkey = defragkey ? defragkey(dictGetKey(de)) : NULL;
        void *newval = defragval ? defragval(dictGetVal(de)) : NULL;
        if (entryIsKey(de)) {
            if (newkey) *bucketref = newkey;
        } else if (d->type->no_value) {
            dictEntryNoValue *entry = decodeEntryNoValue(de), *newentry;
            if ((newentry = defragalloc(entry))) {
                newde = (dictEntry *) newentry;
                entry = newentry;
            }
            if (newkey) entry->key = newkey;
        } else {
            assert(entryIsNormal(de));
            newde = defragalloc(de);
            if (newde) de = newde;
            if (newkey) de->key = newkey;
            if (newval) de->v.val = newval;
        }
        if (newde) {
            *bucketref = newde;
        }
        bucketref = dictGetNextLink(*bucketref);
    }
}

/* This is like dictGetRandomKey() from the POV of the API, but will do more
 * work to ensure a better distribution of the returned element.
 *
 * This function improves the distribution because the dictGetRandomKey()
 * problem is that it selects a random bucket, then it selects a random
 * element from the chain in the bucket. However elements being in different
 * chain lengths will have different probabilities of being reported. With
 * this function instead what we do is to consider a "linear" range of the table
 * that may be constituted of N buckets with chains of different lengths
 * appearing one after the other. Then we report a random element in the range.
 * In this way we smooth away the problem of different chain lengths. */
#define GETFAIR_NUM_ENTRIES 15
dictEntry *dictGetFairRandomKey(dict *d) {
    dictEntry *entries[GETFAIR_NUM_ENTRIES];
    unsigned int count = dictGetSomeKeys(d,entries,GETFAIR_NUM_ENTRIES);
    /* Note that dictGetSomeKeys() may return zero elements in an unlucky
     * run() even if there are actually elements inside the hash table. So
     * when we get zero, we call the true dictGetRandomKey() that will always
     * yield the element if the hash table has at least one. */
    if (count == 0) return dictGetRandomKey(d);
    unsigned int idx = rand() % count;
    return entries[idx];
}

/* Function to reverse bits. Algorithm from:
 * http://graphics.stanford.edu/~seander/bithacks.html#ReverseParallel */
static unsigned long rev(unsigned long v) {
    unsigned long s = CHAR_BIT * sizeof(v); // bit size; must be power of 2
    unsigned long mask = ~0UL;
    while ((s >>= 1) > 0) {
        mask ^= (mask << s);
        v = ((v >> s) & mask) | ((v << s) & ~mask);
    }
    return v;
}

/* dictScan() is used to iterate over the elements of a dictionary.
 *
 * Iterating works the following way:
 *
 * 1) Initially you call the function using a cursor (v) value of 0.
 * 2) The function performs one step of the iteration, and returns the
 *    new cursor value you must use in the next call.
 * 3) When the returned cursor is 0, the iteration is complete.
 *
 * The function guarantees all elements present in the
 * dictionary get returned between the start and end of the iteration.
 * However it is possible some elements get returned multiple times.
 *
 * For every element returned, the callback argument 'fn' is
 * called with 'privdata' as first argument and the dictionary entry
 * 'de' as second argument.
 *
 * HOW IT WORKS.
 *
 * The iteration algorithm was designed by Pieter Noordhuis.
 * The main idea is to increment a cursor starting from the higher order
 * bits. That is, instead of incrementing the cursor normally, the bits
 * of the cursor are reversed, then the cursor is incremented, and finally
 * the bits are reversed again.
 *
 * This strategy is needed because the hash table may be resized between
 * iteration calls.
 *
 * dict.c hash tables are always power of two in size, and they
 * use chaining, so the position of an element in a given table is given
 * by computing the bitwise AND between Hash(key) and SIZE-1
 * (where SIZE-1 is always the mask that is equivalent to taking the rest
 *  of the division between the Hash of the key and SIZE).
 *
 * For example if the current hash table size is 16, the mask is
 * (in binary) 1111. The position of a key in the hash table will always be
 * the last four bits of the hash output, and so forth.
 *
 * WHAT HAPPENS IF THE TABLE CHANGES IN SIZE?
 *
 * If the hash table grows, elements can go anywhere in one multiple of
 * the old bucket: for example let's say we already iterated with
 * a 4 bit cursor 1100 (the mask is 1111 because hash table size = 16).
 *
 * If the hash table will be resized to 64 elements, then the new mask will
 * be 111111. The new buckets you obtain by substituting in ??1100
 * with either 0 or 1 can be targeted only by keys we already visited
 * when scanning the bucket 1100 in the smaller hash table.
 *
 * By iterating the higher bits first, because of the inverted counter, the
 * cursor does not need to restart if the table size gets bigger. It will
 * continue iterating using cursors without '1100' at the end, and also
 * without any other combination of the final 4 bits already explored.
 *
 * Similarly when the table size shrinks over time, for example going from
 * 16 to 8, if a combination of the lower three bits (the mask for size 8
 * is 111) were already completely explored, it would not be visited again
 * because we are sure we tried, for example, both 0111 and 1111 (all the
 * variations of the higher bit) so we don't need to test it again.
 *
 * WAIT... YOU HAVE *TWO* TABLES DURING REHASHING!
 *
 * Yes, this is true, but we always iterate the smaller table first, then
 * we test all the expansions of the current cursor into the larger
 * table. For example if the current cursor is 101 and we also have a
 * larger table of size 16, we also test (0)101 and (1)101 inside the larger
 * table. This reduces the problem back to having only one table, where
 * the larger one, if it exists, is just an expansion of the smaller one.
 *
 * LIMITATIONS
 *
 * This iterator is completely stateless, and this is a huge advantage,
 * including no additional memory used.
 *
 * The disadvantages resulting from this design are:
 *
 * 1) It is possible we return elements more than once. However this is usually
 *    easy to deal with in the application level.
 * 2) The iterator must return multiple elements per call, as it needs to always
 *    return all the keys chained in a given bucket, and all the expansions, so
 *    we are sure we don't miss keys moving during rehashing.
 * 3) The reverse cursor is somewhat hard to understand at first, but this
 *    comment is supposed to help.
 */
unsigned long dictScan(dict *d,
                       unsigned long v,
                       dictScanFunction *fn,
                       void *privdata)
{
    return dictScanDefrag(d, v, fn, NULL, privdata);
}

void dictScanDefragBucket(dict *d,dictScanFunction *fn,
                          dictDefragFunctions *defragfns,
                          void *privdata,
                          dictEntry **bucketref) {
    dictEntry **plink, *de, *next;

    /* Emit entries at bucket */
    if (defragfns) dictDefragBucket(d, bucketref, defragfns);

    de = *bucketref;
    plink = bucketref;
    while (de) {
        next = dictGetNext(de);
        fn(privdata, de, plink);

        if (!next) break; /* if last element, break */

        /* if `*plink` still pointing to 'de', then it means that the 
         * visited item wasn't deleted by fn() */
        if (*plink == de)            
            plink = &(de->next);

        de = next;
    }
}

/* Like dictScan, but additionally reallocates the memory used by the dict
 * entries using the provided allocation function. This feature was added for
 * the active defrag feature.
 *
 * The 'defragfns' callbacks are called with a pointer to memory that callback
 * can reallocate. The callbacks should return a new memory address or NULL,
 * where NULL means that no reallocation happened and the old memory is still
 * valid. */
unsigned long dictScanDefrag(dict *d,
                             unsigned long v,
                             dictScanFunction *fn,
                             dictDefragFunctions *defragfns,
                             void *privdata)
{
    int htidx0, htidx1;
    unsigned long m0, m1;

    if (dictSize(d) == 0) return 0;

    /* This is needed in case the scan callback tries to do dictFind or alike. */
    dictPauseRehashing(d);

    if (!dictIsRehashing(d)) {
        htidx0 = 0;
        m0 = DICTHT_SIZE_MASK(d->ht_size_exp[htidx0]);
        dictScanDefragBucket(d, fn, defragfns, privdata, &d->ht_table[htidx0][v & m0]);

        /* Set unmasked bits so incrementing the reversed cursor
         * operates on the masked bits */
        v |= ~m0;

        /* Increment the reverse cursor */
        v = rev(v);
        v++;
        v = rev(v);

    } else {
        htidx0 = 0;
        htidx1 = 1;

        /* Make sure t0 is the smaller and t1 is the bigger table */
        if (DICTHT_SIZE(d->ht_size_exp[htidx0]) > DICTHT_SIZE(d->ht_size_exp[htidx1])) {
            htidx0 = 1;
            htidx1 = 0;
        }

        m0 = DICTHT_SIZE_MASK(d->ht_size_exp[htidx0]);
        m1 = DICTHT_SIZE_MASK(d->ht_size_exp[htidx1]);

        dictScanDefragBucket(d, fn, defragfns, privdata, &d->ht_table[htidx0][v & m0]);

        /* Iterate over indices in larger table that are the expansion
         * of the index pointed to by the cursor in the smaller table */
        do {
            dictScanDefragBucket(d, fn, defragfns, privdata, &d->ht_table[htidx1][v & m1]);

            /* Increment the reverse cursor not covered by the smaller mask.*/
            v |= ~m1;
            v = rev(v);
            v++;
            v = rev(v);

            /* Continue while bits covered by mask difference is non-zero */
        } while (v & (m0 ^ m1));
    }

    dictResumeRehashing(d);

    return v;
}

/* ------------------------- private functions ------------------------------ */

/* Because we may need to allocate huge memory chunk at once when dict
 * resizes, we will check this allocation is allowed or not if the dict
 * type has resizeAllowed member function. */
static int dictTypeResizeAllowed(dict *d, size_t size) {
    if (d->type->resizeAllowed == NULL) return 1;
    return d->type->resizeAllowed(
                    DICTHT_SIZE(_dictNextExp(size)) * sizeof(dictEntry*),
                    (double)d->ht_used[0] / DICTHT_SIZE(d->ht_size_exp[0]));
}

/* Returning DICT_OK indicates a successful expand or the dictionary is undergoing rehashing, 
 * and there is nothing else we need to do about this dictionary currently. While DICT_ERR indicates
 * that expand has not been triggered (may be try shrinking?)*/
int dictExpandIfNeeded(dict *d) {
    /* Incremental rehashing already in progress. Return. */
    if (dictIsRehashing(d)) return DICT_OK;

    /* If the hash table is empty expand it to the initial size. */
    if (DICTHT_SIZE(d->ht_size_exp[0]) == 0) {
        dictExpand(d, DICT_HT_INITIAL_SIZE);
        return DICT_OK;
    }

    /* If we reached the 1:1 ratio, and we are allowed to resize the hash
     * table (global setting) or we should avoid it but the ratio between
     * elements/buckets is over the "safe" threshold, we resize doubling
     * the number of buckets. */
    if ((dict_can_resize == DICT_RESIZE_ENABLE &&
         d->ht_used[0] >= DICTHT_SIZE(d->ht_size_exp[0])) ||
        (dict_can_resize != DICT_RESIZE_FORBID &&
         d->ht_used[0] >= dict_force_resize_ratio * DICTHT_SIZE(d->ht_size_exp[0])))
    {
        if (dictTypeResizeAllowed(d, d->ht_used[0] + 1))
            dictExpand(d, d->ht_used[0] + 1);
        return DICT_OK;
    }
    return DICT_ERR;
}

/* Expand the hash table if needed (OK=Expanded, ERR=Not expanded) */
static int _dictExpandIfNeeded(dict *d) {
    /* Automatic resizing is disallowed. Return */
    if (d->pauseAutoResize > 0) return DICT_ERR;
    
    return dictExpandIfNeeded(d);
}

/* Returning DICT_OK indicates a successful shrinking or the dictionary is undergoing rehashing, 
 * and there is nothing else we need to do about this dictionary currently. While DICT_ERR indicates
 * that shrinking has not been triggered (may be try expanding?)*/
int dictShrinkIfNeeded(dict *d) {
    /* Incremental rehashing already in progress. Return. */
    if (dictIsRehashing(d)) return DICT_OK;
    
    /* If the size of hash table is DICT_HT_INITIAL_SIZE, don't shrink it. */
    if (DICTHT_SIZE(d->ht_size_exp[0]) <= DICT_HT_INITIAL_SIZE) return DICT_OK;

    /* If we reached below 1:8 elements/buckets ratio, and we are allowed to resize
     * the hash table (global setting) or we should avoid it but the ratio is below 1:32,
     * we'll trigger a resize of the hash table. */
    if ((dict_can_resize == DICT_RESIZE_ENABLE &&
         d->ht_used[0] * HASHTABLE_MIN_FILL <= DICTHT_SIZE(d->ht_size_exp[0])) ||
        (dict_can_resize != DICT_RESIZE_FORBID &&
         d->ht_used[0] * HASHTABLE_MIN_FILL * dict_force_resize_ratio <= DICTHT_SIZE(d->ht_size_exp[0])))
    {
        if (dictTypeResizeAllowed(d, d->ht_used[0]))
            dictShrink(d, d->ht_used[0]);
        return DICT_OK;
    }
    return DICT_ERR;
}

static void _dictShrinkIfNeeded(dict *d) 
{
    /* Automatic resizing is disallowed. Return */
    if (d->pauseAutoResize > 0) return;

    dictShrinkIfNeeded(d);
}

static void _dictRehashStepIfNeeded(dict *d, uint64_t visitedIdx) {
    if ((!dictIsRehashing(d)) || (d->pauserehash != 0))
        return;
    /* rehashing not in progress if rehashidx == -1 */
    if ((long)visitedIdx >= d->rehashidx && d->ht_table[0][visitedIdx]) {
        /* If we have a valid hash entry at `idx` in ht0, we perform
         * rehash on the bucket at `idx` (being more CPU cache friendly) */
        _dictBucketRehash(d, visitedIdx);
    } else {
        /* If the hash entry is not in ht0, we rehash the buckets based
         * on the rehashidx (not CPU cache friendly). */
        dictRehash(d,1);
    }
}

/* Our hash table capability is a power of two */
static signed char _dictNextExp(unsigned long size)
{
    if (size <= DICT_HT_INITIAL_SIZE) return DICT_HT_INITIAL_EXP;
    if (size >= LONG_MAX) return (8*sizeof(long)-1);

    return 8*sizeof(long) - __builtin_clzl(size-1);
}

/* Finds and returns the link within the dict where the provided key should
 * be inserted using dictInsertKeyAtLink() if the key does not already exist in
 * the dict. If the key exists in the dict, NULL is returned and the optional
 * 'existing' entry pointer is populated, if provided. */
dictEntryLink dictFindLinkForInsert(dict *d, const void *key, dictEntry **existing) {
    unsigned long idx, table;
    dictCmpCache cmpCache = {0};
    dictEntry *he;
    uint64_t hash = dictHashKey(d, key, d->useStoredKeyApi);
    if (existing) *existing = NULL;
    idx = hash & DICTHT_SIZE_MASK(d->ht_size_exp[0]);

    /* Rehash the hash table if needed */
    _dictRehashStepIfNeeded(d,idx);

    /* Expand the hash table if needed */
    _dictExpandIfNeeded(d);
    keyCmpFunc cmpFunc = dictGetCmpFunc(d);

    for (table = 0; table <= 1; table++) {
        if (table == 0 && (long)idx < d->rehashidx) continue; 
        idx = hash & DICTHT_SIZE_MASK(d->ht_size_exp[table]);
        /* Search if this slot does not already contain the given key */
        he = d->ht_table[table][idx];
        while(he) {
            void *he_key = dictGetKey(he);
            if (key == he_key || cmpFunc(&cmpCache, key, he_key)) {
                if (existing) *existing = he;
                return NULL;
            }
            he = dictGetNext(he);
        }
        if (!dictIsRehashing(d)) break;
    }

    /* If we are in the process of rehashing the hash table, the bucket is
     * always returned in the context of the second (new) hash table. */
    dictEntry **bucket = &d->ht_table[dictIsRehashing(d) ? 1 : 0][idx];
    return bucket;
}


void dictEmpty(dict *d, void(callback)(dict*)) {
    /* Someone may be monitoring a dict that started rehashing, before
     * destroying the dict fake completion. */
    if (dictIsRehashing(d) && d->type->rehashingCompleted)
        d->type->rehashingCompleted(d);

    /* Subtract the size of all buckets. */
    if (d->type->bucketChanged)
        d->type->bucketChanged(d, -(long long)dictBuckets(d));

    _dictClear(d,0,callback);
    _dictClear(d,1,callback);
    d->rehashidx = -1;
    d->pauserehash = 0;
    d->pauseAutoResize = 0;
}

void dictSetResizeEnabled(dictResizeEnable enable) {
    dict_can_resize = enable;
}

uint64_t dictGetHash(dict *d, const void *key) {
    return dictHashKey(d, key, d->useStoredKeyApi);
}

/* Provides the old and new ht size for a given dictionary during rehashing. This method
 * should only be invoked during initialization/rehashing. */
void dictRehashingInfo(dict *d, unsigned long long *from_size, unsigned long long *to_size) {
    /* Invalid method usage if rehashing isn't ongoing. */
    assert(dictIsRehashing(d));
    *from_size = DICTHT_SIZE(d->ht_size_exp[0]);
    *to_size = DICTHT_SIZE(d->ht_size_exp[1]);
}

/* ------------------------------- Debugging ---------------------------------*/
#define DICT_STATS_VECTLEN 50
void dictFreeStats(dictStats *stats) {
    zfree(stats->clvector);
    zfree(stats);
}

void dictCombineStats(dictStats *from, dictStats *into) {
    into->buckets += from->buckets;
    into->maxChainLen = (from->maxChainLen > into->maxChainLen) ? from->maxChainLen : into->maxChainLen;
    into->totalChainLen += from->totalChainLen;
    into->htSize += from->htSize;
    into->htUsed += from->htUsed;
    for (int i = 0; i < DICT_STATS_VECTLEN; i++) {
        into->clvector[i] += from->clvector[i];
    }
}

dictStats *dictGetStatsHt(dict *d, int htidx, int full) {
    unsigned long *clvector = zcalloc(sizeof(unsigned long) * DICT_STATS_VECTLEN);
    dictStats *stats = zcalloc(sizeof(dictStats));
    stats->htidx = htidx;
    stats->clvector = clvector;
    stats->htSize = DICTHT_SIZE(d->ht_size_exp[htidx]);
    stats->htUsed = d->ht_used[htidx];
    if (!full) return stats;
    /* Compute stats. */
    for (unsigned long i = 0; i < DICTHT_SIZE(d->ht_size_exp[htidx]); i++) {
        dictEntry *he;

        if (d->ht_table[htidx][i] == NULL) {
            clvector[0]++;
            continue;
        }
        stats->buckets++;
        /* For each hash entry on this slot... */
        unsigned long chainlen = 0;
        he = d->ht_table[htidx][i];
        while(he) {
            chainlen++;
            he = dictGetNext(he);
        }
        clvector[(chainlen < DICT_STATS_VECTLEN) ? chainlen : (DICT_STATS_VECTLEN-1)]++;
        if (chainlen > stats->maxChainLen) stats->maxChainLen = chainlen;
        stats->totalChainLen += chainlen;
    }

    return stats;
}

/* Generates human readable stats. */
size_t dictGetStatsMsg(char *buf, size_t bufsize, dictStats *stats, int full) {
    if (stats->htUsed == 0) {
        return snprintf(buf,bufsize,
            "Hash table %d stats (%s):\n"
            "No stats available for empty dictionaries\n",
            stats->htidx, (stats->htidx == 0) ? "main hash table" : "rehashing target");
    }
    size_t l = 0;
    l += snprintf(buf + l, bufsize - l,
                  "Hash table %d stats (%s):\n"
                  " table size: %lu\n"
                  " number of elements: %lu\n",
                  stats->htidx, (stats->htidx == 0) ? "main hash table" : "rehashing target",
                  stats->htSize, stats->htUsed);
    if (full) {
        l += snprintf(buf + l, bufsize - l,
                      " different slots: %lu\n"
                      " max chain length: %lu\n"
                      " avg chain length (counted): %.02f\n"
                      " avg chain length (computed): %.02f\n"
                      " Chain length distribution:\n",
                      stats->buckets, stats->maxChainLen,
                      (float) stats->totalChainLen / stats->buckets, (float) stats->htUsed / stats->buckets);

        for (unsigned long i = 0; i < DICT_STATS_VECTLEN - 1; i++) {
            if (stats->clvector[i] == 0) continue;
            if (l >= bufsize) break;
            l += snprintf(buf + l, bufsize - l,
                          "   %ld: %ld (%.02f%%)\n",
                          i, stats->clvector[i], ((float) stats->clvector[i] / stats->htSize) * 100);
        }
    }

    /* Make sure there is a NULL term at the end. */
    buf[bufsize-1] = '\0';
    /* Unlike snprintf(), return the number of characters actually written. */
    return strlen(buf);
}

void dictGetStats(char *buf, size_t bufsize, dict *d, int full) {
    size_t l;
    char *orig_buf = buf;
    size_t orig_bufsize = bufsize;

    dictStats *mainHtStats = dictGetStatsHt(d, 0, full);
    l = dictGetStatsMsg(buf, bufsize, mainHtStats, full);
    dictFreeStats(mainHtStats);
    buf += l;
    bufsize -= l;
    if (dictIsRehashing(d) && bufsize > 0) {
        dictStats *rehashHtStats = dictGetStatsHt(d, 1, full);
        dictGetStatsMsg(buf, bufsize, rehashHtStats, full);
        dictFreeStats(rehashHtStats);
    }
    /* Make sure there is a NULL term at the end. */
    orig_buf[orig_bufsize-1] = '\0';
}

static int dictDefaultCompare(dictCmpCache *cache, const void *key1, const void *key2) {
    (void)(cache); /*unused*/
    return key1 == key2;
}

/* ------------------------------- Benchmark ---------------------------------*/

#ifdef REDIS_TEST
#include "testhelp.h"

#define UNUSED(V) ((void) V)
#define TEST(name) printf("test — %s\n", name);

uint64_t hashCallback(const void *key) {
    return dictGenHashFunction((unsigned char*)key, strlen((char*)key));
}

int compareCallback(dictCmpCache *cache, const void *key1, const void *key2) {
    int l1,l2;
    UNUSED(cache);

    l1 = strlen((char*)key1);
    l2 = strlen((char*)key2);
    if (l1 != l2) return 0;
    return memcmp(key1, key2, l1) == 0;
}

void freeCallback(dict *d, void *val) {
    UNUSED(d);

    zfree(val);
}

char *stringFromLongLong(long long value) {
    char buf[32];
    int len;
    char *s;

    len = snprintf(buf,sizeof(buf),"%lld",value);
    s = zmalloc(len+1);
    memcpy(s, buf, len);
    s[len] = '\0';
    return s;
}

char *stringFromSubstring(void) {
    #define LARGE_STRING_SIZE 10000
    #define MIN_STRING_SIZE 100
    #define MAX_STRING_SIZE 500
    static char largeString[LARGE_STRING_SIZE + 1];
    static int init = 0;
    if (init == 0) {
        /* Generate a large string */
        for (size_t i = 0; i < LARGE_STRING_SIZE; i++) {
            /* Random printable ASCII character (33 to 126) */
            largeString[i] = 33 + (rand() % 94);
        }
        /* Null-terminate the large string */
        largeString[LARGE_STRING_SIZE] = '\0';
        init = 1;
    }
    /* Randomly choose a size between minSize and maxSize */
    size_t substringSize = MIN_STRING_SIZE + (rand() % (MAX_STRING_SIZE - MIN_STRING_SIZE + 1));
    size_t startIndex = rand() % (LARGE_STRING_SIZE - substringSize + 1);
    /* Allocate memory for the substring (+1 for null terminator) */
    char *s = zmalloc(substringSize + 1);
    memcpy(s, largeString + startIndex, substringSize); // Copy the substring
    s[substringSize] = '\0'; // Null-terminate the string
    return s;
}

dictType BenchmarkDictType = {
    hashCallback,
    NULL,
    NULL,
    compareCallback,
    freeCallback,
    NULL,
    NULL
};

#define start_benchmark() start = timeInMilliseconds()
#define end_benchmark(msg) do { \
    elapsed = timeInMilliseconds()-start; \
    printf(msg ": %ld items in %lld ms\n", count, elapsed); \
} while(0)

/* ./redis-server test dict [<count> | --accurate] */
int dictTest(int argc, char **argv, int flags) {
    long j;
    long long start, elapsed;
    int retval;
    dict *d = dictCreate(&BenchmarkDictType);
    dictEntry* de = NULL;
    dictEntry* existing = NULL;
    long count = 0;
    unsigned long new_dict_size, current_dict_used, remain_keys;
    int accurate = (flags & REDIS_TEST_ACCURATE);

    if (argc == 4) {
        if (accurate) {
            count = 5000000;
        } else {
            count = strtol(argv[3],NULL,10);
        }
    } else {
        count = 5000;
    }

    TEST("Add 16 keys and verify dict resize is ok") {
        dictSetResizeEnabled(DICT_RESIZE_ENABLE);
        for (j = 0; j < 16; j++) {
            retval = dictAdd(d,stringFromLongLong(j),(void*)j);
            assert(retval == DICT_OK);
        }
        while (dictIsRehashing(d)) dictRehashMicroseconds(d,1000);
        assert(dictSize(d) == 16);
        assert(dictBuckets(d) == 16);
    }

    TEST("Use DICT_RESIZE_AVOID to disable the dict resize and pad to (dict_force_resize_ratio * 16)") {
        /* Use DICT_RESIZE_AVOID to disable the dict resize, and pad
         * the number of keys to (dict_force_resize_ratio * 16), so we can satisfy
         * dict_force_resize_ratio in next test. */
        dictSetResizeEnabled(DICT_RESIZE_AVOID);
        for (j = 16; j < (long)dict_force_resize_ratio * 16; j++) {
            retval = dictAdd(d,stringFromLongLong(j),(void*)j);
            assert(retval == DICT_OK);
        }
        current_dict_used = dict_force_resize_ratio * 16;
        assert(dictSize(d) == current_dict_used);
        assert(dictBuckets(d) == 16);
    }

    TEST("Add one more key, trigger the dict resize") {
        retval = dictAdd(d,stringFromLongLong(current_dict_used),(void*)(current_dict_used));
        assert(retval == DICT_OK);
        current_dict_used++;
        new_dict_size = 1UL << _dictNextExp(current_dict_used);
        assert(dictSize(d) == current_dict_used);
        assert(DICTHT_SIZE(d->ht_size_exp[0]) == 16);
        assert(DICTHT_SIZE(d->ht_size_exp[1]) == new_dict_size);

        /* Wait for rehashing. */
        dictSetResizeEnabled(DICT_RESIZE_ENABLE);
        while (dictIsRehashing(d)) dictRehashMicroseconds(d,1000);
        assert(dictSize(d) == current_dict_used);
        assert(DICTHT_SIZE(d->ht_size_exp[0]) == new_dict_size);
        assert(DICTHT_SIZE(d->ht_size_exp[1]) == 0);
    }

    TEST("Delete keys until we can trigger shrink in next test") {
        /* Delete keys until we can satisfy (1 / HASHTABLE_MIN_FILL) in the next test. */
        for (j = new_dict_size / HASHTABLE_MIN_FILL + 1; j < (long)current_dict_used; j++) {
            char *key = stringFromLongLong(j);
            retval = dictDelete(d, key);
            zfree(key);
            assert(retval == DICT_OK);
        }
        current_dict_used = new_dict_size / HASHTABLE_MIN_FILL + 1;
        assert(dictSize(d) == current_dict_used);
        assert(DICTHT_SIZE(d->ht_size_exp[0]) == new_dict_size);
        assert(DICTHT_SIZE(d->ht_size_exp[1]) == 0);
    }

    TEST("Delete one more key, trigger the dict resize") {
        current_dict_used--;
        char *key = stringFromLongLong(current_dict_used);
        retval = dictDelete(d, key);
        zfree(key);
        unsigned long oldDictSize = new_dict_size;
        new_dict_size = 1UL << _dictNextExp(current_dict_used);
        assert(retval == DICT_OK);
        assert(dictSize(d) == current_dict_used);
        assert(DICTHT_SIZE(d->ht_size_exp[0]) == oldDictSize);
        assert(DICTHT_SIZE(d->ht_size_exp[1]) == new_dict_size);

        /* Wait for rehashing. */
        while (dictIsRehashing(d)) dictRehashMicroseconds(d,1000);
        assert(dictSize(d) == current_dict_used);
        assert(DICTHT_SIZE(d->ht_size_exp[0]) == new_dict_size);
        assert(DICTHT_SIZE(d->ht_size_exp[1]) == 0);
    }

    TEST("Empty the dictionary and add 128 keys") {
        dictEmpty(d, NULL);
        for (j = 0; j < 128; j++) {
            retval = dictAdd(d,stringFromLongLong(j),(void*)j);
            assert(retval == DICT_OK);
        }
        while (dictIsRehashing(d)) dictRehashMicroseconds(d,1000);
        assert(dictSize(d) == 128);
        assert(dictBuckets(d) == 128);
    }

    TEST("Use DICT_RESIZE_AVOID to disable the dict resize and reduce to 3") {
        /* Use DICT_RESIZE_AVOID to disable the dict reset, and reduce
         * the number of keys until we can trigger shrinking in next test. */
        dictSetResizeEnabled(DICT_RESIZE_AVOID);
        remain_keys = DICTHT_SIZE(d->ht_size_exp[0]) / (HASHTABLE_MIN_FILL * dict_force_resize_ratio) + 1;
        for (j = remain_keys; j < 128; j++) {
            char *key = stringFromLongLong(j);
            retval = dictDelete(d, key);
            zfree(key);
            assert(retval == DICT_OK);
        }
        current_dict_used = remain_keys;
        assert(dictSize(d) == remain_keys);
        assert(dictBuckets(d) == 128);
    }

    TEST("Delete one more key, trigger the dict resize") {
        current_dict_used--;
        char *key = stringFromLongLong(current_dict_used);
        retval = dictDelete(d, key);
        zfree(key);
        new_dict_size = 1UL << _dictNextExp(current_dict_used);
        assert(retval == DICT_OK);
        assert(dictSize(d) == current_dict_used);
        assert(DICTHT_SIZE(d->ht_size_exp[0]) == 128);
        assert(DICTHT_SIZE(d->ht_size_exp[1]) == new_dict_size);

        /* Wait for rehashing. */
        dictSetResizeEnabled(DICT_RESIZE_ENABLE);
        while (dictIsRehashing(d)) dictRehashMicroseconds(d,1000);
        assert(dictSize(d) == current_dict_used);
        assert(DICTHT_SIZE(d->ht_size_exp[0]) == new_dict_size);
        assert(DICTHT_SIZE(d->ht_size_exp[1]) == 0);
    }

    TEST("Restore to original state") {
        dictEmpty(d, NULL);
        dictSetResizeEnabled(DICT_RESIZE_ENABLE);
    }
    srand(12345);
    start_benchmark();
    for (j = 0; j < count; j++) {
        /* Create a dynamically allocated substring */
        char *key = stringFromSubstring();

        /* Insert the range directly from the large string */
        de = dictAddRaw(d, key, &existing);
        assert(de != NULL || existing != NULL);
        /* If key already exists NULL is returned so we need to free the temp key string */
        if (de == NULL) zfree(key);
    }
    end_benchmark("Inserting random substrings (100-500B) from large string with symbols");
    assert((long)dictSize(d) <= count);
    dictEmpty(d, NULL);

    start_benchmark();
    for (j = 0; j < count; j++) {
        retval = dictAdd(d,stringFromLongLong(j),(void*)j);
        assert(retval == DICT_OK);
    }
    end_benchmark("Inserting via dictAdd() non existing");
    assert((long)dictSize(d) == count);

    dictEmpty(d, NULL);

    start_benchmark();
    for (j = 0; j < count; j++) {
        de = dictAddRaw(d,stringFromLongLong(j),NULL);
        assert(de != NULL);
    }
    end_benchmark("Inserting via dictAddRaw() non existing");
    assert((long)dictSize(d) == count);

    start_benchmark();
    for (j = 0; j < count; j++) {
        void *key = stringFromLongLong(j);
        de = dictAddRaw(d,key,&existing);
        assert(existing != NULL);
        zfree(key);
    }
    end_benchmark("Inserting via dictAddRaw() existing (no insertion)");
    assert((long)dictSize(d) == count);

    /* Wait for rehashing. */
    while (dictIsRehashing(d)) {
        dictRehashMicroseconds(d,100*1000);
    }

    start_benchmark();
    for (j = 0; j < count; j++) {
        char *key = stringFromLongLong(j);
        dictEntry *de = dictFind(d,key);
        assert(de != NULL);
        zfree(key);
    }
    end_benchmark("Linear access of existing elements");

    start_benchmark();
    for (j = 0; j < count; j++) {
        char *key = stringFromLongLong(j);
        dictEntry *de = dictFind(d,key);
        assert(de != NULL);
        zfree(key);
    }
    end_benchmark("Linear access of existing elements (2nd round)");

    start_benchmark();
    for (j = 0; j < count; j++) {
        char *key = stringFromLongLong(rand() % count);
        dictEntry *de = dictFind(d,key);
        assert(de != NULL);
        zfree(key);
    }
    end_benchmark("Random access of existing elements");

    start_benchmark();
    for (j = 0; j < count; j++) {
        dictEntry *de = dictGetRandomKey(d);
        assert(de != NULL);
    }
    end_benchmark("Accessing random keys");

    start_benchmark();
    for (j = 0; j < count; j++) {
        char *key = stringFromLongLong(rand() % count);
        key[0] = 'X';
        dictEntry *de = dictFind(d,key);
        assert(de == NULL);
        zfree(key);
    }
    end_benchmark("Accessing missing");

    start_benchmark();
    for (j = 0; j < count; j++) {
        char *key = stringFromLongLong(j);
        retval = dictDelete(d,key);
        assert(retval == DICT_OK);
        key[0] += 17; /* Change first number to letter. */
        retval = dictAdd(d,key,(void*)j);
        assert(retval == DICT_OK);
    }
    end_benchmark("Removing and adding");
    dictRelease(d);

    TEST("Use dict without values (no_value=1)") {
        dictType dt = BenchmarkDictType;
        dt.no_value = 1;

        /* Allocate array of size count and fill it with keys (stringFromLongLong(j) */
        char **lookupKeys = zmalloc(sizeof(char*) * count);
        for (long j = 0; j < count; j++)
            lookupKeys[j] = stringFromLongLong(j);


        /* Add keys without values. */
        dict *d = dictCreate(&dt);
        for (j = 0; j < count; j++) {
            retval = dictAdd(d,lookupKeys[j],NULL);
            assert(retval == DICT_OK);
        }

        /* Now, we should be able to find the keys. */
        for (j = 0; j < count; j++) {
            dictEntry *de = dictFind(d,lookupKeys[j]);
            assert(de != NULL);
        }

        /* Find non exists keys. */
        for (j = 0; j < count; j++) {
            /* Temporarily override first char of key */
            char tmp = lookupKeys[j][0];
            lookupKeys[j][0] = 'X';
            dictEntry *de = dictFind(d,lookupKeys[j]);
            lookupKeys[j][0] = tmp;
            assert(de == NULL);
        }

        dictRelease(d);
        zfree(lookupKeys);
    }

    TEST("Test dictFindLink() functionality") {
        dictType dt = BenchmarkDictType;
        dict *d = dictCreate(&dt);
        
        /* find in empty dict */
        dictEntryLink link = dictFindLink(d, "key", NULL);
        assert(link == NULL);

        /* Add keys to dict and test */
        for (j = 0; j < 10; j++) {
            /* Add another key to dict */
            char *key = stringFromLongLong(j);
            retval = dictAdd(d, key, (void*)j);
            assert(retval == DICT_OK);
            /* find existing keys with dictFindLink() */
            dictEntryLink link = dictFindLink(d, key, NULL);
            assert(link != NULL);
            assert(*link != NULL);
            assert(dictGetKey(*link) != NULL);
            
            /* Test that the key found is the correct one */
            void *foundKey = dictGetKey(*link);
            assert(compareCallback( NULL, foundKey, key));

            /* Test finding a non-existing key */
            char *nonExistingKey = stringFromLongLong(j + 10);
            link = dictFindLink(d, nonExistingKey, NULL);
            assert(link == NULL);

            /* Test with bucket parameter */
            dictEntryLink bucket = NULL;
            link = dictFindLink(d, key, &bucket);
            assert(link != NULL);
            assert(bucket != NULL);

            /* Test bucket parameter with non-existing key */
            link = dictFindLink(d, nonExistingKey, &bucket);
            assert(link == NULL);
            assert(bucket != NULL); /* Bucket should still be set even for non-existing keys */

            /* Clean up */
            zfree(nonExistingKey);
        }

        dictRelease(d);
    }

    return 0;
}
#endif
