#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <stdint.h>
#include "hash_table.h"

#define BUFFER_SIZE 1024
#define PORT 8080

// 假设每个报文都有一个唯一的ID和分片数
typedef struct {
    uint64_t message_id;
    int fragment_count;
    char data[BUFFER_SIZE * TABLE_SIZE]; 
} FragmentedMessage;

void sendFragmentedMessage(int socket_fd, struct sockaddr_in* client_addr, FragmentedMessage* message) {
    sendto(socket_fd, message->data, message->fragment_count * BUFFER_SIZE, 0, (struct sockaddr*)client_addr, sizeof(struct sockaddr_in));
    free(message); 
}

void processFragment(HashTable* table, uint64_t message_id, char* data, int len, struct sockaddr_in* client_addr, int server_fd) {
    FragmentedMessage* message = searchHashTable(table, message_id);
    if (message == NULL) {
        message = (FragmentedMessage*)malloc(sizeof(FragmentedMessage));
        message->message_id = message_id;
        message->fragment_count = 0;
        insertHashTable(table, message_id, message);
    }
    memcpy(message->data + message->fragment_count * BUFFER_SIZE, data, len);
    message->fragment_count++;
    if (message->fragment_count == TABLE_SIZE) {
        sendFragmentedMessage(server_fd, client_addr, message);
        freeHashTable(table);
    }
}

int main() {
    int server_fd, new_socket;
    struct sockaddr_in address;
    int opt = 1;
    char buffer[BUFFER_SIZE];
    int addrlen = sizeof(address);
    HashTable table;
    initHashTable(&table);
    if ((server_fd = socket(AF_INET, SOCK_DGRAM, 0)) == 0) {
        perror("socket failed");
        exit(EXIT_FAILURE);
    }
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt))) {
        perror("setsockopt");
        exit(EXIT_FAILURE);
    }
    // if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt))) {
    //     perror("setsockopt");
    //     exit(EXIT_FAILURE);
    // }
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);
    if (bind(server_fd, (struct sockaddr*)&address, sizeof(address)) < 0) {
        perror("bind failed");
        exit(EXIT_FAILURE);
    }
    while (1) {
        new_socket = recvfrom(server_fd, buffer, BUFFER_SIZE, 0, (struct sockaddr*)&address, (socklen_t*)&addrlen);
        if (new_socket < 0) {
            perror("recvfrom failed");
            continue;
        }
        // 假设报文ID是buffer的前8个字节
        uint64_t message_id = *(uint64_t*)buffer;
        processFragment(&table, message_id, buffer + sizeof(uint64_t), new_socket - sizeof(uint64_t), &address, server_fd);
    }
    close(server_fd);
    freeHashTable(&table);
    return 0;
}