//===-- text.c - Text in the built-in font, through a glyph atlas ---------===//
//
// SPDX-License-Identifier: MIT OR GPL-2.0-only
//
// Each glyph is rasterised once per size by stb_truetype into the atlas, one
// byte of coverage per pixel, then painted from there every time it's drawn,
// as M5's cg_font.c does. A full atlas starts over.
//
// stb_truetype doesn't check the offsets in a font file, so it must only see
// the font built into mim: a guest can't hand it one.
//
//===----------------------------------------------------------------------===//

#include "runtime/canvas/canvas_impl.h"

#include <assert.h>
#include <string.h>

// embedded from extern/fonts by runtime/canvas/CMakeLists.txt
extern const unsigned char cv_font_data[];

// The biggest text drawn, in device pixels; anything bigger is drawn at this size
static const float cv_max_text_size = 256.0f;

static void *tt_malloc(const size_t size, void *const budget) {
    return mem_try_alloc(budget, size, "glyph");
}

static void tt_free(void *const ptr, void *const budget) {
    if (ptr != NULL) {
        mem_free(budget, ptr);
    }
}

// static, so nothing else that links stb_truetype clashes with this copy
#pragma GCC diagnostic ignored "-Wunused-function"
#define STBTT_STATIC
#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_malloc(_size, _budget) tt_malloc(_size, _budget)
#define STBTT_free(_ptr, _budget) tt_free(_ptr, _budget)
#define STBTT_assert(_x) assert(_x)
#include "stb_truetype.h"

// The font, the same for every canvas: stb_truetype only ever reads it
static stbtt_fontinfo g_font;
static bool g_font_ready;

static void reset_atlas(cv_impl_t *const cv) {
    memset(cv->glyphs, 0, cv_max_glyphs * sizeof(cv_glyph_t));
    cv->glyph_count = 0;
    cv->shelf_x = 0;
    cv->shelf_y = 0;
    cv->shelf_height = 0;
}

void cv_text_init(cv_impl_t *const cv) {
    if (!g_font_ready) {
        const int offset = stbtt_GetFontOffsetForIndex(cv_font_data, 0);
        const int ok = stbtt_InitFont(&g_font, cv_font_data, offset);
        assert(ok && "the built-in font is a font");
        (void)ok;
        g_font_ready = true;
    }
    cv->atlas = mem_alloc(cv->budget, (size_t)cv_atlas_side * cv_atlas_side, "glyph atlas");
    cv->glyphs = mem_alloc(cv->budget, cv_max_glyphs * sizeof(cv_glyph_t), "glyphs");
    reset_atlas(cv);
}

// The slot for (glyph, size): where it is, or the empty slot where it would go
static cv_glyph_t *slot(cv_impl_t *const cv, const int32_t glyph, const uint32_t size) {
    uint32_t h = ((uint32_t)glyph * 2654435761u ^ size * 40503u) & (cv_max_glyphs - 1);
    while (cv->glyphs[h].size != 0 && (cv->glyphs[h].glyph != glyph || cv->glyphs[h].size != size)) {
        h = (h + 1) & (cv_max_glyphs - 1);
    }
    return &cv->glyphs[h];
}

// The glyph at `size` quarter pixels, rasterised into the atlas the first time. Valid until the
// next call, which may start the atlas over.
static const cv_glyph_t *find_glyph(cv_impl_t *const cv, const int32_t glyph, const uint32_t size) {
    cv_glyph_t *g = slot(cv, glyph, size);
    if (g->size != 0) {
        return g;
    }
    // keep the table half empty, so a probe stays short
    if (cv->glyph_count == cv_max_glyphs / 2) {
        reset_atlas(cv);
        g = slot(cv, glyph, size);
    }
    const float scale = stbtt_ScaleForMappingEmToPixels(&g_font, (float)size / 4.0f);
    int x0, y0, x1, y1;
    stbtt_GetGlyphBitmapBox(&g_font, glyph, scale, scale, &x0, &y0, &x1, &y1);
    const uint32_t w = (uint32_t)(x1 - x0);
    const uint32_t h = (uint32_t)(y1 - y0);
    assert(w < cv_atlas_side && h < cv_atlas_side && "cv_max_text_size keeps glyphs small");
    // a gap of one pixel, so smoothing never picks up a neighbour
    if (cv->shelf_x + w > cv_atlas_side) {
        cv->shelf_x = 0;
        cv->shelf_y += cv->shelf_height + 1;
        cv->shelf_height = 0;
    }
    if (cv->shelf_y + h > cv_atlas_side) {
        reset_atlas(cv);
        g = slot(cv, glyph, size);
    }
    *g = (cv_glyph_t){
        .glyph = glyph,
        .size = size,
        .x = (uint16_t)cv->shelf_x,
        .y = (uint16_t)cv->shelf_y,
        .width = (uint16_t)w,
        .height = (uint16_t)h,
        .left = (int16_t)x0,
        .top = (int16_t)y0,
    };
    if (w != 0 && h != 0) {
        uint8_t *const at = cv->atlas + (size_t)g->y * cv_atlas_side + g->x;
        // whatever an earlier atlas left here; stb_truetype leaves it if it runs out of budget
        for (uint32_t row = 0; row < h; row++) {
            memset(at + (size_t)row * cv_atlas_side, 0, w);
        }
        g_font.userdata = cv->budget;
        stbtt_MakeGlyphBitmap(&g_font, at, (int)w, (int)h, cv_atlas_side, scale, scale, glyph);
    }
    cv->shelf_x += w + 1;
    cv->shelf_height = h > cv->shelf_height ? h : cv->shelf_height;
    cv->glyph_count++;
    return g;
}

