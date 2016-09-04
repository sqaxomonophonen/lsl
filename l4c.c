#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include <stdarg.h>


//////////////////////////////////////////////////////////////////////////////
// LEXER
//////////////////////////////////////////////////////////////////////////////

struct str {
	char* ptr;
	size_t len;
};

static void str_cstr(struct str* s, char* cs)
{
	s->ptr = cs;
	s->len = strlen(cs);
};

#if 0
static void str_print(struct str* s)
{
	fwrite(s->ptr, s->len, 1, stdout);
}
#endif

enum token_type {
	T__META = 0, // for special sexpr tokens

	T_NUMBER,

	T_IDENTIFIER,

	//T__KWMIN,
	T_RETURN,
	T_IF,
	T_ELSE,
	T_FOR,
	T_BREAK,
	T_CONTINUE,
	T_GOTO,
	T_SWITCH,
	T_CASE,
	T_DEFAULT,
	//T__KWMAX,

	// top-level port qualifiers
	T_IN,
	T_OUT,
	T_SIGNAL,
	T_UNIFORM,

	T_STRUCT,
	T_UNION,
	T_ENUM,

	T__TYPMIN,
	T_AUTO,
	T_TYPEOF,
	T_VOID,
	T_BOOL,
	T_INT,
	T_UINT,
	T_I8,
	T_U8,
	T_I32,
	T_U32,
	T_I64,
	T_F32,
	T_F64,
	T__TYPMAX,

	//T__PNCTMIN,
	T_COMMA,
	T_SEMICOLON,
	T_ASSIGN,
	T_EQ,
	T_NEQ,
	T_PLUS,
	T_MINUS,
	T_MUL,
	T_DIV,
	T_MOD,
	T_LPAREN,
	T_RPAREN,
	//T__PNCTMAX,

	T_WHITESPACE,

	T_EOF
};

static inline int token_type_is_type(enum token_type tt)
{
	return tt > T__TYPMIN && tt < T__TYPMAX;
}

struct token {
	enum token_type type;
	union {
		int v[4]; // when type==T__META
		struct str str; // when type!=T__META
	};
};

struct lexer;

// (returns lexer_state_fn* but I can't do recursive typedefs :-/ )
typedef void* (*lexer_state_fn)(struct lexer*);

struct lexer {
	struct str src;
	int line, column;
	int pos;
	int start;
	struct token token;
	int has_token;
	lexer_state_fn state_fn;
};

static void* lex_main(struct lexer* l);

static void lexer_init(struct lexer* l, char* src)
{
	memset(l, 0, sizeof(*l));
	str_cstr(&l->src, src);
	l->state_fn = lex_main;
}

static int lexer_ch(struct lexer* l)
{
	if (l->pos >= l->src.len) {
		l->pos++;
		return -1;
	}

	int ch = l->src.ptr[l->pos]; // TODO proper utf8 decode?
	if (ch == '\n') {
		l->line++;
		l->column = 0;
	} else {
		l->column++;
	}
	l->pos++;
	return ch;
}

static void lexer_backup(struct lexer* l)
{
	l->pos--;
}

static int lexer_accept_char(struct lexer* l, char valid)
{
	int ch = lexer_ch(l);
	if (ch == valid) return 1;
	lexer_backup(l);
	return 0;
}

static int char_in(char ch, const char* valid)
{
	size_t valid_length = strlen(valid);
	for (int i = 0; i < valid_length; i++) if (valid[i] == ch) return 1;
	return 0;
}

static int lexer_accept(struct lexer* l, const char* valid)
{
	int ch = lexer_ch(l);
	if (char_in(ch, valid)) return 1;
	lexer_backup(l);
	return 0;
}

static int lexer_accept_fn(struct lexer* l, int(*fn)(int))
{
	int ch = lexer_ch(l);
	if (fn(ch)) return 1;
	lexer_backup(l);
	return 0;
}


static int lexer_accept_run(struct lexer* l, const char* valid)
{
	int i = 0;
	while (lexer_accept(l, valid)) i++;
	return i;
}

static void lexer_accept_run_fn(struct lexer* l, int(*fn)(int))
{
	while (lexer_accept_fn(l, fn)) {}
}

static void lexer_eat(struct lexer* l)
{
	l->start = l->pos;
}

