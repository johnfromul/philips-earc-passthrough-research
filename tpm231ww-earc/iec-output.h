#ifndef PHILIPS_IEC_OUTPUT_H
#define PHILIPS_IEC_OUTPUT_H

#include "iec-mat.h"

#define IEC_RATE 192000U
#define IEC_CHANNELS 0x63fU
#define IEC_FRAMES_PER_BURST 3840U
#define IEC_DEVICE_FLAGS 0x11

struct iec_output {
    struct audio_stream_out client;
    struct audio_stream_out *stock;
    struct iec_mat_decoder decoder;
    uint8_t raw[MAT_FRAME_BYTES + TRUEHD_MAX_AU_BYTES];
    size_t burst_bytes;
    uint64_t base_frames, reported_frames;
};

static uint32_t iec_rate(const struct audio_stream *s) { (void)s; return IEC_RATE; }
static uint32_t iec_channels(const struct audio_stream *s) { (void)s; return IEC_CHANNELS; }
static uint32_t iec_format(const struct audio_stream *s) { (void)s; return AUDIO_FORMAT_IEC61937; }
static uint32_t iec_device(const struct audio_stream *s) { (void)s; return AUDIO_DEVICE_OUT_HDMI_EARC; }
static size_t iec_buffer_size(const struct audio_stream *s) { (void)s; return MAT_FRAME_BYTES; }
static uint32_t iec_latency(const struct audio_stream_out *s) { (void)s; return 180; }
static int iec_set_rate(struct audio_stream *s, uint32_t rate)
{ (void)s; return rate == IEC_RATE ? 0 : -EINVAL; }
static int iec_set_format(struct audio_stream *s, uint32_t format)
{ (void)s; return format == AUDIO_FORMAT_IEC61937 ? 0 : -EINVAL; }
static int iec_set_device(struct audio_stream *s, uint32_t device)
{ (void)s; return device == AUDIO_DEVICE_OUT_HDMI_EARC ? 0 : -EINVAL; }
static int iec_effect(const struct audio_stream *s, void *effect)
{ (void)s; (void)effect; return -ENOSYS; }
static int iec_next_timestamp(const struct audio_stream_out *s, int64_t *time)
{ (void)s; (void)time; return -ENOSYS; }

static int iec_dump(const struct audio_stream *stream, int fd)
{
    struct mat_stream_out *m = lock_shim_stream((const struct audio_stream_out *)stream);
    if (!m)
        return -ENODEV;
    dprintf(fd, "Philips IEC MAT output: bursts=%u\n", m->mat_bursts_emitted);
    unlock_shim_stream(m);
    return 0;
}

static char *iec_get_parameters(const struct audio_stream *stream, const char *keys)
{
    (void)stream;
    if (!strcmp(keys, "sup_formats"))
        return strdup("sup_formats=AUDIO_FORMAT_IEC61937");
    if (!strcmp(keys, "sup_sampling_rates"))
        return strdup("sup_sampling_rates=192000");
    if (!strcmp(keys, "sup_channels"))
        return strdup("sup_channels=AUDIO_CHANNEL_OUT_7POINT1");
    return strdup("");
}

static int iec_set_parameters(struct audio_stream *stream, const char *keys)
{
    struct mat_stream_out *m = lock_shim_stream((struct audio_stream_out *)stream);
    if (!m)
        return -ENODEV;
    struct audio_stream *stock = &m->iec->stock->common;
    int result = stock->set_parameters ? stock->set_parameters(stock, keys) : 0;
    unlock_shim_stream(m);
    return result;
}

static int iec_position_locked(struct mat_stream_out *m, uint64_t *frames, struct timespec *time)
{
    uint64_t status[2];
    if (m->fd >= 0 && read(m->fd, status, sizeof(status)) == sizeof(status) && status[1]) {
        m->iec->reported_frames = m->iec->base_frames + status[0] * IEC_FRAMES_PER_BURST;
        if (time) {
            time->tv_sec = (time_t)(status[1] / 1000000000ULL);
            time->tv_nsec = (long)(status[1] % 1000000000ULL);
        }
    } else if (time) {
        return -ENODATA;
    }
    *frames = m->iec->reported_frames;
    return 0;
}

