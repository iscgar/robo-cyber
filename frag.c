#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <fcntl.h>
#include <sys/un.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/reboot.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <linux/reboot.h>

#include "report.h"
#include "common.h"

#define ID_WINDOW_SIZE  5
#define DROP_PACKET(x)  DEBUG(printf("Packet dropped: %s\n", x))
#define PACKET_SIZE     1024
/* #define MOCK_SEND */
/* #define MOCK_CLIENT */

typedef enum
{
    E_NORMAL,
    E_CONTROL
} msg_type_e;

typedef enum
{
    E_CONTROL_NORMAL,
    E_CONTROL_RESTART,
    E_CONTROL_UPDATE_FILE
} msg_control_type_e;

typedef struct 
{
    msg_type_e type;
    msg_control_type_e control_type;
    char cmd[CMD_LEN];
} protocol_t;

typedef struct collector_t
{
    hdr_t **packets;
    uint32_t fragments;
    uint32_t total_fragments;
    uint32_t fragments_bitmap;
    uint32_t last_update;
    uint32_t packet_size;
    uint32_t id;
    uint8_t available;
} collector_t;

#ifdef WINDOW
static uint32_t id_bitmap = 0;
#endif

static uint32_t last_received_id = 0;
static uint32_t g_id = 0;
static collector_t collectors[MAX_COLLECTORS];

hdr_t** break_packet(uint8_t *packet, uint32_t size, uint32_t src, uint32_t dst, uint32_t *o_frags)
{
    /* Variable definition */
    hdr_t **packets;
    hdr_t * frag;
    uint32_t id = g_id++;
    uint32_t orig_size = size;
    uint32_t frag_idx, total_fragments;

    if ((packet == NULL) || (o_frags == NULL))
    {
        REPORT();
        return NULL;
    }

    /* Code section */
    *o_frags = 0;

    /* We at least have one fragment */
    frag_idx = 0;
    total_fragments = (size + MAX_PACKET_SIZE - 1) / MAX_PACKET_SIZE;

    if (total_fragments > 0)
    {
        /* Allocate memory for the array of fragments */
        packets = (hdr_t **)malloc(sizeof(hdr_t *) * total_fragments);

        if (packets == NULL)
        {
            REPORT();
        }
        else
        {
            uint32_t frag_size = sizeof(hdr_t) + MAX_PACKET_SIZE;

            do
            {
                /* Allocate memory for the fragment */
                frag = (hdr_t *)malloc(frag_size);

                if (frag == NULL)
                {
                    REPORT();
                    goto error;
                }

                /* Clear all previously remaind data */
                memset(frag, 0, frag_size);

                /* Build header */
                frag->size = orig_size;
                frag->id = id;
                frag->frag_idx = frag_idx;
                frag->src = src;
                frag->dst = dst;

                /* Copy the data */
                memcpy((uint8_t *)frag + sizeof(hdr_t), 
                       packet,
                       size < MAX_PACKET_SIZE ? size : MAX_PACKET_SIZE);

                /* Set the fragment in the list */
                packets[frag_idx] = frag;

                /* Quit loop if we finished processing */
                if (size <= MAX_PACKET_SIZE)
                {
                    break;
                }

                /* Advance on the buffer to next fragment */
                packet += MAX_PACKET_SIZE;

                size -= MAX_PACKET_SIZE;

                /* Increase number of fragments */
                ++frag_idx;
            } while (size);

            *o_frags = total_fragments;
        }
    }

    goto done;

error:
    if (frag_idx > 0)
    {
        while (frag_idx--)
        {
            free(packets[frag_idx]);
            packets[frag_idx] = NULL;
        }

        free(packets);
        packets = NULL;
    }

done:
    return packets;
}

