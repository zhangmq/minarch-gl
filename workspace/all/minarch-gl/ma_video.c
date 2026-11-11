#include <string.h>
#include <math.h>

#include "ma_internal.h"
#include "scaler.h"
#include "ma_video.h"
#include "ma_gl.h"


// The legacy scaler-era aspect field (0 = integer scale, -1 = stretch, >0 =
// keep-aspect). Nothing reads renderer.aspect any more -- the present geometry
// comes from PLAT_compute_present_rect -- but the field is still written so the
// frame description stays a faithful copy of what upstream keeps there.
// Single source for both refresh points below.
static double renderer_aspect_for_scaling(int src_w, int src_h) {
	switch (screen_scaling) {
	case SCALE_ASPECT_SCREEN: return (double)src_w / (src_h ? src_h : 1);
	case SCALE_NATIVE:
	case SCALE_CROPPED:      return 0.0;
	case SCALE_FULLSCREEN:   return -1.0;
	default:                 return core.aspect_ratio;
	}
}

// Kept as the Screen Scaling menu's re-init hook (ma_menu.c calls it when the
// mode changes; ma_video.h declares it). P3 removed the CPU scalers -- the
// present geometry is recomputed on every present by
// PLAT_compute_present_rect (which reads screen_scaling), so all this needs to
// do is refresh the renderer's frame description.
void selectScaler(int src_w, int src_h, int src_p) {
	renderer.src_w = src_w;
	renderer.src_h = src_h;
	renderer.src_p = src_p;
	renderer.true_w = src_w;
	renderer.true_h = src_h;
	renderer.aspect = renderer_aspect_for_scaling(src_w, src_h);
	renderer.dst_p = 1; // sentinel: frame description initialised
}

static void screen_flip(SDL_Surface* screen) {
	(void)screen;
	// Presentation never contributes to the frame clock. The old
	// use_core_fps branch paced from inside this callback with
	// GFX_flip_fixed_rate while the other branch did not, so which clock a
	// core ran on depended on the sync-reference setting. Audio
	// backpressure plus the timer fallback now live in the run loop
	// (pace_frame, minarch.c) and cover both cases; this only puts the
	// finished frame on the display.
	GFX_GL_Swap();
}

