#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

int main(int argc, char* argv[]) {
    if (argc != 3) {
        printf("Некорректное число аргументов");
        return 1;
    }

    char* server_ip = argv[1];
    int port = atoi(argv[2]);

    int client_socket;
    struct sockaddr_in server_address;

    client_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);

    server_address.sin_family = AF_INET;
    server_address.sin_port = htons(port);

    socklen_t server_address_size = sizeof(server_address);
    connect(client_socket, (struct sockaddr*)&server_address, server_address_size);

    int b = 1;
    sendto(client_socket, &b, sizeof(int), 0, (struct sockaddr*)&server_address,
           server_address_size);

    printf("Connected to server: %s:%d\n", server_ip, port);

    float expected, actual;
    char buffer[1024];

    int rec = recvfrom(client_socket, buffer, sizeof(float) * 2, 0, (struct sockaddr*) &server_address, &server_address_size);
    memcpy(&expected, buffer, sizeof(float));
    memcpy(&actual, buffer + sizeof(float), sizeof(float));

    if (expected != actual) {
        sprintf(buffer, "НЕВЕРНОЕ распределение. Ожидалось %.3f, получил %.3f.", expected,
                actual);
    } else {
        sprintf(buffer, "ВЕРНОЕ распределение");
    }

    sendto(client_socket, buffer, strlen(buffer), 0, (struct sockaddr *) &server_address, server_address_size);

    close(client_socket);
    printf("Client shutdown\n");

    return 0;
}