static void lexer_emit(struct lexer* l, enum token_type type)
{
	l->token.type = type;
	l->token.str.ptr = l->src.ptr + l->start;
	l->token.str.len = l->pos - l->start;

	l->has_token = 1;

	lexer_eat(l);
}

static void* lex_eof(struct lexer* l) {
	lexer_eat(l);
	lexer_emit(l, T_EOF);
	return lex_eof;
}

static void* lex_number(struct lexer* l)
{
	lexer_accept(l, "+-");
	const char* dec_digits = "0123456789";
	const char* hex_digits = "0123456789abcdefABCDEF";
	const char* digits = lexer_accept_char(l, '0') && lexer_accept(l, "xX") ? hex_digits : dec_digits;
	lexer_accept_run(l, digits);
	if (lexer_accept_char(l, '.')) {
		lexer_accept_run(l, digits);
	}
	if (lexer_accept(l, "eE")) {
		lexer_accept(l, "+-");
		lexer_accept_run(l, dec_digits);
	}
	lexer_emit(l, T_NUMBER);
	return lex_main;
}

static void* lex_ignore_line(struct lexer* l)
{
	int curline = l->line;
	while (l->line == curline) if (lexer_ch(l) == -1) return lex_eof;
	return lex_main;
}

static void* lex_multiline_comment(struct lexer* l)
{
	for (;;) {
		int ch = lexer_ch(l);
		if (ch == -1) {
			return lex_eof;
		} else if (ch == '*') {
			if (lexer_accept_char(l, '/')) {
				return lex_main;
			}
		}
	}
}

static int is_alpha(int ch)
{
	return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || ch == '_';
}

static int is_num(int ch)
{
	return ch >= '0' && ch <= '9';
}

static int is_identifier(int ch)
{
	return is_alpha(ch) || is_num(ch);
}

static void* lex_identifier(struct lexer* l)
{
	lexer_accept_run_fn(l, is_identifier);
	lexer_emit(l, T_IDENTIFIER);
	return lex_main;
}

static void* lex_main(struct lexer* l)
{
	if (lexer_ch(l) == -1) return lex_eof;
	lexer_backup(l);

	const char* ws = " \t\n\r";
	if (lexer_accept_run(l, ws)) {
		lexer_emit(l, T_WHITESPACE);
		return lex_main;
	}

	// eat comments
	if (lexer_accept_char(l, '/')) {
		if (lexer_accept_char(l, '/')) {
			return lex_ignore_line;
		} else if (lexer_accept_char(l, '*')) {
			return lex_multiline_comment;
		} else {
			lexer_backup(l);
		}
	}

	{
		struct {
			const char* str;
			enum token_type tt;
		} pnct[] = {
			{"==", T_EQ},
			{"!=", T_NEQ},
			// TODO
			{NULL}
		};
		for (int i = 0; pnct[i].str; i++) {
			const char* str = pnct[i].str;
			enum token_type tt = pnct[i].tt;
			size_t len = strlen(str);
			int n;
			for (n = 0; n < len; n++) {
				if (!lexer_accept_char(l, str[n])) {
					break;
				}
			}
			if (n == len) {
				lexer_emit(l, tt);
				return lex_main;
			}
			for (int j = 0; j < n; j++) lexer_backup(l);
		}
	}

	{
		struct {
			char ch;
			enum token_type tt;
		} pnct[] = {
			{',', T_COMMA},
			{';', T_SEMICOLON},
			{'=', T_ASSIGN},
			{'+', T_PLUS},
			{'-', T_MINUS},
			{'*', T_MUL},
			{'/', T_DIV},
			{'%', T_MOD},
			{'(', T_LPAREN},
			{')', T_RPAREN},
			// TODO
			{0}
		};
		for (int i = 0; pnct[i].ch; i++) {
			char ch = pnct[i].ch;
			enum token_type tt = pnct[i].tt;
			if (lexer_accept_char(l, ch)) {
				lexer_emit(l, tt);
				return lex_main;
			}
		}
	}

	// eat number
	if (lexer_accept(l, ".+-0123456789")) {
		lexer_backup(l);
		return lex_number;
	}

	// eat identifier
	if (lexer_accept_fn(l, is_identifier)) {
		return lex_identifier;
	}

	assert(!"lexer error");
	return NULL;
}

#define PRM(s,e) \
	{ \
		size_t l = strlen(s); \
		if (l == t->str.len && memcmp(t->str.ptr, s, l) == 0) { \
			t->type = e; \
		} \
	}
