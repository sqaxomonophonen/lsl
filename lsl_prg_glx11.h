/*
#if __STDC_VERSION__ >= 199901L
#define _XOPEN_SOURCE 600
#else
#define _XOPEN_SOURCE 500
#endif
*/

#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include <X11/Xlib.h>
#include <X11/XKBlib.h>

#define GL_GLEXT_PROTOTYPES
#include <GL/glx.h>


#define MAX_VERTICES (1<<16)
#define MAX_ELEMENTS (1<<17)

#define CHKGL \
	do { \
		GLenum CHKGL_error = glGetError(); \
		if (CHKGL_error != GL_NO_ERROR) { \
			fprintf(stderr, "OPENGL ERROR %d in %s:%d\n", CHKGL_error, __FILE__, __LINE__); \
			exit(EXIT_FAILURE); \
		} \
	} while (0)

struct draw_vertex {
	union lsl_vec2 position;
	union lsl_vec2 uv;
	union lsl_vec4 color;
};

Display* dpy;
XVisualInfo* vis;
GLXContext ctx;
XIM xim;
struct draw_vertex* draw_vertices;
GLuint vertex_buffer;
GLuint vertex_array;
GLushort* draw_elements;
GLuint element_buffer;
int draw_n_vertices;
int draw_n_elements;
GLuint atlas_texture;
int tmp_ctx_error;
struct lsl_rect clipping_rect;
int viewport_height;
union lsl_vec2 dotuv;

#define MAX_WIN (32)

struct win {
	int open;
	int(*proc)(struct lsl_frame*, void*);
	void* usr;
	Window window;
	XIC xic;
	struct lsl_frame frame;
} wins[MAX_WIN];

void lsl_win_open(const char* title, int(*proc)(struct lsl_frame*, void*), void* usr)
{
	struct win* lw = NULL;
	for (int i = 0; i < MAX_WIN; i++) {
		lw = &wins[i];
		if (!lw->open) {
			lw->open = 1;
			lw->proc = proc;
			lw->usr = usr;
			break;
		}
	}

	if (!lw->open) {
		fprintf(stderr, "no free windows\n");
		exit(EXIT_FAILURE);
	}

	XSetWindowAttributes attrs;
	Window root = RootWindow(dpy, vis->screen);
	attrs.colormap = XCreateColormap(
		dpy,
		root,
		vis->visual,
		AllocNone);
	attrs.background_pixmap = None;
	attrs.border_pixel = 0;
	attrs.event_mask =
		StructureNotifyMask
		| EnterWindowMask
		| LeaveWindowMask
		| ButtonPressMask
		| ButtonReleaseMask
		| PointerMotionMask
		| KeyPressMask
		| KeyReleaseMask
		| ExposureMask
		| VisibilityChangeMask;

	lw->window = XCreateWindow(
		dpy,
		root,
		0, 0,
		100, 100,
		0,
		vis->depth,
		InputOutput,
		vis->visual, CWBorderPixel | CWColormap | CWEventMask,
		&attrs);

	if (!lw->window) {
		fprintf(stderr, "XCreateWindow failed\n");
		exit(EXIT_FAILURE);
	}

	lw->xic = XCreateIC(
		xim,
		XNInputStyle,
		XIMPreeditNothing | XIMStatusNothing,
		XNClientWindow,
		lw->window,
		XNFocusWindow,
		lw->window,
		NULL);
	if (lw->xic == NULL) {
		fprintf(stderr, "XCreateIC failed\n");
		exit(EXIT_FAILURE);
	}

	XStoreName(dpy, lw->window, title);
	XMapWindow(dpy, lw->window);
}


static struct win* wlookup(Window w)
{
	for (int i = 0; i < MAX_WIN; i++) {
		struct win* lw = &wins[i];
		if (lw->open && lw->window == w) return lw;
	}
	return NULL;
}

static void handle_text_event(char* text, int length, struct lsl_frame* f)
{
	if (length <= 0) return;
	if ((f->text_length + length) >= LSL_MAX_TEXT_LENGTH) return;
	memcpy(f->text + f->text_length, text, length);
	f->text_length += length;
	f->text[f->text_length] = 0;
}

