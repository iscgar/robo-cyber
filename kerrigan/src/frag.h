#ifndef _FRAG_H_
#define _FRAG_H_

typedef struct hdr_t
{
    uint32_t src;
    uint32_t dst;
    uint32_t size;
    uint32_t id;
    uint32_t frag_idx;
} hdr_t;

typedef enum
{
    E_ERR,
    E_FRAG,
    E_SUCCESS,
    E_EXISTS
} frag_e;

extern void init_collectors();
extern frag_e collect_packets(const uint8_t *pkt_buffer, uint32_t pkt_len, uint8_t **o_full_packet, uint32_t *o_full_packet_size);
extern uint8_t** break_packet(const uint8_t *packet, uint32_t size, uint32_t src, uint32_t dst, uint32_t *o_frags);

#endif /* !_FRAG_H_ */