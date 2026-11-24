#include <climits>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <sound/asound.h>
#include <string_view>
#include <sys/ioctl.h>
#include <unistd.h>

#include "iec_mat_carrier.h"

namespace {
constexpr std::string_view kEndpoint = "EARC NONPCM Playback";
constexpr std::string_view kDummyCodecSuffix = " snd-soc-dummy-dai-";

bool matches_endpoint(std::string_view name) {
    if (name == kEndpoint) return true;
    if (name.substr(0, kEndpoint.size()) != kEndpoint) return false;
    name.remove_prefix(kEndpoint.size());
    if (name.substr(0, kDummyCodecSuffix.size()) != kDummyCodecSuffix) return false;
    name.remove_prefix(kDummyCodecSuffix.size());
    return !name.empty() && name.find_first_not_of("0123456789") == std::string_view::npos;
}

struct PcmAddress {
    unsigned card;
    unsigned device;
};

int find_endpoint(PcmAddress& address) {
    FILE* pcm = std::fopen("/proc/asound/pcm", "r");
    if (!pcm) {
        std::perror("/proc/asound/pcm");
        return 1;
    }
    unsigned matches = 0;
    char line[1024];
    while (std::fgets(line, sizeof(line), pcm)) {
        PcmAddress candidate{};
        int name_offset = 0;
        if (std::sscanf(line, "%u-%u: %n", &candidate.card, &candidate.device, &name_offset) != 2 ||
            name_offset == 0) continue;
        std::string_view name(line + name_offset);
        const auto colon = name.find(':');
        if (colon == std::string_view::npos ||
            name.substr(colon).find(": playback ") == std::string_view::npos) continue;
        name = name.substr(0, colon);
        while (!name.empty() && name.back() == ' ') name.remove_suffix(1);
        if (matches_endpoint(name)) {
            address = candidate;
            ++matches;
        }
    }
    const bool read_error = std::ferror(pcm);
    std::fclose(pcm);
    if (read_error || matches > 1) {
        std::fputs("Could not identify a unique eARC non-PCM endpoint.\n", stderr);
        return 1;
    }
    if (matches == 0) {
        std::fputs("EARC NONPCM Playback is absent from /proc/asound/pcm.\n", stderr);
        return 2;
    }
    return 0;
}

void set_mask(snd_pcm_hw_params& hw, unsigned param, unsigned value) {
    auto& mask = hw.masks[param - SNDRV_PCM_HW_PARAM_FIRST_MASK];
    mask = {};
    mask.bits[value / 32] = 1U << (value % 32);
}

snd_interval& interval(snd_pcm_hw_params& hw, unsigned param) {
    return hw.intervals[param - SNDRV_PCM_HW_PARAM_FIRST_INTERVAL];
}

void set_exact(snd_pcm_hw_params& hw, unsigned param, unsigned value) {
    auto& range = interval(hw, param);
    range.min = range.max = value;
    range.integer = 1;
}

int refine_carrier(int fd, const PcmAddress& address) {
    snd_pcm_info info{};
    if (ioctl(fd, SNDRV_PCM_IOCTL_INFO, &info) < 0) {
        std::perror("SNDRV_PCM_IOCTL_INFO");
        return 1;
    }
    if (info.card < 0 || static_cast<unsigned>(info.card) != address.card ||
        info.device != address.device || info.stream != SNDRV_PCM_STREAM_PLAYBACK ||
        !matches_endpoint({reinterpret_cast<const char*>(info.id),
                           strnlen(reinterpret_cast<const char*>(info.id), sizeof(info.id))})) {
        std::fputs("The PCM node identity changed after discovery.\n", stderr);
        return 1;
    }

    snd_pcm_hw_params hw{};
    for (auto& mask : hw.masks)
        for (auto& bits : mask.bits) bits = UINT_MAX;
    for (auto& range : hw.intervals) range.max = UINT_MAX;
    hw.rmask = UINT_MAX;
    hw.flags = SNDRV_PCM_HW_PARAMS_NORESAMPLE;
    set_mask(hw, SNDRV_PCM_HW_PARAM_ACCESS, SNDRV_PCM_ACCESS_RW_INTERLEAVED);
    set_mask(hw, SNDRV_PCM_HW_PARAM_FORMAT, SNDRV_PCM_FORMAT_S16_LE);
    set_mask(hw, SNDRV_PCM_HW_PARAM_SUBFORMAT, SNDRV_PCM_SUBFORMAT_STD);
    set_exact(hw, SNDRV_PCM_HW_PARAM_RATE, tcl_iec::kCarrierRate);
    set_exact(hw, SNDRV_PCM_HW_PARAM_CHANNELS, tcl_iec::kCarrierChannels);

    if (ioctl(fd, SNDRV_PCM_IOCTL_HW_REFINE, &hw) < 0) {
        std::perror("SNDRV_PCM_IOCTL_HW_REFINE");
        return 1;
    }
    const auto& period = interval(hw, SNDRV_PCM_HW_PARAM_PERIOD_SIZE);
    const auto& buffer = interval(hw, SNDRV_PCM_HW_PARAM_BUFFER_SIZE);
    std::printf("S16_LE / %u Hz / %u channels accepted by HW_REFINE\n",
                tcl_iec::kCarrierRate, tcl_iec::kCarrierChannels);
    std::printf("Period frames %u..%u, buffer frames %u..%u\n",
                period.min, period.max, buffer.min, buffer.max);
    return 0;
}
}  // namespace

int main() {
    PcmAddress address{};
    if (const int result = find_endpoint(address)) return result;
    char path[64];
    std::snprintf(path, sizeof(path), "/dev/snd/pcmC%uD%up", address.card, address.device);
    const int fd = open(path, O_RDWR | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) {
        std::perror(path);
        return 1;
    }
    const int result = refine_carrier(fd, address);
    close(fd);
    return result;
}
