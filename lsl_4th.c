#include <stdio.h>

#include "lsl_prg.h"

int winproc(struct lsl_frame* f, void* usr)
{
	if (f->text_length) {
		printf("(%d) [%s] (mod=%d)\n", f->text[0], f->text, f->mod);
	}

	lsl_set_type_index(1);
	lsl_set_cursor(50, 20);
	lsl_set_vertical_gradient(
		(union lsl_vec4) { .r=1,   .g=1,   .b=1,   .a=1},
		(union lsl_vec4) { .r=0.5, .g=0.5, .b=0.5, .a=0.5}
	);
	lsl_printf("helloooo\nand good morning ");
	lsl_set_type_index(2);
	lsl_set_color((union lsl_vec4) { .r=1, .g=1, .b=1, .a=1});
	lsl_printf("world!");

	return f->button[0];
}

int lsl_main(int argc, char** argv)
{
	lsl_set_atlas("default.atls");

	lsl_win_open("lsl-4th", winproc, NULL);

	lsl_main_loop();

	return 0;
}
