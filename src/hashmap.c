/*
 JCC: JIT C Compiler

 Copyright (C) 2025 George Watson

 This program is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.

 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with this program.  If not, see <https://www.gnu.org/licenses/>.

 This file was original part of chibicc by Rui Ueyama (MIT) https://github.com/rui314/chibicc
*/

// This is an implementation of the open-addressing hash table.

#include "jcc.h"
#include "./internal.h"

// Initial hash bucket size
#define INIT_SIZE 16

// Rehash if the usage exceeds 70%.
#define HIGH_WATERMARK 70

// We'll keep the usage below 50% after rehashing.
#define LOW_WATERMARK 50

// Represents a deleted hash entry
#define TOMBSTONE ((void *)-1)

static uint64_t fnv_hash(char *s, int len) {
    uint64_t hash = 0xcbf29ce484222325;
    for (int i = 0; i < len; i++) {
        hash *= 0x100000001b3;
        hash ^= (unsigned char)s[i];
    }
    return hash;
}

// Simple hash function for integer keys
static uint64_t int_hash(long long key) {
    // Mix the bits using a simple multiplicative hash
    uint64_t hash = (uint64_t)key;
    hash ^= hash >> 33;
    hash *= 0xff51afd7ed558ccd;
    hash ^= hash >> 33;
    hash *= 0xc4ceb9fe1a85ec53;
    hash ^= hash >> 33;
    return hash;
}

void hashmap_put2(HashMap *map, const char *key, int keylen, void *val);

// Make room for new entires in a given hashmap by removing
// tombstones and possibly extending the bucket size.
static void rehash(HashMap *map) {
    // Compute the size of the new hashmap.
    int nkeys = 0;
    for (int i = 0; i < map->capacity; i++)
        if (map->buckets[i].key && map->buckets[i].key != TOMBSTONE)
            nkeys++;

    int cap = map->capacity;
    while ((nkeys * 100) / cap >= LOW_WATERMARK)
        cap = cap * 2;
    assert(cap > 0);

    // Create a new hashmap and copy all key-values.
    HashMap map2 = {};
    map2.buckets = calloc(cap, sizeof(HashEntry));
    if (!map2.buckets) {
        fprintf(stderr, "FATAL: out of memory in HashMap rehash\n");
        exit(1);  // Cannot continue without memory
    }
    map2.capacity = cap;

    for (int ii = 0; ii < map->capacity; ii++) {
        HashEntry *ent = &map->buckets[ii];
        if (ent->key && ent->key != TOMBSTONE)
            hashmap_put2(&map2, ent->key, ent->keylen, ent->val);
    }

    assert(map2.used == nkeys);

    // Free the old buckets before replacing with new ones
    free(map->buckets);
    *map = map2;
}

static bool match(HashEntry *ent, char *key, int keylen) {
    return ent->key && ent->key != TOMBSTONE &&
    ent->keylen == keylen && memcmp(ent->key, key, keylen) == 0;
}

static HashEntry *get_entry(HashMap *map, char *key, int keylen) {
    if (!map->buckets)
        return NULL;

    uint64_t hash = fnv_hash(key, keylen);

    for (int i = 0; i < map->capacity; i++) {
        HashEntry *ent = &map->buckets[(hash + i) % map->capacity];
        if (match(ent, key, keylen))
            return ent;
        if (ent->key == NULL)
            return NULL;
    }
    unreachable();
    return NULL;
}

static HashEntry *get_or_insert_entry(HashMap *map, char *key, int keylen) {
    if (!map->buckets) {
        map->buckets = calloc(INIT_SIZE, sizeof(HashEntry));
        if (!map->buckets) {
            fprintf(stderr, "FATAL: out of memory in HashMap initialization\n");
            exit(1);
        }
        map->capacity = INIT_SIZE;
    } else if ((map->used * 100) / map->capacity >= HIGH_WATERMARK) {
        rehash(map);
    }

    uint64_t hash = fnv_hash(key, keylen);

    for (int i = 0; i < map->capacity; i++) {
        HashEntry *ent = &map->buckets[(hash + i) % map->capacity];

        if (match(ent, key, keylen))
            return ent;

        if (ent->key == TOMBSTONE) {
            ent->key = key;
            ent->keylen = keylen;
            return ent;
        }

        if (ent->key == NULL) {
            ent->key = key;
            ent->keylen = keylen;
            map->used++;
            return ent;
        }
    }
    unreachable();
    return NULL;
}

