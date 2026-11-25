/*
 * audio.primary.merak.so shim for TCL C9K (MT5896 / V670, 32-bit ARM Android 14).
 *
 * Wraps the stock vendor HAL (/vendor/lib/hw/audio.primary.merak.orig.so):
 * 1. Intercepts AUDIO_FORMAT_IEC61937 (0x0D000000) 192 kHz 8-ch (MAT 2.0 HBR) output
 *    streams (Kodi AudioTrack IEC, AudioProbe, etc.) -> /dev/earc_mat_bypass directly.
 * 2. Intercepts AUDIO_FORMAT_DOLBY_TRUEHD (0x0E000000) raw TrueHD streams (Moonfin,
 *    Jellyfin, Plex, Stremio, ExoPlayer, AudioProbe TRUEHD_RAW) -> packs 24 TrueHD
 *    access units on-the-fly into bit-exact 61,440-byte IEC61937/MAT 2.0 bursts ->
 *    /dev/earc_mat_bypass with accurate CLOCK_MONOTONIC presentation timestamps.
 * 3. Passes all other streams (PCM, AC3, E-AC-3, DTS, UI, mic, patches) untouched
 *    to the stock vendor HAL.
 */

#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/system_properties.h>
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

#define AUDIO_FORMAT_IEC61937        0x0D000000U
#define AUDIO_FORMAT_DOLBY_TRUEHD    0x0E000000U
#define AUDIO_CHANNEL_OUT_7POINT1    0x0000063FU
#define AUDIO_DEVICE_OUT_HDMI_EARC   0x00040001U

#define MAT_FRAME_BYTES              61440U
#define MAT_BYTES_PER_FRAME          16U /* 8 ch * 16-bit */
#define MAT_SAMPLE_RATE              192000U
#define TRUEHD_AU_STRIDE             2560U
#define TRUEHD_AUS_PER_MAT           24U
#define TRUEHD_ACC_MAX               8192U

/* Bit-exact IEC61937-9 / Dolby MAT 2.0 constants (pre-swapped 16-bit LE, verified against truehd.iec) */
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

struct audio_config {
    uint32_t sample_rate;
    uint32_t channel_mask;
    uint32_t format;
    uint8_t  offload_info[64];
    uint32_t frame_count;
};

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

struct mat_stream_out {
    struct audio_stream_out stream;
    pthread_mutex_t lock;
    int fd;
    int is_raw_truehd;
    uint32_t sample_rate;
    uint32_t channel_mask;
    uint32_t format;
    uint32_t devices;

    /* Hardware-clocked presentation position tracking (monotonic across pause/resume) */
    uint64_t written_frames;
    uint64_t base_frames;        /* preserved across pause/resume; reset only on flush() */
    uint64_t last_presented;     /* monotonic clamp */
    uint64_t session_bursts;     /* 20ms MAT bursts written since open/resume */
    uint64_t hw_consumed_bursts; /* bursts already clocked out past the 4-frame kernel queue */
    uint64_t last_hw_sync_ns;    /* CLOCK_MONOTONIC when hw_consumed_bursts last advanced */
    int started;

    /* Raw TrueHD -> MAT 2.0 packer state */
    uint8_t  mat_buf[MAT_FRAME_BYTES];
    uint32_t au_count;
    uint8_t  thd_acc[TRUEHD_ACC_MAX];
    size_t   thd_acc_len;
    uint32_t mat_bursts_emitted;
};

static void *g_orig_handle = NULL;
static struct hw_module_t *g_orig_hmi = NULL;
static char *(*g_orig_get_parameters)(const struct audio_hw_device *, const char *) = NULL;
static int   (*g_orig_open_output_stream)(struct audio_hw_device *, int, uint32_t, int,
                                          struct audio_config *, struct audio_stream_out **,
                                          const char *) = NULL;
static void  (*g_orig_close_output_stream)(struct audio_hw_device *, struct audio_stream_out *) = NULL;

static pthread_mutex_t g_active_lock = PTHREAD_MUTEX_INITIALIZER;
static struct mat_stream_out *g_active_mat = NULL;

static uint64_t now_monotonic_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

