#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "hash_table.h"
#include "facet.h"

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

void print(const char*str){
    while(*str){
        uart_write(UART,*str);
        str++;
    }
}
void println(const char*str){
    print(str);
    uart_write(UART, '\r');
    uart_write(UART, '\n');
}

void delay(uint32_t loops){
    for(int i=0;i<loops;i++){
        int tmp = GPIO_A->OUTPUT;
    }
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
    initHashTable(&table, TABLE_SIZE, LOAD_FACTOR_EXPAND, defaultHash, defaultCmp);
    insertHashTable(&table, "name", "Alice");
    insertHashTable(&table, "age", "30");
    insertHashTable(&table, "country", "USA");
    printf("插入数据完成。\n");
    char* name = (char*)searchHashTable(&table, "name");
    char* age = (char*)searchHashTable(&table, "age");
    char* country = (char*)searchHashTable(&table, "country");
    printf("搜索结果：\n");
    printf("姓名: %s\n", name ? name : "未找到");
    printf("年龄: %s\n", age ? age : "未找到");
    printf("国家: %s\n", country ? country : "未找到");
    insertHashTable(&table, "age", "31"); 
    printf("更新年龄后：%s\n", (char*)searchHashTable(&table, "age"));
    deleteHashTable(&table, "country");
    printf("删除国家后：%s\n", (char*)searchHashTable(&table, "country"));
    Report report;
    handleMergeMessage(&table, &report);
    printf("合并报告数据: %s\n", report.data);
    free(report.data);
    freeHashTable(&table);
    printf("释放内存完成。\n");
    return 0;
}

void irqCallback(){
}

