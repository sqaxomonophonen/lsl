#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#define STB_RECT_PACK_IMPLEMENTATION
#include "stb_rect_pack.h"

static void fread_line(FILE* f, char* buf, size_t sz)
{
	int n = 0;
	while (!feof(f)) {
		assert(fread(buf, 1, 1, f));
		if ((n+1) >= sz || *buf == '\n') {
			*buf = 0;
			return;
		}
		n++;
		buf++;
	}
	assert(0);
}

static void split2(char* str, char** part2)
{
	while (*str) {
		if (*str == ' ') {
			*str = 0;
			*part2 = str+1;
			return;
		}
		str++;
	}
}

static void hex2pixels(char* str, char* out, size_t sz)
{
	for (;;) {
		char ch = *str;
		if (!ch) break;
		int value = (ch >= '0' && ch <= '9') ? (ch-'0') : (ch >= 'A' && ch <= 'F') ? (10+ch-'A') : -1;
		for (int i = 3; i >= 0; i--) {
			*out++ = (value & (1<<i)) ? 255 : 0;
			sz--;
			if (sz == 0) return;
		}
		str++;
	}
}


#define N (3)

struct glyph {
	struct stbrp_rect rect;
	int codepoint;
	long bitmap_offset;
	int xoff;
	int yoff;
};

static int glyph_compar(const void* va, const void* vb)
{
	const struct glyph* a = va;
	const struct glyph* b = vb;
	return a->codepoint - b->codepoint;
}

static void blit(char* dst, int stride, char* src, int w, int h, int x0, int y0)
{
	for (int y = 0; y < h; y++) {
		for (int x = 0; x < w; x++) {
			dst[x + x0 + (y + y0)*stride] = src[x + y*w];
		}
	}
}

static void fwrite_s16(FILE* f, short v)
{
	fwrite(&v, sizeof(v), 1, f);
}

static void fwrite_s32(FILE* f, int v)
{
	fwrite(&v, sizeof(v), 1, f);
}

