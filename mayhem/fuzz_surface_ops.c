#include <SDL3/SDL.h>
#include <string.h>

static const SDL_PixelFormat kFormats[] = {
    SDL_PIXELFORMAT_INDEX1LSB, SDL_PIXELFORMAT_INDEX1MSB, SDL_PIXELFORMAT_INDEX2LSB,
    SDL_PIXELFORMAT_INDEX2MSB, SDL_PIXELFORMAT_INDEX4LSB, SDL_PIXELFORMAT_INDEX4MSB,
    SDL_PIXELFORMAT_INDEX8, SDL_PIXELFORMAT_RGB332, SDL_PIXELFORMAT_XRGB4444,
    SDL_PIXELFORMAT_XBGR4444, SDL_PIXELFORMAT_XRGB1555, SDL_PIXELFORMAT_XBGR1555,
    SDL_PIXELFORMAT_ARGB4444, SDL_PIXELFORMAT_RGBA4444, SDL_PIXELFORMAT_ABGR4444,
    SDL_PIXELFORMAT_BGRA4444, SDL_PIXELFORMAT_ARGB1555, SDL_PIXELFORMAT_RGBA5551,
    SDL_PIXELFORMAT_ABGR1555, SDL_PIXELFORMAT_BGRA5551, SDL_PIXELFORMAT_RGB565,
    SDL_PIXELFORMAT_BGR565, SDL_PIXELFORMAT_RGB24, SDL_PIXELFORMAT_BGR24,
    SDL_PIXELFORMAT_XRGB8888, SDL_PIXELFORMAT_RGBX8888, SDL_PIXELFORMAT_XBGR8888,
    SDL_PIXELFORMAT_BGRX8888, SDL_PIXELFORMAT_ARGB8888, SDL_PIXELFORMAT_RGBA8888,
    SDL_PIXELFORMAT_ABGR8888, SDL_PIXELFORMAT_BGRA8888, SDL_PIXELFORMAT_XRGB2101010,
    SDL_PIXELFORMAT_XBGR2101010, SDL_PIXELFORMAT_ARGB2101010, SDL_PIXELFORMAT_ABGR2101010,
    SDL_PIXELFORMAT_RGB48, SDL_PIXELFORMAT_BGR48, SDL_PIXELFORMAT_RGBA64,
    SDL_PIXELFORMAT_ARGB64, SDL_PIXELFORMAT_BGRA64, SDL_PIXELFORMAT_ABGR64,
    SDL_PIXELFORMAT_RGB48_FLOAT, SDL_PIXELFORMAT_BGR48_FLOAT, SDL_PIXELFORMAT_RGBA64_FLOAT,
    SDL_PIXELFORMAT_ARGB64_FLOAT, SDL_PIXELFORMAT_BGRA64_FLOAT, SDL_PIXELFORMAT_ABGR64_FLOAT,
    SDL_PIXELFORMAT_RGB96_FLOAT, SDL_PIXELFORMAT_BGR96_FLOAT, SDL_PIXELFORMAT_RGBA128_FLOAT,
    SDL_PIXELFORMAT_ARGB128_FLOAT, SDL_PIXELFORMAT_BGRA128_FLOAT, SDL_PIXELFORMAT_ABGR128_FLOAT,
    SDL_PIXELFORMAT_YV12, SDL_PIXELFORMAT_IYUV, SDL_PIXELFORMAT_YUY2, SDL_PIXELFORMAT_UYVY,
    SDL_PIXELFORMAT_YVYU, SDL_PIXELFORMAT_NV12, SDL_PIXELFORMAT_NV21, SDL_PIXELFORMAT_P010,
};

static const SDL_Colorspace kColorspaces[] = {
    SDL_COLORSPACE_SRGB, SDL_COLORSPACE_SRGB_LINEAR, SDL_COLORSPACE_HDR10,
    SDL_COLORSPACE_JPEG, SDL_COLORSPACE_BT601_LIMITED, SDL_COLORSPACE_BT601_FULL,
    SDL_COLORSPACE_BT709_LIMITED, SDL_COLORSPACE_BT709_FULL, SDL_COLORSPACE_BT2020_LIMITED,
    SDL_COLORSPACE_BT2020_FULL, SDL_COLORSPACE_RGB_DEFAULT, SDL_COLORSPACE_YUV_DEFAULT,
};

#define NFORMATS (sizeof(kFormats) / sizeof(kFormats[0]))
#define NCOLORSPACES (sizeof(kColorspaces) / sizeof(kColorspaces[0]))

typedef struct {
    Uint8 src_fmt, dst_fmt, src_cs, dst_cs;
    Uint8 w, h, dw, dh;
    Uint8 blend, scale, flags, ckey;
    Uint8 rect[8];
    Uint8 mods[4];
} Header;

static SDL_BlendMode pick_blend(Uint8 b)
{
    static const SDL_BlendMode modes[] = {
        SDL_BLENDMODE_NONE, SDL_BLENDMODE_BLEND, SDL_BLENDMODE_BLEND_PREMULTIPLIED,
        SDL_BLENDMODE_ADD, SDL_BLENDMODE_ADD_PREMULTIPLIED, SDL_BLENDMODE_MOD, SDL_BLENDMODE_MUL,
    };
    return modes[b % (sizeof(modes) / sizeof(modes[0]))];
}