static void promote_identifer_if_keyword(struct token* t)
{
	if (t->type != T_IDENTIFIER) return;

	PRM("return", T_RETURN);
	PRM("if", T_IF);
	PRM("else", T_ELSE);
	PRM("for", T_FOR);
	PRM("break", T_BREAK);
	PRM("continue", T_CONTINUE);
	PRM("goto", T_GOTO);
	PRM("switch", T_SWITCH);
	PRM("case", T_CASE);
	PRM("default", T_DEFAULT);

	PRM("in", T_IN);
	PRM("out", T_OUT);
	PRM("signal", T_SIGNAL);
	PRM("uniform", T_UNIFORM);

	PRM("struct", T_STRUCT);
	PRM("union", T_UNION);
	PRM("enum", T_ENUM);

	PRM("auto", T_AUTO);
	PRM("typeof", T_TYPEOF);
	PRM("void", T_VOID);
	PRM("bool", T_BOOL);
	PRM("int", T_INT);
	PRM("uint", T_UINT);
	PRM("i8", T_I8);
	PRM("u8", T_U8);
	PRM("i32", T_I32);
	PRM("u32", T_U32);
	PRM("i64", T_I64);
	PRM("f32", T_F32);
	PRM("f64", T_F64);
}
#undef PRM

static struct token lexer_next(struct lexer* l)
{
	for (;;) {
		assert(l->state_fn != NULL);
		l->state_fn = l->state_fn(l);
		if (l->has_token && l->token.type != T_WHITESPACE) { // XXX always skip whitespace?
			promote_identifer_if_keyword(&l->token);
			l->has_token = 0;
			return l->token;
		}
	}
}




//////////////////////////////////////////////////////////////////////////////
// S-EXPRESSIONS
//////////////////////////////////////////////////////////////////////////////

struct sexpr {
	struct token atom;
	struct sexpr* sexpr;
	struct sexpr* next;
};

static inline int sexpr_is_atom(struct sexpr* e)
{
	if (e->atom.type != T_LPAREN) {
		assert(e->sexpr == NULL);
		return 1;
	} else {
		return 0;
	}
}

static inline int sexpr_is_list(struct sexpr* e)
{
	return e->atom.type == T_LPAREN;
}

static struct sexpr* sexpr_new()
{
	struct sexpr* e = calloc(1, sizeof(*e));
	assert(e != NULL);
	return e;
}

static struct sexpr* sexpr_new_atom(struct token atom)
{
	struct sexpr* e = sexpr_new();
	e->atom = atom;
	return e;
}

static struct sexpr* sexpr_new_list(struct sexpr* head, ...)
{
	struct sexpr* e = sexpr_new();
	e->atom.type = T_LPAREN; // XXX or use special? T__LIST?
	e->sexpr = head;
	struct sexpr* cur = head;

	va_list args;
	va_start(args, head);
	for (;;) {
		struct sexpr* arg = va_arg(args, struct sexpr*);
		if (arg == NULL) break;
		cur->next = arg;
		cur = cur->next;
	}
	va_end(args);
	return e;
}

static struct sexpr** sexpr_get_append_cursor(struct sexpr* e)
{
	assert(sexpr_is_list(e));
	struct sexpr** cursor = &e->sexpr;
	while (*cursor != NULL) cursor = &((*cursor)->next);
	return cursor;
}

static struct sexpr** sexpr_append(struct sexpr** append_cursor, struct sexpr* e)
{
	assert(e->next == NULL);
	assert(*append_cursor == NULL);
	*append_cursor = e;
	return &e->next;
}



//////////////////////////////////////////////////////////////////////////////
// PARSER
//////////////////////////////////////////////////////////////////////////////

struct parser {
	struct lexer lexer;
	struct token next_token;

	int err;
};

static void parser_errf(struct parser* p, const char* fmt, ...)
{
	// TODO record which token caused the error?
	// TODO error code/type?
	// TODO snprintf -> p->err_str or something

	p->err = 1;

	va_list args;
	va_start(args, fmt);
	fprintf(stderr, "PARSER ERROR: ");
	vfprintf(stderr, fmt, args);
	va_end(args);
	fprintf(stderr, "\n");
	abort();
}

static void parser_err_unexp(struct parser* p)
{
	parser_errf(p, "unexpected token");
}

static void parser_err_exp(struct parser* p, enum token_type tt)
{
	parser_errf(p, "unexpected token (expected %d)", tt); // TODO token_type -> char*
}

