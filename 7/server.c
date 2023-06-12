#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <semaphore.h>
#include <string.h>
#include <time.h>
#include <arpa/inet.h>
#include <sys/random.h>
#include <sys/socket.h>
#include <netinet/in.h>

int getRandomInt(int exclusive_max) {
    int rnd;
    getrandom(&rnd, sizeof(int), GRND_NONBLOCK);
    return abs(rnd) % exclusive_max;
}

int main(int argc, char* argv[]) {
    int port = atoi(argv[1]);
    int count = atoi(argv[2]);

    if (argc != count + 4) {
        printf("Некорректное число аргументов");
        return 1;
    }

    int server_socket, client_socket;
    struct sockaddr_in server_address, client_address;
    socklen_t client_address_length;
    if ((server_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) < 0) {
        perror("socket");
        exit(EXIT_FAILURE);
    }

    server_address.sin_family = AF_INET;
    server_address.sin_addr.s_addr = INADDR_ANY;
    server_address.sin_port = htons(port);

    if (bind(server_socket, (struct sockaddr *)&server_address, sizeof(server_address)) < 0) {
        perror("bind");
        exit(EXIT_FAILURE);
    }

    int sum = atoi(argv[3]);
    float parts[count];
    for (int i = 0; i < count; ++i) {
        parts[i] = atof(argv[i + 4]);
    }

    client_address_length = sizeof(client_address);

    int b;
    struct sockaddr_in monitor;
    recvfrom(server_socket, &b, sizeof(int), 0,
             (struct sockaddr *)&monitor, &client_address_length);

    printf("Monitor connected.\n");

    struct sockaddr_in *clients = (struct sockaddr_in *)malloc(count * 16);
    for (int i = 0; i < count; ++i) {
        client_address_length = sizeof(client_address);

        recvfrom(server_socket, &b, sizeof(int), 0,
                 (struct sockaddr *)&clients[i], &client_address_length);
        printf("Client %d connected.\n", i + 1);
    }

    int shm = shm_open("shm", O_CREAT | O_RDWR, 0666);
    ftruncate(shm, 4 * count);
    float* lawyer_distribution = mmap(NULL, 4 * count, PROT_READ | PROT_WRITE, MAP_SHARED, shm, 0);

    char* sem_names[count];
    for (int i = 0; i < count; ++i) {
        char* str = (char*)malloc(100 * sizeof(char));
        sprintf(str, "SEM_%d", i + 1);
        sem_names[i] = str;
    }

    sem_t* sem[count];
    for (int i = 0; i < count; ++i) {
        lawyer_distribution[i] = -1;
        sem[i] = sem_open(sem_names[i], O_CREAT, 0666, 1);
    }

    for (int i = 0; i < count; ++i) {
        pid_t pid = fork();
        if (pid == 0) {
            sem_wait(sem[i]);

            sleep(getRandomInt(2));

            int rnd = getRandomInt(10);
            if (rnd >= 8) {
                // адвокат решает поступить нечестно (с шансом 20%)
                int rnd2 = getRandomInt(2);
                if (rnd2 == 0) {
                    // адвокат часть денег себе
                    lawyer_distribution[i] = sum * parts[i] / 2;
                    printf("[info]: Адвокат забирает часть денег от %d наследника себе\n", i + 1);
                } else {
                    // адвокат отдает часть денег другому наследнику

                    int rnd_person = getRandomInt(count);
                    while (rnd_person == i) {
                        rnd_person = getRandomInt(count);
                    }

                    sem_wait(sem[rnd_person]);

                    // отдаем деньги другому наследнику только если ему уже распределено, если нет -
                    // то поступаем честно
                    if (lawyer_distribution[rnd_person] != -1) {
                        printf(
                            "[info]: Адвокат отсыпает часть денег от %d наследника %d наследнику\n",
                            i + 1, rnd_person + 1);
                        lawyer_distribution[rnd_person] += sum * parts[i] / 2;
                        lawyer_distribution[i] = sum * parts[i] / 2;
                    } else {
                        printf(
                            "[info]: Адвокат поступает честно и распределяет наследство %d "
                            "наследнику\n",
                            i + 1);
                        lawyer_distribution[i] = sum * parts[i];
                    }

                    sem_post(sem[rnd_person]);
                }
            } else {
                // адвокат поступает честно с шансом 80%
                printf("[info]: Адвокат поступает честно и распределяет наследство %d наследнику\n",
                       i + 1);
                lawyer_distribution[i] = sum * parts[i];
            }

            sem_post(sem[i]);

            return 0;
        }
    }

    for (int i = 0; i < count; ++i) {
        wait(NULL);
    }

    float *buffer = (float*)malloc(sizeof(float) * 2);

    printf("\nНаследники проверяют распределение наследства в соответствии со своими долями!\n");
    for (int i = 0; i < count; ++i) {
        float expected = sum * parts[i];
        float actual = lawyer_distribution[i];

        buffer[0] = expected;
        buffer[1] = actual;

        int r;
        r = sendto(server_socket, buffer, sizeof(float) * 2, 0, (struct sockaddr*)&clients[i], client_address_length);
        char* str = (char*)malloc(2048);
        int bytes_received = recvfrom(server_socket, str, sizeof(char) * 2048, 0, (struct sockaddr*)&clients[i], &client_address_length);
        if (bytes_received < 0) {
            perror("recv");
            exit(EXIT_FAILURE);
        }

        printf("%d-ый наследник: %s", i + 1, str);

        char *monitor_message = malloc(2048);
        sprintf(monitor_message, "Сервер отправил клиенту N%d два числа: %.3f, %.3f.\nСервер получил от клиента N%d сообщение: %s", i + 1, expected, actual, i + 1, str);

        r = sendto(server_socket, monitor_message, strlen(monitor_message), 0, (struct sockaddr*)&monitor, sizeof(monitor));

        free(str);
        free(monitor_message);
    }

    free(buffer);

    sleep(2);
    char finalization_msg[] = {"stop"};
    sendto(server_socket, finalization_msg, strlen(finalization_msg), 0, (struct sockaddr*)&monitor, client_address_length);

    close(server_socket);
    free(clients);

    munmap(lawyer_distribution, 4 * count);
    close(shm);
    shm_unlink("shm");
    for (int i = 0; i < count; ++i) {
        sem_close(sem[i]);
        sem_unlink(sem_names[i]);
    }

    return 0;
}