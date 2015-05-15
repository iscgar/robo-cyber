#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <fcntl.h>
#include <sys/un.h>
#include <sys/types.h>
#include "report.h"
#include "common.h"
#include "frag.h"

#define MAX_COLLECTORS 128
#define ID_WINDOW_SIZE 5
#define DROP_PACKET(x) printf("Packet dropped: %s\n", x)

typedef struct collector_t
{
    uint8_t **packets;
    uint32_t fragments;
    uint32_t total_fragments;
    /* uint32_t fragments_bitmap; */
    uint32_t last_update;
    uint32_t packet_size;
    uint32_t id;
    uint8_t available;
} collector_t;

static uint8_t* build_packet(collector_t *collector);
static void reset_collector(collector_t *collector);
static void free_frag_packets(uint8_t **packets, uint32_t num);
static void free_fragments(collector_t *collector);
static uint32_t get_available_collector();


#ifdef WINDOW
static uint32_t id_bitmap = 0;
#endif

static uint32_t g_id = 0;
static uint32_t last_received_id = 0;
static collector_t collectors[MAX_COLLECTORS];

void init_collectors(void)
{
    uint32_t index;

    /* Init all the collectors */
    for (index = 0; index < MAX_COLLECTORS; ++index)
    {
        reset_collector(&collectors[index]);
    }
}

frag_e collect_packets(const uint8_t *pkt_buffer, uint32_t pkt_len, uint8_t **o_full_packet, uint32_t *o_full_packet_size)
{
    /* Variable definition */
    uint32_t index;
    uint8_t *packet;
    collector_t *collector = NULL;
    hdr_t *pkt;
    struct timespec t;

    /* Code section */
    if (pkt_buffer == NULL)
    {
        REPORT();
        DROP_PACKET("Invalid packet buffer");

        return E_ERR;
    }

    /* Get the packet */
    pkt = (hdr_t *)pkt_buffer;

    /* Check packet size */
    if (pkt_len != MAX_PACKET_SIZE + sizeof(hdr_t))
    {
        /* Drop packet. Too large. */
        DROP_PACKET("Invalid fragment size");
        printf("%d\n", pkt_len);

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
        uint32_t total_fragments = pkt->size / MAX_PACKET_SIZE + !!(pkt->size % MAX_PACKET_SIZE);

        /* XXX: This is because of the check below if the fragment exists
         using bitmap of 32 bits. Only undefined behaviour, though, and no crash */
        /* if (total_fragments > 32)
        {
            REPORT();
            DROP_PACKET("Can't accept more than 32 fragments");

            return E_ERR;
        } */

        /* Add the fragment to the fragments list */
        collector->packets = (uint8_t **)calloc(collector->total_fragments * sizeof(uint8_t *), 1);

        if (collector->packets == NULL)
        {
            REPORT();
            DROP_PACKET("Failed to allocate packet fragments pointers memory");
            return E_ERR;
        }

        /* Set all the primary arguments */     
        collector->id = pkt->id;
        collector->total_fragments = total_fragments;

        /* Collector is not available */
        collector->available = 0;

        /* Set packet size */
        collector->packet_size = pkt->size;
    }

    if (collector->packet_size != pkt->size)
    {
        if (collector->packet_size > pkt->size)
        {
            /* Will cause memory overwrite in build_packet() */
            REPORT();
        }

        DROP_PACKET("Packet size mismatch");

        return E_ERR;
    }

    /* Make basic checks */
    /* Was the fragment already received? */
    /* XXX: if (collector->fragments_bitmap & (1 << pkt->frag_idx))*/
    if (collector->packets[pkt->frag_idx] != NULL)
    {
        /* Fragment already exist */
        DROP_PACKET("Fragment already exist");

        return E_EXISTS;

    }

    /* Check for valid fragment index */
    if (pkt->frag_idx >= collector->total_fragments)
    {
        /* Invalid fragment ID */
        DROP_PACKET("Invalid fragment ID");

        return E_ERR;

    }

    /* No need to check for fragment size as all fragments are the same size */

    /* Set the fragment in the right place */
    /* XXX: pkt->frag_idx should be used instead of collector->fragments */
    collector->packets[pkt->frag_idx] = (uint8_t *)malloc(MAX_PACKET_SIZE);

    if (collector->packets[pkt->frag_idx] == NULL)
    {
        REPORT();
        DROP_PACKET("Could not allocate memory for fragment");
        free_fragments(collector);

        return E_ERR;
    }

    /* Copy the packet */
    memcpy(collector->packets[pkt->frag_idx], (uint8_t *)&pkt[1], MAX_PACKET_SIZE);

    /* Increase number of fragments */
    ++collector->fragments;

    /* Update the bitmap */
    /* XXX: collector->fragments_bitmap |= 1 << pkt->frag_idx; */

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

        if ((o_full_packet == NULL) || (o_full_packet_size == NULL))
        {
            REPORT();
            return E_ERR;
        }

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

        printf("Finished packet: ID: %d Fragments: %d Size: %d Packet buffer: %p\n", collector->id, collector->total_fragments, collector->packet_size, packet);
        print_hex(packet, collector->packet_size);

        *o_full_packet = packet;
        *o_full_packet_size = collector->packet_size;

        /* Reset this collector */
        free_fragments(collector);

        return E_SUCCESS;
    }

    return E_FRAG;
}

