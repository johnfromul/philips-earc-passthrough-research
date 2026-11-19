#pragma once
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace tcl_iec {
// Deliberately only the FFmpeg-generated TrueHD/MAT carrier profile.
// Not a general IEC61937 parser or a decoder; does not modify payloads.
constexpr std::size_t kBurstBytes = 61440;
constexpr std::size_t kPayloadBytes = 61424;
constexpr unsigned kCarrierRate = 192000;
constexpr unsigned kCarrierChannels = 8;
constexpr unsigned kCarrierBits = 16;
inline uint16_t le16(const uint8_t* p) {
    return uint16_t(p[0]) | (uint16_t(p[1]) << 8);
}
inline bool valid_mat_burst(const uint8_t* p, std::size_t n) {
    if (!p || n != kBurstBytes) return false;
    if (le16(p) != 0xf872 || le16(p+2) != 0x4e1f ||
        le16(p+4) != 0x16 || le16(p+6) != kPayloadBytes) return false;
    static constexpr uint8_t sync[] = {0x9e,0x07,0x03,0x00};
    if (std::memcmp(p+8, sync, sizeof(sync)) != 0) return false;
    for (std::size_t i=8+kPayloadBytes; i<n; ++i) if (p[i]) return false;
    return true;
}
// Offline staging reference: arbitrary fragments in, complete unchanged bursts out.
// Callback returns true only if the *entire* burst is accepted synchronously.
// Failure is sticky until reset; no resync guess, PCM fallback or retry of partial write.
class MatFramer {
    std::array<uint8_t,kBurstBytes> data_{};
    std::size_t used_=0;
    bool failed_=false;
public:
    void reset() { used_=0; failed_=false; }
    bool complete() const { return !failed_ && used_==0; }
    template<class Emit> bool feed(const uint8_t* p, std::size_t n, Emit emit) {
        if (failed_ || (!p && n)) { failed_=true; return false; }
        while (n) {
            const auto count=std::min(n,kBurstBytes-used_);
            std::memcpy(data_.data()+used_,p,count);
            used_+=count; p+=count; n-=count;
            if (used_==kBurstBytes) {
                if (!valid_mat_burst(data_.data(),used_) || !emit(data_.data(),used_)) {
                    failed_=true; return false;
                }
                used_=0;
            }
        }
        return true;
    }
};
}