static void mat_reset_packer_locked(struct mat_stream_out *m)
{
    memset(m->mat_buf, 0, MAT_FRAME_BYTES);
    m->au_count = 0;
    m->thd_acc_len = 0;
}

#define KERNEL_Q_BURSTS              8U /* matches Q_SLOTS=8 in tcl_earc_mat.c */
#define DEFAULT_PIPE_DELAY_MS        160 /* default SE DSP ring + eARC TX + soundbar MAT delay (ms) */

static int mat_get_pipe_delay_ms(void)
{
    char val[PROP_VALUE_MAX];
    if (__system_property_get("debug.earc_sync_ms", val) > 0)
        return atoi(val);
    return DEFAULT_PIPE_DELAY_MS;
}

static uint64_t mat_frames_per_burst(const struct mat_stream_out *m)
{
    return m->is_raw_truehd ? (uint64_t)(m->sample_rate / 50U)
                            : (uint64_t)(MAT_FRAME_BYTES / MAT_BYTES_PER_FRAME);
}

static uint64_t mat_calc_presented_locked(struct mat_stream_out *m, uint64_t now_ns)
{
    uint64_t pos = m->base_frames;
    if (m->started) {
        uint64_t fpb = mat_frames_per_burst(m);
        int64_t pipe_delay_frames = ((int64_t)mat_get_pipe_delay_ms() * (int64_t)m->sample_rate) / 1000LL;
        int64_t raw_frames = (int64_t)(m->hw_consumed_bursts * fpb);
        if (m->hw_consumed_bursts > 0 && now_ns > m->last_hw_sync_ns) {
            uint64_t dt_ns = now_ns - m->last_hw_sync_ns;
            uint64_t interp = (dt_ns * (uint64_t)m->sample_rate) / 1000000000ULL;
            if (interp > fpb * 2U)
                interp = fpb * 2U;
            raw_frames += (int64_t)interp;
        }
        int64_t adjusted = raw_frames - pipe_delay_frames;
        if (adjusted < 0)
            adjusted = 0;
        pos = m->base_frames + (uint64_t)adjusted;
    }
    if (pos > m->written_frames)
        pos = m->written_frames;
    if (pos < m->last_presented)
        pos = m->last_presented;
    m->last_presented = pos;
    return pos;
}

static void mat_close_fd_locked(struct mat_stream_out *m)
{
    if (m->started) {
        /* Preserve exact presentation position across pause/standby; align written_frames so drift never accumulates */
        m->base_frames = mat_calc_presented_locked(m, now_monotonic_ns());
        m->written_frames = m->base_frames;
    }
    if (m->fd >= 0) {
        close(m->fd);
        m->fd = -1;
        ALOGI("closed %s (mode=%s written=%llu presented=%llu mat_bursts=%u)",
              BYPASS_DEV, m->is_raw_truehd ? "TRUEHD_RAW" : "IEC61937",
              (unsigned long long)m->written_frames,
              (unsigned long long)m->base_frames,
              m->mat_bursts_emitted);
    }
    m->started = 0;
    m->session_bursts = 0;
    m->hw_consumed_bursts = 0;
    m->last_hw_sync_ns = 0;
    mat_reset_packer_locked(m);
}

