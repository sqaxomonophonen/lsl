#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

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

#define FRAME_STACK_MAX (16)
struct lsl_frame frame_stack[FRAME_STACK_MAX];
int frame_stack_top_index;

static void frame_stack_reset(struct lsl_frame* f)
{
	frame_stack_top_index = 0;
	frame_stack[0] = *f;
}

static void assert_valid_frame_stack_top(int i)
{
	assert(i >= 0 && i < FRAME_STACK_MAX);
}

struct lsl_frame* lsl_frame_top()
{
	return &frame_stack[frame_stack_top_index];
}


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

union lsl_vec2 lsl_vec2_add(union lsl_vec2 a, union lsl_vec2 b)
{
	union lsl_vec2 r;
	for (int i = 0; i < 2; i++) r.s[i] = a.s[i] + b.s[i];
	return r;
}

union lsl_vec2 lsl_vec2_sub(union lsl_vec2 a, union lsl_vec2 b)
{
	union lsl_vec2 r;
	for (int i = 0; i < 2; i++) r.s[i] = a.s[i] - b.s[i];
	return r;
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

int lsl_rect_not_empty(struct lsl_rect* r)
{
	return r->dim.w > 0 && r->dim.h > 0;
}


int lsl_rect_contains_point(struct lsl_rect* rect, union lsl_vec2 point)
{
	int inside = 1;
	for (int axis = 0; axis < 2; axis++) {
		inside &=
			point.s[axis] >= rect->p0.s[axis]
			&& point.s[axis] < (rect->p0.s[axis] + rect->dim.s[axis]);
	}
	return inside;
}

void lsl_rect_split_vertical(struct lsl_rect* r, int height, struct lsl_rect* a, struct lsl_rect* b)
{
	struct lsl_rect cp = *r;
	if (a != NULL) *a = (struct lsl_rect) { .p0 = cp.p0, .dim = { .w = cp.dim.w, .h = height }};
	if (b != NULL) *b = (struct lsl_rect) { .p0 = { .x = cp.p0.x, .y = cp.p0.y + height }, .dim = { .w = cp.dim.w, .h = cp.dim.h - height }};
}

void lsl_rect_split_horizontal(struct lsl_rect* r, int width, struct lsl_rect* a, struct lsl_rect* b)
{
	struct lsl_rect cp = *r;
	if (a != NULL) *a = (struct lsl_rect) { .p0 = cp.p0, .dim = { .w = width, .h = cp.dim.h }};
	if (b != NULL) *b = (struct lsl_rect) { .p0 = { .x = cp.p0.x + width, .y = cp.p0.y }, .dim = { .w = cp.dim.w - width, .h = cp.dim.h }};
}

static void set_vh_pointer(int xp, int yp)
{
	if (xp && !yp) {
		lsl_set_pointer(LSL_POINTER_HORIZONTAL);
	} else if (!xp && yp) {
		lsl_set_pointer(LSL_POINTER_VERTICAL);
	} else if (xp && yp) {
		lsl_set_pointer(LSL_POINTER_4WAY);
	}
}

int drag_active_id;
int drag_initial_x;
int drag_initial_y;
int drag_initial_mx;
int drag_initial_my;
int drag_next_id = 1;

int lsl_drag(struct lsl_rect* r, int* drag_id, int* x, int* y, int fx, int fy)
{
	if (x == NULL && y == NULL) return 0;

	struct lsl_frame* f = lsl_frame_top();

	int btn = f->button[0];
	int retval = 0;

	if (drag_active_id) {
		if (drag_active_id != *drag_id) return 0;

		set_vh_pointer(x != NULL, y != NULL);

		retval = LSL_DRAG_CONT;
		if (!btn) {
			lsl_set_pointer(0);
			*drag_id = drag_active_id = 0;
			retval = LSL_DRAG_STOP;
		}
		if (x != NULL) *x = drag_initial_x + (f->mpos.x - drag_initial_mx) * fx;
		if (y != NULL) *y = drag_initial_y + (f->mpos.y - drag_initial_my) * fy;
	} else if (r == NULL || lsl_rect_contains_point(r, f->mpos)) {
		if (r != NULL) set_vh_pointer(x != NULL, y != NULL);

		if (btn) {
			drag_active_id = *drag_id = drag_next_id++;
			retval = LSL_DRAG_START;
			if (x != NULL) drag_initial_x = *x;
			if (y != NULL) drag_initial_y = *y;
			drag_initial_mx = f->mpos.x;
			drag_initial_my = f->mpos.y;
		}
	}
	return retval;
}

void lsl_frame_push_clip(struct lsl_rect* r)
{
	struct lsl_frame* src = &frame_stack[frame_stack_top_index];

	assert_valid_frame_stack_top(++frame_stack_top_index);
	struct lsl_frame* dst = &frame_stack[frame_stack_top_index];
	memcpy(dst, src, sizeof(*dst));
	dst->rect = (struct lsl_rect) { .p0 = lsl_vec2_add(src->rect.p0, r->p0), .dim = r->dim };

	dst->mpos = lsl_vec2_sub(dst->mpos, r->p0);
	if (!lsl_rect_contains_point(r, src->mpos)) {
		// XXX what about dragging?
		dst->minside = 0;
		memset(&dst->button, 0, sizeof(dst->button));
		memset(&dst->button_cycles, 0, sizeof(dst->button_cycles));
	}
}

void lsl_frame_pop()
{
	assert_valid_frame_stack_top(--frame_stack_top_index);
}

#ifdef USE_GLX11
#include "lsl_prg_glx11.h"
#else
#error "missing lsl define/implementation (2)"
#endif

