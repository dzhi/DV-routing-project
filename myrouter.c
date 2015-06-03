#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <inttypes.h> // string formatting for uint16_t

// Size of the buffer for packet payload
#define BUFFER_SIZE 65536

// Maximum number of routers supported, not including this one
// (Really only needs to be 5 for the purposes of the project)
#define DV_CAPACITY 16

// Max line size in topology file (for fgets)
#define MAX_LINE_LEN 80

enum packet_type {
    DATA_PACKET = 1,
    DV_PACKET = 2
};

struct dv_entry {
    uint16_t dest_port;
    uint16_t first_hop_port;
    uint32_t cost;
};
void ntoh_dv_entry(struct dv_entry *n, struct dv_entry *h) {
    h->dest_port = ntohs(n->dest_port);
    h->first_hop_port = ntohs(n->first_hop_port);
    h->cost = ntohl(n->cost);
}
void hton_dv_entry(struct dv_entry *h, struct dv_entry *n) {
    n->dest_port = htons(h->dest_port);
    n->first_hop_port = htons(h->first_hop_port);
    n->cost = htonl(h->cost);
}

// Singly linked list of information about neighboring nodes
struct neighbor_list_node {
    uint16_t port;
    uint32_t cost;
    struct dv_entry *dv; // The neighbor node's DV (an array of DV entries)
    int dv_length; // Number of entries in the neighbor node's DV
    struct neighbor_list_node *next;
};

struct neighbor_list_node *
neighbor_list_find(struct neighbor_list_node *list_head, uint16_t port) {
    struct neighbor_list_node *node = list_head;
    for (; node!=NULL; node = node->next) {
        if (node->port == port) {
            return node;
        }
    }
    return NULL;
}

//-----------------------------------------------------------------------------
// Global variables
char my_label; // used find immediate neighbors in topology files
// TODO set own label. also, not sure if okay to assume node names are 1 char?
uint16_t my_port;
struct dv_entry my_dv[DV_CAPACITY];
int my_dv_length = 0;
struct neighbor_list_node *my_neighbor_list_head = NULL;
//-----------------------------------------------------------------------------

void broadcast_my_dv(int socket_fd) {
    char message[(DV_CAPACITY+1)*(sizeof(struct dv_entry))];
    // See comment on message format
    message[0] = (char) DV_PACKET;
    struct dv_entry *dv = ((struct dv_entry *) message)+1;
    int i;
    for (i=0; i<my_dv_length; i++) {
        hton_dv_entry(&(my_dv[i]), &(dv[i]));
    }

    struct neighbor_list_node *node = my_neighbor_list_head;
    for (; node!=NULL; node = node->next) {
        struct sockaddr_in dest_addr;
        dest_addr.sin_family = AF_INET;
        dest_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK); // 127.0.0.1
        dest_addr.sin_port = htons(node->port);
        if (sendto(socket_fd, message,
                (my_dv_length+1)*(sizeof(struct dv_entry)),
                0, (struct sockaddr *) &dest_addr, sizeof dest_addr) < 0) {
            perror("Local error trying to send packet");
        }
    }
}

// Message format:
// 1 byte indicating that this is a DV packet
// Padding to fill the length of a dv_entry
// 0 or more DV entries
void handle_dv_packet(int socket_fd, uint16_t sender_port,
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
        printf("Entry: Dest port %u first hop port %u cost %u\n",
                e.dest_port, e.first_hop_port, e.cost);
    }

    // TODO: Use Bellman-Ford to update my_dv

    // TODO: Only broadcast if my_dv changed
    broadcast_my_dv(socket_fd);
}

