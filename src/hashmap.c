/*
 CCCC: Comprehensiev C Compensation Compiler

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

// Forward declarations for internal raw operations (no key copying)
static void hashmap_put2_raw(HashMap *map, const char *key, int keylen, void *val);
static HashEntry *get_or_insert_entry_raw(HashMap *map, char *key, int keylen);

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
            hashmap_put2_raw(&map2, ent->key, ent->keylen, ent->val);
    }

    assert(map2.used == nkeys);

    // Free the old buckets before replacing with new ones
    free(map->buckets);
    *map = map2;
}

static bool match(HashEntry *ent, char *key, int keylen) {
    if (!ent->key || ent->key == TOMBSTONE || ent->keylen != keylen)
        return false;
    // Integer keys (keylen == -1) store the value in the pointer itself, so
    // compare pointer values directly. This path is hit when rehash() re-inserts
    // int-keyed entries via the raw string path; memcmp(key, key, -1) would
    // otherwise read with a negative size and corrupt/crash.
    if (keylen == -1)
        return ent->key == key;
    return memcmp(ent->key, key, keylen) == 0;
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

static HashEntry *get_or_insert_entry_raw(HashMap *map, char *key, int keylen) {
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

// Wrapper that makes an owned copy of string keys so the HashMap controls
// their lifetime. Integer keys (keylen == -1) are stored as-is.
static HashEntry *get_or_insert_entry(HashMap *map, const char *key, int keylen) {
    char *owned_key = (char *)key;
    if (keylen != -1) {
        owned_key = malloc(keylen + 1);
        if (!owned_key) {
            fprintf(stderr, "FATAL: out of memory in HashMap put\n");
            exit(1);
        }
        memcpy(owned_key, key, keylen);
        owned_key[keylen] = '\0';
    }

    HashEntry *ent = get_or_insert_entry_raw(map, owned_key, keylen);
    if (keylen != -1 && ent->key != owned_key) {
        // Existing entry found; free our unused copy.
        free(owned_key);
    }
    return ent;
}

void *hashmap_get2(HashMap *map, const char *key, int keylen) {
    HashEntry *ent = get_entry(map, (char*)key, keylen);
    return ent ? ent->val : NULL;
}

void *hashmap_get(HashMap *map, const char *key) {
    return hashmap_get2(map, key, strlen(key));
}

static void hashmap_put2_raw(HashMap *map, const char *key, int keylen, void *val) {
    HashEntry *ent = get_or_insert_entry_raw(map, (char*)key, keylen);
    ent->val = val;
}

void hashmap_put2(HashMap *map, const char *key, int keylen, void *val) {
    HashEntry *ent = get_or_insert_entry(map, key, keylen);
    ent->val = val;
}

void hashmap_put(HashMap *map, const char *key, void *val) {
    hashmap_put2(map, key, strlen(key), val);
}

// Borrowed key variants: the caller guarantees the key outlives the map.
void hashmap_put2_borrowed(HashMap *map, const char *key, int keylen, void *val) {
    HashEntry *ent = get_or_insert_entry_raw(map, (char*)key, keylen);
    ent->val = val;
}

void hashmap_put_borrowed(HashMap *map, const char *key, void *val) {
    hashmap_put2_borrowed(map, key, strlen(key), val);
}

void hashmap_delete2(HashMap *map, const char *key, int keylen) {
    HashEntry *ent = get_entry(map, (char*)key, keylen);
    if (ent) {
        if (ent->keylen != -1)
            free(ent->key);
        ent->key = TOMBSTONE;
    }
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

void hashmap_deinit(HashMap *map) {
    if (!map || !map->buckets)
        return;
    for (int i = 0; i < map->capacity; i++) {
        HashEntry *ent = &map->buckets[i];
        if (ent->key && ent->key != TOMBSTONE && ent->keylen != -1)
            free(ent->key);
    }
    free(map->buckets);
    map->buckets = NULL;
    map->capacity = 0;
    map->used = 0;
}

// Like hashmap_deinit but does not free keys (for borrowed-key maps).
void hashmap_deinit_borrowed(HashMap *map) {
    if (!map || !map->buckets)
        return;
    free(map->buckets);
    map->buckets = NULL;
    map->capacity = 0;
    map->used = 0;
}

// Clone a HashMap for snapshot/restore around a preprocessor sub-pass.
// The bucket array and owned string keys are deep-copied so the snapshot is
// fully independent of the live map; values (arena-allocated) are shared.
// Restore with hashmap_restore().
HashMap hashmap_snapshot(const HashMap *map) {
    HashMap snap = *map;
    if (map->capacity > 0) {
        snap.buckets = malloc(map->capacity * sizeof(HashEntry));
        if (!snap.buckets) {
            fprintf(stderr, "FATAL: out of memory in hashmap_snapshot\n");
            exit(1);
        }
        memcpy(snap.buckets, map->buckets, map->capacity * sizeof(HashEntry));
        for (int i = 0; i < map->capacity; i++) {
            HashEntry *ent = &snap.buckets[i];
            if (ent->key && ent->key != TOMBSTONE && ent->keylen != -1) {
                char *k = malloc(ent->keylen + 1);
                if (!k) {
                    fprintf(stderr, "FATAL: out of memory in hashmap_snapshot\n");
                    exit(1);
                }
                memcpy(k, ent->key, ent->keylen + 1);
                ent->key = k;
            }
        }
    }
    return snap;
}

// Restore a HashMap from a snapshot taken with hashmap_snapshot.
// Frees the live map's bucket array and owned string keys (values are
// arena-allocated and shared, so not freed) before installing the snapshot,
// which owns its own independent copies.
void hashmap_restore(HashMap *map, HashMap snapshot) {
    for (int i = 0; i < map->capacity; i++) {
        HashEntry *ent = &map->buckets[i];
        if (ent->key && ent->key != TOMBSTONE && ent->keylen != -1)
            free(ent->key);
    }
    free(map->buckets);
    *map = snapshot;
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