static inline int prefix_bp(enum token_type tt)
{
	switch (tt) {
		case T_LPAREN:
			return 1;
		case T_PLUS:
		case T_MINUS:
			// TODO
			return 100;
		default:
			return -1;
	}
}

static inline int infix_bp(enum token_type tt)
{
	switch (tt) {
		// expression terminators
		case T_EOF:
		case T_COMMA:
		case T_RPAREN:
		case T_SEMICOLON:
			return 0;
		case T_ASSIGN:
			return 5;
		case T_PLUS:
		case T_MINUS:
			return 40;
		case T_MUL:
		case T_DIV:
			return 50;
		case T_LPAREN:
			return 100;
		default:
			return -1;
	}
}

static inline int is_unary_op(enum token_type tt) {
	switch (tt) {
		case T_PLUS:
		case T_MINUS:
			return 1;
		default:
			return 0;
	}
}

static inline int is_binary_op(enum token_type tt) {
	switch (tt) {
		case T_PLUS:
		case T_MINUS:
		case T_MUL:
		case T_DIV:
		case T_ASSIGN:
			return 1;
		default:
			return 0;
	}
}

static inline int is_binary_op_right_associcative(enum token_type tt)
{
	return 0; // TODO
}

static inline struct token parser_next_token(struct parser* p)
{
	struct token t = p->next_token;
	p->next_token = lexer_next(&p->lexer);
	return t;
}

static void parser_init(struct parser* p, char* src)
{
	memset(p, 0, sizeof(*p));
	lexer_init(&p->lexer, src);
	parser_next_token(p);
}

static int parser_expect(struct parser* p, enum token_type tt)
{
	struct token t = parser_next_token(p);
	if (t.type != T_RPAREN) {
		parser_err_exp(p, tt);
		return 0;
	}
	return 1;
}

static struct sexpr* parse_expr_rec(struct parser* p, int rbp, int depth)
{
	if (depth >= 1024) {
		parser_errf(p, "maximum expression recursion depth exceeded");
		return NULL;
	}

	struct token t = parser_next_token(p);

	// null denotation
	struct sexpr* left = NULL;
	{
		enum token_type tt = t.type;
		int bp;
		if (tt == T_NUMBER || tt == T_IDENTIFIER || token_type_is_type(tt)) {
			left = sexpr_new_atom(t);
		} else if ((bp = prefix_bp(tt)) != -1) {
			struct sexpr* operand = parse_expr_rec(p, bp, depth+1);
			if (operand == NULL) return NULL;
			if (is_unary_op(tt)) {
				left = sexpr_new_list(sexpr_new_atom(t), operand, NULL);
			} else if (tt == T_LPAREN) {
				left = operand;
				if (!parser_expect(p, T_RPAREN)) return NULL;
			} else {
				parser_err_unexp(p);
				return NULL;
			}
		} else {
			parser_err_unexp(p);
			return NULL;
		}
	}

	for (;;) {
		int lbp = infix_bp(p->next_token.type);
		if (lbp == -1) {
			parser_err_unexp(p);
			return NULL;
		}
		if (rbp >= lbp) break;

		// left denotation
		t = parser_next_token(p);
		if (is_binary_op(t.type)) { // parse binary op
			int bp = lbp;
			if (is_binary_op_right_associcative(t.type)) bp--;
			struct sexpr* right = parse_expr_rec(p, bp, depth+1);
			if (right == NULL) return NULL;
			left = sexpr_new_list(sexpr_new_atom(t), left, right, NULL);
		} else if (t.type == T_LPAREN) { // parse call
			left = sexpr_new_list(left, NULL);
			struct sexpr** cursor = sexpr_get_append_cursor(left);
			int more = p->next_token.type != T_RPAREN;
			while (more) {
				struct sexpr* a = parse_expr_rec(p, 0, depth+1);
				if (a == NULL) return NULL;
				cursor = sexpr_append(cursor, a);

				if (p->next_token.type != T_RPAREN && p->next_token.type != T_COMMA) {
					parser_err_unexp(p);
					return NULL;
				}
				more = p->next_token.type == T_COMMA;
				parser_next_token(p);
			}
		} else {
			parser_err_unexp(p);
			return NULL;
		}
	}

	return left;
}

static struct sexpr* parse_expr(struct parser* p)
{
	return parse_expr_rec(p, 0, 0);
}


#ifdef TEST