static const char* bitmap_font[] = {
	['0'] = 
		" 111 "
		"1   1"
		"1   1"
		"1  11"
		"1 1 1"
		"11  1"
		"1   1"
		"1   1"
		" 111 ",
	['1'] =
		"   1 "
		" 111 "
		"   1 "
		"   1 "
		"   1 "
		"   1 "
		"   1 "
		"   1 "
		"   1 ",
	['2'] =
		" 111 "
		"1   1"
		"    1"
		"   1 "
		"  1  "
		" 1   "
		"1    "
		"1    "
		"11111",
	['3'] =
		" 111 "
		"1   1"
		"    1"
		"    1"
		" 111 "
		"    1"
		"    1"
		"1   1"
		" 111 ",
	['4'] =
		"1   1"
		"1   1"
		"1   1"
		"1   1"
		"1   1"
		"1   1"
		"11111"
		"    1"
		"    1",
	['5'] =
		"11111"
		"1    "
		"1    "
		"1111 "
		"    1"
		"    1"
		"    1"
		"1   1"
		" 111 ",
	['6'] =
		" 111 "
		"1    "
		"1    "
		"1111 "
		"1   1"
		"1   1"
		"1   1"
		"1   1"
		" 111 ",
	['7'] =
		"11111"
		"    1"
		"    1"
		"   1 "
		"  1  "
		"  1  "
		"  1  "
		"  1  "
		"  1  ",
	['8'] =
		" 111 "
		"1   1"
		"1   1"
		"1   1"
		" 111 "
		"1   1"
		"1   1"
		"1   1"
		" 111 ",
	['9'] =
		" 111 "
		"1   1"
		"1   1"
		"1   1"
		"1   1"
		" 1111"
		"    1"
		"    1"
		" 111 ",
	['.'] = 
		"     "
		"     "
		"     "
		"     "
		"     "
		"     "
		"     "
		" 11  "
		" 11  ",
	[','] = 
		"     "
		"     "
		"     "
		"     "
		"     "
		"     "
		"  1  "
		"  1  "
		" 1   ",
	[' '] = 
		"     "
		"     "
		"     "
		"     "
		"     "
		"     "
		"     "
		"     "
		"     ",
	['('] = 
		"   1 "
		"  1  "
		" 1   "
		" 1   "
		" 1   "
		" 1   "
		" 1   "
		"  1  "
		"   1 ",
	[')'] = 
		" 1   "
		"  1  "
		"   1 "
		"   1 "
		"   1 "
		"   1 "
		"   1 "
		"  1  "
		" 1   ",
	['/'] = 
		"   1 "
		"   1 "
		"   1 "
		"  1  "
		"  1  "
		"  1  "
		" 1   "
		" 1   "
		" 1   ",
	['x'] = 
		"     "
		"     "
		"1   1"
		"1   1"
		" 1 1 "
		"  1  "
		" 1 1 "
		"1   1"
		"1   1",
	['%'] = 
		" 1   "
		"1 1  "
		"1 1 1"
		" 1 1 "
		"  1  "
		" 1 1 "
		"1 1 1"
		"  1 1"
		"   1 ",
	['-'] =
		"     "
		"     "
		"     "
		"     "
		" 111 "
		"     "
		"     "
		"     "
		"     ",
	['c'] = 
        "     "
        "     "
        " 111 "
        "1   1"
        "1    "
        "1    "
        "1    "
        "1   1"
        " 111 ",
	['m'] = 
        "     "
        "     "
        "11 11"
        "1 1 1"
        "1 1 1"
        "1   1"
        "1   1"
        "1   1"
        "1   1",
	['z'] =
		"     "
        "     "
        "     "
        "11111"
        "   1 "
        "  1  "
        " 1   "
        "1    "
        "11111",
	['h'] =
		"     "
        "1    "
        "1    "
        "1    "
        "1111 "
        "1   1"
        "1   1"
        "1   1"
        "1   1",
	['D'] = 
		"1111 "
		"1   1"
		"1   1"
		"1   1"
		"1   1"
		"1   1"
		"1   1"
		"1   1"
		"1111 ",
	['J'] = 
		"  111"
		"    1"
		"    1"
		"    1"
		"    1"
		"1   1"
		"1   1"
		"1   1"
		" 111 ",
	['A'] = 
		"  1  "
		" 1 1 "
		"1   1"
		"1   1"
		"11111"
		"1   1"
		"1   1"
		"1   1"
		"1   1",
	['M'] = 
		"1   1"
		"11 11"
		"1 1 1"
		"1   1"
		"1   1"
		"1   1"
		"1   1"
		"1   1"
		"1   1",
	[':'] = 
		"     "
		"     "
		"  1  "
		"     "
		"     "
		"     "
		"  1  "
		"     "
		"     ",
	['B'] = 
		"1111 "
		"1   1"
		"1   1"
		"1111 "
		"1   1"
		"1   1"
		"1   1"
		"1   1"
		"1111 ",
	['C'] = 
		" 111 "
		"1   1"
		"1    "
		"1    "
		"1    "
		"1    "
		"1    "
		"1   1"
		" 111 ",
	['N'] = 
		"1   1"
		"1   1"
		"11  1"
		"1   1"
		"1 1 1"
		"1   1"
		"1  11"
		"1   1"
		"1   1",
	['H'] = 
		"1   1"
		"1   1"
		"1   1"
		"1   1"
		"11111"
		"1   1"
		"1   1"
		"1   1"
		"1   1",
};

/* ---------------------------------------------------------------------------
 * Debug HUD: layout only.
 *
 * Every coordinate here is in DEVICE pixels.  The old rasterizer drew into a
 * half-resolution surface that the composite stage upscaled 2x, so both the
 * positions and each glyph pixel are scaled by HUD_SCALE; the GPU renderer
 * (generic_video.c) reproduces exactly that with quads from the font atlas.
 * ------------------------------------------------------------------------ */