void *hashmap_get2(HashMap *map, const char *key, int keylen) {
    HashEntry *ent = get_entry(map, (char*)key, keylen);
    return ent ? ent->val : NULL;
}

void *hashmap_get(HashMap *map, const char *key) {
    return hashmap_get2(map, key, strlen(key));
}

void hashmap_put2(HashMap *map, const char *key, int keylen, void *val) {
    HashEntry *ent = get_or_insert_entry(map, (char*)key, keylen);
    ent->val = val;
}

void hashmap_put(HashMap *map, const char *key, void *val) {
    hashmap_put2(map, key, strlen(key), val);
}

void hashmap_delete2(HashMap *map, const char *key, int keylen) {
    HashEntry *ent = get_entry(map, (char*)key, keylen);
    if (ent)
        ent->key = TOMBSTONE;
}


void hashmap_delete(HashMap *map, const char *key) {
    hashmap_delete2(map, key, strlen(key));
}

// Integer key HashMap functions
// These avoid the overhead of snprintf() and strdup() for integer keys

static bool match_int(HashEntry *ent, long long key) {
    // For integer keys, we store the key as a pointer value
    // keylen is set to -1 to distinguish from string keys
    return ent->key && ent->key != TOMBSTONE &&
           ent->keylen == -1 && (long long)ent->key == key;
}

static HashEntry *get_entry_int(HashMap *map, long long key) {
    if (!map->buckets)
        return NULL;

    uint64_t hash = int_hash(key);

    for (int i = 0; i < map->capacity; i++) {
        HashEntry *ent = &map->buckets[(hash + i) % map->capacity];
        if (match_int(ent, key))
            return ent;
        if (ent->key == NULL)
            return NULL;
    }
    return NULL;
}

static HashEntry *get_or_insert_entry_int(HashMap *map, long long key) {
    if (!map->buckets) {
        map->buckets = calloc(INIT_SIZE, sizeof(HashEntry));
        if (!map->buckets) {
            fprintf(stderr, "FATAL: out of memory in HashMap initialization\n");
            exit(1);
        }
        map->capacity = INIT_SIZE;
    } else if ((map->used * 100) / map->capacity >= HIGH_WATERMARK) {
        rehash(map);
    }

    uint64_t hash = int_hash(key);

    for (int i = 0; i < map->capacity; i++) {
        HashEntry *ent = &map->buckets[(hash + i) % map->capacity];

        if (match_int(ent, key))
            return ent;

        if (ent->key == TOMBSTONE) {
            ent->key = (char *)key;
            ent->keylen = -1;  // Mark as integer key
            return ent;
        }

        if (ent->key == NULL) {
            ent->key = (char *)key;
            ent->keylen = -1;  // Mark as integer key
            map->used++;
            return ent;
        }
    }
    unreachable();
    return NULL;
}

void *hashmap_get_int(HashMap *map, long long key) {
    HashEntry *ent = get_entry_int(map, key);
    return ent ? ent->val : NULL;
}

void hashmap_put_int(HashMap *map, long long key, void *val) {
    HashEntry *ent = get_or_insert_entry_int(map, key);
    ent->val = val;
}

void hashmap_delete_int(HashMap *map, long long key) {
    HashEntry *ent = get_entry_int(map, key);
    if (ent)
        ent->key = TOMBSTONE;
}

// Iterate over all entries in the map, calling the iterator function
// for each valid entry. The iterator receives the key, keylen, value,
// and user_data. If the iterator returns non-zero, iteration stops.
void hashmap_foreach(HashMap *map, HashMapIterator iter, void *user_data) {
    if (!map || !map->buckets || !iter)
        return;
    
    for (int i = 0; i < map->capacity; i++) {
        HashEntry *ent = &map->buckets[i];
        
        // Skip empty and deleted entries
        if (!ent->key || ent->key == TOMBSTONE)
            continue;
        
        // Call user callback
        int result = iter(ent->key, ent->keylen, ent->val, user_data);
        if (result != 0)
            break;  // User requested stop
    }
}