int n_failed;

#define OK "\e[32m\e[1mOK\e[0m "
#define FAIL "\e[41m\e[33m\e[1m!!\e[0m "

#if 0
static void sexpr_print(struct sexpr* e)
{
	if (sexpr_is_atom(e)) {
		if (e->atom.str.ptr != NULL) {
			str_print(&e->atom.str);
		} else {
			assert(!"invalid atom");
		}
	} else if (sexpr_is_list(e)) {
		printf("(");
		int first = 1;
		for (struct sexpr* i = e->sexpr; i; i = i->next) {
			if (!first) printf(" ");
			sexpr_print(i);
			first = 0;
		}
		printf(")");
	} else {
		assert(!"not atom or list?!");
	}
}
#endif

static void wrstr(char** d, const char* s)
{
	strcpy(*d, s);
	*d += strlen(s);
}

static void sexpr_str_rec(struct sexpr* e, char** sp)
{
	if (sexpr_is_atom(e)) {
		if (e->atom.str.ptr != NULL) {
			memcpy(*sp, e->atom.str.ptr, e->atom.str.len);
			*sp += e->atom.str.len;
		} else {
			assert(!"invalid atom");
		}
	} else if (sexpr_is_list(e)) {
		wrstr(sp, "(");
		int first = 1;
		for (struct sexpr* i = e->sexpr; i; i = i->next) {
			if (!first) wrstr(sp, " ");
			sexpr_str_rec(i, sp);
			first = 0;
		}
		wrstr(sp, ")");
	} else {
		assert(!"not atom or list?!");
	}
}

char tmpstr[65536];
static char* sexpr_str(struct sexpr* e)
{
	char* s = tmpstr;
	char* cpy = s;
	sexpr_str_rec(e, &cpy);
	*cpy = 0;
	return s;
}

static void test_parse_expr(char* src, char* expected_sexpr_str)
{
	struct parser p;
	parser_init(&p, src);
	struct sexpr* actual_expr = parse_expr(&p);
	char* actual_expr_str = sexpr_str(actual_expr);
	if (strcmp(actual_expr_str, expected_sexpr_str) == 0) {
		printf(OK "%s => %s\n", src, actual_expr_str);
	} else {
		printf(FAIL "%s parsed to '%s', expected '%s'\n", src, actual_expr_str, expected_sexpr_str);
		n_failed++;
	}
}

int main(int argc, char** argv)
{
	test_parse_expr("123", "123");
	test_parse_expr("foo", "foo");
	test_parse_expr("i=0", "(= i 0)");
	test_parse_expr("i=i+1", "(= i (+ i 1))");
	test_parse_expr("1 + 2 * 3", "(+ 1 (* 2 3))");
	test_parse_expr("1 * 2 + 3", "(+ (* 1 2) 3)");
	test_parse_expr("1 * (2 + 3)", "(* 1 (+ 2 3))");
	test_parse_expr("(1 * (2 + 3))", "(* 1 (+ 2 3))");
	test_parse_expr("i = (1 * (2 + 3))", "(= i (* 1 (+ 2 3)))");
	test_parse_expr("(i = 20) + 22", "(+ (= i 20) 22)");
	test_parse_expr("x*y+3.1415+x", "(+ (+ (* x y) 3.1415) x)");
	test_parse_expr("fn()", "(fn)");
	test_parse_expr("fn(x)", "(fn x)");
	test_parse_expr("i8(x)", "(i8 x)");
	test_parse_expr("4+4(x)", "(+ 4 (4 x))");
	test_parse_expr("(4+4)(x)", "((+ 4 4) x)");
	test_parse_expr("1 + fn(x)", "(+ 1 (fn x))");
	test_parse_expr("fn(2*x+1)", "(fn (+ (* 2 x) 1))");
	test_parse_expr("fn(x)+2", "(+ (fn x) 2)");
	test_parse_expr("fn(x,y)", "(fn x y)");
	test_parse_expr("fn(x+2,3*y)", "(fn (+ x 2) (* 3 y))");
	test_parse_expr("foo(x+2,3*y)*4", "(* (foo (+ x 2) (* 3 y)) 4)");

	if (n_failed) {
		printf("\n %d TEST(S) FAILED\n", n_failed);
		return EXIT_FAILURE;
	} else {
		printf("\n ALL TESTS PASSED\n");
		return EXIT_SUCCESS;
	}
}

#endif
