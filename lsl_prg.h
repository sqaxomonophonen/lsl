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
};

union lsl_vec4 {
	float s[4];
	struct { float x, y, z, w; };
	struct { float r, g, b, a; };
};

struct lsl_frame {
	int w, h;

	int mx, my;
	int button[LSL_MAX_BUTTONS];
	int button_cycles[LSL_MAX_BUTTONS];
	int mod;
	int text_length;
	char text[LSL_MAX_TEXT_LENGTH + 1]; /* UTF-8 */
};

int lsl_main(int argc, char** argv);

void lsl_set_atlas(char* f);
void lsl_set_type_index(unsigned int index);
void lsl_set_cursor(int x, int y);
void lsl_set_vertical_gradient(union lsl_vec4 color0, union lsl_vec4 color1);
void lsl_set_color(union lsl_vec4 color);
void lsl_putch(int codepoint);
int lsl_printf(const char* fmt, ...);
void lsl_win_open(const char* title, int(*proc)(struct lsl_frame*, void*), void* usr);
void lsl_main_loop();

#define LSL_PRG_H
#endif