static int iec_position(const struct audio_stream_out *out, uint64_t *frames, struct timespec *time)
{
    struct mat_stream_out *m = lock_shim_stream(out);
    if (!m)
        return -ENODEV;
    int result = iec_position_locked(m, frames, time);
    unlock_shim_stream(m);
    return result;
}

static int iec_render_position(const struct audio_stream_out *out, uint32_t *frames)
{
    uint64_t total;
    int result = iec_position(out, &total, NULL);
    if (!result)
        *frames = (uint32_t)total;
    return result;
}

static void iec_stop_locked(struct mat_stream_out *m)
{
    uint64_t frames;
    iec_position_locked(m, &frames, NULL);
    m->iec->base_frames = frames;
    mat_close_fd_locked(m);
    m->iec->decoder.remaining = 0;
    m->iec->decoder.synced = 0;
    m->iec->burst_bytes = 0;
}

static int iec_standby(struct audio_stream *stream)
{
    struct mat_stream_out *m = lock_shim_stream((struct audio_stream_out *)stream);
    if (!m)
        return -ENODEV;
    iec_stop_locked(m);
    struct audio_stream *stock = &m->iec->stock->common;
    int result = stock->standby ? stock->standby(stock) : 0;
    unlock_shim_stream(m);
    return result;
}

static int iec_volume(struct audio_stream_out *out, float left, float right)
{
    struct mat_stream_out *m = lock_shim_stream(out);
    if (!m)
        return -ENODEV;
    struct audio_stream_out *stock = m->iec->stock;
    int result = stock->set_volume ? stock->set_volume(stock, left, right) : 0;
    unlock_shim_stream(m);
    return result;
}

static int iec_burst_locked(struct mat_stream_out *m)
{
    struct iec_output *iec = m->iec;
    if (!iec_mat_valid(m->mat_buf)) {
        ALOGW("IEC output received a burst other than 192 kHz TrueHD MAT");
        return -EINVAL;
    }

    /* The original HAL needs TrueHD input to keep NPCM2 selected. */
    size_t size = iec_mat_decode(&iec->decoder, m->mat_buf, iec->raw);
    for (size_t sent = 0; sent < size;) {
        ssize_t count = iec->stock->write(iec->stock, iec->raw + sent, size - sent);
        if (count <= 0 || (size_t)count > size - sent)
            return count < 0 ? (int)count : -EIO;
        sent += (size_t)count;
    }
    if (m->fd < 0) {
        m->fd = open(BYPASS_DEV, O_RDWR | O_CLOEXEC);
        if (m->fd < 0) {
            /* The stock stream can need a few bursts before the carrier is ready. */
            if (errno == ENODEV)
                return 0;
            return -errno;
        }
        ALOGI("IEC MAT writer attached to the Philips carrier");
    }
    int result = mat_write_all_locked(m, m->mat_buf, MAT_FRAME_BYTES);
    if (!result)
        m->mat_bursts_emitted++;
    return result;
}

static ssize_t iec_write(struct audio_stream_out *out, const void *buffer, size_t size)
{
    struct mat_stream_out *m = lock_shim_stream(out);
    if (!m)
        return -ENODEV;
    const uint8_t *input = buffer;
    int result = 0;
    for (size_t left = size; left;) {
        size_t take = MAT_FRAME_BYTES - m->iec->burst_bytes;
        if (take > left)
            take = left;
        memcpy(m->mat_buf + m->iec->burst_bytes, input, take);
        m->iec->burst_bytes += take;
        input += take;
        left -= take;
        if (m->iec->burst_bytes == MAT_FRAME_BYTES) {
            m->iec->burst_bytes = 0;
            result = iec_burst_locked(m);
            if (result) {
                ALOGW("IEC MAT write failed: %d", result);
                iec_stop_locked(m);
                break;
            }
        }
    }
    unlock_shim_stream(m);
    return result ? result : (ssize_t)size;
}

