#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "hash_table.h"

size_t stringHash(const void* key, size_t size) {
    const char* str = (const char*)key;
    size_t hash = 0;
    while (*str) {
        hash = hash * 31 + *str;
        str++;
    }
    return hash % size;
}

int stringCmp(const void* a, const void* b) {
    const char* str1 = (const char*)a;
    const char* str2 = (const char*)b;
    return strcmp(str1, str2) == 0;
}

int main() {
   /*
    HashTable table;
    initHashTable(&table);
    insertHashTable(&table, 100, "Value for key 100");
    insertHashTable(&table, 200, "Value for key 200");
    insertHashTable(&table, 300, "Value for key 300");
    insertHashTable(&table, 400, "Value for key 400");
    void* value = searchHashTable(&table, 300);
    if (value != NULL) {
        printf("Found value for key 300: %s\n", (char*)value);
    } else {
        printf("Value for key 300 not found.\n");
    }
    value = searchHashTable(&table, 400);
    if (value != NULL) {
        printf("Found value for key 400: %s\n", (char*)value);
    } else {
        printf("Value for key 400 not found.\n");
    }
    freeHashTable(&table);
    return 0;
    */

    HashTable table;
    initHashTable(&table, TABLE_SIZE, 0.75f, stringHash, stringCmp);
    const char* keys[] = {"apple", "banana", "cherry", "date", "elderberry"};
    const char* values[] = {"fruit1", "fruit2", "fruit3", "fruit4", "fruit5"};
    size_t num_elements = sizeof(keys) / sizeof(keys[0]);
    for (size_t i = 0; i < num_elements; ++i) {
        insertHashTable(&table, strdup(keys[i]), strdup(values[i])); 
    }
    // 搜索一个键
    const char* search_key = "cherry";
    void* found_value = searchHashTable(&table, search_key);
    if (found_value) {
        printf("Found value for key '%s': %s\n", search_key, (const char*)found_value);
    } else {
        printf("Key '%s' not found.\n", search_key);
    }
    // 删除一个键值对
    deleteHashTable(&table, search_key);
    found_value = searchHashTable(&table, search_key);
    if (found_value) {
        printf("Found value for key '%s' after deletion: %s\n", search_key, (const char*)found_value);
    } else {
        printf("Key '%s' not found after deletion.\n", search_key);
    }
    freeHashTable(&table);
    return 0;

}

void irqCallback(){
}