static int mat_write_all_locked(struct mat_stream_out *m, const uint8_t *p, size_t left)
{
    if (m->fd < 0) {
        m->fd = open(BYPASS_DEV, O_WRONLY);
        if (m->fd < 0) {
            int err = errno;
            ALOGE("failed to open %s: %s (%d)", BYPASS_DEV, strerror(err), err);
            pthread_mutex_unlock(&m->lock);
            usleep(20000);
            pthread_mutex_lock(&m->lock);
            return -err;
        }
        ALOGI("opened %s for %s stream (rate=%u ch=0x%x base=%llu)",
              BYPASS_DEV, m->is_raw_truehd ? "TRUEHD_RAW->MAT" : "IEC61937_MAT",
              m->sample_rate, m->channel_mask, (unsigned long long)m->base_frames);
    }

    int fd = m->fd;
    size_t total = left;

    /* Release m->lock around blocking write() so get_presentation_position/pause/binder never stall */
    pthread_mutex_unlock(&m->lock);
    int io_err = 0;
    while (left > 0) {
        ssize_t w = write(fd, p, left);
        if (w < 0) {
            if (errno == EINTR) continue;
            io_err = errno;
            break;
        }
        if (w == 0) break;
        p += (size_t)w;
        left -= (size_t)w;
    }
    pthread_mutex_lock(&m->lock);

    if (io_err) {
        if (m->fd == fd) {
            ALOGE("write(%s) failed: %s", BYPASS_DEV, strerror(io_err));
            mat_close_fd_locked(m);
        }
        return -EIO;
    }

    if (m->fd == fd) {
        uint64_t bursts_now = (total >= MAT_FRAME_BYTES) ? (total / MAT_FRAME_BYTES) : 1ULL;
        m->session_bursts += bursts_now;
        m->started = 1;
        if (m->session_bursts > KERNEL_Q_BURSTS) {
            m->hw_consumed_bursts = m->session_bursts - KERNEL_Q_BURSTS;
            m->last_hw_sync_ns = now_monotonic_ns();
        }
    }
    return 0;
}

/* Copy TrueHD AU bytes into MAT payload with 16-bit LE byte swap */
static void copy_swap16(uint8_t *dst, const uint8_t *src, size_t len)
{
    size_t i = 0;
    for (; i + 1 < len; i += 2) {
        dst[i]     = src[i + 1];
        dst[i + 1] = src[i];
    }
    if (i < len) {
        dst[i]     = 0;
        dst[i + 1] = src[i];
    }
}

/* Pack one complete raw TrueHD access unit into m->mat_buf; flush 61,440-byte MAT burst every 24 AUs */
static int mat_push_truehd_au_locked(struct mat_stream_out *m, const uint8_t *au, size_t au_len)
{
    uint32_t idx = m->au_count;
    size_t dst_off;
    size_t max_len;

    if (idx == 0) {
        dst_off = 28; /* 8-byte IEC header + 20-byte MAT_START_CODE */
        max_len = TRUEHD_AU_STRIDE + 8 - dst_off; /* 2540 bytes */
    } else if (idx == 11) {
        dst_off = idx * TRUEHD_AU_STRIDE + 8;
        max_len = 30716 - dst_off; /* stop before MAT_MIDDLE_CODE at 30716 */
    } else if (idx == 23) {
        dst_off = idx * TRUEHD_AU_STRIDE + 8;
        max_len = 61416 - dst_off; /* stop before MAT_END_CODE at 61416 */
    } else {
        dst_off = idx * TRUEHD_AU_STRIDE + 8;
        max_len = TRUEHD_AU_STRIDE;
    }

    if (au_len > max_len)
        au_len = max_len;

    copy_swap16(m->mat_buf + dst_off, au, au_len);
    m->au_count++;

    if (m->au_count == TRUEHD_AUS_PER_MAT) {
        memcpy(m->mat_buf + 0,     IEC61937_MAT_HDR, sizeof(IEC61937_MAT_HDR));
        memcpy(m->mat_buf + 8,     MAT_START_CODE,   sizeof(MAT_START_CODE));
        memcpy(m->mat_buf + 30716, MAT_MIDDLE_CODE,  sizeof(MAT_MIDDLE_CODE));
        memcpy(m->mat_buf + 61416, MAT_END_CODE,     sizeof(MAT_END_CODE));

        int rc = mat_write_all_locked(m, m->mat_buf, MAT_FRAME_BYTES);
        memset(m->mat_buf, 0, MAT_FRAME_BYTES);
        m->au_count = 0;
        if (rc != 0)
            return rc;

        m->mat_bursts_emitted++;
        /* 24 TrueHD AUs = 20.000 ms = 960 frames at 48 kHz */
        m->written_frames += (uint64_t)(m->sample_rate / 50U);
    }
    return 0;
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
        while (m->thd_acc_len - pos >= 4) {
            const uint8_t *p = m->thd_acc + pos;
            /* TrueHD AU length in 16-bit words is in bits [11:0] of first 2 bytes */
            size_t au_len = (size_t)(((p[0] & 0x0fU) << 8) | p[1]) * 2U;
            if (au_len < 4 || au_len > 2560) {
                /* Resync: skip 1 byte until valid AU header */
                pos++;
                continue;
            }
            if (m->thd_acc_len - pos < au_len)
                break; /* wait for more bytes */

            int rc = mat_push_truehd_au_locked(m, p, au_len);
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
        }
    }
    return (ssize_t)bytes;
}

