#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include <stdarg.h>


//////////////////////////////////////////////////////////////////////////////
// LEXER
//////////////////////////////////////////////////////////////////////////////

enum token_type {
	T_NUMBER = 1,

	T_IDENTIFIER,

	//T__KWMIN,
	T_RETURN,
	T_IF,
	T_ELSE,
	T_WHILE,
	T_DO,
	T_FOR,
	T_BREAK,
	T_CONTINUE,
	T_SWITCH,
	T_CASE,
	T_DEFAULT,
	//T__KWMAX,

	T_STRUCT,
	T_ENUM,

	//T__TYPMIN,
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
	//T__TYPMAX,

	T__PNCTMIN,
	T_COMMA,
	T_SEMICOLON,
	T_EQ,
	T_NEQ,
	T_PLUS,
	T_MINUS,
	T_MUL,
	T_DIV,
	T_MOD,
	T_LPAREN,
	T_RPAREN,
	T__PNCTMAX,

	T_WHITESPACE,

	T_EOF
};

struct token {
	enum token_type type;

	// filled out by lexer_emit():
	int string_length;
	const char* string;
	int line, column;
};

struct lexer;

// (returns lexer_state_fn* but I can't do recursive typedefs :-/ )
typedef void* (*lexer_state_fn)(struct lexer*);

struct lexer {
	int src_length;
	const char* src;
	int line, column;
	int pos;
	int start;
	struct token token;
	int has_token;
	lexer_state_fn state_fn;
};

static void* lex_main(struct lexer* l);

static void lexer_init(struct lexer* l, const char* src)
{
	memset(l, 0, sizeof(*l));
	l->src = src;
	l->src_length = strlen(src);
	l->state_fn = lex_main;
}

static int lexer_ch(struct lexer* l)
{
	if (l->pos >= l->src_length) {
		l->pos++;
		return -1;
	}

	int ch = l->src[l->pos]; // TODO proper utf8 decode?
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
	l->token.string = l->src + l->start;
	l->token.string_length = l->pos - l->start;
	l->token.line = l->line;
	l->token.column = l->column;

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
		if (l == t->string_length && memcmp(t->string, s, l) == 0) { \
			t->type = e; \
		} \
	}

