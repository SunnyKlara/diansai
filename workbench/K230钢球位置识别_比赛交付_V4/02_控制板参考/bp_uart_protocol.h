#ifndef BP_UART_PROTOCOL_H
#define BP_UART_PROTOCOL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define BP_RX_BUFFER_SIZE 40U

typedef struct {
    bool valid;
    float position_cm;
} bp_position_t;

typedef struct {
    char buffer[BP_RX_BUFFER_SIZE];
    size_t length;
} bp_rx_parser_t;

void bp_rx_parser_init(bp_rx_parser_t *parser);

/*
 * Feed one received UART byte.
 * Returns true only when a complete, checksum-valid BP frame was decoded.
 * This routine is intended for the main loop. If UART bytes arrive in an ISR,
 * first place them in a ring buffer and call this routine outside the ISR.
 */
bool bp_rx_push_byte(
    bp_rx_parser_t *parser,
    uint8_t byte,
    bp_position_t *output
);

bool bp_parse_line(const char *line, bp_position_t *output);

#endif