static SDL_Surface *make_surface(int w, int h, SDL_PixelFormat fmt, const Uint8 *px, size_t pxlen)
{
    SDL_Surface *s = SDL_CreateSurface(w, h, fmt);
    int y;
    if (!s) {
        return NULL;
    }
    if (px) {
        for (y = 0; y < h && pxlen; y++) {
            size_t n = (size_t)s->pitch < pxlen ? (size_t)s->pitch : pxlen;
            memcpy((Uint8 *)s->pixels + (size_t)y * s->pitch, px, n);
            px += n;
            pxlen -= n;
        }
    }
    return s;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    Header hd;
    SDL_PixelFormat sf, df;
    SDL_Surface *src, *dst, *tmp;
    SDL_Rect sr, dr;
    int w, h, dw, dh;

    if (size < sizeof(hd)) {
        return -1;
    }
    memcpy(&hd, data, sizeof(hd));
    data += sizeof(hd);
    size -= sizeof(hd);

    sf = kFormats[hd.src_fmt % NFORMATS];
    df = kFormats[hd.dst_fmt % NFORMATS];
    w = 1 + hd.w % 48;
    h = 1 + hd.h % 48;
    dw = 1 + hd.dw % 48;
    dh = 1 + hd.dh % 48;

    /* Raw pixel-format conversion (including YUV and colorspace handling). */
    {
        SDL_Colorspace scs = kColorspaces[hd.src_cs % NCOLORSPACES];
        SDL_Colorspace dcs = kColorspaces[hd.dst_cs % NCOLORSPACES];
        size_t spitch = (size_t)w * 16 + 16, dpitch = (size_t)w * 16 + 16;
        Uint8 *sbuf = SDL_calloc(1, spitch * (size_t)h * 2);
        Uint8 *dbuf = SDL_calloc(1, dpitch * (size_t)h * 2);
        if (sbuf && dbuf) {
            memcpy(sbuf, data, size < spitch * h ? size : spitch * h);
            SDL_ConvertPixelsAndColorspace(w, h, sf, scs, 0, sbuf, (int)spitch,
                                           df, dcs, 0, dbuf, (int)dpitch);
        }
        SDL_free(sbuf);
        SDL_free(dbuf);
    }

    if (SDL_ISPIXELFORMAT_FOURCC(sf) || SDL_ISPIXELFORMAT_FOURCC(df)) {
        return 0;
    }

    src = make_surface(w, h, sf, data, size);
    dst = make_surface(dw, dh, df, NULL, 0);
    if (!src || !dst) {
        SDL_DestroySurface(src);
        SDL_DestroySurface(dst);
        return -1;
    }

    if (SDL_ISPIXELFORMAT_INDEXED(sf)) {
        SDL_Palette *pal = SDL_CreateSurfacePalette(src);
        if (pal) {
            int i;
            for (i = 0; i < pal->ncolors && (size_t)i < size; i++) {
                pal->colors[i].r = data[i];
                pal->colors[i].g = (Uint8)(data[i] * 3);
                pal->colors[i].b = (Uint8)(data[i] * 7);
                pal->colors[i].a = (hd.flags & 1) ? data[(i + 1) % size] : 255;
            }
        }
    }
    if (SDL_ISPIXELFORMAT_INDEXED(df)) {
        SDL_CreateSurfacePalette(dst);
    }

    if (hd.flags & 2) {
        SDL_SetSurfaceColorKey(src, true, hd.ckey);
    }
    if (hd.flags & 4) {
        SDL_SetSurfaceRLE(src, true);
    }
    SDL_SetSurfaceBlendMode(src, pick_blend(hd.blend));
    SDL_SetSurfaceColorMod(src, hd.mods[0], hd.mods[1], hd.mods[2]);
    SDL_SetSurfaceAlphaMod(src, hd.mods[3]);

    sr.x = (Sint8)hd.rect[0] / 2;
    sr.y = (Sint8)hd.rect[1] / 2;
    sr.w = hd.rect[2] % 64;
    sr.h = hd.rect[3] % 64;
    dr.x = (Sint8)hd.rect[4] / 2;
    dr.y = (Sint8)hd.rect[5] / 2;
    dr.w = hd.rect[6] % 64;
    dr.h = hd.rect[7] % 64;

    SDL_BlitSurface(src, (hd.flags & 8) ? &sr : NULL, dst, (hd.flags & 16) ? &dr : NULL);
    SDL_BlitSurfaceScaled(src, (hd.flags & 8) ? &sr : NULL, dst, (hd.flags & 16) ? &dr : NULL,
                          (hd.scale & 1) ? SDL_SCALEMODE_LINEAR : SDL_SCALEMODE_NEAREST);
    SDL_BlitSurfaceTiled(src, (hd.flags & 8) ? &sr : NULL, dst, (hd.flags & 16) ? &dr : NULL);
    SDL_FillSurfaceRect(dst, (hd.flags & 16) ? &dr : NULL, hd.ckey * 0x01010101u);

    tmp = SDL_ConvertSurface(src, df);
    SDL_DestroySurface(tmp);
    tmp = SDL_ScaleSurface(src, dw, dh, (hd.scale & 1) ? SDL_SCALEMODE_LINEAR : SDL_SCALEMODE_NEAREST);
    SDL_DestroySurface(tmp);
    tmp = SDL_RotateSurface(src, (float)hd.rect[0] * 1.5f);
    SDL_DestroySurface(tmp);
    SDL_FlipSurface(src, (SDL_FlipMode)(hd.scale % 4));
    SDL_PremultiplySurfaceAlpha(src, (hd.flags & 32) != 0);

    SDL_DestroySurface(src);
    SDL_DestroySurface(dst);
    return 0;
}