#define HUD_GLYPH_W 5
#define HUD_GLYPH_H 9
#define HUD_CELL_W  6   /* 5 px glyph + 1 px letter spacing */
#define HUD_SCALE   2   /* the old half-resolution surface, doubled */

const char *MA_HUD_glyph(unsigned char c) {
	/* Upstream indexed bitmap_font with any byte of the text, but the array is
	 * only sized by its highest designator ('N'); anything past that was an
	 * out-of-bounds read that happened to be zero.  Treat those as space. */
	if (c >= sizeof(bitmap_font) / sizeof(bitmap_font[0])) c = ' ';
	const char *g = bitmap_font[c];
	return g ? g : bitmap_font[' '];
}

static void hud_text_item(MA_HudItem *items, int *n, int max, char *text,
		int ax, int ay, int dev_w, int dev_h) {
	int len = (int)strlen(text);
	if (len <= 0 || *n >= max) return;

	int w = (HUD_CELL_W * len - 1) * HUD_SCALE;
	int h = HUD_GLYPH_H * HUD_SCALE;
	/* The old rasterizer read a negative anchor as "align to that edge":
	 * ox = width - w + ox, oy = height - h + oy. */
	int x = ax < 0 ? dev_w - w + ax : ax;
	int y = ay < 0 ? dev_h - h + ay : ay;
	if (x < 0) x = 0;
	if (y < 0) y = 0;

	MA_HudItem *it = &items[(*n)++];
	it->x = x;
	it->y = y;
	it->w = w;
	it->h = h;
	it->argb = 0xFFFFFFFFu;
	it->grad = 0.0f;
	it->text = text;
}

static void hud_rect_item(MA_HudItem *items, int *n, int max,
		int x, int y, int w, int h, uint32_t argb, float grad) {
	if (w <= 0 || h <= 0 || *n >= max) return;
	MA_HudItem *it = &items[(*n)++];
	it->x = x;
	it->y = y;
	it->w = w;
	it->h = h;
	it->argb = argb;
	it->grad = grad;
	it->text = NULL;
}

