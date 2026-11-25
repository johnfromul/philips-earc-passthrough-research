#ifndef PHILIPS_IEC_MAT_H
#define PHILIPS_IEC_MAT_H

/* Strip the MAT framing and padding for the stock TrueHD shadow stream.
 * The receiver is sent the original IEC burst separately.
 */
struct iec_mat_decoder {
    uint8_t data[MAT_FRAME_BYTES + TRUEHD_MAX_AU_BYTES];
    size_t remaining;
    int synced;
};

static int iec_mat_valid(const uint8_t *frame)
{
    if (memcmp(frame, IEC61937_MAT_HDR, sizeof(IEC61937_MAT_HDR)))
        return 0;
    for (size_t i = 0; i < sizeof(MAT_CODES) / sizeof(MAT_CODES[0]); ++i) {
        const struct mat_code_desc *code = &MAT_CODES[i];
        if (memcmp(frame + sizeof(IEC61937_MAT_HDR) + code->pos,
                   code->code, code->len))
            return 0;
    }
    return 1;
}

static size_t iec_mat_decode(struct iec_mat_decoder *state, const uint8_t *frame,
                             uint8_t *raw)
{
    uint8_t *data = state->data;
    const uint8_t *payload = frame + sizeof(IEC61937_MAT_HDR);
    size_t size = state->remaining;
    for (size_t i = 0; i + 1 < sizeof(MAT_CODES) / sizeof(MAT_CODES[0]); ++i) {
        size_t first = MAT_CODES[i].pos + MAT_CODES[i].len;
        size_t count = MAT_CODES[i + 1].pos - first;
        copy_swap16(data + size, payload + first, count);
        size += count;
    }

    size_t pos = 0, written = 0;
    while (size - pos >= 8) {
        const uint8_t *header = data + pos;
        int major = header[4] == 0xf8 && header[5] == 0x72 &&
                    header[6] == 0x6f && header[7] == 0xba;
        if (major && size - pos < 10)
            break;

        if (major)
            state->synced = (header[8] >> 4) == 0;

        size_t length = (((header[0] & 0x0fU) << 8) | header[1]) * 2U;
        if (!state->synced || length == 0) {
            pos += 2;
            continue;
        }
        if (length < 8 || length > TRUEHD_MAX_AU_BYTES || (major && length < 28)) {
            state->synced = 0;
            pos += 2;
            continue;
        }
        if (length > size - pos)
            break;

        memcpy(raw + written, header, length);
        written += length;
        pos += length;
    }

    state->remaining = size - pos;
    memmove(data, data + pos, state->remaining);
    return written;
}

#endif
