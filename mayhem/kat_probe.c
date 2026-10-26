/* Known-answer probe for SDL's loaders and converters. Prints one "key=value" line per check;
 * mayhem/test.sh compares the output against fixed expected values. Built dynamically against
 * the normal-flags libSDL3, so it dies silently under the sabotage shim and cannot pass by
 * exiting 0. Usage: kat_probe <sample.wav> <sample.png> */
#include <SDL3/SDL.h>
#include <stdio.h>

static int fail(const char *what)
{
    fprintf(stderr, "kat_probe: %s failed: %s\n", what, SDL_GetError());
    return 1;
}

static void px(SDL_Surface *s, const char *tag, int x, int y)
{
    Uint8 r, g, b, a;
    if (!SDL_ReadSurfacePixel(s, x, y, &r, &g, &b, &a)) {
        r = g = b = a = 0;
    }
    printf("%s(%d,%d)=%02x%02x%02x%02x\n", tag, x, y, r, g, b, a);
}

int main(int argc, char **argv)
{
    SDL_AudioSpec spec;
    Uint8 *buf = NULL, *out = NULL;
    Uint32 len = 0;
    int outlen = 0;
    SDL_Surface *s, *c, *r;
    SDL_IOStream *io;

    if (argc < 3) {
        return 2;
    }

    if (!SDL_LoadWAV(argv[1], &spec, &buf, &len)) {
        return fail("LoadWAV");
    }
    printf("wav.format=0x%04x channels=%d freq=%d len=%u\n", (unsigned)spec.format, spec.channels, spec.freq, len);
    SDL_free(buf);

    s = SDL_LoadPNG(argv[2]);
    if (!s) {
        return fail("LoadPNG");
    }
    printf("png.size=%dx%d\n", s->w, s->h);
    px(s, "png", 100, 40);
    px(s, "png", 300, 50);
    px(s, "png", 150, 120);
    SDL_DestroySurface(s);

    {
        const Sint16 in[4] = { 0x4000, -0x4000, 0, -0x8000 };
        const SDL_AudioSpec src = { SDL_AUDIO_S16LE, 1, 8000 };
        const SDL_AudioSpec dst = { SDL_AUDIO_F32LE, 2, 8000 };
        const float *f;
        int i;
        if (!SDL_ConvertAudioSamples(&src, (const Uint8 *)in, sizeof(in), &dst, &out, &outlen)) {
            return fail("ConvertAudioSamples");
        }
        f = (const float *)out;
        printf("audio.outlen=%d", outlen);
        for (i = 0; i < outlen / 4; i++) {
            printf(" %.4f", f[i]);
        }
        printf("\n");
        SDL_free(out);
    }

    s = SDL_CreateSurface(3, 2, SDL_PIXELFORMAT_ARGB8888);
    if (!s) {
        return fail("CreateSurface");
    }
    {
        int x, y;
        for (y = 0; y < 2; y++) {
            for (x = 0; x < 3; x++) {
                SDL_WriteSurfacePixel(s, x, y, (Uint8)(40 * x + 10), (Uint8)(100 * y + 20), (Uint8)(80 + x + y), (Uint8)(255 - 50 * x));
            }
        }
    }
    c = SDL_ConvertSurface(s, SDL_PIXELFORMAT_RGB565);
    if (!c) {
        return fail("ConvertSurface");
    }
    printf("rgb565 pitch=%d p00=%04x p20=%04x p12=%04x\n", c->pitch,
           ((Uint16 *)c->pixels)[0], ((Uint16 *)c->pixels)[2],
           ((Uint16 *)((Uint8 *)c->pixels + c->pitch))[2]);
    SDL_DestroySurface(c);

    io = SDL_IOFromDynamicMem();
    if (!io || !SDL_SaveBMP_IO(s, io, false)) {
        return fail("SaveBMP");
    }
    {
        Sint64 n = SDL_GetIOSize(io);
        SDL_PropertiesID p = SDL_GetIOProperties(io);
        void *mem = SDL_GetPointerProperty(p, SDL_PROP_IOSTREAM_DYNAMIC_MEMORY_POINTER, NULL);
        SDL_IOStream *rd = SDL_IOFromConstMem(mem, (size_t)n);
        r = SDL_LoadBMP_IO(rd, true);
        printf("bmp.size=%lld magic=%c%c\n", (long long)n, ((char *)mem)[0], ((char *)mem)[1]);
        if (!r) {
            return fail("LoadBMP");
        }
        px(r, "bmp", 0, 0);
        px(r, "bmp", 2, 1);
        SDL_DestroySurface(r);
        SDL_CloseIO(io);
    }
    SDL_DestroySurface(s);
    return 0;
}