static uint8_t* build_packet(collector_t *collector)
{
    /* Variable definition */
    uint8_t *packet, *packet_ptr;
    uint32_t i = 0;
    uint32_t total;
    hdr_t **pkts;

    /* Code section */
    /* Allocate memory for the packet */
    packet_ptr = packet = (uint8_t *)malloc(collector->packet_size);

    if (packet == NULL)
    {
        return NULL;
    }

    /* Get the packets array */
    pkts = collector->packets;

    /* Get total size */
    total = collector->packet_size;

    /* Start building the packet */
    while (total >= MAX_PACKET_SIZE)
    {
        memcpy(packet_ptr, pkts[i] + 1, MAX_PACKET_SIZE);
        packet_ptr += MAX_PACKET_SIZE;
        total -= MAX_PACKET_SIZE;
        ++i;
    }

    /* Copy leftovers */
    if (total > 0)
    {
        memcpy(packet_ptr, pkts[i] + 1, total);
    }

    return packet;
}

static void reset_collector(collector_t *collector)
{
    /* Reset the collector */
    memset(collector, 0, sizeof(collector_t));
    collector->available = 1;
}

static void free_fragments(collector_t * collector)
{
    /* Variable definition */
    uint32_t index;

    /* Code section */
    /* Free all fragments */
    for (index = 0; index < collector->fragments; ++index)
    {
        /* Free the fragment */
        free(collector->packets[index]);
        collector->packets[index] = NULL;
    }

    free(collector->packets);

    reset_collector(collector);
}

static uint32_t get_available_collector()
{
    /* Variable definition */
    uint32_t index;
    uint32_t lowest_time = 0xffffffff;
    uint32_t lowest_index = 0;

    /* Code section */
    /* Init all the collectors */
    for (index = 0; index < MAX_COLLECTORS; ++index)
    {
        if (collectors[index].available)
        {
            return index;
        }
    }

    /* If there is no available collector - get the collector that was update the least */
    for (index = 0; index < MAX_COLLECTORS; ++index)
    {
        if (collectors[index].last_update < lowest_time)
        {
            lowest_time = collectors[index].last_update;
            lowest_index = index;
        }
    }

    /* Free all its fragments */
    free_fragments(&collectors[lowest_index]);

    return index;
}

