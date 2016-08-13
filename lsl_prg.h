#ifndef LSL_PRG_H

#define LSL_MAX_BUTTONS (5)
#define LSL_MAX_TEXT_LENGTH (31)

#define LSL_MOD_LSHIFT (1<<0)
#define LSL_MOD_RSHIFT (1<<1)
#define LSL_MOD_LCTRL (1<<2)
#define LSL_MOD_RCTRL (1<<3)
#define LSL_MOD_LALT (1<<4)
#define LSL_MOD_RALT (1<<5)

union lsl_vec2 {
	float s[2];
	struct { float x, y; };
	struct { float u, v; };
	struct { float w, h; };
};

union lsl_vec4 {
	float s[4];
	struct { float x, y, z, w; };
	struct { float r, g, b, a; };
};

struct lsl_rect {
	union lsl_vec2 p0, dim;
};

union lsl_vec2 lsl_vec2_add(union lsl_vec2 a, union lsl_vec2 b);
union lsl_vec2 lsl_vec2_sub(union lsl_vec2 a, union lsl_vec2 b);

int lsl_rect_not_empty(struct lsl_rect* r);
int lsl_rect_contains_point(struct lsl_rect* r, union lsl_vec2 point);
void lsl_rect_split_vertical(struct lsl_rect* r, int height, struct lsl_rect* a, struct lsl_rect* b);
void lsl_rect_split_horizontal(struct lsl_rect* r, int width, struct lsl_rect* a, struct lsl_rect* b);
struct lsl_rect lsl_rect_intersection(struct lsl_rect a, struct lsl_rect b);

struct lsl_frame {
	struct lsl_rect rect;
	int minside;
	union lsl_vec2 mpos;
	int button[LSL_MAX_BUTTONS];
	int button_cycles[LSL_MAX_BUTTONS];
	int mod;
	int text_length;
	char text[LSL_MAX_TEXT_LENGTH + 1]; /* UTF-8 */
};

struct lsl_frame* lsl_frame_top();

int lsl_main(int argc, char** argv);

void lsl_set_atlas(char* f);
void lsl_set_type_index(unsigned int index);
void lsl_set_cursor(int x, int y);
void lsl_set_vertical_gradient(union lsl_vec4 color0, union lsl_vec4 color1);
void lsl_set_color(union lsl_vec4 color);
void lsl_putch(int codepoint);
int lsl_printf(const char* fmt, ...);
void lsl_fill_rect(struct lsl_rect*);
void lsl_clear();
void lsl_win_open(const char* title, int(*proc)(void*), void* usr);
void lsl_main_loop();

void lsl_frame_push_clip(struct lsl_rect* r);
void lsl_frame_pop();

#define LSL_POINTER_HORIZONTAL (1)
#define LSL_POINTER_VERTICAL (2)
#define LSL_POINTER_4WAY (4)
void lsl_set_pointer(int);


#define LSL_DRAG_START (1)
#define LSL_DRAG_CONT (2)
#define LSL_DRAG_STOP (3)

/*
dragging handler

args:
 handle: area from which dragging can start. not updated by lsl_drag(), nor
 evaluted while dragging

 id: used to distinguish lsl_drag() calls. *id should be 0 before the first
 call, otherwise you can safely ignore it

 x/y: intptrs to be updated as a result of dragging (you can pass NULLs for
 vertical/horizontal dragging)

 fx/fy: factors to multiply on x/y. useful for mirroring or scaling.

returns 0 when not dragging, LSL_DRAG_START on the first frame of dragging,
LSL_DRAG_STOP on the last frame of dragging, and LSL_DRAG_CONT between those
two.

subtleties:
 - x/y NULLs or not-NULLs are also used for picking an appropriate cursor while
   hovering (i.e. vertical/horizontal/4-way cursor)
 - you can modify *x and *y as you please while dragging to implement
   constraints, e.g. min/max bounds, or snapping (it won't "confuse"
   lsl_drag(); values are calculated from inital values, i.e. not set relative
   to their current values)
 - you should call lsl_drag() for every frame while dragging, at least until
   LSL_DRAG_STOP (TODO I might want an lsl_drag_cancel(int* id, int* x, int*
   y); in case I want to "escape" this)

*/
int lsl_drag(struct lsl_rect* handle, int* id, int* x, int* y, int fx, int fy);

#define LSL_PRG_H
#endif
