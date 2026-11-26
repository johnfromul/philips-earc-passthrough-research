/*
 * Philips TPM231WW .214 audio HAL wrapper.
 * MAT packing and stream callbacks adapted from the TCL eARC research project,
 * revision e889c6b (native/audio_primary_merak_shim.c).
 * Wraps stock audio and handles raw TrueHD sent to HDMI eARC.
 */

#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <android/log.h>

#define LOG_TAG "earc_mat_hal"
#define ALOGI(...) __android_log_print(ANDROID_LOG_INFO,  LOG_TAG, __VA_ARGS__)
#define ALOGW(...) __android_log_print(ANDROID_LOG_WARN,  LOG_TAG, __VA_ARGS__)
#define ALOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

#define ORIG_HAL_PATH "/vendor/lib/hw/audio.primary.merak.orig.so"
#define BYPASS_DEV    "/dev/earc_mat_bypass"

#define HARDWARE_MODULE_TAG          0x48574d54U /* 'TMWH' */
#define AUDIO_MODULE_API_VERSION_0_1 0x0001
#define HARDWARE_HAL_API_VERSION     0x0100

#define AUDIO_FORMAT_DOLBY_TRUEHD    0x0E000000U
#define AUDIO_FORMAT_IEC61937        0x0D000000U
#define AUDIO_DEVICE_OUT_HDMI_EARC   0x00040001U

#define MAT_FRAME_BYTES              61440U
#define MAT_PAYLOAD_BYTES            61424U
#define TRUEHD_NOMINAL_AU_SPACING    2560U
#define TRUEHD_MAX_AU_BYTES          8190U   /* 12-bit word length max: 4095 * 2 */
#define TRUEHD_ACC_MAX               16384U
#define MAX_SHIM_STREAMS             8U

/* Bit-exact IEC61937-9 / Dolby MAT 2.0 constants (pre-swapped 16-bit LE, verified against FFmpeg spdifenc.c) */
static const uint8_t IEC61937_MAT_HDR[8] = {
    0x72, 0xf8, 0x1f, 0x4e, 0x16, 0x00, 0xf0, 0xef
};
static const uint8_t MAT_START_CODE[20] = {
    0x9e, 0x07, 0x03, 0x00, 0x01, 0x84, 0x01, 0x01,
    0x00, 0x80, 0xa5, 0x56, 0xf4, 0x3b, 0x83, 0x81,
    0x80, 0x49, 0xe0, 0x77
};
static const uint8_t MAT_MIDDLE_CODE[12] = {
    0xc1, 0xc3, 0x49, 0x42, 0xfa, 0x3b, 0x83, 0x82,
    0x80, 0x49, 0xe0, 0x77
};
static const uint8_t MAT_END_CODE[16] = {
    0xc2, 0xc3, 0xc4, 0xc0, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x11, 0x97
};

struct mat_code_desc {
    uint32_t pos;
    uint32_t len;
    const uint8_t *code;
};

static const struct mat_code_desc MAT_CODES[3] = {
    { 0U,     sizeof(MAT_START_CODE),  MAT_START_CODE  },
    { 30708U, sizeof(MAT_MIDDLE_CODE), MAT_MIDDLE_CODE },
    { 61408U, sizeof(MAT_END_CODE),    MAT_END_CODE    },
};

/* Minimal 32-bit AOSP hardware/hardware.h + hardware/audio.h ABI (verified against audio.primary.merak.so) */
struct hw_module_t;
struct hw_device_t;

struct hw_module_methods_t {
    int (*open)(const struct hw_module_t *module, const char *id, struct hw_device_t **device);
};

struct hw_module_t {
    uint32_t tag;
    uint16_t module_api_version;
    uint16_t hal_api_version;
    const char *id;
    const char *name;
    const char *author;
    struct hw_module_methods_t *methods;
    void *dso;
    uint32_t reserved[32 - 7];
};

struct hw_device_t {
    uint32_t tag;
    uint32_t version;
    struct hw_module_t *module;
    uint32_t reserved[12];
    int (*close)(struct hw_device_t *device);
};

struct audio_offload_info {
    uint16_t version, size;
    uint32_t sample_rate, channel_mask, format, stream_type, bit_rate;
    int64_t duration_us;
    uint8_t has_video, is_streaming;
    uint32_t bit_width, buffer_size, usage, encapsulation_mode;
    int32_t content_id, sync_id;
} __attribute__((aligned(8)));

struct audio_config {
    uint32_t sample_rate;
    uint32_t channel_mask;
    uint32_t format;
    struct audio_offload_info offload_info;
    uint32_t frame_count;
};