static void handle_key_event(XKeyEvent* e, struct win* lw)
{
	struct lsl_frame* f = &lw->frame;

	KeySym sym = XkbKeycodeToKeysym(dpy, e->keycode, 0, 0);
	int mask = 0;
	int is_keypress = e->type == KeyPress;
	switch (sym) {
		case XK_Return:
			if (is_keypress) {
				// XLookupString would give "\r" :-/
				handle_text_event("\n", 1, f);
			}
			return;
		case XK_Shift_L:
			mask = LSL_MOD_LSHIFT;
			break;
		case XK_Shift_R:
			mask = LSL_MOD_RSHIFT;
			break;
		case XK_Control_L:
			mask = LSL_MOD_LCTRL;
			break;
		case XK_Control_R:
			mask = LSL_MOD_RCTRL;
			break;
		case XK_Alt_L:
			mask = LSL_MOD_LALT;
			break;
		case XK_Alt_R:
			mask = LSL_MOD_RALT;
			break;
	}

	if (mask) {
		if (is_keypress) {
			f->mod |= mask;
		} else {
			f->mod &= ~mask;
		}
	} else if (is_keypress) {
		char buf[16];
		int len = Xutf8LookupString(lw->xic, e, buf, sizeof(buf), NULL, NULL);
		if (len > 0) {
			handle_text_event(buf, len, f);
		}
	}
}

static void get_dim(Window w, int* width, int* height)
{
	Window _root;
	int _x, _y;
	unsigned int _width, _height, _border_width, _depth;
	XGetGeometry(dpy, w, &_root, &_x, &_y, &_width, &_height, &_border_width, &_depth);
	if (width != NULL) *width = _width;
	if (height != NULL) *height = _height;
}

static GLuint create_shader(const char* src, GLenum type)
{
	GLuint shader = glCreateShader(type); CHKGL;
	AN(shader);
	glShaderSource(shader, 1, &src, 0); CHKGL;
	glCompileShader(shader); CHKGL;

	GLint status;
	glGetShaderiv(shader, GL_COMPILE_STATUS, &status); CHKGL;
	if (status == GL_FALSE) {
		GLint msglen;
		glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &msglen); CHKGL;
		GLchar* msg = malloc(msglen + 1);
		AN(msg);
		glGetShaderInfoLog(shader, msglen, NULL, msg);
		const char* stype = type == GL_VERTEX_SHADER ? "vertex" : type == GL_FRAGMENT_SHADER ? "fragment" : "waaaat";
		fprintf(stderr, "%s shader compile error: %s -- source:\n%s", stype, msg, src);
		exit(EXIT_FAILURE);
	}

	return shader;
}

static GLuint create_program(const char* vert_src, const char* frag_src)
{
	GLuint vert_shader = create_shader(vert_src, GL_VERTEX_SHADER);
	GLuint frag_shader = create_shader(frag_src, GL_FRAGMENT_SHADER);

	GLuint prg = glCreateProgram(); CHKGL;
	AN(prg);

	glAttachShader(prg, vert_shader); CHKGL;
	glAttachShader(prg, frag_shader); CHKGL;

	glLinkProgram(prg);

	GLint status;
	glGetProgramiv(prg, GL_LINK_STATUS, &status); CHKGL;
	if (status == GL_FALSE) {
		GLint msglen;
		glGetProgramiv(prg, GL_INFO_LOG_LENGTH, &msglen); CHKGL;
		GLchar* msg = (GLchar*) malloc(msglen + 1);
		AN(msg);
		glGetProgramInfoLog(prg, msglen, NULL, msg);
		fprintf(stderr, "shader link error: %s", msg);
		exit(EXIT_FAILURE);
	}

	glDeleteShader(vert_shader);
	glDeleteShader(frag_shader);

	return prg;
}