// Count entries that match a predicate
// The predicate function should return non-zero for entries to count
int hashmap_count_if(HashMap *map, HashMapIterator predicate, void *user_data) {
    if (!map || !map->buckets || !predicate)
        return 0;
    
    int count = 0;
    for (int i = 0; i < map->capacity; i++) {
        HashEntry *ent = &map->buckets[i];
        
        // Skip empty and deleted entries
        if (!ent->key || ent->key == TOMBSTONE)
            continue;
        
        // Call predicate and count if it returns non-zero
        if (predicate(ent->key, ent->keylen, ent->val, user_data) != 0)
            count++;
    }
    return count;
}

void hashmap_test(void) {
    HashMap *map = calloc(1, sizeof(HashMap));
    if (!map) {
        fprintf(stderr, "FATAL: out of memory in hashmap_test\n");
        exit(1);
    }

    for (int i = 0; i < 5000; i++)
        hashmap_put(map, format("key %d", i), (void *)(size_t)i);
    for (int i2 = 1000; i2 < 2000; i2++)
        hashmap_delete(map, format("key %d", i2));
    for (int i3 = 1500; i3 < 1600; i3++)
        hashmap_put(map, format("key %d", i3), (void *)(size_t)i3);
    for (int i4 = 6000; i4 < 7000; i4++)
        hashmap_put(map, format("key %d", i4), (void *)(size_t)i4);

    for (int i5 = 0; i5 < 1000; i5++)
        assert((size_t)hashmap_get(map, format("key %d", i5)) == i5);
    for (int i6 = 1000; i6 < 1500; i6++)
        assert(hashmap_get(map, "no such key") == NULL);
    for (int i7 = 1500; i7 < 1600; i7++)
        assert((size_t)hashmap_get(map, format("key %d", i7)) == i7);
    for (int i8 = 1600; i8 < 2000; i8++)
        assert(hashmap_get(map, "no such key") == NULL);
    for (int i9 = 2000; i9 < 5000; i9++)
        assert((size_t)hashmap_get(map, format("key %d", i9)) == i9);
    for (int i0 = 5000; i0 < 6000; i0++)
        assert(hashmap_get(map, "no such key") == NULL);
    for (int i10 = 6000; i10 < 7000; i10++)
        hashmap_put(map, format("key %d", i10), (void *)(size_t)i10);

    assert(hashmap_get(map, "no such key") == NULL);
    printf("OK\n");
}

// Test callback for counting entries
static int count_callback(char *key, int keylen, void *val, void *user_data) {
    int *count = (int *)user_data;
    (*count)++;
    return 0;  // Continue iteration
}

// Test callback that stops after 3 entries
static int stop_after_3_callback(char *key, int keylen, void *val, void *user_data) {
    int *count = (int *)user_data;
    (*count)++;
    return (*count >= 3) ? 1 : 0;  // Stop after 3
}

// Test callback for count_if (count entries with value > 100)
static int value_gt_100_predicate(char *key, int keylen, void *val, void *user_data) {
    return ((size_t)val > 100) ? 1 : 0;
}

void hashmap_test_iteration(void) {
    HashMap map = {};
    
    // Add some test entries
    hashmap_put(&map, "key1", (void *)(size_t)10);
    hashmap_put(&map, "key2", (void *)(size_t)200);
    hashmap_put(&map, "key3", (void *)(size_t)30);
    hashmap_put(&map, "key4", (void *)(size_t)400);
    hashmap_put(&map, "key5", (void *)(size_t)50);
    
    // Test 1: Count all entries using foreach
    int count = 0;
    hashmap_foreach(&map, count_callback, &count);
    assert(count == 5);
    
    // Test 2: Stop iteration early
    count = 0;
    hashmap_foreach(&map, stop_after_3_callback, &count);
    assert(count == 3);
    
    // Test 3: Count entries with value > 100
    int count_gt_100 = hashmap_count_if(&map, value_gt_100_predicate, NULL);
    assert(count_gt_100 == 2);  // key2 (200) and key4 (400)
    
    // Test 4: Delete an entry and verify count decreases
    hashmap_delete(&map, "key3");
    count = 0;
    hashmap_foreach(&map, count_callback, &count);
    assert(count == 4);
    
    // Test 5: Test with empty map
    HashMap empty_map = {};
    count = 0;
    hashmap_foreach(&empty_map, count_callback, &count);
    assert(count == 0);
    
    // Test 6: Test with NULL iterator (should not crash)
    hashmap_foreach(&map, NULL, NULL);
    
    printf("HashMap iteration tests: OK\n");
}