_Static_assert(sizeof(struct audio_offload_info) == 64, "offload info ABI");
_Static_assert(__builtin_offsetof(struct audio_config, frame_count) == 80, "config ABI");

struct audio_stream {
    uint32_t (*get_sample_rate)(const struct audio_stream *stream);
    int      (*set_sample_rate)(struct audio_stream *stream, uint32_t rate);
    size_t   (*get_buffer_size)(const struct audio_stream *stream);
    uint32_t (*get_channels)(const struct audio_stream *stream);
    uint32_t (*get_format)(const struct audio_stream *stream);
    int      (*set_format)(struct audio_stream *stream, uint32_t format);
    int      (*standby)(struct audio_stream *stream);
    int      (*dump)(const struct audio_stream *stream, int fd);
    uint32_t (*get_device)(const struct audio_stream *stream);
    int      (*set_device)(struct audio_stream *stream, uint32_t device);
    int      (*set_parameters)(struct audio_stream *stream, const char *kv_pairs);
    char *   (*get_parameters)(const struct audio_stream *stream, const char *keys);
    int      (*add_audio_effect)(const struct audio_stream *stream, void *effect);
    int      (*remove_audio_effect)(const struct audio_stream *stream, void *effect);
};

struct audio_stream_out {
    struct audio_stream common;                                                         /* +0x00 .. +0x37 */
    uint32_t (*get_latency)(const struct audio_stream_out *stream);                     /* +0x38 */
    int      (*set_volume)(struct audio_stream_out *stream, float left, float right);   /* +0x3c */
    ssize_t  (*write)(struct audio_stream_out *stream, const void *buffer, size_t bytes);/* +0x40 */
    int      (*get_render_position)(const struct audio_stream_out *stream, uint32_t *dsp_frames); /* +0x44 */
    int      (*get_next_write_timestamp)(const struct audio_stream_out *stream, int64_t *ts);     /* +0x48 */
    int      (*set_callback)(struct audio_stream_out *stream, void *cb, void *cookie);  /* +0x4c */
    int      (*pause)(struct audio_stream_out *stream);                                 /* +0x50 */
    int      (*resume)(struct audio_stream_out *stream);                                /* +0x54 */
    int      (*drain)(struct audio_stream_out *stream, int type);                       /* +0x58 */
    int      (*flush)(struct audio_stream_out *stream);                                 /* +0x5c */
    int      (*get_presentation_position)(const struct audio_stream_out *stream,
                                          uint64_t *frames, struct timespec *timestamp);/* +0x60 */
    void    *extra[40];
};

struct audio_hw_device {
    struct hw_device_t common;                                                          /* +0x00 .. +0x3f */
    uint32_t (*get_supported_devices)(const struct audio_hw_device *dev);               /* +0x40 */
    int      (*init_check)(const struct audio_hw_device *dev);                          /* +0x44 */
    int      (*set_voice_volume)(struct audio_hw_device *dev, float volume);            /* +0x48 */
    int      (*set_master_volume)(struct audio_hw_device *dev, float volume);           /* +0x4c */
    int      (*get_master_volume)(struct audio_hw_device *dev, float *volume);          /* +0x50 */
    int      (*set_mode)(struct audio_hw_device *dev, int mode);                        /* +0x54 */
    int      (*set_mic_mute)(struct audio_hw_device *dev, int state);                   /* +0x58 */
    int      (*get_mic_mute)(const struct audio_hw_device *dev, int *state);            /* +0x5c */
    int      (*set_parameters)(struct audio_hw_device *dev, const char *kv_pairs);      /* +0x60 */
    char *   (*get_parameters)(const struct audio_hw_device *dev, const char *keys);    /* +0x64 */
    size_t   (*get_input_buffer_size)(const struct audio_hw_device *dev,
                                      const struct audio_config *config);               /* +0x68 */
    int      (*open_output_stream)(struct audio_hw_device *dev, int handle,
                                   uint32_t devices, int flags,
                                   struct audio_config *config,
                                   struct audio_stream_out **stream_out,
                                   const char *address);                                /* +0x6c */
    void     (*close_output_stream)(struct audio_hw_device *dev,
                                    struct audio_stream_out *stream_out);               /* +0x70 */
};

struct iec_output;