frag_e collect_packets(uint8_t *pkt_buffer, uint32_t pkt_len, uint8_t **o_full_packet, uint32_t *o_full_packet_size)
{
    /* Variable definition */
    uint32_t index;
    uint8_t * packet;
    collector_t *collector = NULL;
    hdr_t * pkt;
    struct timespec t;

    if (pkt_buffer == NULL)
    {
        REPORT();
        DROP_PACKET("Invalid packet buffer");

        return E_ERR;
    }

    /* Code section */
    /* Get the packet */
    pkt = (hdr_t *)pkt_buffer;

    /* Check packet size */
    if (pkt_len != MAX_PACKET_SIZE + sizeof(hdr_t))
    {
        /* Drop packet. Invalid length. */
        DROP_PACKET("Invalid fragment length");
        DEBUG(printf("%d\n", pkt_len));

        return E_ERR;
    }

#ifdef WINDOW
    /* Accept only packets that are ID_WINDOW_SIZE IDs in the area of the last id received */
    if ((pkt->id - last_received_id > ID_WINDOW_SIZE) && (last_received_id - pkt->id > ID_WINDOW_SIZE))
    {
        /* Drop the packet if not in window */
        DROP_PACKET("Not in window");

        return E_ERR;
    }

    /* Check against replay attacks */
    if (id_bitmap & 1 << (ID_WINDOW_SIZE + last_received_id - pkt->id))
    {
        DROP_PACKET("Replayed packet");

        return E_ERR;
    }
#endif

    /* Get the relevant collector */
    for (index = 0; index < MAX_COLLECTORS; ++index)
    {
        if (collectors[index].id == pkt->id)
        {
            collector = &collectors[index];
            break;
        }
    }

    /* No relevant collector found */
    if (collector == NULL)
    {
        /* Get an available collector */
        collector = &collectors[get_available_collector()];
    }

    if (pkt->size > MAX_PACKET_TOTAL)
    {
        DROP_PACKET("Packet too large");

        return E_ERR;
    }

    /* Check if it is the first fragment received for this collector */
    if (collector->available)
    {
        uint32_t fragments = (pkt->size + MAX_PACKET_SIZE - 1) / MAX_PACKET_SIZE;

        /* Add the fragment to the fragments list */
        collector->packets = malloc(collector->total_fragments * sizeof(hdr_t *));

        if (collector->packets == NULL)
        {
            REPORT();
            DROP_PACKET("Failed to allocate packet headers memory");

            return E_ERR;
        }

        /* Set all the primary arguments */     
        collector->id = pkt->id;
        collector->total_fragments = fragments;

        /* Collector is not available */
        collector->available = 0;
    }

    /* Set packet size */
    collector->packet_size = pkt->size;

    /* Make basic checks */
    /* Was the fragment already received? */
    if (collector->fragments_bitmap & (1 << pkt->frag_idx))
    {
        /* Fragment already exist */
        DROP_PACKET("Fragment alread exist");

        return E_ERR;
    }

    /* Check for valid fragment index */
    if (pkt->frag_idx >= collector->total_fragments)
    {
        /* Invalid fragment ID */
        DROP_PACKET("Invalid fragment ID");

        return E_ERR;

    }

    /* Check declared size of packet */
    if (pkt->size != collector->packet_size)
    {
        /* Invalid declared packet size */
        DROP_PACKET("Invalid declared packet size");

        return E_ERR;
    }

    /* No need to check for fragment size as all fragments are the same size */

    /* Set the fragment in the right place */
    collector->packets[collector->fragments] = malloc(MAX_PACKET_SIZE + sizeof(hdr_t));

    if (collector->packets[collector->fragments] == NULL)
    {
        REPORT();
        DROP_PACKET("Could not allocate memory for fragment");

        return E_ERR;
    }

    /* Copy the packet */
    memcpy(collector->packets[collector->fragments], pkt, MAX_PACKET_SIZE + sizeof(hdr_t));

    /* Increase number of fragments */
    collector->fragments++;

    /* Update the bitmap */
    collector->fragments_bitmap |= 1 << pkt->frag_idx;

    /* Get the time */
    clock_gettime(CLOCK_REALTIME, &t);

    /* Set last update */
    collector->last_update = (uint32_t) ((t.tv_sec * 1000UL) + (t.tv_nsec / 1000000UL));

    if (collector->fragments == collector->total_fragments)
    {
#ifdef WINDOW
        /* Check whether I should advance the last ID fields */
        if (
            ((last_received_id < (uint32_t)-ID_WINDOW_SIZE) && (collector->id > last_received_id)) ||
            ((last_received_id >= (uint32_t)-ID_WINDOW_SIZE) && ((collector->id < ID_WINDOW_SIZE) || collector->id > last_received_id))
           )
        {
            /* Move the bitmap according to the id shift */
            id_bitmap <<= collector->id - last_received_id;

            last_received_id = collector->id;
        }

        /* Finally set this ID as received */
        id_bitmap |= 1 << (last_received_id - collector->id + ID_WINDOW_SIZE);
#endif

        /* Send the packet to reassembly */
        packet = build_packet(collector);

        if (packet == NULL)
        {
            REPORT();
            DROP_PACKET("Failed to reassemble packet");

            *o_full_packet = NULL;
            *o_full_packet_size = 0;

            /* Reset this collector */
            free_fragments(collector);

            return E_ERR;
        }

        DEBUG(printf("Finished packet: ID: %d Fragments: %d Size: %d Packet buffer: %p\n", collector->id, collector->total_fragments, collector->packet_size, packet));
        DEBUG(print_hex(packet, collector->packet_size));

        *o_full_packet = packet;
        *o_full_packet_size = collector->packet_size;

        /* Reset this collector */
        free_fragments(collector);

        return E_SUCCESS;
    }

    return E_FRAG;
}

