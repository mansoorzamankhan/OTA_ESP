#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
typedef struct {
    bool (*begin)(uint32_t length);
    bool (*write)(const uint8_t *data, size_t length);
    bool (*end)(void);
    void (*abort)(void);
} ota_backend_t;
typedef struct {
    ota_backend_t backend;
    uint32_t total, written;
    uint16_t sector;
    uint8_t sequence;
    size_t used;
    bool active, complete, poisoned;
    uint8_t buffer[4096];
} ota_protocol_t;
uint16_t ota_crc16(const uint8_t *data, size_t length);
void ota_protocol_reset(ota_protocol_t *s);
bool ota_protocol_command(ota_protocol_t *s, const uint8_t *p, size_t n, uint8_t ack[20]);
/* true means an ACK is ready; false means a packet was accepted without ACK. */
bool ota_protocol_data(ota_protocol_t *s, const uint8_t *p, size_t n, uint8_t ack[20]);
void ota_protocol_progress(const ota_protocol_t *s, uint8_t out[10]);