struct mat_stream_out {
    struct audio_stream_out *stream;
    struct iec_output *iec;
    ssize_t (*write_stock)(struct audio_stream_out *, const void *, size_t);
    int (*standby_stock)(struct audio_stream *);
    int (*pause_stock)(struct audio_stream_out *);
    int (*flush_stock)(struct audio_stream_out *);
    int (*drain_stock)(struct audio_stream_out *, int);
    pthread_mutex_t lock;
    /* Callback lifetime is protected by g_active_lock. */
    unsigned int active_callbacks;
    int closing;
    int fd;
    uint32_t resync_skips;

    /* Dynamic IEC 61937-9 / Dolby MAT 2.0 input_timing & output_timing packer state */
    uint8_t  mat_buf[MAT_FRAME_BYTES]; /* [0..7]=IEC hdr, [8..61431]=MAT payload, [61432..61439]=0 */
    uint32_t mat_filled;               /* bytes filled in [0 .. MAT_PAYLOAD_BYTES) */
    uint16_t thd_prev_time;
    int      thd_prev_size;
    int      thd_samples_per_frame;
    uint16_t thd_output_timing;
    int      thd_output_timing_valid;
    int      thd_oi_delta;
    uint8_t  thd_acc[TRUEHD_ACC_MAX];
    size_t   thd_acc_len;
    int      thd_synced;
    uint32_t mat_bursts_emitted;
};

static void *g_orig_handle = NULL;
static struct hw_module_t *g_orig_hmi = NULL;
static int   (*g_orig_open_output_stream)(struct audio_hw_device *, int, uint32_t, int,
                                          struct audio_config *, struct audio_stream_out **,
                                          const char *) = NULL;
static void  (*g_orig_close_output_stream)(struct audio_hw_device *, struct audio_stream_out *) = NULL;

static pthread_mutex_t g_active_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_callbacks_idle = PTHREAD_COND_INITIALIZER;
static struct mat_stream_out *g_mat_streams[MAX_SHIM_STREAMS];

static int register_shim_stream(struct mat_stream_out *m)
{
    int registered = 0;
    pthread_mutex_lock(&g_active_lock);
    for (uint32_t i = 0; i < MAX_SHIM_STREAMS; i++) {
        if (g_mat_streams[i] == NULL) {
            g_mat_streams[i] = m;
            registered = 1;
            break;
        }
    }
    pthread_mutex_unlock(&g_active_lock);
    return registered;
}

static struct mat_stream_out *unregister_shim_stream(struct audio_stream_out *out)
{
    struct mat_stream_out *found = NULL;
    pthread_mutex_lock(&g_active_lock);
    for (uint32_t i = 0; i < MAX_SHIM_STREAMS; i++) {
        if (g_mat_streams[i] && g_mat_streams[i]->stream == out) {
            found = g_mat_streams[i];
            g_mat_streams[i] = NULL;
            found->closing = 1;
            while (found->active_callbacks)
                pthread_cond_wait(&g_callbacks_idle, &g_active_lock);
            break;
        }
    }
    pthread_mutex_unlock(&g_active_lock);
    return found;
}

static struct mat_stream_out *lock_shim_stream(const struct audio_stream_out *out)
{
    struct mat_stream_out *found = NULL;
    pthread_mutex_lock(&g_active_lock);
    for (uint32_t i = 0; i < MAX_SHIM_STREAMS; i++) {
        if (g_mat_streams[i] && g_mat_streams[i]->stream == out) {
            found = g_mat_streams[i];
            found->active_callbacks++;
            break;
        }
    }
    pthread_mutex_unlock(&g_active_lock);
    if (found)
        pthread_mutex_lock(&found->lock);
    return found;
}

static void unlock_shim_stream(struct mat_stream_out *m)
{
    pthread_mutex_unlock(&m->lock);
    pthread_mutex_lock(&g_active_lock);
    if (--m->active_callbacks == 0 && m->closing)
        pthread_cond_broadcast(&g_callbacks_idle);
    pthread_mutex_unlock(&g_active_lock);
}

static void mat_reset_packer_locked(struct mat_stream_out *m)
{
    memset(m->mat_buf, 0, MAT_FRAME_BYTES);
    m->mat_filled = 0;
    m->thd_prev_time = 0;
    m->thd_prev_size = 0;
    m->thd_samples_per_frame = 0;
    m->thd_output_timing = 0;
    m->thd_output_timing_valid = 0;
    m->thd_oi_delta = 0;
    m->thd_acc_len = 0;
    m->thd_synced = 0;
}

static void mat_close_fd_locked(struct mat_stream_out *m)
{
    if (m->fd >= 0) {
        close(m->fd);
        m->fd = -1;
        ALOGI("Closed MAT writer: bursts=%u parser_skips=%u",
              m->mat_bursts_emitted, m->resync_skips);
    }
}