/* ---- audio_stream callbacks ---- */
static uint32_t mat_get_sample_rate(const struct audio_stream *s)
{
    return ((const struct mat_stream_out *)s)->sample_rate;
}
static int mat_set_sample_rate(struct audio_stream *s, uint32_t r) { (void)s; (void)r; return 0; }
static size_t mat_get_buffer_size(const struct audio_stream *s)
{
    const struct mat_stream_out *m = (const struct mat_stream_out *)s;
    return m->is_raw_truehd ? 4096U : MAT_FRAME_BYTES;
}
static uint32_t mat_get_channels(const struct audio_stream *s)
{
    return ((const struct mat_stream_out *)s)->channel_mask;
}
static uint32_t mat_get_format(const struct audio_stream *s)
{
    return ((const struct mat_stream_out *)s)->format;
}
static int mat_set_format(struct audio_stream *s, uint32_t f) { (void)s; (void)f; return 0; }
static int mat_standby(struct audio_stream *s)
{
    struct mat_stream_out *m = (struct mat_stream_out *)s;
    pthread_mutex_lock(&m->lock);
    mat_close_fd_locked(m);
    pthread_mutex_unlock(&m->lock);
    return 0;
}
static int mat_dump(const struct audio_stream *s, int fd) { (void)s; (void)fd; return 0; }
static uint32_t mat_get_device(const struct audio_stream *s)
{
    return ((const struct mat_stream_out *)s)->devices;
}
static int mat_set_device(struct audio_stream *s, uint32_t d)
{
    ((struct mat_stream_out *)s)->devices = d;
    return 0;
}
static int mat_set_parameters(struct audio_stream *s, const char *kv)
{
    (void)s; (void)kv;
    return 0;
}
static char *mat_get_parameters(const struct audio_stream *s, const char *keys)
{
    const struct mat_stream_out *m = (const struct mat_stream_out *)s;
    if (keys && strstr(keys, "sup_formats"))
        return strdup(m->is_raw_truehd ? "sup_formats=AUDIO_FORMAT_DOLBY_TRUEHD"
                                       : "sup_formats=AUDIO_FORMAT_IEC61937");
    if (keys && strstr(keys, "sup_sampling_rates"))
        return strdup(m->is_raw_truehd ? "sup_sampling_rates=48000|96000|192000"
                                       : "sup_sampling_rates=192000");
    if (keys && strstr(keys, "sup_channels"))
        return strdup("sup_channels=AUDIO_CHANNEL_OUT_7POINT1");
    return strdup("");
}
static int mat_add_effect(const struct audio_stream *s, void *e) { (void)s; (void)e; return 0; }
static int mat_rem_effect(const struct audio_stream *s, void *e) { (void)s; (void)e; return 0; }