int MA_HUD_layout(MA_HudItem *items, int max, int dev_w, int dev_h) {
	if (!show_debug || SDL_GetTicks() <= 5000) return 0;
	if (isnan(perf.ratio) || isnan(perf.fps) || isnan(perf.req_fps)
			|| isnan(perf.buffer_ms)) return 0;
	if (perf.buffer_size < 0 || perf.buffer_free < 0) return 0;

	static char line[8][250];
	int n = 0;
	int x = (2 + renderer.src_x) * HUD_SCALE;
	int y = (2 + renderer.src_y) * HUD_SCALE;
	int step = 14 * HUD_SCALE;
	int scale = renderer.scale;
	if (scale == -1) scale = 1; /* nearest neighbor flag */

	snprintf(line[0], sizeof(line[0]), "%ix%i %ix %i/%i",
			renderer.src_w, renderer.src_h, scale,
			perf.samplerate_in, perf.samplerate_out);
	hud_text_item(items, &n, max, line[0], x, y, dev_w, dev_h);

	snprintf(line[1], sizeof(line[1]), "%.03f/%i/%.0f/%i/%i/%i",
			perf.ratio, perf.buffer_size, perf.buffer_ms, perf.buffer_free,
			perf.buffer_target, perf.avg_buffer_free);
	hud_text_item(items, &n, max, line[1], x, y + step, dev_w, dev_h);

	snprintf(line[2], sizeof(line[2]), "%i,%i %ix%i",
			renderer.dst_x, renderer.dst_y,
			renderer.src_w * scale, renderer.src_h * scale);
	hud_text_item(items, &n, max, line[2], -x, y, dev_w, dev_h);

	snprintf(line[3], sizeof(line[3]), "%ix%i,%i",
			renderer.dst_w, renderer.dst_h,
			fmt == RETRO_PIXEL_FORMAT_XRGB8888 ? 8888 : 565);
	hud_text_item(items, &n, max, line[3], -x, -y, dev_w, dev_h);

	/* Frame timing stats */
	snprintf(line[4], sizeof(line[4]), "%.1f/%.1f A:%.1f M:%.1f D:%d",
			perf.fps, perf.req_fps, perf.avg_frame_ms, perf.max_frame_ms,
			perf.frame_drops);
	hud_text_item(items, &n, max, line[4], x, -y, dev_w, dev_h);

	/* CPU stats */
	PLAT_getCPUSpeed();
	PLAT_getCPUTemp();
	snprintf(line[5], sizeof(line[5]), "%.0f%%/%ihz/%ic",
			perf.cpu_usage, perf.cpu_speed, perf.cpu_temp);
	hud_text_item(items, &n, max, line[5], x, -y - step, dev_w, dev_h);

	/* GPU stats */
	PLAT_getGPUUsage();
	PLAT_getGPUSpeed();
	PLAT_getGPUTemp();
	snprintf(line[6], sizeof(line[6]), "%.0f%%/%ihz/%ic",
			perf.gpu_usage, perf.gpu_speed, perf.gpu_temp);
	hud_text_item(items, &n, max, line[6], x, -y - 2 * step, dev_w, dev_h);

	if (currentshaderpass > 0) {
		snprintf(line[7], sizeof(line[7]), "%i/%ix%i/%ix%i/%ix%i",
				currentshaderpass, currentshadersrcw, currentshadersrch,
				currentshadertexw, currentshadertexh,
				currentshaderdstw, currentshaderdsth);
		hud_text_item(items, &n, max, line[7], x, -y - 3 * step, dev_w, dev_h);
	}

	/* Audio buffer gauge: black background, the filled part (colour sampled
	 * from the atlas gradient at `fill`, exactly drawGauge's maths), then a
	 * 1 px outline (2 px at this scale). */
	double fill = (double)(perf.buffer_size - perf.buffer_free)
			/ (double)perf.buffer_size;
	if (fill < 0.0) fill = 0.0;
	if (fill > 1.0) fill = 1.0;
	int gx = x;
	int gy = y + 30 * HUD_SCALE;
	int gw = dev_w / 2;
	int gh = 8 * HUD_SCALE;
	hud_rect_item(items, &n, max, gx, gy, gw, gh, 0xFF000000u, 0.0f);
	int filled = (int)(fill * (double)gw);
	if (filled > 0)
		hud_rect_item(items, &n, max, gx, gy, filled, gh, 0, (float)fill);
	int b = HUD_SCALE;
	hud_rect_item(items, &n, max, gx, gy, gw, b, 0xFFFFFFFFu, 0.0f);
	hud_rect_item(items, &n, max, gx, gy + gh - b, gw, b, 0xFFFFFFFFu, 0.0f);
	hud_rect_item(items, &n, max, gx, gy, b, gh, 0xFFFFFFFFu, 0.0f);
	hud_rect_item(items, &n, max, gx + gw - b, gy, b, gh, 0xFFFFFFFFu, 0.0f);

	return n;
}

int PLAT_debug_hud_active(void) {
	return show_debug && SDL_GetTicks() > 5000;
}