static int mat_write_all_locked(struct mat_stream_out *m, const uint8_t *p, size_t left)
{
    while (left) {
        ssize_t written = write(m->fd, p, left);
        if (written < 0 && errno == EINTR)
            continue;
        if (written <= 0)
            return written < 0 ? -errno : -EPIPE;
        p += (size_t)written;
        left -= (size_t)written;
    }
    return 0;
}

/* Copy TrueHD AU bytes into MAT payload with 16-bit LE byte swap (32-bit rev16 fast path) */
static void copy_swap16(uint8_t *dst, const uint8_t *src, size_t len)
{
    size_t i = 0;
    for (; i + 4 <= len; i += 4) {
        uint32_t v;
        memcpy(&v, src + i, 4);
        v = ((v & 0x00ff00ffU) << 8) | ((v & 0xff00ff00U) >> 8);
        memcpy(dst + i, &v, 4);
    }
    for (; i + 1 < len; i += 2) {
        dst[i]     = src[i + 1];
        dst[i + 1] = src[i];
    }
    if (i < len) {
        dst[i]     = 0;
        dst[i + 1] = src[i];
    }
}

/* Bounded MSB-first bit reader for TrueHD Major Sync substream restart header (output_timing) */
struct bit_reader {
    const uint8_t *buf;
    size_t bit_pos;
    size_t bit_end;
};

static inline size_t br_left(const struct bit_reader *br)
{
    return (br->bit_pos < br->bit_end) ? (br->bit_end - br->bit_pos) : 0U;
}

static inline uint32_t br_get(struct bit_reader *br, unsigned int nbits)
{
    uint32_t val = 0;
    for (unsigned int i = 0; i < nbits; i++) {
        size_t p = br->bit_pos++;
        val = (val << 1) | ((br->buf[p >> 3] >> (7U - (p & 7U))) & 1U);
    }
    return val;
}

static inline void br_skip(struct bit_reader *br, size_t nbits)
{
    br->bit_pos += nbits;
}

struct thd_au_info {
    uint16_t input_timing;
    uint16_t output_timing;
    int      has_output_timing;
    int      samples_per_frame;
};

/* Parse TrueHD / MLP Access Unit timing & optional Major Sync restart-header output_timing */
static int truehd_parse_au_info(const uint8_t *au_data, size_t au_size, struct thd_au_info *au)
{
    memset(au, 0, sizeof(*au));
    if (au_size < 4U)
        return -1;

    au->input_timing = (uint16_t)(((uint16_t)au_data[2] << 8) | au_data[3]);

    const uint8_t *data = au_data + 4;
    size_t size = au_size - 4U;
    if (size < 6U || data[0] != 0xf8U || data[1] != 0x72U || data[2] != 0x6fU)
        return 0;

    uint8_t ratebits;
    if (data[3] == 0xbaU)
        ratebits = data[4] >> 4;
    else if (data[3] == 0xbbU)
        ratebits = data[5] >> 4;
    else
        return -1;

    /* 44.1 / 88.2 / 176.4 kHz family cannot be carried on a fixed 192.000 kHz 20ms ring */
    if ((ratebits & 0x08U) != 0U)
        return -2;

    au->samples_per_frame = 40 << (ratebits & 0x03U); /* 40 (48k), 80 (96k), or 160 (192k) */
    if (size < 28U)
        return 0;

    size_t major_sync_size = 28U;
    if (data[3] == 0xbaU && (data[25] & 1U)) {
        size_t ext_size = (size_t)(data[26] >> 4);
        major_sync_size += 2U + ext_size * 2U;
    }
    if (major_sync_size >= size)
        return 0;

    int num_substreams = (int)(data[16] >> 4);
    if (num_substreams <= 0)
        return 0;

    struct bit_reader br = {
        .buf     = data + major_sync_size,
        .bit_pos = 0U,
        .bit_end = (size - major_sync_size) * 8U,
    };

    for (int i = 0; i < num_substreams; i++) {
        if (br_left(&br) < 16U)
            return 0;
        uint32_t extra_word = br_get(&br, 1U);
        br_skip(&br, 15U);
        if (extra_word) {
            if (br_left(&br) < 16U)
                return 0;
            br_skip(&br, 16U);
        }
    }

    if (br_left(&br) < 2U)
        return 0;
    if (!br_get(&br, 1U)) /* !block_header_exists */
        return 0;
    if (!br_get(&br, 1U)) /* !restart_header_exists */
        return 0;
    if (br_left(&br) < (13U + 1U + 16U))
        return 0;

    uint32_t sync_word = br_get(&br, 13U);
    if (sync_word != (0x31eaU >> 1))
        return 0;
    br_skip(&br, 1U); /* noise_type */

    au->output_timing = (uint16_t)br_get(&br, 16U);
    au->has_output_timing = 1;
    return 0;
}

