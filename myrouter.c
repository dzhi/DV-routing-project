#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>

#define BUFFER_SIZE 65536

enum packet_type {
    DATA_PACKET = 1,
    DV_PACKET = 2
};

struct dv_entry {
    uint16_t port;
    uint16_t cost;
};
void ntoh_dv_entry (struct dv_entry *n, struct dv_entry *h) {
    h->port = ntohs(n->port);
    h->cost = ntohs(n->cost);
}
void hton_dv_entry (struct dv_entry *h, struct dv_entry *n) {
    n->port = htons(h->port);
    n->cost = htons(h->cost);
}

// Message format:
// 1 byte indicating that this is a DV packet
// Padding to fill the length of a dv_entry
// 0 or more DV entries
void handle_dv_packet(uint16_t sender_port,
        char *buffer, ssize_t bytes_received) {
    if (bytes_received % sizeof(struct dv_entry) != 0) {
        printf("Message not understood\n");
        return;
    }
    printf("DV packet from port %u:\n", sender_port);
    unsigned int i;
    for (i=sizeof(struct dv_entry); i < bytes_received; i+=sizeof(struct dv_entry)) {
        struct dv_entry e;
        ntoh_dv_entry((struct dv_entry *)(buffer+i), &e);
        printf("Entry: Port %u cost %u\n", e.port, e.cost);
    }
}

void print_hexadecimal(char *bytes, int len) {
    int i;
    for (i=0; i<len; i++) {
        if (i!=0) {
            if (i%16 == 0)
                printf("\n");
            else if (i%4 == 0)
                printf(" ");
        }
        printf("%02X", bytes[i]);
    }
}

// Send a UDP packet in Bash using
//      echo -n "Test" > /dev/udp/localhost/10001
// Send hexadecimal bytes in Bash using
//      echo 54657374 | xxd -r -p > /dev/udp/localhost/10001
void server_loop(int socket_fd) {
    char buffer[BUFFER_SIZE];
    struct sockaddr_in remote_addr;
    socklen_t remote_addr_len;
    ssize_t bytes_received = recvfrom(socket_fd, buffer, BUFFER_SIZE, 0,
            (struct sockaddr *) &remote_addr, &remote_addr_len);
    if (bytes_received < 0) {
        perror("Error receiving data");
        return;
    }

    uint16_t sender_port = ntohs(remote_addr.sin_port);
    uint32_t sender_ip_addr = ntohl(remote_addr.sin_addr.s_addr);
    unsigned char ip_bytes[4];
    ip_bytes[0] = sender_ip_addr & 0xFF;
    ip_bytes[1] = (sender_ip_addr>>8) & 0xFF;
    ip_bytes[2] = (sender_ip_addr>>16) & 0xFF;
    ip_bytes[3] = (sender_ip_addr>>24) & 0xFF;

    printf("Received %d bytes ", (int) bytes_received);
    printf("from IP address %u.%u.%u.%u ",
            ip_bytes[3], ip_bytes[2], ip_bytes[1], ip_bytes[0]);
    printf("port %u:\n", sender_port);
    printf("ASCII: %.*s\n", (int) bytes_received, buffer);
    printf("Hexadecimal:\n");
    print_hexadecimal(buffer, bytes_received);
    printf("\n");

    if (bytes_received == 0) {
        printf("Message not understood\n");
        return;
    }

    switch ((unsigned char) buffer[0]) {
        case DATA_PACKET:
            printf("Data packet received (behavior not yet implemented)\n");
        break;
        case DV_PACKET:
            handle_dv_packet(sender_port, buffer, bytes_received);
        break;
        default:
            printf("Message not understood\n");
    }
}

int str_to_uint16(const char *str, uint16_t *result) {
    char *end;
    errno = 0;
    long int value = strtol(str, &end, 10);
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
            sizeof server_addr) < 0) {
        perror("Error binding socket");
        exit(1);
    }

    while (1) {
        server_loop(socket_fd);
    }
    return 0;
}