static void video_refresh_callback_main(const void *data, unsigned width, unsigned height, size_t pitch) {
	// return;

	Special_render();
	
	static uint32_t last_flip_time = 0;
	
	// 10 seems to be the sweet spot that allows 2x in NES and SNES and 8x in GB at 60fps
	// 14 will let GB hit 10x but NES and SNES will drop to 1.5x at 30fps (not sure why)
	// but 10 hurts PS...
	// TODO: 10 was based on rg35xx, probably different results on other supported platforms
	if (fast_forward && SDL_GetTicks()-last_flip_time<10) return;
	
	// FFVII menus 
	// 16: 30/200
	// 15: 30/180
	// 14: 45/180
	// 12: 30/150
	// 10: 30/120 (optimize text off has no effect)
	//  8: 60/210 (with optimize text off)
	// eg. PS@10 60/240
	if (!data) {
		return;
	}

	// Source geometry changed (or the sentinel was cleared by a config /
	// core change): refresh the renderer's frame description and reset the
	// shader-chain textures.
	//
	// P3: there is no CPU scaler step any more -- the present geometry is
	// computed by PLAT_compute_present_rect (RA viewport model, shared with
	// the hw-render path) inside PLAT_GL_Swap. src_x/src_y stay 0: NATIVE /
	// CROPPED oversize is handled by the present rect overflowing the screen
	// and being clipped by the viewport, like RA.
	if (renderer.dst_p==0 || width!=renderer.true_w || height!=renderer.true_h) {
		renderer.src_w = width;
		renderer.src_h = height;
		renderer.src_p = pitch;
		renderer.src_x = 0;
		renderer.src_y = 0;
		renderer.true_w = width;
		renderer.true_h = height;
		renderer.aspect = renderer_aspect_for_scaling(width, height);
		renderer.dst_p = 1; // sentinel: frame description initialised
		GFX_clearAll();
		if (!shader_reset_suppressed) {
			GFX_resetShaders();
		} else {
			shader_reset_suppressed = 0; // consume suppression after one use
		}
	}
	
	// The debug HUD is NOT stamped into the core frame here any more: it is
	// composited by the ONE shared overlay stage (generic_video.c
	// PLAT_composite_overlays), which runs after the frame from EITHER path is
	// complete. That is what keeps the software and hw-render HUDs identical;
	// stamping it here made the software HUD travel the shader chain while the
	// hw-render HUD was drawn at device resolution after the present.
	// PLAT_draw_debug_hud (below) is the single rasterizer both use.

	renderer.src = (void*)data;
	renderer.dst = screen->pixels;
	GFX_blitRenderer(&renderer);

	screen_flip(screen);
	last_flip_time = SDL_GetTicks();
}

const void* lastframe = NULL;
static Uint32* rgbaData = NULL;
static size_t rgbaDataSize = 0;

// ARM NEON SIMD optimization for pixel format conversion
#if defined(__ARM_NEON) || defined(__aarch64__)
#include <arm_neon.h>

// Convert 8 RGB565 pixels to RGBA using NEON (processes 16 bytes → 32 bytes)
static inline void convert_rgb565_to_rgba_neon(const uint16_t* __restrict src, uint32_t* __restrict dst) {
	// Load 8 RGB565 pixels (128 bits)
	uint16x8_t rgb565 = vld1q_u16(src);
	
	// Extract RGB components using bit manipulation
	// R: bits 11-15 (5 bits) → scale to 8 bits
	// G: bits 5-10 (6 bits) → scale to 8 bits  
	// B: bits 0-4 (5 bits) → scale to 8 bits
	
	uint8x8_t r5 = vmovn_u16(vshrq_n_u16(vandq_u16(rgb565, vdupq_n_u16(0xF800)), 11));
	uint8x8_t g6 = vmovn_u16(vshrq_n_u16(vandq_u16(rgb565, vdupq_n_u16(0x07E0)), 5));
	uint8x8_t b5 = vmovn_u16(vandq_u16(rgb565, vdupq_n_u16(0x001F)));
	
	// Scale 5-bit to 8-bit: (val * 255) / 31 ≈ (val << 3) | (val >> 2)
	// Scale 6-bit to 8-bit: (val * 255) / 63 ≈ (val << 2) | (val >> 4)
	uint8x8_t r8 = vorr_u8(vshl_n_u8(r5, 3), vshr_n_u8(r5, 2));
	uint8x8_t g8 = vorr_u8(vshl_n_u8(g6, 2), vshr_n_u8(g6, 4));
	uint8x8_t b8 = vorr_u8(vshl_n_u8(b5, 3), vshr_n_u8(b5, 2));
	uint8x8_t a8 = vdup_n_u8(0xFF);
	
	// Interleave RGBA
	uint8x8x4_t rgba;
	rgba.val[0] = r8;
	rgba.val[1] = g8;
	rgba.val[2] = b8;
	rgba.val[3] = a8;
	
	// Store as RGBA (32 bytes)
	vst4_u8((uint8_t*)dst, rgba);
}