void print_hexadecimal(char *bytes, int length) {
    int i;
    for (i=0; i<length; i++) {
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
// Or instead of "... > /dev/udp/localhost/10001", use
//      ... | nc -u -p 12345 -w0 localhost 10001
// to specify the sending port (here, 12345) and not be Bash-specific.
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
    // printf("ASCII: %.*s\n", (int) bytes_received, buffer);
    printf("Hexadecimal:\n");
    print_hexadecimal(buffer, bytes_received);
    printf("\n");

    if (bytes_received == 0) {
        printf("Message not understood\n");
        return;
    }

    switch (buffer[0]) {
        case DATA_PACKET:
            printf("Data packet received (behavior not yet implemented)\n");
        break;
        case DV_PACKET:
            handle_dv_packet(socket_fd, sender_port, buffer, bytes_received);
        break;
        default:
            printf("Message not understood\n");
    }
}

struct neighbor_list_node *
new_neighbor_list_node(uint16_t port, uint16_t cost,
        struct neighbor_list_node *next) {
    struct neighbor_list_node *n = malloc(sizeof(struct neighbor_list_node));
    if (n==NULL) {
        fprintf(stderr, "Error: Memory allocation failed\n");
        exit(1);
    }
    n->port = port;
    n->cost = cost;
    n->dv = NULL;
    n->dv_length = 0;
    n->next = next;
    return n;
}

// TODO: Hardcoded for now
// Definitely needs refactoring
void initialize_neighbors_old() {
    if (my_port == 10000) {
        struct neighbor_list_node *b = new_neighbor_list_node(10001, 3, NULL);
        struct neighbor_list_node *e = new_neighbor_list_node(10005, 1, b);
        my_neighbor_list_head = e;
        my_dv[0].dest_port = 10001;
        my_dv[0].first_hop_port = 10001;
        my_dv[0].cost = 3;
        my_dv[1].dest_port = 10005;
        my_dv[1].first_hop_port = 10005;
        my_dv[1].cost = 1;
        my_dv_length = 2;
    }
    // TODO: More data missing
}

void find_label(const char *file_name) {
    // find first edge where dest port matches this router's port
    // the dest label is then the label corresponding to this port

    FILE* f = fopen(file_name, "rt");
    char line[MAX_LINE_LEN];

    while(fgets(line, MAX_LINE_LEN, f) != NULL){
        char src;
        char dest;
        uint16_t port;
        uint16_t cost;

        if (sscanf(line, "%c,%c,%" SCNd16 ",%" SCNd16 "", &src, &dest, &port, &cost) < 4){
            fprintf(stderr, "Error: cannot read network topology file");
            exit(1);
        }

        if (port == my_port){
            my_label = dest;
            fclose(f);
            return;
        }
    }
    // if port not found while scanning, error
    fclose(f);
    fprintf(stderr, "Error: Port number not in network topology file\n");
    exit(1);
}

// The router finds its immediate neighbors from file
// Initialize neighbors from tuples of 
//      <source router, destination router, destination UDP port, link cost>
// (seems to be destination port, even though spec says source port?)
void initialize_neighbors(const char *file_name) {

    struct neighbor_list_node *next = NULL; // tail node added first, has NULL next
    struct neighbor_list_node* current = NULL;

    FILE* f = fopen(file_name, "rt");
    char line[MAX_LINE_LEN];

    while(fgets(line, MAX_LINE_LEN, f) != NULL){
        char src;
        char dest;
        uint16_t port;
        uint16_t cost;

        // TODO check if sscanf was successful
        if (sscanf(line, "%c,%c,%" SCNd16 ",%" SCNd16 "", &src, &dest, &port, &cost) < 4){
            //sscanf(line, "%c,%c,%u,%u", &src, &dest, &port, &cost);
            fprintf(stderr, "Error: cannot read network topology file");
            exit(1);

        }

        if (src == my_label) {
            current = new_neighbor_list_node(port, cost, next);
            // TODO update entry and DV length
            next = current;
        }
    }

    fclose(f);
    my_neighbor_list_head = current;

    return;
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
    if (str_to_uint16(port_no_str, &my_port) < 0) {
        fprintf(stderr, "Error: Invalid port number %s\n", port_no_str);
        exit(1);
    }

    // initialize_neighbors_old();
    // TODO give user option to specify file
    find_label("sample_topology.txt"); // find node's own name
    initialize_neighbors("sample_topology.txt");


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
    server_addr.sin_port = htons(my_port);
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