static int iec_open(struct audio_hw_device *dev, int handle, int flags,
                    struct audio_config *config, struct audio_stream_out **out,
                    const char *address)
{
    if (config->sample_rate != IEC_RATE || config->channel_mask != IEC_CHANNELS ||
        (flags & 0x40) != 0)
        return -EINVAL;

    struct mat_stream_out *m = calloc(1, sizeof(*m));
    struct iec_output *iec = calloc(1, sizeof(*iec));
    if (!m || !iec) {
        free(m);
        free(iec);
        return -ENOMEM;
    }
    if (pthread_mutex_init(&m->lock, NULL)) {
        free(m);
        free(iec);
        return -ENOMEM;
    }
    struct audio_config raw = {
        .sample_rate = 48000, .channel_mask = IEC_CHANNELS, .format = AUDIO_FORMAT_DOLBY_TRUEHD,
        .offload_info = {
            .version = 2, .size = sizeof(struct audio_offload_info),
            .sample_rate = 48000, .channel_mask = IEC_CHANNELS, .format = AUDIO_FORMAT_DOLBY_TRUEHD,
            .stream_type = 3, .duration_us = -1, .bit_width = 16, .usage = 1
        }
    };
    int result = g_orig_open_output_stream(dev, handle, AUDIO_DEVICE_OUT_HDMI_EARC,
                                          IEC_DEVICE_FLAGS, &raw, &iec->stock, address);
    if (result || !iec->stock || !iec->stock->write) {
        if (!result && iec->stock)
            g_orig_close_output_stream(dev, iec->stock);
        ALOGW("IEC shadow TrueHD stream failed: %d", result);
        pthread_mutex_destroy(&m->lock);
        free(m);
        free(iec);
        return result ? result : -ENODEV;
    }

    iec->client = (struct audio_stream_out){
        .common = {
            .get_sample_rate = iec_rate, .set_sample_rate = iec_set_rate,
            .get_buffer_size = iec_buffer_size, .get_channels = iec_channels,
            .get_format = iec_format, .set_format = iec_set_format,
            .get_device = iec_device, .set_device = iec_set_device, .dump = iec_dump,
            .standby = iec_standby, .get_parameters = iec_get_parameters,
            .set_parameters = iec_set_parameters, .add_audio_effect = iec_effect,
            .remove_audio_effect = iec_effect
        },
        .get_latency = iec_latency, .set_volume = iec_volume, .write = iec_write,
        .get_render_position = iec_render_position, .get_presentation_position = iec_position,
        .get_next_write_timestamp = iec_next_timestamp
    };
    m->iec = iec;
    m->stream = &iec->client;
    m->fd = -1;
    if (!register_shim_stream(m)) {
        g_orig_close_output_stream(dev, iec->stock);
        pthread_mutex_destroy(&m->lock);
        free(m);
        free(iec);
        return -EBUSY;
    }
    config->frame_count = IEC_FRAMES_PER_BURST;
    *out = m->stream;
    ALOGI("INTERCEPT IEC MAT rate=%u ch=0x%x flags=0x%x", IEC_RATE, IEC_CHANNELS, flags);
    return 0;
}

static void iec_close(struct audio_hw_device *dev, struct mat_stream_out *m)
{
    pthread_mutex_lock(&m->lock);
    iec_stop_locked(m);
    g_orig_close_output_stream(dev, m->iec->stock);
    pthread_mutex_unlock(&m->lock);
    pthread_mutex_destroy(&m->lock);
    free(m->iec);
    free(m);
}

#endif