static int mat_emit_current_frame_locked(struct mat_stream_out *m)
{
    memcpy(m->mat_buf + 0, IEC61937_MAT_HDR, sizeof(IEC61937_MAT_HDR));
    memset(m->mat_buf + 8U + MAT_PAYLOAD_BYTES, 0, MAT_FRAME_BYTES - 8U - MAT_PAYLOAD_BYTES);

    int rc = mat_write_all_locked(m, m->mat_buf, MAT_FRAME_BYTES);
    memset(m->mat_buf, 0, MAT_FRAME_BYTES);
    m->mat_filled = 0;
    if (rc != 0)
        return rc;

    m->mat_bursts_emitted++;
    /* One 61,440-byte MAT 2.0 frame = 20.000 ms = sample_rate / 50 frames (960 @ 48k, 1920 @ 96k, 3840 @ 192k) */
    return 0;
}

/*
 * Dynamic IEC 61937-9 / Dolby MAT 2.0 AU packer (matches FFmpeg spdif_header_truehd):
 * Places AUs at exact input_timing offsets, seamlessly splitting large UHD Remux AUs (>2560 B)
 * around MAT_MIDDLE_CODE (30708) and across consecutive 61,440-byte MAT frames.
 */
static int mat_pack_truehd_au_locked(struct mat_stream_out *m, const uint8_t *au_buf, size_t au_len)
{
    if (au_len < 10U)
        return 0;

    struct thd_au_info au;
    int prc = truehd_parse_au_info(au_buf, au_len, &au);
    if (prc < 0)
        return 0;

    if (au.samples_per_frame > 0)
        m->thd_samples_per_frame = au.samples_per_frame;
    if (m->thd_samples_per_frame <= 0)
        return 0;

    uint16_t input_timing = au.input_timing;
    int max_padding = (int)(MAT_PAYLOAD_BYTES * 2U);
    int padding_remaining = 0;
    int output_discontinuity = 0;

    m->thd_output_timing = (uint16_t)(m->thd_output_timing + (uint16_t)m->thd_samples_per_frame);
    if (au.has_output_timing) {
        if (m->thd_output_timing_valid && au.output_timing != m->thd_output_timing) {
            int bytes_per_sample = (int)TRUEHD_NOMINAL_AU_SPACING / m->thd_samples_per_frame;
            uint16_t out_minus_spf = (uint16_t)(au.output_timing - (uint16_t)m->thd_samples_per_frame);
            int prev_oi_delta = m->thd_oi_delta;
            int curr_oi_delta = (int)(int16_t)(out_minus_spf - input_timing);
            int prev_padding = m->thd_prev_size ? ((int)TRUEHD_NOMINAL_AU_SPACING - m->thd_prev_size) : 0;
            int disc_padding = (prev_oi_delta - curr_oi_delta) * bytes_per_sample;

            output_discontinuity = 1;
            padding_remaining = prev_padding + disc_padding;
            if (padding_remaining < 0 || padding_remaining > max_padding)
                padding_remaining = 0;
        }
        m->thd_output_timing = au.output_timing;
        m->thd_output_timing_valid = 1;
    }

    if (m->thd_prev_size > 0 && !output_discontinuity) {
        uint16_t delta_samples = (uint16_t)(input_timing - m->thd_prev_time);
        int delta_bytes = ((int)delta_samples * (int)TRUEHD_NOMINAL_AU_SPACING) / m->thd_samples_per_frame;
        padding_remaining = delta_bytes - m->thd_prev_size;
        if (padding_remaining < 0 || padding_remaining > max_padding)
            padding_remaining = 0;
    }

    if (m->thd_output_timing_valid) {
        uint16_t out_minus_spf = (uint16_t)(m->thd_output_timing - (uint16_t)m->thd_samples_per_frame);
        m->thd_oi_delta = (int)(int16_t)(out_minus_spf - input_timing);
    }

    uint32_t next_code_idx = 0;
    for (; next_code_idx < 3U; next_code_idx++) {
        if (m->mat_filled <= MAT_CODES[next_code_idx].pos)
            break;
    }
    if (next_code_idx >= 3U) {
        mat_reset_packer_locked(m);
        return 0;
    }

    int total_frame_size = (int)au_len;
    const uint8_t *dataptr = au_buf;
    int data_remaining = (int)au_len;
    uint8_t *payload = m->mat_buf + 8U;

    while (padding_remaining > 0 || data_remaining > 0 ||
           MAT_CODES[next_code_idx].pos == m->mat_filled) {

        if (MAT_CODES[next_code_idx].pos == m->mat_filled) {
            int code_len = (int)MAT_CODES[next_code_idx].len;
            int code_len_rem = code_len;
            memcpy(payload + MAT_CODES[next_code_idx].pos, MAT_CODES[next_code_idx].code, (size_t)code_len);
            m->mat_filled += (uint32_t)code_len;

            next_code_idx++;
            if (next_code_idx == 3U) {
                next_code_idx = 0U;
                int rc = mat_emit_current_frame_locked(m);
                if (rc != 0) {
                    mat_reset_packer_locked(m);
                    return rc;
                }
                payload = m->mat_buf + 8U;
                /* Account for the 16-byte IEC inter-frame header/tail gap (61440 - 61424) */
                code_len_rem += (int)(MAT_FRAME_BYTES - MAT_PAYLOAD_BYTES);
            }

            if (padding_remaining > 0) {
                int used_pad = (padding_remaining < code_len_rem) ? padding_remaining : code_len_rem;
                padding_remaining -= used_pad;
                code_len_rem -= used_pad;
            }
            if (code_len_rem > 0)
                total_frame_size += code_len_rem;
        }

        if (padding_remaining > 0) {
            int avail = (int)(MAT_CODES[next_code_idx].pos - m->mat_filled);
            int pad_now = (avail < padding_remaining) ? avail : padding_remaining;
            if (pad_now > 0) {
                memset(payload + m->mat_filled, 0, (size_t)pad_now);
                m->mat_filled += (uint32_t)pad_now;
                padding_remaining -= pad_now;
            }
            if (padding_remaining > 0)
                continue;
        }

        if (data_remaining > 0) {
            int avail = (int)(MAT_CODES[next_code_idx].pos - m->mat_filled);
            int ins_now = (avail < data_remaining) ? avail : data_remaining;
            if (ins_now > 0) {
                copy_swap16(payload + m->mat_filled, dataptr, (size_t)ins_now);
                m->mat_filled += (uint32_t)ins_now;
                dataptr += ins_now;
                data_remaining -= ins_now;
            }
        }
    }

    m->thd_prev_time = input_timing;
    m->thd_prev_size = total_frame_size;
    return 0;
}

