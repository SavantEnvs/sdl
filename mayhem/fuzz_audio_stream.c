#include <SDL3/SDL.h>
#include <string.h>

static const SDL_AudioFormat kFormats[] = {
    SDL_AUDIO_U8, SDL_AUDIO_S8, SDL_AUDIO_S16LE, SDL_AUDIO_S16BE,
    SDL_AUDIO_S32LE, SDL_AUDIO_S32BE, SDL_AUDIO_F32LE, SDL_AUDIO_F32BE,
};
static const int kRates[] = { 4000, 8000, 11025, 22050, 32000, 44100, 48000, 96000, 192000 };

#define NFORMATS (sizeof(kFormats) / sizeof(kFormats[0]))
#define NRATES (sizeof(kRates) / sizeof(kRates[0]))

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    SDL_AudioSpec src, dst;
    SDL_AudioStream *stream;
    Uint8 ratio_b, gain_b;
    Uint8 out[4096];
    int n, rounds = 0;

    if (size < 8) {
        return -1;
    }
    src.format = kFormats[data[0] % NFORMATS];
    src.channels = 1 + data[1] % 8;
    src.freq = kRates[data[2] % NRATES];
    dst.format = kFormats[data[3] % NFORMATS];
    dst.channels = 1 + data[4] % 8;
    dst.freq = kRates[data[5] % NRATES];
    ratio_b = data[6];
    gain_b = data[7];
    data += 8;
    size -= 8;

    stream = SDL_CreateAudioStream(&src, &dst);
    if (!stream) {
        return -1;
    }
    SDL_SetAudioStreamFrequencyRatio(stream, 0.25f + (float)ratio_b / 64.0f);
    SDL_SetAudioStreamGain(stream, (float)gain_b / 64.0f);

    if (size > 0 && SDL_PutAudioStreamData(stream, data, (int)size)) {
        SDL_FlushAudioStream(stream);
        while ((n = SDL_GetAudioStreamData(stream, out, sizeof(out))) > 0 && rounds++ < 4096) {
        }
    }
    SDL_DestroyAudioStream(stream);
    return 0;
}