static void draw_flush()
{
	if (!draw_n_vertices || !draw_n_elements) {
		// nothing to do
		return;
	}

	glBindBuffer(GL_ARRAY_BUFFER, vertex_buffer);
	glBufferSubData(GL_ARRAY_BUFFER, 0, draw_n_vertices * sizeof(struct draw_vertex), draw_vertices);

	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, element_buffer);
	glBufferSubData(GL_ELEMENT_ARRAY_BUFFER, 0, draw_n_elements * sizeof(GLushort), draw_elements);

	glBindTexture(GL_TEXTURE_2D, atlas_texture);
	glScissor(clipping_rect.x, viewport_height - clipping_rect.h - clipping_rect.y, clipping_rect.w, clipping_rect.h);
	glDrawElements(GL_TRIANGLES, draw_n_elements, GL_UNSIGNED_SHORT, 0);

	draw_n_vertices = 0;
	draw_n_elements = 0;
}

static void draw_append(int n_vertices, int n_elements, struct draw_vertex* vertices, GLushort* elements)
{
	int flush = 0;
	flush |= draw_n_vertices + n_vertices > MAX_VERTICES;
	flush |= draw_n_elements + n_elements > MAX_ELEMENTS;
	if (flush) {
		draw_flush();
		ASSERT((draw_n_vertices + n_vertices) <= MAX_VERTICES);
		ASSERT((draw_n_elements + n_elements) <= MAX_ELEMENTS);
	}

	memcpy(draw_vertices + draw_n_vertices, vertices, n_vertices * sizeof(*vertices));

	GLushort* ebase = draw_elements + draw_n_elements;
	memcpy(ebase, elements, n_elements * sizeof(*ebase));
	for (int i = 0; i < n_elements; i++) ebase[i] += draw_n_vertices;

	draw_n_vertices += n_vertices;
	draw_n_elements += n_elements;
}

static void draw_glyph(struct glyph* gly)
{
	float dx0 = cursor_x + gly->xoff + clipping_rect.x;
	float dy0 = cursor_y + gly->yoff + clipping_rect.y;
	float dx1 = dx0 + gly->w;
	float dy1 = dy0 + gly->h;
	float u0 = (float)gly->x / (float)atlas_width;
	float v0 = (float)gly->y / (float)atlas_height;
	float u1 = (float)(gly->x + gly->w) / (float)atlas_width;
	float v1 = (float)(gly->y + gly->h) / (float)atlas_height;

	struct draw_vertex vs[4] = {
		{ .position = { .x = dx0, .y = dy0 }, .uv = { .u = u0, .v = v0 }, .color = draw_color0 },
		{ .position = { .x = dx1, .y = dy0 }, .uv = { .u = u1, .v = v0 }, .color = draw_color0 },
		{ .position = { .x = dx1, .y = dy1 }, .uv = { .u = u1, .v = v1 }, .color = draw_color1 },
		{ .position = { .x = dx0, .y = dy1 }, .uv = { .u = u0, .v = v1 }, .color = draw_color1 }
	};
	GLushort es[6] = {0,1,2,0,2,3};

	draw_append(4, 6, vs, es);

}

void lsl_begin(struct lsl_rect* r)
{
	// TODO allow nested?
	clipping_rect = *r;
}

void lsl_end()
{
	draw_flush();
}

void lsl_rect(struct lsl_rect* r)
{
	float dx0 = r->x + clipping_rect.x;
	float dy0 = r->y + clipping_rect.y;
	float dx1 = dx0 + r->w;
	float dy1 = dy0 + r->h;

	struct draw_vertex vs[4] = {
		{ .position = { .x = dx0, .y = dy0 }, .uv = dotuv, .color = draw_color0 },
		{ .position = { .x = dx1, .y = dy0 }, .uv = dotuv, .color = draw_color0 },
		{ .position = { .x = dx1, .y = dy1 }, .uv = dotuv, .color = draw_color1 },
		{ .position = { .x = dx0, .y = dy1 }, .uv = dotuv, .color = draw_color1 }
	};
	GLushort es[6] = {0,1,2,0,2,3};

	draw_append(4, 6, vs, es);
}

void lsl_clear()
{
	struct lsl_rect r = clipping_rect;
	// clear x/y because lsl_rect is relative to clipping_rect
	r.x = 0; r.y = 0;
	lsl_rect(&r);
}

