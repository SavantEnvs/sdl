#include <SDL3/SDL.h>

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    SDL_IOStream *io = SDL_IOFromConstMem(data, size);
    SDL_AudioSpec spec;
    Uint8 *buf = NULL;
    Uint32 len = 0;

    if (!io) {
        return -1;
    }
    if (!SDL_LoadWAV_IO(io, true, &spec, &buf, &len)) {
        return -1;
    }

    if (spec.freq >= 4000 && spec.freq <= 192000 && len <= (1u << 20)) {
        const SDL_AudioSpec dst = { SDL_AUDIO_F32, 2, 44100 };
        Uint8 *out = NULL;
        int outlen = 0;
        if (SDL_ConvertAudioSamples(&spec, buf, (int)len, &dst, &out, &outlen)) {
            SDL_free(out);
        }
    }
    SDL_free(buf);
    return 0;
}