/* ---- audio_stream_out callbacks ---- */
static uint32_t mat_get_latency(const struct audio_stream_out *s)
{
    (void)s;
    int total = (int)(KERNEL_Q_BURSTS * 20U) + mat_get_pipe_delay_ms();
    return (total > 20) ? (uint32_t)total : 20U;
}
static int mat_set_volume(struct audio_stream_out *s, float l, float r)
{
    (void)s; (void)l; (void)r;
    return 0;
}
static ssize_t mat_write(struct audio_stream_out *s, const void *buffer, size_t bytes)
{
    struct mat_stream_out *m = (struct mat_stream_out *)s;
    pthread_mutex_lock(&m->lock);

    ssize_t ret;
    if (m->is_raw_truehd) {
        ret = mat_write_truehd_raw_locked(m, (const uint8_t *)buffer, bytes);
    } else {
        int rc = mat_write_all_locked(m, (const uint8_t *)buffer, bytes);
        if (rc != 0) {
            ret = rc;
        } else {
            m->written_frames += bytes / MAT_BYTES_PER_FRAME;
            m->mat_bursts_emitted += (uint32_t)(bytes / MAT_FRAME_BYTES);
            ret = (ssize_t)bytes;
        }
    }

    pthread_mutex_unlock(&m->lock);
    return ret;
}
static int mat_get_render_position(const struct audio_stream_out *s, uint32_t *dsp_frames)
{
    struct mat_stream_out *m = (struct mat_stream_out *)s;
    pthread_mutex_lock(&m->lock);
    if (dsp_frames)
        *dsp_frames = (uint32_t)mat_calc_presented_locked(m, now_monotonic_ns());
    pthread_mutex_unlock(&m->lock);
    return 0;
}
static int mat_get_next_write_timestamp(const struct audio_stream_out *s, int64_t *ts)
{
    (void)s; (void)ts;
    return -ENOSYS;
}
static int mat_pause(struct audio_stream_out *s)
{
    struct mat_stream_out *m = (struct mat_stream_out *)s;
    pthread_mutex_lock(&m->lock);
    mat_close_fd_locked(m);
    pthread_mutex_unlock(&m->lock);
    return 0;
}
static int mat_resume(struct audio_stream_out *s)
{
    (void)s;
    return 0;
}
static int mat_drain(struct audio_stream_out *s, int type)
{
    (void)s; (void)type;
    return 0;
}
static int mat_flush(struct audio_stream_out *s)
{
    struct mat_stream_out *m = (struct mat_stream_out *)s;
    pthread_mutex_lock(&m->lock);
    mat_close_fd_locked(m);
    m->written_frames = 0;
    m->base_frames = 0;
    m->last_presented = 0;
    pthread_mutex_unlock(&m->lock);
    return 0;
}
static int mat_get_presentation_position(const struct audio_stream_out *s,
                                         uint64_t *frames, struct timespec *timestamp)
{
    struct mat_stream_out *m = (struct mat_stream_out *)s;
    pthread_mutex_lock(&m->lock);
    clock_gettime(CLOCK_MONOTONIC, timestamp);
    uint64_t now_ns = (uint64_t)timestamp->tv_sec * 1000000000ULL + (uint64_t)timestamp->tv_nsec;
    *frames = mat_calc_presented_locked(m, now_ns);
    pthread_mutex_unlock(&m->lock);
    return 0;
}

/* ---- audio_hw_device hooks ---- */
static char *shim_get_parameters(const struct audio_hw_device *dev, const char *keys)
{
    char *res = g_orig_get_parameters ? g_orig_get_parameters(dev, keys) : strdup("");
    if (res && keys && strstr(keys, "sup_formats")) {
        if ((strstr(res, "AUDIO_FORMAT_DOLBY_TRUEHD") || strstr(res, "AUDIO_FORMAT_E_AC3_JOC")) &&
            !strstr(res, "AUDIO_FORMAT_IEC61937")) {
            size_t len = strlen(res) + 32;
            char *ext = (char *)malloc(len);
            if (ext) {
                snprintf(ext, len, "%s|AUDIO_FORMAT_IEC61937", res);
                ALOGI("sup_formats augmented with AUDIO_FORMAT_IEC61937");
                free(res);
                return ext;
            }
        }
    }
    return res;
}

