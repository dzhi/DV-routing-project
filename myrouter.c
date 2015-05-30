#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>

#define BUFFER_SIZE 1024

// Just prints all packets received
// Send a UDP packet in Bash using
//      echo -n "your message here" >/dev/udp/localhost/10001
void print_server_loop(int socket_fd) {
    while (1) {
        char buffer[BUFFER_SIZE];
        struct sockaddr remote_addr;
        socklen_t remote_addr_len;
        ssize_t bytes_received = recvfrom(socket_fd, buffer, BUFFER_SIZE, 0,
                &remote_addr, &remote_addr_len);
        if (bytes_received < 0) {
            perror("Error receiving data");
        } else {
            printf("Received %d bytes:\n", (int) bytes_received);
            printf("%.*s\n", (int) bytes_received, buffer);
        }
    }
}

int str_to_uint16(const char *str, uint16_t *result)
{
    char *end;
    long int value = strtol(str, &end, 10);
    errno = 0;
    if (errno == ERANGE || value > UINT16_MAX || value < 0
            || end == str || *end != '\0')
        return -1;
    *result = (uint16_t) value;
    return 0;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "Error: No port number provided\n");
        exit(1);
    }

    char *port_no_str = argv[1];
    uint16_t port_no;
    if (str_to_uint16(port_no_str, &port_no) < 0) {
        fprintf(stderr, "Error: Invalid port number %s\n", port_no_str);
        exit(1);
    }

    // AF_INET ---> IPv4
    // SOCK_DGRAM ---> UDP
    int socket_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (socket_fd < 0) {
        perror("Error creating socket");
        exit(1);
    }
    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    server_addr.sin_port = htons(port_no);
    if (bind(socket_fd,
            (struct sockaddr *) &server_addr,
            sizeof(server_addr)) < 0) {
        perror("Error binding socket");
        exit(1);
    }

    print_server_loop(socket_fd);
    return 0;
}