void lsl_main_loop()
{
	/* opengl initialization stuff will fail without a context. we're
	 * expected to have at least one window at this point, so bind context
	 * to the first that is found */
	for (int i = 0; i < MAX_WIN; i++) {
		struct win* lw = &wins[i];
		if (!lw->open) continue;
		glXMakeCurrent(dpy, lw->window, ctx);
		break;
	}

	GLuint glprg;
	GLuint u_texture;
	GLuint u_scaling;

	{
		const GLchar* vert_src =
			"#version 130\n"

			"uniform vec2 u_scaling;\n"

			"attribute vec2 a_position;\n"
			"attribute vec2 a_uv;\n"
			"attribute vec4 a_color;\n"

			"varying vec2 v_uv;\n"
			"varying vec4 v_color;\n"

			"void main()\n"
			"{\n"
			"	v_uv = a_uv;\n"
			"	v_color = a_color;\n"
			"	gl_Position = vec4(a_position * u_scaling * vec2(2,2) + vec2(-1,1), 0, 1);\n"
			"}\n"
			;

		const GLchar* frag_src =
			"#version 130\n"

			"uniform sampler2D u_texture;\n"

			"varying vec2 v_uv;\n"
			"varying vec4 v_color;\n"

			"void main()\n"
			"{\n"
			"	float v = texture2D(u_texture, v_uv).r;\n"
			"	gl_FragColor = v_color * vec4(v,v,v,v);\n"
			"}\n"
			;
		glprg = create_program(vert_src, frag_src);
		u_texture = glGetUniformLocation(glprg, "u_texture"); CHKGL;
		u_scaling = glGetUniformLocation(glprg, "u_scaling"); CHKGL;

		GLuint a_position = glGetAttribLocation(glprg, "a_position"); CHKGL;
		GLuint a_uv = glGetAttribLocation(glprg, "a_uv"); CHKGL;
		GLuint a_color = glGetAttribLocation(glprg, "a_color"); CHKGL;

		size_t vertices_sz = MAX_VERTICES * sizeof(struct draw_vertex);
		AN(draw_vertices = malloc(vertices_sz));
		glGenBuffers(1, &vertex_buffer); CHKGL;
		glGenVertexArrays(1, &vertex_array); CHKGL;
		glBindVertexArray(vertex_array); CHKGL;
		glBindBuffer(GL_ARRAY_BUFFER, vertex_buffer); CHKGL;
		glBufferData(GL_ARRAY_BUFFER, vertices_sz, NULL, GL_STREAM_DRAW); CHKGL;
		glEnableVertexAttribArray(a_position); CHKGL;
		glEnableVertexAttribArray(a_uv); CHKGL;
		glEnableVertexAttribArray(a_color); CHKGL;

		#define OFZ(e) (GLvoid*)((size_t)&(((struct draw_vertex*)0)->e))
		glVertexAttribPointer(a_position, 2, GL_FLOAT, GL_FALSE, sizeof(struct draw_vertex), OFZ(position)); CHKGL;
		glVertexAttribPointer(a_uv, 2, GL_FLOAT, GL_FALSE, sizeof(struct draw_vertex), OFZ(uv)); CHKGL;
		glVertexAttribPointer(a_color, 4, GL_FLOAT, GL_FALSE, sizeof(struct draw_vertex), OFZ(color)); CHKGL;
		#undef OFZ

		size_t elements_sz = MAX_ELEMENTS * sizeof(GLushort);
		AN(draw_elements = malloc(elements_sz));
		glGenBuffers(1, &element_buffer); CHKGL;
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, element_buffer); CHKGL;
		glBufferData(GL_ELEMENT_ARRAY_BUFFER, elements_sz, NULL, GL_STREAM_DRAW); CHKGL;
	}

	// setup atlas texture
	{
		char* bitmap = load_atlas();

		glGenTextures(1, &atlas_texture); CHKGL;
		int level = 0;
		int border = 0;
		glBindTexture(GL_TEXTURE_2D, atlas_texture);
		glTexImage2D(
			GL_TEXTURE_2D,
			level,
			1,
			atlas_width, atlas_height,
			border,
			GL_RED,
			GL_UNSIGNED_BYTE,
			bitmap); CHKGL;

		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST); CHKGL;
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST); CHKGL;
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER); CHKGL;
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER); CHKGL;

		free(bitmap);

		// setup dotuv
		struct glyph* gly = &types[0].glyphs[0];
		dotuv = (union lsl_vec2) { .u = (float)(gly->x+1) / (float)atlas_width, .v = (float)(gly->y+1) / (float)atlas_height };
	}

	glEnable(GL_BLEND);
	glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA); CHKGL;

	glEnable(GL_SCISSOR_TEST);

	for (;;) {
		while (XPending(dpy)) {
			XEvent xe;
			XNextEvent(dpy, &xe);
			Window w = xe.xany.window;
			if (XFilterEvent(&xe, w)) continue;

			struct win* lw = wlookup(w);
			if (lw == NULL) continue;

			struct lsl_frame* f = &lw->frame;

			switch (xe.type) {
				case EnterNotify:
					f->mx = xe.xcrossing.x;
					f->my = xe.xcrossing.y;
					break;
				case LeaveNotify:
					f->mx = -1;
					f->my = -1;
					break;
				case ButtonPress:
				case ButtonRelease:
				{
					int i = xe.xbutton.button - 1;
					if (i >= 0 && i < LSL_MAX_BUTTONS) {
						f->button[i] = xe.type == ButtonPress;
						f->button_cycles[i]++;
					}
				}
				break;
				case MotionNotify:
					f->mx = xe.xmotion.x;
					f->my = xe.xmotion.y;
					break;
				case KeyPress:
				case KeyRelease:
					handle_key_event(&xe.xkey, lw);
					break;
			}
		}

		for (int i = 0; i < MAX_WIN; i++) {
			struct win* lw = &wins[i];
			if (!lw->open) continue;

			// fetch window dimensions
			struct lsl_frame* f = &lw->frame;
			get_dim(lw->window, &f->w, &f->h);
			viewport_height = f->h;

			glXMakeCurrent(dpy, lw->window, ctx);
			glViewport(0, 0, f->w, f->h);

			glUseProgram(glprg);
			glUniform1i(u_texture, 0);
			glUniform2f(u_scaling, 1.0f / (float)f->w, -1.0f / (float)f->h);

			glBindVertexArray(vertex_array);

			// run user callback
			int ret = lw->proc(f, lw->usr);

			draw_flush();

			glXSwapBuffers(dpy, lw->window);

			// clear per-frame input stuff
			for (int i = 0; i < LSL_MAX_BUTTONS; i++) f->button_cycles[i] = 0;
			f->text_length = 0;

			if (ret != 0) {
				return; // XXX or close window?
			}
		}
	}
}

