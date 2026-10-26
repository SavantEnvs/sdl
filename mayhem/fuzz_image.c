#include <SDL3/SDL.h>

/* One harness per decoder, selected at compile time: -DFUZZ_LOADER=SDL_LoadBMP_IO etc. */
#ifndef FUZZ_LOADER
#error "define FUZZ_LOADER"
#endif

/* The decoders allocate width*height*bytes straight from header-claimed dimensions; without a cap
 * a 40-byte input asks for tens of GB and ASan aborts with allocation-size-too-big. */
#define MAX_PIXELS ((uint64_t)1 << 22)

static uint32_t rd_le(const uint8_t *p, int n)
{
    uint32_t v = 0;
    while (n--) {
        v = (v << 8) | p[n];
    }
    return v;
}

static int dims_ok(const uint8_t *d, size_t n)
{
    uint64_t w = 0, h = 0;
#if defined(FUZZ_IS_BMP)
    if (n < 26) {
        return 1;
    }
    if (rd_le(d + 14, 4) >= 40) {
        int32_t sh = (int32_t)rd_le(d + 22, 4);
        w = rd_le(d + 18, 4);
        h = sh < 0 ? (uint64_t)(-(int64_t)sh) : (uint64_t)sh;
    } else {
        w = rd_le(d + 18, 2);
        h = rd_le(d + 20, 2);
    }
    return w * h <= MAX_PIXELS;
#elif defined(FUZZ_IS_PNG)
    if (n < 24) {
        return 1;
    }
    w = ((uint64_t)d[16] << 24) | (d[17] << 16) | (d[18] << 8) | d[19];
    h = ((uint64_t)d[20] << 24) | (d[21] << 16) | (d[22] << 8) | d[23];
    return w * h <= MAX_PIXELS;
#else
    size_t i;
    for (i = 0; i + 9 < n; i++) {
        if (d[i] == 0xFF && (d[i + 1] == 0xC0 || d[i + 1] == 0xC1 || d[i + 1] == 0xC2)) {
            h = (d[i + 5] << 8) | d[i + 6];
            w = (d[i + 7] << 8) | d[i + 8];
            if (w * h > MAX_PIXELS) {
                return 0;
            }
        }
    }
    return 1;
#endif
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    SDL_IOStream *io;
    SDL_Surface *surf, *conv;

    if (!dims_ok(data, size)) {
        return -1;
    }
    io = SDL_IOFromConstMem(data, size);
    if (!io) {
        return -1;
    }
    surf = FUZZ_LOADER(io, true);
    if (!surf) {
        return -1;
    }

    if ((int64_t)surf->w * surf->h <= (1 << 18)) {
        conv = SDL_ConvertSurface(surf, SDL_PIXELFORMAT_ARGB8888);
        if (conv) {
            SDL_IOStream *dst = SDL_IOFromDynamicMem();
            if (dst) {
                SDL_SaveBMP_IO(conv, dst, true);
            }
            dst = SDL_IOFromDynamicMem();
            if (dst) {
                SDL_SavePNG_IO(conv, dst, true);
            }
            SDL_DestroySurface(conv);
        }
    }
    SDL_DestroySurface(surf);
    return 0;
}