static int create_listening_socket(uint16_t port)
{
    /* Variable definition */
    int sockfd;
    struct sockaddr_in bind_addr;

    /* Code section */
    /* Create a socket for connection testing */
    sockfd = socket(AF_INET, SOCK_DGRAM, 0);

    /* Reset the addr struct */
    memset(&bind_addr, 0, sizeof(struct sockaddr_in));

    /* Fill it with appropriate data */
    bind_addr.sin_family = AF_INET;
    bind_addr.sin_port = htons(port);
    inet_aton("localhost", &bind_addr.sin_addr);

    /* Bind it to a network interface */
    if (bind(sockfd, (struct sockaddr *)&bind_addr, sizeof(struct sockaddr_in)) < 0)
    {
        /* Some error occured */
        perror("Error in binding socket");

        return 0;
    }

    return sockfd;
}


void init_collectors(void)
{
    uint32_t index;

    /* Init all the collectors */
    for (index = 0; index < MAX_COLLECTORS; ++index)
    {
        reset_collector(&collectors[index]);
    }
}

#if !(defined(SERVER) || defined(DEFRAG))

void moo_server( void )
{
    /* Variable definition */
    int sockfd;
    uint32_t index;
    uint32_t pkt_len;
    uint32_t addr_len;
    uint32_t full_packet_size;
    struct sockaddr_in addr;
    uint8_t * pkt;
    uint8_t * full_packet;
#ifdef MOCK_SEND
    uint8_t buf[PACKET_SIZE];
    ssize_t recvd_size;
    struct sockaddr_in recv_addr;
    socklen_t recv_addr_len;
#endif

    /* Code section */
    /* Create a socket for connection testing */
    if ((sockfd = create_listening_socket(0x2929)) == 0)
    {
        /* Something wrong happaned */
        return;
    }

    init_collectors();

    for (;;)
    {
        /* Allocate receive buffer */
        pkt = malloc(MAX_PACKET_SIZE + sizeof(hdr_t));

        /* Set address len */
        addr_len = sizeof(struct sockaddr_in);

        /* Receive packet from network */
        pkt_len = recvfrom(sockfd, pkt, MAX_PACKET_SIZE + sizeof(hdr_t), 0, (struct sockaddr *)&addr, &addr_len);

        /* Call fragment receiving loop */
        if (collect_packets(pkt, pkt_len, &full_packet, &full_packet_size) == E_SUCCESS)
        {
            /* Forward to upper level handling of packet */
            handle_packet(full_packet, full_packet_size);

            free(full_packet);
        }
    }
#ifdef MOCK_SEND
    /* Set receive struct size */
    recv_addr_len = sizeof(struct sockaddr_in);

    /* Start receiving */
    recvd_size = recvfrom(sockfd, buf, PACKET_SIZE, 0, (struct sockaddr *)&recv_addr, &recv_addr_len);

    print_hex(buf, recvd_size);
#endif
    /* Close the socket */
    close(sockfd);
}

typedef struct
{
    char from_addr[3 * 4 + 3 + 1];
    char to_addr[3 * 4 + 3 + 1];
} mapping;
mapping address_mapping[] =
    {
        {

            "127.0.0.1",
            "127.0.0.1"
        }
    };

char * get_address_mapping(char * from)
{
    /* Variable definition */
    uint32_t i;

    /* Code section */
    for (i = 0; i < sizeof(address_mapping) / sizeof(mapping); ++i)
    {
        if (!strcmp(from, address_mapping[i].from_addr))
        {
            return address_mapping[i].to_addr;
        }
    }

    return 0;
}

