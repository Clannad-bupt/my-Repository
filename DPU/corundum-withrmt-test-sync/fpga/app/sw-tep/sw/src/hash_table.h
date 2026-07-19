#ifndef HASH_TABLE_H
#define HASH_TABLE_H

#include <stdint.h>
#include <stdlib.h>
/*
#define TABLE_SIZE 256

typedef struct HashItem {
    uint64_t key;
    void* value;
    struct HashItem* next;
} HashItem;

typedef struct {
    HashItem** items; 
} HashTable;

void initHashTable(HashTable* table);
uint64_t hashFunction(uint64_t key);
void insertHashTable(HashTable* table, uint64_t key, void* value);
void* searchHashTable(HashTable* table, uint64_t key);
void freeHashTable(HashTable* table); 

#endif
*/

#define TABLE_SIZE 256
#define LOAD_FACTOR_EXPAND 0.75f
#define LOAD_FACTOR_SHRINK 0.25f

typedef struct HashItem {
    const void* key; // 修改为 const void*
    void* value;
    struct HashItem* next;
} HashItem;

typedef struct {
    HashItem** items;
    size_t size;
    size_t count;
    float load_factor;
    size_t (*hash)(const void* key, size_t size);
    int (*cmp)(const void* a, const void* b);
} HashTable;

typedef struct {
    char* data; // 报告数据
    size_t length; // 数据长度
} Report;

void initHashTable(HashTable* table, size_t initial_size, float load_factor, size_t (*hash_fn)(const void* key, size_t size), int (*cmp_fn)(const void* a, const void* b));
size_t defaultHash(const void* key, size_t size);
int defaultCmp(const void* a, const void* b);
char* processData(const char* key, const char* value);
void* searchHashTable(HashTable* table, const void* key);
void insertHashTable(HashTable* table, const char* key, const char* value);
void deleteHashTable(HashTable* table, const void* key);
void freeHashTable(HashTable* table);
void resizeHashTable(HashTable* table, size_t new_size);
void rehashHashTable(HashTable* table, size_t new_size);
void shrinkHashTable(HashTable* table, size_t new_size);
void handleMergeMessage(HashTable* table, Report* report);
#endif 