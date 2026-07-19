#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "hash_table.h"
/*
void initHashTable(HashTable* table) {
    table->items = (HashItem**)malloc(TABLE_SIZE * sizeof(HashItem*));
    if (table->items == NULL) {
        fprintf(stderr, "Memory allocation failed for hash table.\n");
        exit(EXIT_FAILURE);
    }
    for (int i = 0; i < TABLE_SIZE; i++) {
        table->items[i] = NULL;
    }
}

uint64_t hashFunction(uint64_t key) {
    return key % TABLE_SIZE;
}

void insertHashTable(HashTable* table, uint64_t key, void* value) {
    uint64_t index = hashFunction(key);
    HashItem* item = table->items[index];
    if (item == NULL) {
        table->items[index] = (HashItem*)malloc(sizeof(HashItem));
        if (table->items[index] == NULL) {
            fprintf(stderr, "Memory allocation failed for hash item.\n");
            exit(EXIT_FAILURE);
        }
        table->items[index]->key = key;
        table->items[index]->value = value;
        table->items[index]->next = NULL;
    } else {
        while (item->next != NULL) {
            item = item->next;
        }
        item->next = (HashItem*)malloc(sizeof(HashItem));
        if (item->next == NULL) {
            fprintf(stderr, "Memory allocation failed for hash item.\n");
            exit(EXIT_FAILURE);
        }
        item->next->key = key;
        item->next->value = value;
        item->next->next = NULL;
    }
}

void* searchHashTable(HashTable* table, uint64_t key) {
    uint64_t index = hashFunction(key);
    HashItem* item = table->items[index];
    while (item != NULL) {
        if (item->key == key) {
            return item->value;
        }
        item = item->next;
    }
    return NULL;
}

void freeHashTable(HashTable* table) {
    for (int i = 0; i < TABLE_SIZE; i++) {
        HashItem* item = table->items[i];
        while (item != NULL) {
            HashItem* temp = item;
            item = item->next;
            free(temp->value); 
            free(temp);
        }
    }
    free(table->items); 
}
*/

void initHashTable(HashTable* table, size_t initial_size, float load_factor, size_t (*hash_fn)(const void* key, size_t size), int (*cmp_fn)(const void* a, const void* b)) {
    table->items = (HashItem**)calloc(initial_size, sizeof(HashItem*));
    if (table->items == NULL) {
        fprintf(stderr, "哈希表的内存分配失败。\n");
        exit(EXIT_FAILURE);
    }
    table->size = initial_size;
    table->count = 0;
    table->load_factor = load_factor;
    table->hash = hash_fn;
    table->cmp = cmp_fn;
}

size_t defaultHash(const void* key, size_t size) {
    return (size_t)(uintptr_t)key % size;
}

int defaultCmp(const void* a, const void* b) {
    return strcmp((const char*)a, (const char*)b) == 0;
}

char* processData(const char* key, const char* value) {
    size_t key_len = strlen(key);
    size_t value_len = strlen(value);
    char* processed_data = malloc(key_len + value_len + 2); // +2 for "=" and null terminator
    if (processed_data == NULL) {
        fprintf(stderr, "处理数据的内存分配失败。\n");
        exit(EXIT_FAILURE);
    }
    sprintf(processed_data, "%s=%s", key, value);
    return processed_data;
}

void* searchHashTable(HashTable* table, const void* key) {
    size_t index = table->hash(key, table->size);
    HashItem* item = table->items[index];
    while (item != NULL) {
        if (table->cmp(item->key, key)) {
            return item->value;
        }
        item = item->next;
    }
    return NULL;
}

void insertHashTable(HashTable* table, const char* key, const char* value) {
    table->count++;
    if (table->count > table->size * table->load_factor) {
        resizeHashTable(table, table->size * 2);
    }
    size_t index = table->hash(key, table->size);
    HashItem* item = table->items[index];
    if (item == NULL) {
        table->items[index] = (HashItem*)malloc(sizeof(HashItem));
        if (table->items[index] == NULL) {
            fprintf(stderr, "哈希项的内存分配失败。\n");
            exit(EXIT_FAILURE);
        }
        table->items[index]->key = strdup(key);
        table->items[index]->value = processData(key, value);
        table->items[index]->next = NULL;
    } else {
        while (item->next != NULL) {
            if (table->cmp(item->key, key)) {
                free(item->value);
                item->value = processData(key, value);
                return;
            }
            item = item->next;
        }
        if (table->cmp(item->key, key)) {
            free(item->value);
            item->value = processData(key, value);
        } else {
            item->next = (HashItem*)malloc(sizeof(HashItem));
            if (item->next == NULL) {
                fprintf(stderr, "哈希项的内存分配失败。\n");
                exit(EXIT_FAILURE);
            }
            item->next->key = strdup(key);
            item->next->value = processData(key, value);
            item->next->next = NULL;
        }
    }
}

void deleteHashTable(HashTable* table, const void* key) {
    size_t index = table->hash(key, table->size);
    HashItem* item = table->items[index];
    HashItem* prev = NULL;
    while (item != NULL) {
        if (table->cmp(item->key, key)) {
            if (prev == NULL) {
                table->items[index] = item->next;
            } else {
                prev->next = item->next;
            }
            free(item->value);
            free(item);
            table->count--;
            if (table->count < table->size * LOAD_FACTOR_SHRINK && table->size > TABLE_SIZE) {
                shrinkHashTable(table, table->size / 2);
            }
            return;
        }
        prev = item;
        item = item->next;
    }
}

void freeHashTable(HashTable* table) {
    for (size_t i = 0; i < table->size; i++) {
        HashItem* item = table->items[i];
        while (item != NULL) {
            HashItem* temp = item;
            item = item->next;
            // 移除 const 限定符
            free((void*)temp->key);
            free(temp->value);
            free(temp);
        }
    }
    free(table->items);
}

void resizeHashTable(HashTable* table, size_t new_size) {
    HashItem** new_items = (HashItem**)calloc(new_size, sizeof(HashItem*));
    if (new_items == NULL) {
        fprintf(stderr, "新哈希表的内存分配失败。\n");
        exit(EXIT_FAILURE);
    }
    rehashHashTable(table, new_size);
    free(table->items);
    table->items = new_items;
    table->size = new_size;
}

void rehashHashTable(HashTable* table, size_t new_size) {
    for (size_t i = 0; i < table->size; i++) {
        HashItem* item = table->items[i];
        while (item != NULL) {
            HashItem* next = item->next;
            size_t new_index = table->hash(item->key, new_size);
            item->next = table->items[new_index];
            table->items[new_index] = item;
            item = next;
        }
    }
}

void shrinkHashTable(HashTable* table, size_t new_size) {
    if (new_size < TABLE_SIZE) return;
    resizeHashTable(table, new_size);
}

void handleMergeMessage(HashTable* table, Report* report) {
    report->data = NULL;
    report->length = 0;
    for (size_t i = 0; i < table->size; i++) {
        HashItem* item = table->items[i];
        while (item != NULL) {
            size_t new_length = report->length + strlen(item->value);
            char* new_data = realloc(report->data, new_length + 1); // +1 for null terminator
            if (new_data == NULL) {
                fprintf(stderr, "组装消息的内存分配失败。\n");
                exit(EXIT_FAILURE);
            }
            report->data = new_data;
            strcpy(report->data + report->length, item->value);
            report->length = new_length;
            item = item->next;
        }
    }
}