uint8_t** break_packet(const uint8_t *packet, uint32_t size, uint32_t src, uint32_t dst, uint32_t *o_frags)
{
    /* Variable definition */
    uint8_t **packets, **new_packets;
    hdr_t *frag;
    uint32_t id = g_id++;
    uint32_t orig_size = size;
    const uint32_t frag_size = sizeof(hdr_t) + MAX_PACKET_SIZE;

    /* Code section */

    if ((packet == NULL) || (o_frags == NULL))
    {
        REPORT();
        return NULL;
    }

    /* Allocate memory for the array of fragments */
    packets = (uint8_t **)malloc(sizeof(uint8_t *));

    /* We at least have one fragment */
    *o_frags = 1;

    do
    {
        /* Allocate memory for the fragment */
        frag = (hdr_t *)malloc(frag_size);

        if (frag == NULL)
        {
            REPORT();
            free_frag_packets(packets, *o_frags - 1);
            free(packets);
            packets = NULL;
            break;
        }

        /* Clear all previously remaind data */
        memset(frag, 0, frag_size);

        /* Build header */
        frag->size = orig_size;
        frag->id = id;
        frag->frag_idx = *o_frags - 1;
        frag->src = src;
        frag->dst = dst;

        /* Copy the data */
        memcpy((uint8_t *)&frag[1], packet, size < MAX_PACKET_SIZE ? size : MAX_PACKET_SIZE);

        /* Set the fragment in the list */
        packets[*o_frags - 1] = (uint8_t *)frag;

        /* Quit loop if we finished processing */
        if (size <= MAX_PACKET_SIZE)
        {
            break;
        }

        /* Advance on the buffer to next fragment */
        packet += MAX_PACKET_SIZE;
        size -= MAX_PACKET_SIZE;

        /* Increase number of fragments */
        ++(*o_frags);

        /* Realloc the array */
        new_packets = realloc(packets, *o_frags * sizeof(uint8_t *));

        if (new_packets == NULL)
        {
            REPORT();

            free_frag_packets(packets, *o_frags - 1);
            *o_frags = 0;
            free(packets);
            packets = NULL;
            break;
        }
    } while (size);

    return packets;
}

static uint8_t* build_packet(collector_t *collector)
{
    /* Variable definition */
    uint8_t *packet, *pkt_ptr;
    uint32_t i;
    uint32_t total;
    uint8_t **pkts;

    /* Code section */
    /* Allocate memory for the packet */
    packet = (uint8_t *)malloc(collector->packet_size);

    if (packet == NULL)
    {
        return NULL;
    }

    pkt_ptr = packet;

    /* Get the packets array */
    pkts = collector->packets;

    /* Get total size */
    total = collector->packet_size;

    /* Start building the packet */
    for (i = 0;
         (total >= MAX_PACKET_SIZE) && (i < collector->total_fragments);
         ++i)
    {
        memcpy(pkt_ptr, pkts[i], MAX_PACKET_SIZE);
        pkt_ptr += MAX_PACKET_SIZE;
        total -= MAX_PACKET_SIZE;
    }

    /* Copt the rest of the packet */
    if (total > 0)
    {
        memcpy(pkt_ptr, pkts[i], total);
    }

    return packet;
}

static void reset_collector(collector_t *collector)
{
    /* Reset the collector */
    memset((uint8_t *)collector, 0, sizeof(collector_t));
    collector->available = 1;
}

static void free_frag_packets(uint8_t **packets, uint32_t num)
{
    /* Variable definition */
    uint32_t index;

    /* Code section */
    /* Free all fragments */
    for (index = 0; index < num; ++index)
    {
        /* Free the fragment */
        free(packets[index]);
        packets[index] = NULL;
    }
}

static void free_fragments(collector_t *collector)
{
    if (collector->packets == NULL)
    {
        REPORT();
    }
    else
    {
        free_frag_packets(collector->packets, collector->fragments);
        free(collector->packets);
    }

    reset_collector(collector);
}

static uint32_t get_available_collector()
{
    /* Variable definition */
    uint32_t index;
    uint32_t lowest_time = ~0;
    uint32_t lowest_index = 0;

    /* Code section */
    /* Init all the collectors */
    for (index = 0; index < MAX_COLLECTORS; ++index)
    {
        if (collectors[index].available)
        {
            return index;
        }

        /* If there is no available collector - get the collector that was updated the least */
        if (collectors[index].last_update < lowest_time)
        {
            lowest_time = collectors[index].last_update;
            lowest_index = index;
        }
    }

    /* Free all it's fragments */
    free_fragments(&collectors[lowest_index]);

    /* XXX: Should return 'lowest_index` instead of `index` */
    REPORT();

    return lowest_index;
}