// Convert 4 XRGB8888 pixels to RGBA using NEON (processes 16 bytes → 16 bytes)
static inline void convert_xrgb8888_to_rgba_neon(const uint32_t* __restrict src, uint32_t* __restrict dst) {
	// Load 4 XRGB8888 pixels
	uint32x4_t xrgb = vld1q_u32(src);
	
	// XRGB8888: 0xXXRRGGBB → RGBA: 0xAABBGGRR
	// Extract components
	uint32x4_t r = vandq_u32(vshrq_n_u32(xrgb, 16), vdupq_n_u32(0xFF));
	uint32x4_t g = vandq_u32(vshrq_n_u32(xrgb, 8), vdupq_n_u32(0xFF));
	uint32x4_t b = vandq_u32(xrgb, vdupq_n_u32(0xFF));
	uint32x4_t a = vdupq_n_u32(0xFF);
	
	// Reconstruct as RGBA
	uint32x4_t rgba = vorrq_u32(vorrq_u32(r, vshlq_n_u32(g, 8)), vorrq_u32(vshlq_n_u32(b, 16), vshlq_n_u32(a, 24)));
	
	vst1q_u32(dst, rgba);
}
#endif

// Convert XRGB8888 to RGBA format (handles pitch correctly)
static void convert_xrgb8888_to_rgba(const void* src, uint32_t* dst, unsigned width, unsigned height, size_t pitch) {
	const uint32_t* srcData = (const uint32_t*)src;
	unsigned srcPitchInPixels = pitch / sizeof(uint32_t);
	
	for (unsigned y = 0; y < height; y++) {
		const uint32_t* srcRow = srcData + y * srcPitchInPixels;
		uint32_t* dstRow = dst + y * width;
		unsigned x = 0;

#if defined(__ARM_NEON) || defined(__aarch64__)
		// NEON: process 4 pixels at a time
		for (; x + 3 < width; x += 4) {
			convert_xrgb8888_to_rgba_neon(srcRow + x, dstRow + x);
		}
#else
		// Scalar: process 4 pixels at a time for better cache utilization
		for (; x + 3 < width; x += 4) {
			uint32_t p0 = srcRow[x], p1 = srcRow[x+1], p2 = srcRow[x+2], p3 = srcRow[x+3];
			
			// Swizzle: XRGB -> RGBA (swap R and B, set A=0xFF)
			dstRow[x]   = (p0 & 0x0000FF00) | ((p0 & 0x00FF0000) >> 16) | ((p0 & 0x000000FF) << 16) | 0xFF000000;
			dstRow[x+1] = (p1 & 0x0000FF00) | ((p1 & 0x00FF0000) >> 16) | ((p1 & 0x000000FF) << 16) | 0xFF000000;
			dstRow[x+2] = (p2 & 0x0000FF00) | ((p2 & 0x00FF0000) >> 16) | ((p2 & 0x000000FF) << 16) | 0xFF000000;
			dstRow[x+3] = (p3 & 0x0000FF00) | ((p3 & 0x00FF0000) >> 16) | ((p3 & 0x000000FF) << 16) | 0xFF000000;
		}
#endif
		// Handle remaining pixels in the row
		for (; x < width; x++) {
			uint32_t pixel = srcRow[x];
			dstRow[x] = (pixel & 0x0000FF00) | ((pixel & 0x00FF0000) >> 16) | ((pixel & 0x000000FF) << 16) | 0xFF000000;
		}
	}
}