/* Pad and flush any trailing partial MAT payload (<20ms tail) at end-of-stream drain() */
static int mat_flush_partial_tail_locked(struct mat_stream_out *m)
{
    if (m->mat_filled <= sizeof(MAT_START_CODE))
        return 0;

    uint8_t *payload = m->mat_buf + 8U;
    if (m->mat_filled <= MAT_CODES[1].pos) {
        memset(payload + m->mat_filled, 0, MAT_CODES[1].pos - m->mat_filled);
        memcpy(payload + MAT_CODES[1].pos, MAT_CODES[1].code, MAT_CODES[1].len);
        m->mat_filled = MAT_CODES[1].pos + MAT_CODES[1].len;
    }
    if (m->mat_filled <= MAT_CODES[2].pos) {
        memset(payload + m->mat_filled, 0, MAT_CODES[2].pos - m->mat_filled);
        memcpy(payload + MAT_CODES[2].pos, MAT_CODES[2].code, MAT_CODES[2].len);
        m->mat_filled = MAT_CODES[2].pos + MAT_CODES[2].len;
    }

    int rc = mat_emit_current_frame_locked(m);
    mat_reset_packer_locked(m);
    return rc;
}

static ssize_t mat_write_truehd_raw_locked(struct mat_stream_out *m, const uint8_t *buf, size_t bytes)
{
    size_t consumed = 0;
    while (consumed < bytes) {
        size_t space = TRUEHD_ACC_MAX - m->thd_acc_len;
        size_t take = bytes - consumed;
        if (take > space) take = space;
        memcpy(m->thd_acc + m->thd_acc_len, buf + consumed, take);
        m->thd_acc_len += take;
        consumed += take;

        size_t pos = 0;
        while (m->thd_acc_len - pos >= 10U) {
            const uint8_t *p = m->thd_acc + pos;
            if (!m->thd_synced) {
                /* Lock cleanly onto a TrueHD Major Sync (F8 72 6F BA at bytes 4..7, 48/96/192 kHz) */
                if (p[4] == 0xF8U && p[5] == 0x72U && p[6] == 0x6FU && p[7] == 0xBAU &&
                    ((p[8] >> 4) & 0x08U) == 0U) {
                    size_t au_len = (size_t)(((p[0] & 0x0fU) << 8) | p[1]) * 2U;
                    if (au_len >= 28U && au_len <= TRUEHD_MAX_AU_BYTES) {
                        m->thd_synced = 1;
                    } else {
                        m->resync_skips++;
                        pos++;
                        continue;
                    }
                } else {
                    m->resync_skips++;
                    pos++;
                    continue;
                }
            }
            /* TrueHD AU length in 16-bit words is in bits [11:0] of first 2 bytes (up to 8,190 B) */
            size_t au_len = (size_t)(((p[0] & 0x0fU) << 8) | p[1]) * 2U;
            if (au_len < 8U || au_len > TRUEHD_MAX_AU_BYTES) {
                /* Lost sync: reset MAT packer alignment and re-lock on next Major Sync */
                m->thd_synced = 0;
                m->thd_prev_size = 0;
                m->thd_output_timing_valid = 0;
                m->mat_filled = 0;
                memset(m->mat_buf, 0, MAT_FRAME_BYTES);
                m->resync_skips++;
                pos++;
                continue;
            }
            if (m->thd_acc_len - pos < au_len)
                break; /* wait for complete AU bytes */

            int rc = mat_pack_truehd_au_locked(m, p, au_len);
            if (rc != 0)
                return rc;
            pos += au_len;
        }

        if (pos > 0) {
            size_t rem = m->thd_acc_len - pos;
            if (rem > 0)
                memmove(m->thd_acc, m->thd_acc + pos, rem);
            m->thd_acc_len = rem;
        } else if (m->thd_acc_len == TRUEHD_ACC_MAX) {
            /* Prevent stall on corrupt input */
            m->thd_acc_len = 0;
            m->thd_synced = 0;
        }
    }
    return (ssize_t)bytes;
}

