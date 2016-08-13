#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "lsl_prg.h"
#include "l4d.h"

struct lsl_rect empty_rect;
struct l4d l4d;

enum window_type {
	WINDOW_TIMELINE = 0,
	WINDOW_GRAPH
};

struct window_timeline {
};

struct window_graph {
	int pdrag;
	int px, py;
	struct l4d_container* container;
};

struct window {
	unsigned int layout; // 6 different?
	int editor_width, editor_height;

	struct lsl_rect main_rect, editor_rect;
	int handle_drag_id;

	enum window_type type;
	union {
		struct window_timeline timeline;
		struct window_graph graph;
	};

	struct window* next;
}* windows;

static int winproc_graph(struct window* w)
{
	struct window_graph* wg = &w->graph;

	if (lsl_rect_not_empty(&w->main_rect)) {
		lsl_frame_push_clip(&w->main_rect);
		lsl_set_color((union lsl_vec4) { .r = 0, .g = 0.2, .b = 0.3, .a = 1 });
		lsl_clear();

		lsl_set_color((union lsl_vec4) { .r = 1, .g = 0, .b = 1, .a = 1 });

		for (int i = 0; i < wg->container->nodes_dy.n; i++) {
			struct l4d_node* n = &wg->container->nodes[i];
			struct lsl_rect r = (struct lsl_rect) { .p0 = { .x = n->meta.x - wg->px , .y = n->meta.y - wg->py }, .dim = { .w = 40, .h = 20 } };

			lsl_fill_rect(&r);
			lsl_drag(&r, &n->meta.iusr0, &n->meta.x, &n->meta.y, 1, 1);
		}

		lsl_drag(NULL, &wg->pdrag, &wg->px, &wg->py, -1, -1);
		lsl_frame_pop();
	}

	if (lsl_rect_not_empty(&w->editor_rect)) {
		lsl_frame_push_clip(&w->editor_rect);
		lsl_set_color((union lsl_vec4) { .r = 0.2, .g = 0.0, .b = 0.3, .a = 1 });
		lsl_clear();
		lsl_frame_pop();
	}

	return lsl_frame_top()->button[2];
}

#define HANDLE_SIZE (4)

static void split_vertical(struct lsl_rect* r0, int mirror, int height, struct lsl_rect* a, struct lsl_rect* mid, struct lsl_rect* b)
{
	if (mirror) {
		lsl_rect_split_vertical(r0, height, b, mid);
		lsl_rect_split_vertical(mid, HANDLE_SIZE, mid, a);
	} else {
		lsl_rect_split_vertical(r0, r0->dim.h - height - HANDLE_SIZE, a, mid);
		lsl_rect_split_vertical(mid, HANDLE_SIZE, mid, b);
	}
}

static void split_horizontal(struct lsl_rect* r0, int mirror, int width, struct lsl_rect* a, struct lsl_rect* mid, struct lsl_rect* b)
{
	if (mirror) {
		lsl_rect_split_horizontal(r0, width, b, mid);
		lsl_rect_split_horizontal(mid, HANDLE_SIZE, mid, a);
	} else {
		lsl_rect_split_horizontal(r0, r0->dim.w - width - HANDLE_SIZE, a, mid);
		lsl_rect_split_horizontal(mid, HANDLE_SIZE, mid, b);
	}
}

#define AREA_MIN_SIZE (80)

static void constrain_area(int* size, int area_size)
{
	if (*size < AREA_MIN_SIZE) *size = AREA_MIN_SIZE;
	int max = area_size - AREA_MIN_SIZE - HANDLE_SIZE;
	if (*size > max) *size = max;
}

static int winproc(void* usr)
{
	struct window* win = (struct window*) usr;

	struct lsl_frame* f = lsl_frame_top();

	if (f->text_length) {
		win->layout = (win->layout + 1) % 6;
	}

	struct lsl_rect handle_rect;

	int* handle_x = NULL;
	int* handle_y = NULL;
	int handle_fx = 0;
	int handle_fy = 0;

	int mirror = win->layout & 2;

	if (win->layout < 4) {
		if (win->layout & 1) {
			split_vertical(&f->rect, mirror, win->editor_height, &win->main_rect, &handle_rect, &win->editor_rect);
			handle_y = &win->editor_height;
			handle_fy = mirror ? 1 : -1;
		} else {
			split_horizontal(&f->rect, mirror, win->editor_width, &win->main_rect, &handle_rect, &win->editor_rect);
			handle_x = &win->editor_width;
			handle_fx = mirror ? 1 : -1;
		}
	} else if (win->layout == 4) {
		win->main_rect = f->rect;
		win->editor_rect = empty_rect;
	} else if (win->layout == 5) {
		win->main_rect = empty_rect;
		win->editor_rect = f->rect;
	} else {
		assert(0);
	}

	lsl_drag(&handle_rect, &win->handle_drag_id, handle_x, handle_y, handle_fx, handle_fy);
	constrain_area(&win->editor_width, f->rect.dim.w);
	constrain_area(&win->editor_height, f->rect.dim.h);

	int retval = 0;

	switch (win->type) {
		case WINDOW_TIMELINE:
			break;
		case WINDOW_GRAPH:
			retval = winproc_graph(win);
	}

	lsl_frame_push_clip(&handle_rect);
	lsl_set_color((union lsl_vec4) { .r = 1, .g = 1, .b = 1, .a = 1 });
	lsl_clear();
	lsl_frame_pop();

	return retval;
}


static void set_window_graph_defaults(struct window* w)
{
	w->layout = 0;
	w->editor_width = 480;
	w->editor_height = 320;
	w->type = WINDOW_GRAPH;
	w->graph.container = l4d.root_container;
}

static struct window* clone_win(struct window* ow)
{
	struct window* w = malloc(sizeof(*w));
	if (ow == NULL) {
		memset(w, 0, sizeof(*w));
		set_window_graph_defaults(w);
	} else {
		memcpy(w, ow, sizeof(*w));
	}
	w->next = windows;
	windows = w;
	lsl_win_open("l4", winproc, w);
	return w;
}

int lsl_main(int argc, char** argv)
{
	l4d_init(&l4d);

	lsl_set_atlas("default.atls");

	clone_win(NULL);

	lsl_main_loop();
	return 0;
}