// The code point at text[*at], moving *at past it; U+FFFD for a byte that isn't the start of a
// valid UTF-8 sequence
static uint32_t next_codepoint(const uint8_t *const text, const size_t size, size_t *const at) {
    const uint8_t c = text[*at];
    if (c < 0x80) {
        (*at)++;
        return c;
    }
    uint32_t extra, cp, min;
    if ((c & 0xE0) == 0xC0) {
        extra = 1, cp = c & 0x1F, min = 0x80;
    } else if ((c & 0xF0) == 0xE0) {
        extra = 2, cp = c & 0x0F, min = 0x800;
    } else if ((c & 0xF8) == 0xF0) {
        extra = 3, cp = c & 0x07, min = 0x10000;
    } else {
        (*at)++;
        return 0xFFFD;
    }
    if (size - *at <= extra) {
        (*at)++;
        return 0xFFFD;
    }
    for (uint32_t k = 1; k <= extra; k++) {
        const uint8_t b = text[*at + k];
        if ((b & 0xC0) != 0x80) {
            (*at)++;
            return 0xFFFD;
        }
        cp = cp << 6 | (b & 0x3F);
    }
    *at += extra + 1;
    // overlong, past Unicode, or a UTF-16 surrogate
    if (cp < min || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) {
        return 0xFFFD;
    }
    return cp;
}

// The advance from glyph `prev` (0 for none) to the end of `glyph`, in font units
static int advance(const int prev, const int glyph) {
    int width, left_bearing;
    stbtt_GetGlyphHMetrics(&g_font, glyph, &width, &left_bearing);
    return (prev == 0 ? 0 : stbtt_GetGlyphKernAdvance(&g_font, prev, glyph)) + width;
}

void cv_fill_text(const uint8_t *const text, const size_t size, const float x, const float y) {
    cv_impl_t *const cv = cv_impl();
    const cv_state_t *const s = &cv->state;
    const float device_size = fminf(s->font_size * cv_mean_scale(s), cv_max_text_size);
    if (!isfinite(x) || !isfinite(y) || !(device_size >= 0.25f)) {
        return;
    }
    // the glyphs' shapes come in quarter-pixel sizes; where they go uses the exact size, so the
    // text is as wide as cv_measure_text says
    const uint32_t quarters = (uint32_t)(device_size * 4.0f + 0.5f);
    const float scale = stbtt_ScaleForMappingEmToPixels(&g_font, device_size);
    const cv_point_t origin = cv_to_device(s, x, y);
    const int32_t baseline = (int32_t)floorf(origin.y + 0.5f);
    // ponytail: text runs left to right even under a mirroring transform
    float pen = origin.x;
    int prev = 0;
    size_t at = 0;
    while (at < size && pen < (float)s->clip_x1 + device_size) {
        const int glyph = stbtt_FindGlyphIndex(&g_font, (int)next_codepoint(text, size, &at));
        if (prev != 0) {
            pen += scale * (float)stbtt_GetGlyphKernAdvance(&g_font, prev, glyph);
        }
        const cv_glyph_t *const g = find_glyph(cv, glyph, quarters);
        if (g->width != 0) {
            const uint8_t *const mask = cv->atlas + (size_t)g->y * cv_atlas_side + g->x;
            const int32_t left = (int32_t)floorf(pen + 0.5f) + g->left;
            cv_raster_mask(cv, mask, cv_atlas_side, g->width, g->height, left, baseline + g->top, s->fill);
        }
        pen += scale * (float)advance(0, glyph);
        prev = glyph;
    }
}

float cv_measure_text(const uint8_t *const text, const size_t size) {
    const cv_state_t *const s = &cv_impl()->state;
    int64_t units = 0;
    int prev = 0;
    size_t at = 0;
    while (at < size) {
        const int glyph = stbtt_FindGlyphIndex(&g_font, (int)next_codepoint(text, size, &at));
        units += advance(prev, glyph);
        prev = glyph;
    }
    return (float)units * stbtt_ScaleForMappingEmToPixels(&g_font, s->font_size);
}
