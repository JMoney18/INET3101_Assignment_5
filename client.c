#include <arpa/inet.h>
#include <stdio.h>
#include <sys/socket.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <signal.h>

int main(void)
{
    struct sigaction ignore_pipe = {0};

    ignore_pipe.sa_handler = SIG_IGN;
    sigemptyset(&ignore_pipe.sa_mask);

    if (sigaction(SIGPIPE, &ignore_pipe, NULL) == -1) {
        perror("sigaction SIGPIPE");
        return 1;
    }

    /* Space for 255 filename bytes, a newline, and a null terminator. */
    char request[257];

    printf("Enter a filename: ");
    fflush(stdout);

    if (fgets(request, sizeof(request), stdin) == NULL) {
        fprintf(stderr, "Could not read a filename.\n");
        return 1;
    }

    if (strchr(request, '\n') == NULL) {
        fprintf(stderr, "Filename input is too long or missing a newline.\n");
        return 1;
    }

    if (request[0] == '\n') {
        fprintf(stderr, "Filename cannot be empty.\n");
        return 1;
    }

    int client_socket = socket(AF_INET, SOCK_STREAM, 0);

    if (client_socket == -1) {
        perror("socket");
        return 1;
    }

    struct sockaddr_in server_address = {0};

    server_address.sin_family = AF_INET;
    server_address.sin_port = htons(8080);

    if (inet_pton(AF_INET, "127.0.0.1",
                  &server_address.sin_addr) != 1) {
        fprintf(stderr, "Invalid server address.\n");
        close(client_socket);
        return 1;
    }

    if (connect(client_socket,
                (struct sockaddr *)&server_address,
                sizeof(server_address)) == -1) {
        perror("connect");
        close(client_socket);
        return 1;
    }

    printf("Connected to the server successfully.\n");

    size_t total_sent = 0;
    size_t request_length = strlen(request);

    while (total_sent < request_length) {
        ssize_t sent = send(client_socket,
                            request + total_sent,
                            request_length - total_sent,
                            0);
        
        if (sent == -1 && errno == EINTR)
            continue;

        if (sent <= 0) {
            if (sent == -1)
                perror("send");
            else
                fprintf(stderr, "Could not send the request.\n");

            close(client_socket);
            return 1;
        }

        total_sent += (size_t)sent;
    }

    printf("Filename request sent.\n");

    char response[256];

    printf("Server response:\n");

    while (1) {
        ssize_t received = recv(client_socket,
                                response,
                                sizeof(response),
                                0);

        if (received == -1) {
            if (errno == EINTR)
                continue;

            perror("recv");
            close(client_socket);
            return 1;
        }

        if (received == 0)
            break;

        fwrite(response, 1, (size_t)received, stdout);
    }

    close(client_socket);
    return 0;
}