static int try_sz(int sz, char** filenames, char* outfile)
{
	printf("trying %d×%d...\n", sz, sz);

	char line[4096];
	struct glyph* glyphs[N];
	int n_glyphs[N];
	int heights[N];
	int baselines[N];

	// dot
	n_glyphs[0] = 1;
	heights[0] = 2;
	baselines[0] = 2;
	glyphs[0] = calloc(1, 1*sizeof(**glyphs));
	glyphs[0][0].rect.w = 3;
	glyphs[0][0].rect.h = 3;
	glyphs[0][0].codepoint = 0;

	int n_total_rects = 1; // 1 reserved for dot

	for (int i = 1; i < N; i++) {
		char* filename = filenames[i-1];
		FILE* f = fopen(filename, "r");
		assert(f);

		{
			fread_line(f, line, sizeof(line));

			char* arg;
			split2(line, &arg);

			assert(strcmp(line, "STARTFONT") == 0);
			assert(strcmp(arg, "2.1") <= 0);
		}

		for (;;) {
			fread_line(f, line, sizeof(line));
			char* arg;
			split2(line, &arg);

			if (strcmp(line, "FONTBOUNDINGBOX") == 0) {
				char* arg2;
				char* arg3;
				char* arg4;
				split2(arg, &arg2);
				split2(arg2, &arg3);
				split2(arg3, &arg4);
				heights[i] = atoi(arg2);
				baselines[i] = heights[i] + atoi(arg4);
				break;
			}
		}

		n_glyphs[i] = 0;

		for (;;) {
			fread_line(f, line, sizeof(line));
			char* arg;
			split2(line, &arg);

			if (strcmp(line, "CHARS") == 0) {
				n_glyphs[i] = atoi(arg);
				break;
			}
		}

		assert(n_glyphs[i]);
		n_total_rects += n_glyphs[i];

		glyphs[i] = calloc(1, n_glyphs[i] * sizeof(**glyphs));

		for (int j = 0; j < n_glyphs[i]; j++) {
			struct glyph* glyph = &glyphs[i][j];

			for (;;) {
				fread_line(f, line, sizeof(line));
				char* arg;
				split2(line, &arg);

				if (strcmp(line, "BITMAP") == 0) {
					glyph->bitmap_offset = ftell(f);
					break;
				} else if (strcmp(line, "ENCODING") == 0) {
					glyph->codepoint = atoi(arg);
				} else if (strcmp(line, "BBX") == 0) {
					char* arg2;
					char* arg3;
					char* arg4;
					split2(arg, &arg2);
					split2(arg2, &arg3);
					split2(arg3, &arg4);
					glyph->rect.w = atoi(arg) + 1;
					glyph->rect.h = atoi(arg2) + 1;
					glyph->xoff = atoi(arg3);
					glyph->yoff = -glyph->rect.h - atoi(arg4);
				}
			}
		}

		qsort(glyphs[i], n_glyphs[i], sizeof(**glyphs), glyph_compar);

		fclose(f);
	}

	struct stbrp_rect* all_rects = calloc(1, n_total_rects * sizeof(*all_rects));
	assert(all_rects);

	int offset = 1;
	for (int i = 0; i < N; i++) {
		for (int j = 0; j < n_glyphs[i]; j++) {
			memcpy(all_rects + offset, &glyphs[i][j].rect, sizeof(stbrp_rect));
			offset++;
		}
	}

	// pack rects using stb_rect_pack.h
	int n_nodes = sz;
	stbrp_node* nodes = calloc(1, n_nodes * sizeof(*nodes));
	assert(nodes);
	stbrp_context ctx;
	stbrp_init_target(&ctx, sz, sz, nodes, n_nodes);
	stbrp_pack_rects(&ctx, all_rects, n_total_rects);

	// bail if not all rects were packed
	for (int i = 0; i < n_total_rects; i++) if (!all_rects[i].was_packed) return 0;

	char* bitmap = calloc(sz*sz, 1);
	assert(bitmap);

	int ari = 0;
	char dot[] = {255, 255, 255, 255};
	blit(bitmap, sz, dot, 2, 2, all_rects[ari].x, all_rects[ari].y);
	ari++;
	for (int i = 1; i < N; i++) {
		char* filename = filenames[i-1];
		FILE* f = fopen(filename, "r");
		assert(f);

		for (int j = 0; j < n_glyphs[i]; j++) {
			struct glyph* gly = &glyphs[i][j];
			struct stbrp_rect* prect = &all_rects[ari++];
			assert(fseek(f, gly->bitmap_offset, SEEK_SET) == 0);

			for (int k = 0; k < (prect->h-1); k++) {
				fread_line(f, line, sizeof(line));
				char row[1024];
				hex2pixels(line, row, sizeof(row));
				blit(bitmap, sz, row, (prect->w-1), 1, prect->x, prect->y + k);
			}
		}

		fclose(f);
	}

	FILE* f = fopen(outfile, "wb");

	fwrite("ATLS", 4, 1, f);
	fwrite_s32(f, 1); // version
	fwrite_s32(f, sz); // width
	fwrite_s32(f, sz); // height
	fwrite_s32(f, N); // number of types

	// write number of glyphs for each type
	for (int i = 0; i < N; i++) {
		fwrite_s32(f, n_glyphs[i]);
		fwrite_s16(f, heights[i]);
		fwrite_s16(f, baselines[i]);
	}

	// write glyph data
	ari = 0;
	for (int i = 0; i < N; i++) {
		for (int j = 0; j < n_glyphs[i]; j++) {
			fwrite_s32(f, glyphs[i][j].codepoint);
			fwrite_s16(f, all_rects[ari].w - 1);
			fwrite_s16(f, all_rects[ari].h - 1);
			fwrite_s16(f, all_rects[ari].x);
			fwrite_s16(f, all_rects[ari].y);
			fwrite_s16(f, glyphs[i][j].xoff);
			fwrite_s16(f, glyphs[i][j].yoff);
			ari++;
		}
	}


	// write bitmap
	fwrite(bitmap, sz*sz, 1, f);

	fclose(f);

	return 1;
}

int main(int argc, char** argv)
{
	if (argc != 4) {
		fprintf(stderr, "usage: %s <font.bdf> <symbols.bdf> <out.atlas>\n", argv[0]);
		exit(EXIT_FAILURE);
	}

	for (int sz = 512; sz <= 8192; sz <<= 1) {
		if (try_sz(sz, argv+1, argv[3])) {
			exit(EXIT_SUCCESS);
		}
	}

	exit(EXIT_FAILURE);
}