static void promote_identifer_if_keyword(struct token* t)
{
	if (t->type != T_IDENTIFIER) return;

	PRM("void", T_VOID);
	PRM("return", T_RETURN);
	PRM("if", T_IF);
	PRM("else", T_ELSE);
	PRM("while", T_WHILE);
	PRM("do", T_DO);
	PRM("for", T_FOR);
	PRM("break", T_BREAK);
	PRM("continue", T_CONTINUE);
	PRM("switch", T_SWITCH);
	PRM("case", T_CASE);
	PRM("default", T_DEFAULT);

	PRM("struct", T_STRUCT);
	PRM("enum", T_ENUM);

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
// PARSER
//////////////////////////////////////////////////////////////////////////////

enum expr_type {
	E_ATOM = 1,
	E_UNARY_OP,
	E_BINARY_OP,
	E_CALL
};

struct expr;

struct call_arg {
	struct expr* expr;
	struct call_arg* next;
};

static struct call_arg** call_arg_add(struct call_arg** lst, struct expr* expr)
{
	struct call_arg* a = calloc(1, sizeof(*a));
	assert(a != NULL);
	a->expr = expr;
	if (*lst != NULL) (*lst)->next = a;
	*lst = a;
	return &a->next;
}

struct expr {
	enum expr_type type;
	struct token token;
	union {
		struct {
			struct expr* operand;
		} unary;
		struct {
			struct expr* left;
			struct expr* right;
		} binary;
		struct {
			struct expr* identifier;
			struct call_arg* args;
		} call;
	};
};

static void expr_print_rec(struct expr* e)
{
	if (e == NULL) return;
	#define PBEGIN putchar('(')
	#define PEND putchar(')')
	#define PSEP putchar(' ')
	#define PVAL(x) fwrite(x->token.string, x->token.string_length, 1, stdout)
	switch (e->type) {
		case E_ATOM:
			PVAL(e);
			break;
		case E_UNARY_OP:
			PBEGIN; PVAL(e); PSEP;
			expr_print_rec(e->unary.operand);
			PEND;
			break;
		case E_BINARY_OP:
			PBEGIN; PVAL(e); PSEP;
			expr_print_rec(e->binary.left);
			PSEP;
			expr_print_rec(e->binary.right);
			PEND;
			break;
		case E_CALL:
			PBEGIN;
			PVAL(e->call.identifier);
			for (struct call_arg* a = e->call.args; a; a = a->next) {
				PSEP;
				expr_print_rec(a->expr);
			}
			PEND;
			break;
	}
	#undef PVAL
	#undef PSEP
	#undef PEND
	#undef PBEGIN
}

static void expr_print(struct expr* e)
{
	expr_print_rec(e);
	putchar('\n');
}

static struct expr* expr_new(struct token token)
{
	struct expr* e = calloc(1, sizeof(*e));
	assert(e != NULL);
	e->token = token;
	return e;
}

static struct expr* expr_new_atom(struct token token)
{
	struct expr* e = expr_new(token);
	e->type = E_ATOM;
	return e;
}

static struct expr* expr_new_unary_op(struct token token, struct expr* operand)
{
	struct expr* e = expr_new(token);
	e->type = E_UNARY_OP;
	e->unary.operand = operand;
	return e;
}

static struct expr* expr_new_binary_op(struct token token, struct expr* left, struct expr* right)
{
	struct expr* e = expr_new(token);
	e->type = E_BINARY_OP;
	e->binary.left = left;
	e->binary.right = right;
	return e;
}

static struct expr* expr_new_call(struct token token, struct expr* identifier)
{
	assert(identifier->token.type == T_IDENTIFIER);
	struct expr* e = expr_new(token);
	e->type = E_CALL;
	e->call.identifier = identifier;
	return e;
}

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

static inline int prefix_bp(enum token_type tt)
{
	switch (tt) {
		case T_PLUS:
		case T_MINUS:
			// TODO
			return 100;
		default:
			return -1;
	}
}

static inline int is_unary_op(enum token_type tt) {
	return prefix_bp(tt) != -1;
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

static inline int is_binary_op(enum token_type tt) {
	switch (tt) {
		case T_PLUS:
		case T_MINUS:
		case T_MUL:
		case T_DIV:
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

static struct expr* parse_expr_rec(struct parser* p, int rbp, int depth)
{
	if (depth >= 1024) {
		parser_errf(p, "maximum expression recursion depth exceeded");
		return NULL;
	}

	struct token t = parser_next_token(p);

	// null denotation
	struct expr* left = NULL;
	{
		enum token_type tt = t.type;
		int bp;
		if (tt == T_NUMBER || tt == T_IDENTIFIER) {
			left = expr_new_atom(t);
		} else if ((bp = prefix_bp(tt)) != -1) {
			struct expr* operand = parse_expr_rec(p, bp, depth+1);
			if (operand == NULL) return NULL;
			if (is_unary_op(tt)) {
				left = expr_new_unary_op(t, operand);
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
			struct expr* right = parse_expr_rec(p, bp, depth+1);
			if (right == NULL) return NULL;
			left = expr_new_binary_op(t, left, right);
		} else if (t.type == T_LPAREN) { // parse call
			if (left->token.type != T_IDENTIFIER) {
				parser_err_unexp(p);
				return NULL;
			}

			left = expr_new_call(t, left);
			struct call_arg** lst = &left->call.args;

			int more = p->next_token.type != T_RPAREN;
			while (more) {
				struct expr* a = parse_expr_rec(p, 0, depth+1);
				if (a == NULL) return NULL;
				lst = call_arg_add(lst, a);

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

static struct expr* parse_expr(struct parser* p)
{
	return parse_expr_rec(p, 0, 0);
}

static void parse(const char* src)
{
	struct parser p;
	memset(&p, 0, sizeof(p));
	lexer_init(&p.lexer, src);
	parser_next_token(&p);

	printf("'%s' -> ", src);
	struct expr* e = parse_expr(&p);
	if (e != NULL) expr_print(e);
}

int main(int argc, char** argv)
{
	parse("123");
	parse("foo");
	parse("1 + 2 * 3");
	parse("1 * 2 + 3");
	parse("foo*bar+3.1415+x");
	parse("foo()");
	parse("foo(x)");
	parse("1 + foo(x)");
	parse("foo(2*x+1)");
	parse("foo(x)+2");
	parse("foo(x,y)");
	parse("foo(x+2,3*y)");
	parse("foo(x+2,3*y)*4");

	return 0;
}

