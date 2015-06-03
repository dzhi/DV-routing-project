#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <inttypes.h>
#include <signal.h>

// Size of the buffer for packet payload
#define BUFFER_SIZE 65536

// Maximum number of routers supported, not including this one
// (Really only needs to be 5 for the purposes of the project)
#define DV_CAPACITY 16

// Max line size in topology file (for fgets)
#define MAX_LINE_LEN 80

#define LOG_FILE_NAME_LEN 256

enum packet_type {
    DATA_PACKET = 1,
    DV_PACKET = 2,
    KILLED_PACKET = 3
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

// Linear search of an array of DV entries
struct dv_entry *
dv_find(struct dv_entry *dv, int dv_length, uint16_t dest_port) {
    int i;
    for (i=0; i<dv_length; i++) {
        if (dv[i].dest_port == dest_port) {
            return &(dv[i]);
        }
    }
    return NULL;
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
int my_socket_fd; // Needs to be global for sig handler
FILE *log_file;
//-----------------------------------------------------------------------------

void print_my_dv() {
    fprintf(stdout, "Entries in my DV:\n");
    fprintf(log_file, "Entries in my DV:\n");

    int i;
    for (i = 0; i < my_dv_length; i++) {
        fprintf(stdout, "Dest port %u first hop port %u cost %u\n",
                my_dv[i].dest_port, my_dv[i].first_hop_port, my_dv[i].cost);
        fprintf(log_file, "Dest port %u first hop port %u cost %u\n",
                my_dv[i].dest_port, my_dv[i].first_hop_port, my_dv[i].cost);
    }
    fprintf(log_file, "\n");
    fflush(log_file);
}

void broadcast_my_dv(int socket_fd) {
    printf("Sending DV broadcast\n");
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

// DV message format:
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

    struct neighbor_list_node *sender =
            neighbor_list_find(my_neighbor_list_head, sender_port);
    if (sender == NULL) {
        // Not necessarily the right thing to do
        printf("Warning: Sender is not a known neighbor; ignoring its message\n");
        return;
    }

    int received_dv_length = (bytes_received / sizeof(struct dv_entry)) - 1;
    if (received_dv_length > DV_CAPACITY) {
        printf("Received DV has %d entries, which exceeds the capacity of %d\n",
                received_dv_length, DV_CAPACITY);
        return;
    }

    struct dv_entry *raw_received_dv = ((struct dv_entry *) buffer) + 1;
    int i;
    for (i=0; i<received_dv_length; i++) {
        ntoh_dv_entry(&(raw_received_dv[i]), &(sender->dv[i]));
    }
    sender->dv_length = received_dv_length;

    for (i=0; i<sender->dv_length; i++) {
        printf("Entry: Dest port %u first hop port %u cost %u\n",
                sender->dv[i].dest_port, sender->dv[i].first_hop_port,
                sender->dv[i].cost);
    }

    int change_count = 0;

    // The Bellman-Ford part operates in two parts:
    // First, look for all entries in the current DV which have their first hop
    //  designated as the sender (unless the first hop is the sender itself).
    // If the DV received from the sender causes that cost to *increase*, then
    //  we have to look at the DVs from all the neighbors to see who now gives
    //  the lowest cost (or if the target is now unreachable altogether).
    i = 0;
    while(i<my_dv_length) {
        if (my_dv[i].first_hop_port == sender_port &&
                my_dv[i].dest_port != sender_port) {
            struct dv_entry *senders_entry =
                    dv_find(sender->dv, sender->dv_length, my_dv[i].dest_port);
            if (senders_entry==NULL ||
                    sender->cost + senders_entry->cost > my_dv[i].cost) {
                uint32_t min_cost = UINT32_MAX;
                uint16_t best_first_hop_port;
                int is_reachable = 0;
                struct neighbor_list_node *neighbor;
                for (neighbor = my_neighbor_list_head; neighbor != NULL;
                        neighbor = neighbor->next) {
                    struct dv_entry *neighbors_entry = dv_find(neighbor->dv,
                            neighbor->dv_length, my_dv[i].dest_port);
                    if (neighbors_entry!=NULL &&
                            neighbors_entry->cost + neighbor->cost < min_cost) {
                        min_cost = neighbors_entry->cost + neighbor->cost;
                        best_first_hop_port = neighbor->port;
                        is_reachable = 1;
                    }
                }
                if (is_reachable) {
                    my_dv[i].first_hop_port = best_first_hop_port;
                    my_dv[i].cost = min_cost;
                    change_count++;
                    i++;
                } else {
                    // The target is now unreachable, so delete its entry from
                    //  my_dv. We'll do this by moving the last entry into the
                    //  deleted entry's slot.
                    printf("DV update: Deletion: Dest %u no longer reachable\n",
                            my_dv[i].dest_port);
                    my_dv[i] = my_dv[my_dv_length-1];
                    my_dv_length--;
                    change_count++;
                    // i remains unchanged
                }
            } else {
                i++;
            }
        } else {
            i++;
        }
    }
    // Second, we do the standard Bellman-Ford: If the cost to go through the
    //  sender is now better than the old cost, then update the DV entry.
    for (i=0; i < sender->dv_length; i++) {
        uint32_t dest_port = sender->dv[i].dest_port;
        if (dest_port == my_port) {
            continue;
        }
        struct dv_entry *e = dv_find(my_dv, my_dv_length, dest_port);
        if (e == NULL) {
            if (my_dv_length >= DV_CAPACITY) {
                // Not necessarily the right thing to do
                printf("Warning: DV is full, new entry is discarded\n");
            } else {
                my_dv[my_dv_length].dest_port = dest_port;
                my_dv[my_dv_length].first_hop_port = sender_port;
                my_dv[my_dv_length].cost = sender->cost + sender->dv[i].cost;
                printf("DV update: New entry: Dest %u first hop %u cost %u\n",
                        my_dv[my_dv_length].dest_port,
                        my_dv[my_dv_length].first_hop_port,
                        my_dv[my_dv_length].cost);
                my_dv_length++;
                change_count++;
            }
        } else if (sender->cost + sender->dv[i].cost < e->cost) {
            printf("DV update: Entry for dest %u changed ", dest_port);
            printf("    from first hop %u cost %u ", e->first_hop_port, e->cost);
            e->first_hop_port = sender_port;
            e->cost = sender->cost + sender->dv[i].cost;
            change_count++;
            printf("    to first hop %u cost %u ", e->first_hop_port, e->cost);
        }
    }

    if (change_count > 0) {
        print_my_dv();
        broadcast_my_dv(socket_fd);
    } else {
        printf("DV did not change\n");
    }
}


// handle SIGINT, SIGQUIT, SIGTERM by informing neighbors the router is killed
//TODO the SIGKILL signal (posix) can't be handled/caught
// TODO finish testing kill signal, write a paragraph about it when done
void handle_kill_signal(int sig) {

    // send dying message to all neighbors
    // message consists of KILLED_PACKET, padded to length of single dv_entry

    printf("Sending Killed broadcast\n");
    char message[(DV_CAPACITY+1)];
    message[0] = (char) KILLED_PACKET;

    struct neighbor_list_node *node = my_neighbor_list_head;
    for (; node!=NULL; node = node->next) {
        struct sockaddr_in dest_addr;
        dest_addr.sin_family = AF_INET;
        dest_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK); // 127.0.0.1
        dest_addr.sin_port = htons(node->port);
        if (sendto(my_socket_fd, message, sizeof(struct dv_entry),
                0, (struct sockaddr *) &dest_addr, sizeof dest_addr) < 0) {
            perror("Local error trying to send killed packet");
        }
    }

    signal(sig, SIG_DFL); // Restore default behavior
    raise(sig);
    return;
}

void handle_killed_packet(uint16_t sender_port) {
    // Note: doesn't matter what rest of message is, just that neighbor was
    // killed
    printf("Killed_packet from port %u:\n", sender_port);

    struct neighbor_list_node *sender =
            neighbor_list_find(my_neighbor_list_head, sender_port);
    if (sender == NULL) {
        // Not necessarily the right thing to do
        printf("Warning: Sender is not a known neighbor; ignoring its message\n");
        return;
    }

    // Dead neighbor is now unreachable, so delete its entry from my_dv. We'll
    //  do this by moving the last entry into the deleted entry's slot.
    struct dv_entry *to_delete = dv_find(my_dv, my_dv_length, sender->port);
    if (to_delete == NULL) {
        printf("Warning: Sender not found in my_dv, may have already been removed\n");
        return;
    }
    printf("DV update: Deletion: Neighbor %u died\n", to_delete->dest_port);
    *to_delete = my_dv[my_dv_length-1];
    my_dv_length--;

    return;
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
        printf("%02X", bytes[i] & 0xFF);
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
    socklen_t remote_addr_len = sizeof remote_addr;
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
        case KILLED_PACKET:
            handle_killed_packet(sender_port);
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
    n->dv = malloc(DV_CAPACITY * sizeof(struct dv_entry));
    if (n->dv == NULL) {
        fprintf(stderr, "Error: Memory allocation failed\n");
        exit(1);
    }
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

    while (fgets(line, MAX_LINE_LEN, f) != NULL) {
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
    // If port not found while scanning, error
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
    int num_neighbors = 0;

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
            my_dv[num_neighbors].dest_port = port;
            my_dv[num_neighbors].first_hop_port = port;
            my_dv[num_neighbors].cost = cost;
            num_neighbors++;


            next = current;
        }
    }

