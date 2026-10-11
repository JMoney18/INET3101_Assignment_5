#include <stdio.h>
#include <sys/socket.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include <sys/time.h>
#include <fcntl.h>
#include <sys/stat.h>

/* Accept only simple filenames, without directory paths. */
static int valid_filename(const char *filename)
{
    if (filename[0] == '\0' || strstr(filename, "..") != NULL)
        return 0;

    for (size_t i = 0; filename[i] != '\0'; i++) {
        unsigned char c = (unsigned char)filename[i];

        if (!((c >= 'a' && c <= 'z') ||
              (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') ||
              c == '.' || c == '_' || c == '-'))
            return 0;
    }

    return 1;
}

/* The signal handler sets this flag; main closes sockets during shutdown. */
static volatile sig_atomic_t stopping = 0;

static void handle_signal(int signal_number)
{
    (void)signal_number;
    stopping = 1;
}

int main(void)
{
    /* Handle Ctrl+C without doing resource cleanup inside the handler. */
    struct sigaction action = {0};

    action.sa_handler = handle_signal;
    sigemptyset(&action.sa_mask);

    if (sigaction(SIGINT, &action, NULL) == -1) {
        perror("sigaction");
        return 1;
    }

    /* A disconnected client should cause a send error, not terminate us. */
    struct sigaction ignore_pipe = {0};

    ignore_pipe.sa_handler = SIG_IGN;
    sigemptyset(&ignore_pipe.sa_mask);

    if (sigaction(SIGPIPE, &ignore_pipe, NULL) == -1) {
        perror("sigaction SIGPIPE");
        return 1;
    }

    /* Create an IPv4 TCP socket. */
    int server_socket = socket(AF_INET, SOCK_STREAM, 0);

    if (server_socket == -1) {
        perror("socket");
        return 1;
    }

    printf("TCP socket created successfully.\n");

    /* Allow address reuse after earlier connections when restarting. */
    int reuse_address = 1;

    if (setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR,
                   &reuse_address, sizeof(reuse_address)) == -1) {
        perror("setsockopt SO_REUSEADDR");
        close(server_socket);
        return 1;
    }

    /* Listen on this computer only, using port 8080. */
    struct sockaddr_in server_address = {0};

    server_address.sin_family = AF_INET;
    server_address.sin_port = htons(8080);
    server_address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    if (bind(server_socket,
             (struct sockaddr *)&server_address,
             sizeof(server_address)) == -1) {
        perror("bind");
        close(server_socket);
        return 1;
    }

    printf("Socket bound to 127.0.0.1:8080 successfully.\n");

    if (listen(server_socket, 5) == -1) {
        perror("listen");
        close(server_socket);
        return 1;
    }

    printf("Server is listening on port 8080.\n");

    /* Handle clients one at a time, closing each after one response. */
    while (!stopping) {
        printf("Waiting for a client connection...\n");

        int client_socket = accept(server_socket, NULL, NULL);

        if (client_socket == -1) {
            /* Recheck the shutdown flag after a signal interruption. */
            if (errno == EINTR)
                continue;

            perror("accept");
            break;
        }

        /* Limit each receive wait to five seconds of inactivity. */
        struct timeval timeout = {
            .tv_sec = 5,
            .tv_usec = 0
        };

        if (setsockopt(client_socket, SOL_SOCKET, SO_RCVTIMEO,
                       &timeout, sizeof(timeout)) == -1) {
            perror("setsockopt");
            close(client_socket);
            continue;
        }

        printf("Client connected successfully.\n");

        /* Space for 255 filename bytes and a C string terminator. */
        char filename[256];
        size_t used = 0;
        int complete = 0;

        /* TCP is a byte stream: collect bytes until the ending newline. */
        while (!stopping) {
            char byte;
            ssize_t received = recv(client_socket, &byte, 1, 0);

            if (received == -1) {
                if (errno == EINTR) {
                    if (stopping)
                        break;

                    continue;
                }

                if (errno == EAGAIN || errno == EWOULDBLOCK)
                    fprintf(stderr, "Invalid request: client timed out.\n");
                else
                    perror("recv");

                break;
            }

            /* Zero means the client closed its sending side. */
            if (received == 0) {
                fprintf(stderr, "Client disconnected before completing request.\n");
                break;
            }

            /* Reject null bytes before treating the filename as a string. */
            if (byte == '\0') {
                fprintf(stderr, "Invalid request: embedded null byte.\n");
                break;
            }

            if (byte == '\n') {
                complete = 1;
                break;
            }

            /* Check capacity before writing another filename byte. */
            if (used == sizeof(filename) - 1) {
                fprintf(stderr, "Invalid request: filename exceeds 255 bytes.\n");
                break;
            }

            filename[used++] = byte;
        }

        /* Explicitly terminate the filename within the buffer's bounds. */
        filename[used] = '\0';

        if (complete) {
            printf("Requested filename: %s\n", filename);

            char content[256];
            const char *response;
            size_t response_length;

            /* Validate the name before attempting to open it. */
            int filename_is_valid = valid_filename(filename);
            FILE *file = NULL;

            if (filename_is_valid) {
                /*
                 * Reject symbolic links and avoid blocking when opening
                 * a named pipe.
                 */
                int file_fd = open(filename,
                                   O_RDONLY | O_NOFOLLOW | O_NONBLOCK);

                if (file_fd != -1) {
                    struct stat file_info;

                    /* Inspect the opened descriptor; allow regular files only. */
                    if (fstat(file_fd, &file_info) == -1) {
                        int saved_errno = errno;
                        close(file_fd);
                        errno = saved_errno;
                    } else if (!S_ISREG(file_info.st_mode)) {
                        close(file_fd);
                        errno = EINVAL;
                    } else {
                        /*
                         * Transfer ownership to a FILE stream.
                         * fclose() will also close the descriptor.
                         */
                        file = fdopen(file_fd, "rb");

                        if (file == NULL) {
                            int saved_errno = errno;
                            close(file_fd);
                            errno = saved_errno;
                        }
                    }
                }
            }

            if (!filename_is_valid) {
                response = "ERROR: Invalid filename or path traversal.\n";
                response_length = strlen(response);
            } else if (file == NULL) {
                if (errno == ENOENT)
                    response = "ERROR: File Not Found.\n";
                else
                    response = "ERROR: Could not open requested file.\n";

                response_length = strlen(response);
            } else {
                /* A 256th byte proves the file exceeds the 255-byte limit. */
                size_t bytes_read = fread(content, 1, sizeof(content), file);
                int read_failed = ferror(file);

                fclose(file);

                if (read_failed) {
                    response = "ERROR: Could not read requested file.\n";
                    response_length = strlen(response);
                } else if (bytes_read > 255) {
                    response = "ERROR: File exceeds 255 bytes.\n";
                    response_length = strlen(response);
                } else {
                    /* Send file bytes by count, without needing a terminator. */
                    response = content;
                    response_length = bytes_read;
                }
            }

            size_t total_sent = 0;

            /* send() may write only part; continue from the unsent offset. */
            while (total_sent < response_length && !stopping) {
                ssize_t sent = send(client_socket,
                                    response + total_sent,
                                    response_length - total_sent,
                                    0);

                if (sent == -1) {
                    if (errno == EINTR)
                        continue;

                    perror("send");
                    break;
                }

                if (sent == 0) {
                    fprintf(stderr, "Could not send response.\n");
                    break;
                }

                total_sent += (size_t)sent;
            }
        } else if (!stopping) {
            fprintf(stderr, "No complete filename request received.\n");
        }

        /* Close this client's socket on both success and request failure. */
        close(client_socket);
    }

    close(server_socket);

    printf("\nServer shut down. Listening socket closed.\n");

    return 0;
}