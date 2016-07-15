#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lsl_prg.h"

#define ASSERT(cond) \
	do { \
		if (!(cond)) { \
			fprintf(stderr, "ASSERT(%s) failed in %s() in %s:%d\n", #cond, __func__, __FILE__, __LINE__); \
			exit(EXIT_FAILURE); \
		} \
	} while (0)

#define AN(expr) do { ASSERT((expr) != 0); } while(0)

char* atlas_file;
unsigned int type_index;
int cursor_x;
int cursor_x0;
int cursor_y;
union lsl_vec4 draw_color0;
union lsl_vec4 draw_color1;

int atlas_width;
int atlas_height;
unsigned int n_types;

struct glyph {
	short x,y,w,h,xoff,yoff;
};

struct type {
	int n_glyphs;
	int height;
	int baseline;
	int* codepoints;
	struct glyph* glyphs;
}* types;

static inline int utf8_decode(char** c0z, int* n)
{
	unsigned char** c0 = (unsigned char**)c0z;
	if (*n <= 0) return -1;
	unsigned char c = **c0;
	(*n)--;
	(*c0)++;
	if ((c & 0x80) == 0) return c & 0x7f;
	int mask = 192;
	for (int d = 1; d <= 3; d++) {
		int match = mask;
		mask = (mask >> 1) | 0x80;
		if ((c & mask) == match) {
			int codepoint = (c & ~mask) << (6*d);
			while (d > 0 && *n > 0) {
				c = **c0;
				if ((c & 192) != 128) return -1;
				(*c0)++;
				(*n)--;
				d--;
				codepoint += (c & 63) << (6*d);
			}
			return d == 0 ? codepoint : -1;
		}
	}
	return -1;
}

static short fread_s16(FILE* f)
{
	short result;
	// TODO XXX convert from little-endian if necessary
	ASSERT(fread(&result, 1, sizeof(result), f) == sizeof(result));
	return result;
}

static int fread_s32(FILE* f)
{
	int result;
	// TODO XXX convert from little-endian if necessary
	ASSERT(fread(&result, 1, sizeof(result), f) == sizeof(result));
	return result;
}

static char* load_atlas()
{
	AN(atlas_file);
	FILE* f = fopen(atlas_file, "rb");
	AN(f);
	char buf[4096];

	fread(buf, 4, 1, f);
	ASSERT(memcmp(buf, "ATLS", 4) == 0);

	int version = fread_s32(f);
	ASSERT(version == 1);

	atlas_width = fread_s32(f);
	atlas_height = fread_s32(f);
	n_types = fread_s32(f);

	AN(types = malloc(n_types * sizeof(*types)));
	for (int i = 0; i < n_types; i++) {
		struct type* t = &types[i];
		t->n_glyphs = fread_s32(f);
		t->height = fread_s16(f);
		t->baseline = fread_s16(f);
		t->codepoints = malloc(t->n_glyphs * sizeof(*t->codepoints));
		t->glyphs = malloc(t->n_glyphs * sizeof(*t->glyphs));
	}

	for (int i = 0; i < n_types; i++) {
		struct type* t = &types[i];
		for (int j = 0; j < t->n_glyphs; j++) {
			t->codepoints[j] = fread_s32(f);
			struct glyph* gly = &t->glyphs[j];
			gly->w = fread_s16(f);
			gly->h = fread_s16(f);
			gly->x = fread_s16(f);
			gly->y = fread_s16(f);
			gly->xoff = fread_s16(f);
			gly->yoff = fread_s16(f);
		}
	}

	size_t bitmap_sz = atlas_width * atlas_height;
	char* bitmap = malloc(bitmap_sz);
	AN(bitmap);
	ASSERT(fread(bitmap, bitmap_sz, 1, f) == 1);

	fclose(f);

	return bitmap;
}

static void draw_glyph(struct glyph*);

void lsl_putch(int codepoint)
{
	if (type_index > n_types) return;

	struct type* t = &types[type_index];

	if (codepoint == '\n') {
		cursor_x = cursor_x0;
		cursor_y += t->height;
		return;
	}

	// binary search for codepoint
	int imin = 0;
	int imax = t->n_glyphs - 1;
	while (imin < imax) {
		int imid = (imin + imax) >> 1;
		if (t->codepoints[imid] < codepoint) {
			imin = imid + 1;
		} else {
			imax = imid;
		}
	}
	if (t->codepoints[imin] != codepoint) {
		return;
	}

	int i = imin;
	struct glyph* gly = &t->glyphs[i];

	draw_glyph(gly);

	cursor_x += gly->w;
}

void lsl_set_atlas(char* f)
{
	atlas_file = f;
}

void lsl_set_type_index(unsigned int i)
{
	type_index = i;
}

void lsl_set_cursor(int x, int y)
{
	cursor_x = cursor_x0 = x;
	cursor_y = y;
}

void lsl_set_vertical_gradient(union lsl_vec4 color0, union lsl_vec4 color1)
{
	draw_color0 = color0;
	draw_color1 = color1;
}

void lsl_set_color(union lsl_vec4 color)
{
	lsl_set_vertical_gradient(color, color);
}

void lsl_putch(int codepoint);

int lsl_printf(const char* fmt, ...)
{
	char buf[8192];
	va_list ap;
	va_start(ap, fmt);
	int nret = vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);

	int n = nret;
	char* c = buf;
	while (n > 0) {
		int codepoint = utf8_decode(&c, &n);
		if (codepoint == -1) return -1;
		lsl_putch(codepoint);
	}

	return nret;
}

#ifdef USE_GLX11
#include "lsl_prg_glx11.h"
#else
#error "missing lsl define/implementation (2)"
#endif

