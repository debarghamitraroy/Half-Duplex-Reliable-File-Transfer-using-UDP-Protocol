/* common.h */

#ifndef COMMON_H
#define COMMON_H

#include <stdint.h>

#define SEGMENT_SIZE 1024 // payload bytes per packet
#define FILENAME_MAXLEN 255 // stored file name length (no path)
#define DEFAULT_PORT 9999
#define DEFAULT_TIMEOUT_MS 1500 // retransmission timeout
#define MAX_PACKET_SIZE (sizeof(struct PacketHeader) + SEGMENT_SIZE)

// Simple 32-bit additive checksum (fast, not cryptographic)
static inline uint32_t csum32(const uint8_t *buf, uint32_t len) {
    uint32_t s = 0;
    for (uint32_t i = 0; i < len; ++i)
        s += buf[i];
    return s;
}

// Packet header; payload follows immediately after header (data_len bytes)
#pragma pack(push, 1)
struct PacketHeader {
    uint32_t seq; // sequence number (network byte order)
    uint16_t data_len; // payload length (<= SEGMENT_SIZE), network byte order
    uint8_t eof; // 1 if this is the last packet
    char file[FILENAME_MAXLEN + 1]; // file identifier (ASCII, NUL-terminated)
    uint32_t checksum; // checksum over header (with checksum=0) + payload
};
#pragma pack(pop)

// ACK packet (small)
#pragma pack(push, 1)
struct AckPacket {
    uint32_t ack_seq; // sequence being acknowledged (network byte order)
    uint32_t checksum; // checksum over ack_seq (with checksum=0)
};
#pragma pack(pop)

#endif // COMMON_H