// Convert RGB565 to RGBA format (handles pitch correctly)
static void convert_rgb565_to_rgba(const void* src, uint32_t* dst, unsigned width, unsigned height, size_t pitch) {
	const uint16_t* srcData = (const uint16_t*)src;
	unsigned srcPitchInPixels = pitch / sizeof(uint16_t);
	
	for (unsigned y = 0; y < height; y++) {
		const uint16_t* srcRow = srcData + y * srcPitchInPixels;
		uint32_t* dstRow = dst + y * width;
		unsigned x = 0;

#if defined(__ARM_NEON) || defined(__aarch64__)
		// NEON: process 8 pixels at a time
		for (; x + 7 < width; x += 8) {
			convert_rgb565_to_rgba_neon(srcRow + x, dstRow + x);
		}
#else
		// Scalar: process 4 pixels at a time
		for (; x + 3 < width; x += 4) {
			uint16_t p0 = srcRow[x], p1 = srcRow[x+1], p2 = srcRow[x+2], p3 = srcRow[x+3];
			
			uint8_t r0 = (p0 >> 11) & 0x1F, g0 = (p0 >> 5) & 0x3F, b0 = p0 & 0x1F;
			uint8_t r1 = (p1 >> 11) & 0x1F, g1 = (p1 >> 5) & 0x3F, b1 = p1 & 0x1F;
			uint8_t r2 = (p2 >> 11) & 0x1F, g2 = (p2 >> 5) & 0x3F, b2 = p2 & 0x1F;
			uint8_t r3 = (p3 >> 11) & 0x1F, g3 = (p3 >> 5) & 0x3F, b3 = p3 & 0x1F;
			
			r0 = (r0 << 3) | (r0 >> 2); g0 = (g0 << 2) | (g0 >> 4); b0 = (b0 << 3) | (b0 >> 2);
			r1 = (r1 << 3) | (r1 >> 2); g1 = (g1 << 2) | (g1 >> 4); b1 = (b1 << 3) | (b1 >> 2);
			r2 = (r2 << 3) | (r2 >> 2); g2 = (g2 << 2) | (g2 >> 4); b2 = (b2 << 3) | (b2 >> 2);
			r3 = (r3 << 3) | (r3 >> 2); g3 = (g3 << 2) | (g3 >> 4); b3 = (b3 << 3) | (b3 >> 2);
			
			dstRow[x]   = (0xFF << 24) | (b0 << 16) | (g0 << 8) | r0;
			dstRow[x+1] = (0xFF << 24) | (b1 << 16) | (g1 << 8) | r1;
			dstRow[x+2] = (0xFF << 24) | (b2 << 16) | (g2 << 8) | r2;
			dstRow[x+3] = (0xFF << 24) | (b3 << 16) | (g3 << 8) | r3;
		}
#endif
		// Handle remaining pixels in the row
		for (; x < width; x++) {
			uint16_t pixel = srcRow[x];
			uint8_t r = (pixel >> 11) & 0x1F;
			uint8_t g = (pixel >> 5) & 0x3F;
			uint8_t b = pixel & 0x1F;
			
			r = (r << 3) | (r >> 2);
			g = (g << 2) | (g >> 4);
			b = (b << 3) | (b >> 2);
			
			dstRow[x] = (0xFF << 24) | (b << 16) | (g << 8) | r;
		}
	}
}

/* Software-path frame accounting for the telemetry line: how many video_cb
 * calls carried new pixels vs. were dupes (data == NULL).  A core that skips
 * rendering (Yabadabadoo frameskip, mgba frameskip) shows up here as dupes;
 * the hw path has its own counters in ma_gl.c. */
static unsigned sw_video_new = 0, sw_video_dupe = 0;

void MA_video_take_frame_stats(unsigned *new_frames, unsigned *dupes) {
	*new_frames = sw_video_new;
	*dupes = sw_video_dupe;
	sw_video_new = sw_video_dupe = 0;
}

/* Software present-path cost: conversion + texture upload + draw + swap, i.e.
 * everything a headless harness does NOT do.  MINARCH_VIDEO_PROFILE=1 logs the
 * average once every 5 s next to the loop/present telemetry. */