static int shim_open_output_stream(struct audio_hw_device *dev, int handle,
                                   uint32_t devices, int flags,
                                   struct audio_config *config,
                                   struct audio_stream_out **stream_out,
                                   const char *address)
{
    int is_iec_mat = (config && config->format == AUDIO_FORMAT_IEC61937 &&
                      config->sample_rate == MAT_SAMPLE_RATE);
    int is_truehd  = (config && config->format == AUDIO_FORMAT_DOLBY_TRUEHD);

    if (is_iec_mat || is_truehd) {
        ALOGI("INTERCEPT open_output_stream: %s rate=%u ch=0x%x dev=0x%x flags=0x%x -> %s",
              is_truehd ? "DOLBY_TRUEHD_RAW" : "IEC61937_MAT",
              config->sample_rate, config->channel_mask, devices, flags, BYPASS_DEV);

        struct mat_stream_out *m = (struct mat_stream_out *)calloc(1, sizeof(*m));
        if (!m)
            return -ENOMEM;

        pthread_mutex_init(&m->lock, NULL);
        m->fd = -1;
        m->is_raw_truehd = is_truehd;
        m->sample_rate   = config->sample_rate ? config->sample_rate : (is_truehd ? 48000U : MAT_SAMPLE_RATE);
        m->channel_mask  = config->channel_mask ? config->channel_mask : AUDIO_CHANNEL_OUT_7POINT1;
        m->format        = config->format;
        m->devices       = devices ? devices : AUDIO_DEVICE_OUT_HDMI_EARC;

        m->stream.common.get_sample_rate = mat_get_sample_rate;
        m->stream.common.set_sample_rate = mat_set_sample_rate;
        m->stream.common.get_buffer_size = mat_get_buffer_size;
        m->stream.common.get_channels    = mat_get_channels;
        m->stream.common.get_format      = mat_get_format;
        m->stream.common.set_format      = mat_set_format;
        m->stream.common.standby         = mat_standby;
        m->stream.common.dump            = mat_dump;
        m->stream.common.get_device      = mat_get_device;
        m->stream.common.set_device      = mat_set_device;
        m->stream.common.set_parameters  = mat_set_parameters;
        m->stream.common.get_parameters  = mat_get_parameters;
        m->stream.common.add_audio_effect    = mat_add_effect;
        m->stream.common.remove_audio_effect = mat_rem_effect;

        m->stream.get_latency              = mat_get_latency;
        m->stream.set_volume               = mat_set_volume;
        m->stream.write                    = mat_write;
        m->stream.get_render_position      = mat_get_render_position;
        m->stream.get_next_write_timestamp = mat_get_next_write_timestamp;
        m->stream.pause                    = mat_pause;
        m->stream.resume                   = mat_resume;
        m->stream.drain                    = mat_drain;
        m->stream.flush                    = mat_flush;
        m->stream.get_presentation_position = mat_get_presentation_position;

        config->sample_rate  = m->sample_rate;
        config->channel_mask = m->channel_mask;
        config->format       = m->format;
        config->frame_count  = is_truehd ? 4096U : (MAT_FRAME_BYTES / MAT_BYTES_PER_FRAME);

        pthread_mutex_lock(&g_active_lock);
        g_active_mat = m;
        pthread_mutex_unlock(&g_active_lock);

        *stream_out = &m->stream;
        return 0;
    }

    return g_orig_open_output_stream(dev, handle, devices, flags, config, stream_out, address);
}

static void shim_close_output_stream(struct audio_hw_device *dev, struct audio_stream_out *out)
{
    pthread_mutex_lock(&g_active_lock);
    if (g_active_mat && out == &g_active_mat->stream) {
        struct mat_stream_out *m = g_active_mat;
        g_active_mat = NULL;
        pthread_mutex_unlock(&g_active_lock);

        pthread_mutex_lock(&m->lock);
        mat_close_fd_locked(m);
        pthread_mutex_unlock(&m->lock);
        pthread_mutex_destroy(&m->lock);
        free(m);
        ALOGI("closed intercepted MAT/TrueHD stream");
        return;
    }
    pthread_mutex_unlock(&g_active_lock);

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
    g_orig_get_parameters      = adev->get_parameters;
    g_orig_open_output_stream  = adev->open_output_stream;
    g_orig_close_output_stream = adev->close_output_stream;

    adev->get_parameters      = shim_get_parameters;
    adev->open_output_stream  = shim_open_output_stream;
    adev->close_output_stream = shim_close_output_stream;

    ALOGI("READY: wrapped %s (IEC61937 + DOLBY_TRUEHD_RAW -> MAT 2.0 -> %s)",
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
    .name               = "TCL C9K eARC MAT Audio HAL Shim",
    .author             = "TCL eARC Research",
    .methods            = &g_shim_methods,
};
