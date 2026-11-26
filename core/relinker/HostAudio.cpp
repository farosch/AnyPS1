#include "HostAudio.h"

#include <SDL3/SDL.h>
#include <stdexcept>
#include <string>
#include <cstdint>

namespace anyps1 {

HostAudio::HostAudio()
{
    if (!SDL_WasInit(SDL_INIT_AUDIO) && !SDL_InitSubSystem(SDL_INIT_AUDIO)) {
        throw std::runtime_error(std::string("SDL audio initialization failed: ") + SDL_GetError());
    }
    SDL_AudioSpec spec{};
    spec.freq = 44100;
    spec.format = SDL_AUDIO_S16;
    spec.channels = 2;
    stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);
    if (!stream) {
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
        throw std::runtime_error(std::string("SDL audio stream creation failed: ") + SDL_GetError());
    }
    if (!SDL_ResumeAudioStreamDevice(stream)) {
        SDL_DestroyAudioStream(stream);
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
        throw std::runtime_error(std::string("SDL audio stream resume failed: ") + SDL_GetError());
    }
}

HostAudio::~HostAudio()
{
    if (stream) {
        SDL_DestroyAudioStream(stream);
    }
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
}

void HostAudio::put_samples(const std::int16_t* samples, std::size_t sample_count)
{
    if (stream && sample_count != 0) {
        SDL_PutAudioStreamData(
            stream,
            samples,
            static_cast<int>(sample_count * sizeof(std::int16_t))
        );
    }
}

}
