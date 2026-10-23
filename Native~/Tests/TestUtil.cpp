#include "TestUtil.h"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <thread>

namespace test {

namespace {
void Put16(std::vector<uint8_t>& v, uint16_t x) { v.push_back(x & 0xFF); v.push_back(x >> 8); }
void Put32(std::vector<uint8_t>& v, uint32_t x) { for (int i = 0; i < 4; ++i) v.push_back((x >> (8 * i)) & 0xFF); }

std::vector<uint8_t> WavHeader(int rate, int channels, int bits, int tag, uint32_t dataBytes) {
    std::vector<uint8_t> v;
    v.insert(v.end(), {'R', 'I', 'F', 'F'});
    Put32(v, 36 + dataBytes);
    v.insert(v.end(), {'W', 'A', 'V', 'E', 'f', 'm', 't', ' '});
    Put32(v, 16);
    Put16(v, static_cast<uint16_t>(tag));
    Put16(v, static_cast<uint16_t>(channels));
    Put32(v, static_cast<uint32_t>(rate));
    Put32(v, static_cast<uint32_t>(rate * channels * bits / 8));
    Put16(v, static_cast<uint16_t>(channels * bits / 8));
    Put16(v, static_cast<uint16_t>(bits));
    v.insert(v.end(), {'d', 'a', 't', 'a'});
    Put32(v, dataBytes);
    return v;
}
} // namespace

std::vector<uint8_t> ReadFixture(const std::string& name) {
    const std::string path = std::string(UNA_TEST_FIXTURES) + "/" + name;
    std::vector<uint8_t> data;
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return data;
    std::fseek(f, 0, SEEK_END);
    const long len = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    data.resize(static_cast<size_t>(len));
    if (std::fread(data.data(), 1, data.size(), f) != data.size()) data.clear();
    std::fclose(f);
    return data;
}

std::vector<uint8_t> MakeWavFloat(int rate, int channels, int frames, const std::function<float(int, int)>& gen) {
    const uint32_t bytes = static_cast<uint32_t>(frames * channels * 4);
    std::vector<uint8_t> v = WavHeader(rate, channels, 32, 3, bytes);
    for (int i = 0; i < frames; ++i)
        for (int c = 0; c < channels; ++c) {
            float s = gen(i, c);
            uint32_t u;
            std::memcpy(&u, &s, 4);
            Put32(v, u);
        }
    return v;
}

std::vector<uint8_t> MakeWavPcm(int rate, int channels, int frames, int bits, const std::function<float(int, int)>& gen) {
    const int bps = bits / 8;
    const uint32_t bytes = static_cast<uint32_t>(frames * channels * bps);
    std::vector<uint8_t> v = WavHeader(rate, channels, bits, 1, bytes);
    for (int i = 0; i < frames; ++i)
        for (int c = 0; c < channels; ++c) {
            const double s = gen(i, c);
            if (bits == 8) {
                v.push_back(static_cast<uint8_t>(static_cast<int>(s * 128.0) + 128));
            } else {
                const double scale = bits == 16 ? 32768.0 : (bits == 24 ? 8388608.0 : 2147483648.0);
                const int64_t q = static_cast<int64_t>(s * scale);
                for (int b = 0; b < bps; ++b) v.push_back(static_cast<uint8_t>((q >> (8 * b)) & 0xFF));
            }
        }
    return v;
}

ManualEngine::ManualEngine(int rate, int buffer, int maxVoices) {
    una_config cfg{};
    cfg.struct_size = sizeof(cfg);
    cfg.device_type = UNA_DEVICE_MANUAL;
    cfg.sample_rate = rate;
    cfg.buffer_frames = buffer;
    cfg.channels = 2;
    cfg.max_voices = maxVoices;
    cfg.decode_threads = 2;
    // Mix volumes outlive a session; start every test from unity gain.
    una_set_master_volume(1.0f, 0.0f);
    for (int b = 0; b < UNA_MAX_BUSES; ++b) una_bus_set_volume(b, 1.0f, 0.0f);
    rc_ = una_init(&cfg);
}

ManualEngine::~ManualEngine() {
    if (rc_ == UNA_OK) una_shutdown();
}

void ManualEngine::Render(int frames, std::vector<float>* out, int block) {
    std::vector<float> buf(static_cast<size_t>(block) * 2);
    while (frames > 0) {
        const int n = frames < block ? frames : block;
        una_render(buf.data(), n, 2);
        if (out) out->insert(out->end(), buf.begin(), buf.begin() + n * 2);
        frames -= n;
    }
}

int64_t ManualEngine::MixFrame() const {
    una_clock c{};
    una_get_clock(&c);
    return c.mix_frame;
}

una_clip LoadClip(const std::vector<uint8_t>& data, int mode, int format, int flags) {
    una_clip_params p{};
    p.struct_size = sizeof(p);
    p.mode = mode;
    p.sample_format = format;
    p.flags = flags;
    una_clip clip = 0;
    if (una_clip_load_memory(data.data(), static_cast<int64_t>(data.size()), &p, &clip) != UNA_OK) return 0;
    return clip;
}

int WaitClip(una_clip clip, int state, int timeoutMs) {
    const auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    una_clip_info info{};
    for (;;) {
        una_clip_get_info(clip, &info);
        if (info.state >= state || info.state == UNA_CLIP_FAILED || info.state == UNA_CLIP_INVALID) return info.state;
        if (std::chrono::steady_clock::now() > until) return info.state;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

una_play_params DefaultPlay() {
    una_play_params p{};
    p.struct_size = sizeof(p);
    p.volume = 1.0f;
    p.pitch = 1.0f;
    return p;
}

std::vector<una_event> DrainEvents() {
    std::vector<una_event> all;
    una_event buf[64];
    for (;;) {
        const int n = una_poll_events(buf, 64);
        if (n <= 0) break;
        all.insert(all.end(), buf, buf + n);
    }
    return all;
}

} // namespace test