/* Stock output owns the DSP carrier and all playback clocks. */
static ssize_t shim_write(struct audio_stream_out *out, const void *buffer, size_t bytes)
{
    struct mat_stream_out *m = lock_shim_stream(out);
    if (!m)
        return -ENODEV;
    ssize_t accepted = m->write_stock(out, buffer, bytes);
    if (accepted > 0 && (size_t)accepted <= bytes) {
        if (m->fd < 0) {
            m->fd = open(BYPASS_DEV, O_WRONLY | O_CLOEXEC);
            if (m->fd >= 0)
                ALOGI("MAT writer attached to the active Philips carrier");
        }
        if (m->fd >= 0) {
            ssize_t written = mat_write_truehd_raw_locked(m, buffer, (size_t)accepted);
            if (written < 0) {
                ALOGW("MAT write failed: %s", strerror((int)-written));
                mat_close_fd_locked(m);
                mat_reset_packer_locked(m);
            }
        }
    }
    unlock_shim_stream(m);
    return accepted;
}

static int shim_standby(struct audio_stream *stream)
{
    struct mat_stream_out *m = lock_shim_stream((struct audio_stream_out *)stream);
    if (!m)
        return -ENODEV;
    mat_close_fd_locked(m);
    mat_reset_packer_locked(m);
    int result = m->standby_stock(stream);
    unlock_shim_stream(m);
    return result;
}

static int shim_pause(struct audio_stream_out *out)
{
    struct mat_stream_out *m = lock_shim_stream(out);
    if (!m)
        return -ENODEV;
    mat_close_fd_locked(m);
    int result = m->pause_stock(out);
    unlock_shim_stream(m);
    return result;
}

static int shim_flush(struct audio_stream_out *out)
{
    struct mat_stream_out *m = lock_shim_stream(out);
    if (!m)
        return -ENODEV;
    mat_close_fd_locked(m);
    mat_reset_packer_locked(m);
    int result = m->flush_stock(out);
    unlock_shim_stream(m);
    return result;
}

static int shim_drain(struct audio_stream_out *out, int type)
{
    struct mat_stream_out *m = lock_shim_stream(out);
    if (!m)
        return -ENODEV;
    if (m->fd >= 0) {
        if (mat_flush_partial_tail_locked(m) == 0 && fsync(m->fd) < 0)
            ALOGW("MAT drain failed: %s", strerror(errno));
        mat_close_fd_locked(m);
        mat_reset_packer_locked(m);
    }
    int result = m->drain_stock(out, type);
    unlock_shim_stream(m);
    return result;
}

#include "iec-output.h"