void moo_client( void )
{
    /* Variable definition */
    uint32_t i;
#ifdef MOCK_CLIENT
    uint32_t c;
#endif
    uint32_t numfrags;
    uint8_t packet[PACKET_SIZE];
    uint8_t ** frags;

    /* Socket variable definition */
    int sockfd;
#ifndef MOCK_CLIENT
    int recv_sockfd;
    uint32_t recvd_size;
    uint32_t recv_addr_len;
    struct sockaddr_in recv_addr;
#endif
    struct sockaddr_in send_addr;

    /* Code section */
#ifndef MOCK_CLIENT
    /* Get a listening socket */
    if ((recv_sockfd = create_listening_socket(0x8394)) == 0)
    {
        /* Something wrong happaned */
        return;
    }
#endif

    /* Create a socket for connection testing */
    sockfd = socket(AF_INET, SOCK_DGRAM, 0);

    /* Reset the addr struct */
    memset(&send_addr, 0, sizeof(struct sockaddr_in));

    /* Fill it with appropriate data */
    send_addr.sin_family = AF_INET;
    send_addr.sin_port = htons(0x2929);

#ifndef MOCK_CLIENT
    while (1)
    {
        /* Set receive struct size */
        recv_addr_len = sizeof(struct sockaddr_in);

        /* Start receiving */
        recvd_size = recvfrom(recv_sockfd, packet, PACKET_SIZE, 0, (struct sockaddr *)&recv_addr, &recv_addr_len);

        printf("Received packet with size: %d\n", recvd_size);

        /* Break the pakcet into fragments */
        frags = break_packet(packet, recvd_size, recv_addr.sin_addr.s_addr, 0/*get_address_mapping(recv_addr.sin_addr.s_addr)*/, &numfrags);

        /* Send the packet to it's rightful owner */
#ifndef MOCK_CLIENT
        inet_aton(get_address_mapping(inet_ntoa(recv_addr.sin_addr)), &send_addr.sin_addr);
#else
        inet_aton("localhost", &send_addr.sin_addr);
        printf("%d\n", send_addr.sin_addr.s_addr);
#endif

        /* Send the fragments one by one */
        for (i = 0; i < numfrags; ++i)
        {
            sendto(sockfd, frags[i], sizeof(hdr_t) + MAX_PACKET_SIZE, 0,
                    (struct sockaddr *)&send_addr, sizeof(struct sockaddr_in));
        }
    }
#else
    /* Create the packet to send */
    for (i = 0; i < sizeof(packet); ++i)
    {
        packet[i] = 'a' + i % 26;
    }

    DEBUG(printf("Sending packet:\n"));
    print_hex((uint8_t *)packet, sizeof(packet));

    for (c = 0; c < 50; ++c)
    {
        /* Break the pakcet into fragments */
        frags = break_packet(packet, sizeof(packet), 0x11223344, 0xAABBCCDD, &numfrags);

        /* printf("Number of frags: %d\n", numfrags); */

        /* Send the fragments one by one */
        for (i = 0; i < numfrags; ++i)
        {
            /* printf("Sending fragment #%d\n", i); */

            /* print_hex((uint8_t *)frags[i], sizeof(hdr_t) + MAX_PACKET_SIZE); */

            sendto(sockfd, frags[i], sizeof(hdr_t) + MAX_PACKET_SIZE, 0,
                    (struct sockaddr *)&send_addr, sizeof(struct sockaddr_in));
        }
    }

    g_id = 1;

    /* Break the pakcet into fragments */
    frags = break_packet(packet, sizeof(packet), 0x11223344, 0xAABBCCDD, &numfrags);

    /* Send the fragments one by one */
    for (i = 0; i < numfrags; ++i)
    {
        sendto(sockfd, frags[i], sizeof(hdr_t) + MAX_PACKET_SIZE, 0,
                (struct sockaddr *)&send_addr, sizeof(struct sockaddr_in));
    }
#endif
    close(sockfd);
}
#endif