static int is_extension_supported(const char* extensions, const char* extension)
{
	const char* p0 = extensions;
	const char* p1 = p0;
	for (;;) {
		while (*p1 != ' ' && *p1 != '\0') p1++;
		if (memcmp(extension, p0, p1 - p0) == 0) return 1;
		if (*p1 == '\0') return 0;
		p0 = p1++;
	}
}


static int tmp_ctx_error_handler(Display *dpy, XErrorEvent *ev)
{
	tmp_ctx_error = 1;
	return 0;
}


int main(int argc, char** argv)
{
	dpy = XOpenDisplay(NULL);
	if (!dpy) {
		fprintf(stderr, "XOpenDisplay failed");
		exit(EXIT_FAILURE);
	}

	xim = XOpenIM(dpy, NULL, NULL, NULL);
	if (xim == NULL) {
		fprintf(stderr, "XOpenIM failed");
		exit(EXIT_FAILURE);
	}

	{
		int major = -1;
		int minor = -1;
		Bool success = glXQueryVersion(dpy, &major, &minor);
		if (success == False || major < 1 || (major == 1 && minor < 3)) {
			fprintf(stderr, "invalid glx version, major=%d, minor=%d\n", major, minor);
			exit(EXIT_FAILURE);
		}
	}

	/* find visual */
	vis = NULL;
	GLXFBConfig fb_config = NULL;
	{
		static int attrs[] = {
			GLX_X_RENDERABLE    , True,
			GLX_DRAWABLE_TYPE   , GLX_WINDOW_BIT,
			GLX_RENDER_TYPE     , GLX_RGBA_BIT,
			GLX_X_VISUAL_TYPE   , GLX_TRUE_COLOR,
			GLX_RED_SIZE        , 8,
			GLX_GREEN_SIZE      , 8,
			GLX_BLUE_SIZE       , 8,
			GLX_ALPHA_SIZE      , 8,
			GLX_DEPTH_SIZE      , 0,
			GLX_STENCIL_SIZE    , 8,
			GLX_DOUBLEBUFFER    , True,
			GLX_SAMPLE_BUFFERS  , 0,
			GLX_SAMPLES         , 0,
			None
		};

		int n;
		GLXFBConfig* cs = glXChooseFBConfig(dpy, XDefaultScreen(dpy), attrs, &n);
		if (cs == NULL) {
			fprintf(stderr, "glXChooseFBConfig failed\n");
			exit(1);
		}

		int first_valid = -1;

		for (int i = 0; i < n; i++) {
			XVisualInfo* try_vis = glXGetVisualFromFBConfig(dpy, cs[i]);
			if (try_vis) {
				if (first_valid == -1) first_valid = i;

				#define REJECT(name) \
					{ \
						int value = 0; \
						if (glXGetFBConfigAttrib(dpy, cs[i], name, &value) != Success) { \
							fprintf(stderr, "glXGetFBConfigAttrib failed for " #name " \n"); \
							exit(1); \
						} \
						if (value > 0) { \
							XFree(try_vis); \
							continue; \
						} \
					}
				REJECT(GLX_SAMPLE_BUFFERS);
				REJECT(GLX_SAMPLES);
				REJECT(GLX_ACCUM_RED_SIZE);
				REJECT(GLX_ACCUM_GREEN_SIZE);
				REJECT(GLX_ACCUM_BLUE_SIZE);
				REJECT(GLX_ACCUM_ALPHA_SIZE);
				#undef REJECT

				// not rejected? pick it!
				vis = try_vis;
				fb_config = cs[i];
				break;
			}
		}

		if (vis == NULL) {
			if (first_valid == -1) {
				fprintf(stderr, "found no visual\n");
				exit(1);
			} else {
				vis = glXGetVisualFromFBConfig(dpy, cs[first_valid]);
				fb_config = cs[first_valid];
			}
		}

		XFree(cs);
	}

	/* create gl context */
	ctx = 0;
	{
		PFNGLXCREATECONTEXTATTRIBSARBPROC create_context =
			(PFNGLXCREATECONTEXTATTRIBSARBPROC)
			glXGetProcAddressARB((const GLubyte*)"glXCreateContextAttribsARB");
		if (!create_context) {
			fprintf(stderr, "failed to get proc address for glXCreateContextAttribsARB\n");
			exit(1);
		}

		const char *extensions = glXQueryExtensionsString(
			dpy,
			DefaultScreen(dpy));

		if (!is_extension_supported(extensions, "GLX_ARB_create_context")) {
			fprintf(stderr, "GLX_ARB_create_context not supported\n");
			exit(1);
		}

		int (*old_handler)(Display*, XErrorEvent*) = XSetErrorHandler(&tmp_ctx_error_handler);

		int attrs[] = {
			GLX_CONTEXT_MAJOR_VERSION_ARB, 3,
			GLX_CONTEXT_MINOR_VERSION_ARB, 0,
			None
		};

		ctx = create_context(
			dpy,
			fb_config,
			0,
			True,
			attrs);

		XSync(dpy, False);

		if (!ctx || tmp_ctx_error) {
			fprintf(stderr, "could not create opengl context\n");
			exit(1);
		}

		XSetErrorHandler(old_handler);
	}

	int exit_status = lsl_main(argc, argv);

	XCloseDisplay(dpy);

	return exit_status == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}

