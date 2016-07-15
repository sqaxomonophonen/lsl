#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "lsl_prg.h"

#define DYNARR_INITIAL_CAP (1<<12)
struct dynarr_info {
	int cap;
	int n;
	size_t sz;
	void** data;
};

static inline void dynarr_init(struct dynarr_info* info, size_t sz, void** data)
{
	info->cap = DYNARR_INITIAL_CAP;
	assert(sz <= info->cap);
	assert(*data = malloc(info->cap));
	info->n = 0;
	info->sz = sz;
	info->data = data;
}
#define DYNARR_INIT(expr) dynarr_init(&expr ## _di, sizeof(*expr), (void**)&expr)

static inline void dynarr_set_n(struct dynarr_info* info, int n)
{
	int required_cap = n * info->sz;
	int cap_before = info->cap;

	// grow
	while (required_cap > info->cap) info->cap <<= 2;

	// shrink
	while ((info->cap >> 2) >= DYNARR_INITIAL_CAP && (info->cap >> 2) > required_cap) info->cap >>= 2;

	if (info->cap != cap_before) {
		assert(*info->data = realloc(*info->data, info->cap));
	}
	info->n = n;
}

static inline void* dynarr_set_item(struct dynarr_info* info, int index, void* item)
{
	if (item == NULL) {
		memset(*info->data + index, 0, info->sz);
	} else {
		memcpy(*info->data + index * info->sz, item, info->sz);
	}
	return *info->data + index;
}

static inline void* dynarr_append(struct dynarr_info* info, void* item)
{
	dynarr_set_n(info, info->n + 1);
	return dynarr_set_item(info, info->n - 1, item);
}

static inline void dynarr_delete(struct dynarr_info* info, int index)
{
	assert(index >= 0 && index < info->n);
	int to_move = info->n - index - 1;
	if (to_move > 0) {
		memmove(
			*info->data + index * info->sz,
			*info->data + (index+1) * info->sz,
			to_move * info->sz);
	}
	dynarr_set_n(info, info->n - 1);
}

static inline void* dynarr_insert(struct dynarr_info* info, void* item, int index)
{
	dynarr_set_n(info, info->n + 1);
	assert(index >= 0 && index < info->n);
	int to_move = info->n - index - 1;
	if (to_move > 0) {
		memmove(
			*info->data + (index+1) * info->sz,
			*info->data + index * info->sz,
			to_move * info->sz);
	}
	return dynarr_set_item(info, index, item);
}


struct deck {
	float gain;

	struct dynarr_info routes_di;
	int* routes;

	struct dynarr_info code_di;
	char* code;
};

enum view {
	VIEW_TIMELINE = 1,
	VIEW_EDITOR,
	VIEW_VERTICAL_TIMELINE_TOP,
	VIEW_VERTICAL_EDITOR_TOP,
	VIEW_HORIZONTAL_TIMELINE_LEFT,
	VIEW_HORIZONTAL_EDITOR_LEFT
};

struct {
	enum view view;
	int view_editor_size;

	struct dynarr_info decks_di;
	struct deck* decks;
} state;

static struct deck* insert_deck(int index)
{
	struct deck* d = dynarr_insert(&state.decks_di, NULL, index);
	d->gain = 1;
	DYNARR_INIT(d->routes);
	DYNARR_INIT(d->code);
	return d;
}

static void new_song()
{
	DYNARR_INIT(state.decks);
	insert_deck(0);
}

static void view_timeline(struct lsl_frame* f, struct lsl_rect rect)
{
	lsl_begin(&rect);
	lsl_set_color((union lsl_vec4) { .r = 1, .g = 0, .b = 0, .a = 1 });
	lsl_clear();
	lsl_set_type_index(1);
	lsl_set_cursor(10, 20);
	lsl_set_color((union lsl_vec4) { .r=1, .g=1, .b=1, .a=1});
	lsl_printf("timeline");
	lsl_end();
}

static void view_editor(struct lsl_frame* f, struct lsl_rect rect)
{
	lsl_begin(&rect);
	lsl_set_color((union lsl_vec4) { .r = 0, .g = 1, .b = 0, .a = 1 });
	lsl_clear();
	lsl_set_type_index(1);
	lsl_set_cursor(10, 20);
	lsl_set_color((union lsl_vec4) { .r=1, .g=1, .b=0, .a=1});
	lsl_printf("editor");
	lsl_end();
}

static int winproc(struct lsl_frame* f, void* usr)
{
	switch (state.view) {
		case VIEW_TIMELINE:
			view_timeline(f, (struct lsl_rect) { .x = 0, .y = 0, .w = f->w, .h = f->h });
			break;
		case VIEW_EDITOR:
			view_editor(f, (struct lsl_rect) { .x = 0, .y = 0, .w = f->w, .h = f->h });
			break;
		case VIEW_VERTICAL_TIMELINE_TOP:
			view_timeline(f, (struct lsl_rect) { .x = 0, .y = 0, .w = f->w, .h = f->h - state.view_editor_size });
			view_editor(f, (struct lsl_rect) { .x = 0, .y = f->h - state.view_editor_size, .w = f->w, .h = state.view_editor_size });
			break;
		case VIEW_VERTICAL_EDITOR_TOP:
			view_editor(f, (struct lsl_rect) { .x = 0, .y = 0, .w = f->w, .h = f->h - state.view_editor_size });
			view_timeline(f, (struct lsl_rect) { .x = 0, .y = f->h - state.view_editor_size, .w = f->w, .h = state.view_editor_size });
			break;
		case VIEW_HORIZONTAL_TIMELINE_LEFT:
			view_timeline(f, (struct lsl_rect) { .x = 0, .y = 0, .w = f->w - state.view_editor_size, .h = f->h });
			view_editor(f, (struct lsl_rect) { .x = f->w - state.view_editor_size, .y = 0, .w = state.view_editor_size, .h = f->h });
			break;
		case VIEW_HORIZONTAL_EDITOR_LEFT:
			view_editor(f, (struct lsl_rect) { .x = 0, .y = 0, .w = f->w - state.view_editor_size, .h = f->h });
			view_timeline(f, (struct lsl_rect) { .x = f->w - state.view_editor_size, .y = 0, .w = state.view_editor_size, .h = f->h });
			break;
	}

	return f->button[0];
}

int lsl_main(int argc, char** argv)
{
	new_song();
	state.view = VIEW_VERTICAL_TIMELINE_TOP;
	state.view_editor_size = 100;

	lsl_set_atlas("default.atls");

	lsl_win_open("lsl-4th", winproc, NULL);

	lsl_main_loop();

	return 0;
}
