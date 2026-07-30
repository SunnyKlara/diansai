#include "bp_uart_protocol.h"

#include <stdio.h>
#include <string.h>

static int hex_value(char character)
{
    if (character >= '0' && character <= '9') {
        return character - '0';
    }
    if (character >= 'A' && character <= 'F') {
        return character - 'A' + 10;
    }
    if (character >= 'a' && character <= 'f') {
        return character - 'a' + 10;
    }
    return -1;
}

void bp_rx_parser_init(bp_rx_parser_t *parser)
{
    if (parser == NULL) {
        return;
    }
    parser->length = 0U;
    parser->buffer[0] = '\0';
}

bool bp_parse_line(const char *line, bp_position_t *output)
{
    const char *star;
    const char *cursor;
    char body[BP_RX_BUFFER_SIZE];
    size_t body_length;
    uint8_t calculated_checksum = 0U;
    int high;
    int low;
    int valid_integer;
    float position_cm;

    if (line == NULL || output == NULL || line[0] != '$') {
        return false;
    }

    star = strchr(line, '*');
    if (star == NULL || star[1] == '\0' || star[2] == '\0') {
        return false;
    }

    for (cursor = line + 1; cursor < star; ++cursor) {
        calculated_checksum ^= (uint8_t)(*cursor);
    }

    high = hex_value(star[1]);
    low = hex_value(star[2]);
    if (high < 0 || low < 0) {
        return false;
    }
    if (calculated_checksum != (uint8_t)((high << 4) | low)) {
        return false;
    }

    body_length = (size_t)(star - (line + 1));
    if (body_length == 0U || body_length >= sizeof(body)) {
        return false;
    }
    memcpy(body, line + 1, body_length);
    body[body_length] = '\0';

    if (sscanf(body, "BP,%d,%f", &valid_integer, &position_cm) != 2) {
        return false;
    }
    if (valid_integer != 0 && valid_integer != 1) {
        return false;
    }
    if (valid_integer == 1 && (position_cm < -12.5f || position_cm > 12.5f)) {
        return false;
    }

    output->valid = (valid_integer == 1);
    output->position_cm = output->valid ? position_cm : 0.0f;
    return true;
}

bool bp_rx_push_byte(
    bp_rx_parser_t *parser,
    uint8_t byte,
    bp_position_t *output
)
{
    if (parser == NULL || output == NULL) {
        return false;
    }

    if (byte == (uint8_t)'$') {
        parser->length = 0U;
        parser->buffer[parser->length++] = '$';
        return false;
    }

    if (parser->length == 0U) {
        return false;
    }

    if (parser->length >= BP_RX_BUFFER_SIZE - 1U) {
        bp_rx_parser_init(parser);
        return false;
    }

    parser->buffer[parser->length++] = (char)byte;
    parser->buffer[parser->length] = '\0';

    if (byte == (uint8_t)'\n') {
        bool decoded = bp_parse_line(parser->buffer, output);
        bp_rx_parser_init(parser);
        return decoded;
    }

    return false;
}