static int swprof_on = -1;
static uint64_t swprof_sum_ns = 0;
static unsigned swprof_n = 0;
static uint32_t swprof_last_ms = 0;
static uint64_t swprof_now(void) {
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

void video_refresh_callback(const void* data, unsigned width, unsigned height, size_t pitch) {
	/* Diagnostic: MINARCH_SKIP_PRESENT=1 returns before any conversion/upload/
	 * draw, so the loop time with and without the frontend's video work can be
	 * separated on the device. */
	static int skip_present = -1;
	if (skip_present < 0) {
		const char *e = getenv("MINARCH_SKIP_PRESENT");
		skip_present = (e && e[0] && e[0] != '0') ? 1 : 0;
	}
	if (skip_present) return;

	// GLES hardware-render path: the core passes RETRO_HW_FRAME_BUFFER_VALID
	// and the frame lives in the GL framebuffer, not in a pixel buffer.
	if (MA_GL_is_active()) {
		MA_GL_video_refresh(data, width, height, pitch);
		return;
	}

	// Log NEON availability once on first call
	static int neon_logged = 0;
	if (!neon_logged) {
		neon_logged = 1;
#if defined(__ARM_NEON) || defined(__aarch64__)
		LOG_info("Pixel conversion: ARM NEON SIMD optimizations enabled\n");
#else
		LOG_info("Pixel conversion: Using scalar optimizations (NEON not available)\n");
#endif
	}

	// Early exit if quitting to avoid rendering stale frames
	if (quit) return;

	if (data) sw_video_new++; else sw_video_dupe++;

	// Allocate RGBA buffer if needed
	if (!rgbaData || rgbaDataSize != width * height) {
		if (rgbaData) free(rgbaData);
		rgbaDataSize = width * height;
		rgbaData = (Uint32*)malloc(rgbaDataSize * sizeof(Uint32));
		if (!rgbaData) {
			printf("Failed to allocate memory for RGBA data.\n");
			return;
		}
	}

	// Handle NULL data by reusing last frame
	if (!data) {
		data = lastframe;
		if (!data) return;
	} else {
		// Convert pixel format to RGBA
		if (fmt == RETRO_PIXEL_FORMAT_XRGB8888) {
			convert_xrgb8888_to_rgba(data, rgbaData, width, height, pitch);
		} else {
			convert_rgb565_to_rgba(data, rgbaData, width, height, pitch);
		}
		
		data = rgbaData;
		lastframe = data;
	}
	pitch = width * sizeof(Uint32);

	/* Diagnostic: MINARCH_DUMP_AT=<n> writes frame n (counted in video_cb
	 * calls, the same index the headless harness' YAB_DUMP_AT uses) as PPM so
	 * the two frontends can be proved to show the same scene. */
	{
		static int dump_at = -2;
		static unsigned dump_n = 0;
		if (dump_at == -2) {
			const char *e = getenv("MINARCH_DUMP_AT");
			dump_at = (e && e[0]) ? atoi(e) : -1;
		}
		if (dump_at >= 0 && (int)dump_n == dump_at) {
			char path[128];
			snprintf(path, sizeof(path), "/mnt/sdcard/mdump-%u.ppm", dump_n);
			FILE *f = fopen(path, "wb");
			if (f) {
				fprintf(f, "P6\n%u %u\n255\n", width, height);
				for (unsigned y = 0; y < height; y++) {
					const uint32_t *row = (const uint32_t *)((const unsigned char *)rgbaData + (size_t)y * pitch);
					for (unsigned x = 0; x < width; x++) {
						uint32_t p = row[x];
						fputc((p >> 16) & 0xff, f); fputc((p >> 8) & 0xff, f); fputc(p & 0xff, f);
					}
				}
				fclose(f);
				LOG_info("minarch: dumped %s (%ux%u)\n", path, width, height);
			}
		}
		dump_n++;
	}

	// Set ambient lighting color (if enabled)
	if (ambient_mode && !fast_forward && data) {
		GFX_setAmbientColor(data, width, height, pitch, ambient_mode);
	}


	// Render the frame
	{
		if (swprof_on < 0)
			swprof_on = getenv("MINARCH_VIDEO_PROFILE") ? 1 : 0;
		uint64_t t0 = swprof_on ? swprof_now() : 0;
		video_refresh_callback_main(data, width, height, pitch);
		if (swprof_on) {
			uint64_t t1 = swprof_now();
			swprof_sum_ns += t1 - t0;
			swprof_n++;
			uint32_t now_ms = (uint32_t)(t1 / 1000000ull);
			if (!swprof_last_ms) swprof_last_ms = now_ms;
			if (now_ms - swprof_last_ms >= 5000) {
				LOG_info("minarch: sw present %.2f ms avg over %u frames (convert+upload+draw+swap)\n",
						(double)swprof_sum_ns / 1e6 / (swprof_n ? swprof_n : 1), swprof_n);
				fflush(stdout);
				swprof_sum_ns = 0; swprof_n = 0; swprof_last_ms = now_ms;
			}
		}
	}
}

void Video_cleanup(void) {
	if (rgbaData) {
		free(rgbaData);
		rgbaData = NULL;
	}
}