    fclose(f);
    my_neighbor_list_head = current;
    my_dv_length = num_neighbors;


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
    find_label("sample_topology.txt"); // Find this node's own name
    initialize_neighbors("sample_topology.txt");

    char log_file_name[LOG_FILE_NAME_LEN];
    strcpy(log_file_name, "routing-output_.txt");
    log_file_name[14] = my_label;
    log_file = fopen(log_file_name, "w");
    if (log_file == NULL) {
        fprintf(stderr, "Error: Failed to open log file %s\n", log_file_name);
        exit(1);
    }

    struct neighbor_list_node *node = my_neighbor_list_head;
    fprintf(stdout, "My neighbors are:\n");
    for (; node!=NULL; node = node->next) {
        fprintf(stdout, "Port %u Cost %u\n", node->port, node->cost);
    }

    fprintf(stdout, "My label is %c\n\n", my_label);

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

    my_socket_fd = socket_fd; // set global too

    print_my_dv();
    broadcast_my_dv(socket_fd);

    // After this point (initial contact w/ neighbors), should let neighbors
    //  know if killed
    signal(SIGINT, handle_kill_signal);
    signal(SIGTERM, handle_kill_signal);
    signal(SIGQUIT, handle_kill_signal);

    while (1) {
        server_loop(socket_fd);
    }
    return 0;
}
