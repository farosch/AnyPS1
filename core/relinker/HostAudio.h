#pragma once

#include <cstdint>
#include <cstddef>

struct SDL_AudioStream;

namespace anyps1 {

class HostAudio {
public:
    HostAudio();
    ~HostAudio();
    void put_samples(const std::int16_t* samples, std::size_t sample_count);

    HostAudio(const HostAudio&) = delete;
    HostAudio& operator=(const HostAudio&) = delete;

private:
    SDL_AudioStream* stream = nullptr;
};

}