static int shim_open_output_stream(struct audio_hw_device *dev, int handle,
                                   uint32_t devices, int flags,
                                   struct audio_config *config,
                                   struct audio_stream_out **stream_out,
                                   const char *address)
{
    if (config && config->format == AUDIO_FORMAT_IEC61937 &&
        devices == AUDIO_DEVICE_OUT_HDMI_EARC)
        return iec_open(dev, handle, flags, config, stream_out, address);

    int tap = config && config->format == AUDIO_FORMAT_DOLBY_TRUEHD &&
              config->sample_rate == 48000U && devices == AUDIO_DEVICE_OUT_HDMI_EARC;
    int rc = g_orig_open_output_stream(dev, handle, devices, flags, config, stream_out, address);
    if (rc || !tap || !*stream_out || !(*stream_out)->write)
        return rc;

    struct audio_stream_out *out = *stream_out;
    struct mat_stream_out *m = calloc(1, sizeof(*m));
    if (!m)
        return rc;
    if (pthread_mutex_init(&m->lock, NULL)) {
        free(m);
        return rc;
    }

    m->stream = out;
    m->write_stock = out->write;
    m->standby_stock = out->common.standby;
    m->pause_stock = out->pause;
    m->flush_stock = out->flush;
    m->drain_stock = out->drain;
    m->fd = -1;
    if (!register_shim_stream(m)) {
        pthread_mutex_destroy(&m->lock);
        free(m);
        return rc;
    }

    out->write = shim_write;
    if (m->standby_stock)
        out->common.standby = shim_standby;
    if (m->pause_stock)
        out->pause = shim_pause;
    if (m->flush_stock)
        out->flush = shim_flush;
    if (m->drain_stock)
        out->drain = shim_drain;
    ALOGI("INTERCEPT TrueHD rate=%u ch=0x%x device=0x%x flags=0x%x with the stock stream",
          config->sample_rate, config->channel_mask, devices, flags);
    return rc;
}

static void shim_close_output_stream(struct audio_hw_device *dev, struct audio_stream_out *out)
{
    struct mat_stream_out *m = unregister_shim_stream(out);
    if (m) {
        if (m->iec) {
            iec_close(dev, m);
            return;
        }
        pthread_mutex_lock(&m->lock);
        out->write = m->write_stock;
        out->common.standby = m->standby_stock;
        out->pause = m->pause_stock;
        out->flush = m->flush_stock;
        out->drain = m->drain_stock;
        mat_close_fd_locked(m);
        pthread_mutex_unlock(&m->lock);
        pthread_mutex_destroy(&m->lock);
        free(m);
    }
    g_orig_close_output_stream(dev, out);
}

static int shim_open(const struct hw_module_t *module, const char *id, struct hw_device_t **device)
{
    (void)module;
    if (!g_orig_handle) {
        g_orig_handle = dlopen(ORIG_HAL_PATH, RTLD_NOW);
        if (!g_orig_handle) {
            ALOGE("dlopen(%s) failed: %s", ORIG_HAL_PATH, dlerror());
            return -ENOENT;
        }
        g_orig_hmi = (struct hw_module_t *)dlsym(g_orig_handle, "HMI");
        if (!g_orig_hmi || !g_orig_hmi->methods || !g_orig_hmi->methods->open) {
            ALOGE("dlsym(HMI) failed in %s", ORIG_HAL_PATH);
            return -EINVAL;
        }
    }

    int rc = g_orig_hmi->methods->open(g_orig_hmi, id, device);
    if (rc != 0 || !*device) {
        ALOGE("orig open(%s) failed: %d", id ? id : "null", rc);
        return rc;
    }

    struct audio_hw_device *adev = (struct audio_hw_device *)(*device);
    g_orig_open_output_stream  = adev->open_output_stream;
    g_orig_close_output_stream = adev->close_output_stream;

    adev->open_output_stream  = shim_open_output_stream;
    adev->close_output_stream = shim_close_output_stream;

    ALOGI("READY: wrapped %s (eARC TrueHD -> %s)",
          ORIG_HAL_PATH, BYPASS_DEV);
    return 0;
}

static struct hw_module_methods_t g_shim_methods = {
    .open = shim_open,
};

__attribute__((visibility("default")))
struct hw_module_t HMI = {
    .tag                = HARDWARE_MODULE_TAG,
    .module_api_version = AUDIO_MODULE_API_VERSION_0_1,
    .hal_api_version    = HARDWARE_HAL_API_VERSION,
    .id                 = "audio",
    .name               = "Philips TPM231WW eARC Audio HAL Shim",
    .author             = "Philips TPM231WW eARC port",
    .methods            = &g_shim_methods,
};
