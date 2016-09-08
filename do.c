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
	T__META = 1, // for special sexpr tokens

	T_NUMBER,

	T_IDENTIFIER,

	T__IDPREDEF_MIN,

	T_TRUE,
	T_FALSE,

	T__TYPMIN,
	T_BOOL,
	T_INT,
	T_UINT,
	T_INT8,
	T_UINT8,
	T_INT32,
	T_UINT32,
	T_INT64,
	T_UINT64,
	T_FLOAT32,
	T_FLOAT64,
	T__TYPMAX,

	T__IDPREDEF_MAX,

	T_RETURN,
	T_IF,
	T_ELSE,
	T_FOR,
	T_BREAK,
	T_CONTINUE,
	T_FALLTHROUGH,
	T_GOTO,
	T_SWITCH,
	T_CASE,
	T_DEFAULT,

	T_TYPEOF,

	T_TYPE,

	T_CONST,
	T_VAR,
	T_FUNC,
	T_STRUCT,

	//T__MODMIN,
	T_IN,
	T_OUT,
	//T__MODMAX,

	T_COMMA,
	T_SEMICOLON,
	T_ASSIGN,
	T_EQ,
	T_NEQ,
	T_INC,
	T_DEC,
	T_PLUS,
	T_MINUS,
	T_MUL,
	T_DIV,
	T_MOD,
	T_LPAREN,
	T_RPAREN,
	T_LCURLY,
	T_RCURLY,
	T_LBRACKET,
	T_RBRACKET,

	T_WHITESPACE,

	T_EOF
};

static int tt_is_type(enum token_type tt)
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
	int line, previous_token_line, column;
	int pos;
	int start;
	struct token token, previous_token;
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
			{"++", T_INC},
			{"--", T_DEC},
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
			{'{', T_LCURLY},
			{'}', T_RCURLY},
			{'[', T_LBRACKET},
			{']', T_RBRACKET},
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

	PRM("true", T_TRUE);
	PRM("false", T_FALSE);

	PRM("bool", T_BOOL);
	PRM("int", T_INT);
	PRM("uint", T_UINT);
	PRM("int8", T_INT8);
	PRM("uint8", T_UINT8);
	PRM("int32", T_INT32);
	PRM("uint32", T_UINT32);
	PRM("int64", T_INT64);
	PRM("uint64", T_UINT64);

	PRM("float32", T_FLOAT32);
	PRM("float64", T_FLOAT64);
	PRM("return", T_RETURN);
	PRM("if", T_IF);
	PRM("else", T_ELSE);
	PRM("for", T_FOR);
	PRM("break", T_BREAK);
	PRM("continue", T_CONTINUE);
	PRM("fallthrough", T_FALLTHROUGH);
	PRM("goto", T_GOTO);
	PRM("switch", T_SWITCH);
	PRM("case", T_CASE);
	PRM("default", T_DEFAULT);

	PRM("typeof", T_TYPEOF);

	PRM("type", T_TYPE);

	PRM("const", T_CONST);
	PRM("var", T_VAR);
	PRM("func", T_FUNC);
	PRM("struct", T_STRUCT);

	PRM("in", T_IN);
	PRM("out", T_OUT);
}
#undef PRM

static int promote_ws_to_semicolon(struct lexer* l)
{
	if (l->previous_token_line == l->line) return 0;

	enum token_type tt = l->previous_token.type;
	int promote =
		   tt == T_IDENTIFIER
		|| tt == T_NUMBER
		|| (tt > T__IDPREDEF_MIN && tt < T__IDPREDEF_MAX)
		|| tt == T_RETURN
		|| tt == T_BREAK
		|| tt == T_CONTINUE
		|| tt == T_FALLTHROUGH
		|| tt == T_INC
		|| tt == T_DEC
		|| tt == T_RPAREN
		|| tt == T_RBRACKET
		|| tt == T_RCURLY;
	if (promote) l->token.type = T_SEMICOLON;
	return promote;
}

static struct token lexer_next(struct lexer* l)
{
	for (;;) {
		assert(l->state_fn != NULL);
		l->state_fn = l->state_fn(l);
		if (l->has_token) {
			if (l->token.type == T_WHITESPACE && !promote_ws_to_semicolon(l)) continue;
			promote_identifer_if_keyword(&l->token);
			l->has_token = 0;
			l->previous_token = l->token;
			l->previous_token_line = l->line;
			return l->token;
		}
	}
}




//////////////////////////////////////////////////////////////////////////////
// S-EXPRESSIONS
//////////////////////////////////////////////////////////////////////////////

#define SEXPR_TYPE_ATOM (0)
#define SEXPR_TYPE_LIST (1)
#define SEXPR_TYPE_MASK (1)
#define SEXPR_TYPEOF(e) ((e)->flags & SEXPR_TYPE_MASK)

struct sexpr {
	int flags;
	union {
		struct token atom;
		struct sexpr* list;
	};
	struct sexpr* next;
};


static inline int sexpr_is_atom(struct sexpr* e)
{
	return SEXPR_TYPEOF(e) == SEXPR_TYPE_ATOM;
}

static inline int sexpr_is_list(struct sexpr* e)
{
	return SEXPR_TYPEOF(e) == SEXPR_TYPE_LIST;
}

static struct sexpr* _sexpr_new()
{
	struct sexpr* e = calloc(1, sizeof(*e));
	assert(e != NULL);
	return e;
}

static struct sexpr* sexpr_new_atom(struct token atom)
{
	struct sexpr* e = _sexpr_new();
	e->flags = SEXPR_TYPE_ATOM;
	e->atom = atom;
	return e;
}

static struct sexpr* sexpr_new_list(struct sexpr* head, ...)
{
	struct sexpr* e = _sexpr_new();
	e->flags = SEXPR_TYPE_LIST;
	e->list = head;

	if (head != NULL) {
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
	}

	return e;
}

static struct sexpr* sexpr_new_empty_list()
{
	return sexpr_new_list(NULL);
}

static struct sexpr** sexpr_get_append_cursor(struct sexpr* e)
{
	assert(sexpr_is_list(e));
	struct sexpr** cursor = &e->list;
	while (*cursor != NULL) cursor = &((*cursor)->next);
	return cursor;
}

static void sexpr_append(struct sexpr*** append_cursor, struct sexpr* e)
{
	assert(e->next == NULL);
	assert(**append_cursor == NULL);
	**append_cursor = e;
	*append_cursor = &e->next;
}



//////////////////////////////////////////////////////////////////////////////
// PARSER
//////////////////////////////////////////////////////////////////////////////

struct parser {
	struct lexer lexer;
	struct token current_token, next_token, stashed_token;
	int can_rewind, has_stashed_token;

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

static inline struct token parser_next_token(struct parser* p)
{
	p->current_token = p->next_token;
	if (p->has_stashed_token) {
		p->next_token = p->stashed_token;
		p->has_stashed_token = 0;
	} else {
		p->next_token = lexer_next(&p->lexer);
	}
	p->can_rewind = 1;
	return p->current_token;
}

static void parser_init(struct parser* p, char* src)
{
	memset(p, 0, sizeof(*p));
	lexer_init(&p->lexer, src);
	parser_next_token(p);
}

static inline void parser_rewind(struct parser* p)
{
	assert(p->can_rewind);
	p->stashed_token = p->next_token;
	p->next_token = p->current_token;
	p->can_rewind = 0;
	p->has_stashed_token = 1;
	memset(&p->current_token, 0, sizeof(p->current_token));
}

static inline int parser_accept_and_get(struct parser* p, enum token_type tt, struct token* tp)
{
	struct token t = parser_next_token(p);
	if (t.type != tt) {
		parser_rewind(p);
		return 0;
	} else {
		if (tp != NULL) *tp = t;
		return 1;
	}
}

static inline int parser_accept(struct parser* p, enum token_type tt)
{
	return parser_accept_and_get(p, tt, NULL);
}

static inline int parser_expect(struct parser* p, enum token_type tt)
{
	if (!parser_accept(p, tt)) {
		parser_err_exp(p, tt);
		return 0;
	}
	return 1;
}

// parse

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
		case T_LCURLY:
		case T_RPAREN:
		case T_RBRACKET:
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
		if (tt == T_NUMBER || tt == T_IDENTIFIER || tt_is_type(tt)) {
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
				sexpr_append(&cursor, a);

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

static struct sexpr* parse_type_rec(struct parser* p, int depth);

static struct sexpr* parse_struct_body_rec(struct parser* p, int depth)
{
	struct sexpr* lst = sexpr_new_empty_list();
	struct sexpr** lstc = sexpr_get_append_cursor(lst);

	if (!parser_expect(p, T_LCURLY)) return NULL;
	for (;;) {
		struct token t = parser_next_token(p);
		enum token_type tt = t.type;

		if (tt == T_RCURLY) break;

		if (tt != T_IDENTIFIER) {
			parser_err_unexp(p);
			return NULL;
		}

		struct sexpr* typ = parse_type_rec(p, depth+1);
		if (typ == NULL) {
			return NULL;
		}

		struct sexpr* m = sexpr_new_list(sexpr_new_atom(t), typ, NULL);
		sexpr_append(&lstc, m);

		if (parser_accept(p, T_RCURLY)) break;
		if (!parser_expect(p, T_SEMICOLON)) return NULL;
	}

	return lst;
}

static struct sexpr* parse_type_rec(struct parser* p, int depth)
{
	struct sexpr* top = sexpr_new_empty_list();
	struct sexpr** topc = sexpr_get_append_cursor(top);

	int got_mod = 0;
	int got_typ = 0;
	struct sexpr*** typc = &topc;
	struct sexpr** tmpc;
	for (;;) {
		if (parser_accept(p, T_LBRACKET)) {
			struct sexpr* sz = NULL;
			struct sexpr* arr;
			if (!parser_accept(p, T_RBRACKET)) {
				sz = parse_expr_rec(p, 0, depth+1);
				if (sz == NULL || !parser_expect(p, T_RBRACKET)) return NULL;
				arr = sexpr_new_list(sz, NULL);
			} else {
				arr = sexpr_new_empty_list();
			}
			sexpr_append(typc, arr);
			tmpc = sexpr_get_append_cursor(arr);
			typc = &tmpc;
			continue;
		}

		struct token t = parser_next_token(p);
		enum token_type tt = t.type;
		if (!got_mod && (tt == T_IN || tt == T_OUT)) {
			got_mod = 1;
			sexpr_append(typc, sexpr_new_atom(t));
			typc = &topc;
			continue;
		} else if (tt == T_IDENTIFIER || tt_is_type(tt)) {
			got_typ = 1;
			sexpr_append(typc, sexpr_new_atom(t));
			break;
		} else if (tt == T_STRUCT) {
			struct sexpr* body = parse_struct_body_rec(p, depth+1);
			if (body == NULL) return NULL;
			got_typ = 1;
			sexpr_append(typc, sexpr_new_atom(t));
			sexpr_append(typc, body);
			break;
		} else {
			parser_rewind(p);
			break;
		}
	}

	if (!got_mod && !got_typ) {
		parser_err_unexp(p);
		return NULL;
	}

	return top;
}

static struct sexpr* parse_rec(struct parser* p, int depth, int fnlvl)
{
	struct sexpr* ss = sexpr_new_list(NULL);
	struct sexpr** ssc = sexpr_get_append_cursor(ss);

	for (;;) {
		struct token t = parser_next_token(p);
		enum token_type tt = t.type;

		if (tt == T_EOF) break;
		if (fnlvl > 0 && tt == T_RCURLY) {
			parser_rewind(p);
			break;
		}
		if (tt == T_SEMICOLON) continue; // XXX?

		int is_valdef = tt == T_VAR || tt == T_CONST;
		if (is_valdef || tt == T_TYPE || tt == T_FUNC) {
			struct token identifier = parser_next_token(p);
			if (identifier.type != T_IDENTIFIER) {
				parser_err_unexp(p);
				return NULL;
			}
			struct sexpr* def = sexpr_new_list(sexpr_new_atom(t), sexpr_new_atom(identifier), NULL);
			struct sexpr** defc = sexpr_get_append_cursor(def);

			int got_semicolon = 0;

			if (is_valdef) {
				struct sexpr* type = NULL;
				int got_assign = 0;
				int got_type = 0;
				int got_expr = 0;
				if (parser_accept(p, T_ASSIGN)) {
					got_assign = 1;
				} else if (parser_accept(p, T_SEMICOLON)) {
					got_semicolon = 1;
				} else {
					type = parse_type_rec(p, depth+1);
					if (type == NULL) return NULL;
					got_type = 1;
				}

				if (type == NULL) type = sexpr_new_empty_list();
				sexpr_append(&defc, type);

				if (!got_semicolon && (got_assign || parser_accept(p, T_ASSIGN))) {
					struct sexpr* expr = parse_expr_rec(p, 0, depth+1);
					if (expr == NULL) return NULL;
					sexpr_append(&defc, expr);
					got_expr = 1;
				}
			} else if (tt == T_TYPE) {
				struct sexpr* type = parse_type_rec(p, depth+1);
				if (type == NULL) return NULL;
				sexpr_append(&defc, type);
			} else if (tt == T_FUNC) {
				if (!parser_expect(p, T_LPAREN)) return NULL;

				// parse argument list
				struct sexpr* arglist = sexpr_new_empty_list();
				struct sexpr** arglistc = sexpr_get_append_cursor(arglist);
				for (;;) {
					if (parser_accept(p, T_RPAREN)) break;
					struct token id = parser_next_token(p);
					if (id.type != T_IDENTIFIER) {
						parser_err_unexp(p);
						return NULL;
					}
					struct sexpr* typ = parse_type_rec(p, depth+1);
					if (typ == NULL) return NULL;
					sexpr_append(&arglistc, sexpr_new_list(sexpr_new_atom(id), typ, NULL));

					if (parser_accept(p, T_COMMA)) continue;
					if (!parser_expect(p, T_RPAREN)) return NULL;
					break;
				}
				sexpr_append(&defc, arglist);

				// parse return list
				struct sexpr* retlist = sexpr_new_empty_list();
				struct sexpr** retlistc = sexpr_get_append_cursor(retlist);
				if (!parser_accept(p, T_LCURLY)) {
					int is_list = parser_accept(p, T_LPAREN);
					for (;;) {
						struct sexpr* typ = parse_type_rec(p, depth+1);
						if (typ == NULL) return NULL;
						sexpr_append(&retlistc, typ);
						if (is_list && parser_accept(p, T_COMMA)) continue;
						if (!is_list) break;
						if (!parser_expect(p, T_RPAREN)) return NULL;
						break;
					}
					if (!parser_expect(p, T_LCURLY)) return NULL;
				}
				sexpr_append(&defc, retlist);

				struct sexpr* body = parse_rec(p, depth+1, fnlvl+1);
				if (body == NULL) {
					parser_err_unexp(p);
					return NULL;
				}
				sexpr_append(&defc, body);

				if (!parser_expect(p, T_RCURLY)) return NULL;
			} else {
				assert(0);
			}

			if (!got_semicolon && !parser_expect(p, T_SEMICOLON)) return NULL;

			sexpr_append(&ssc, def);

			continue;
		}

		// function body statements
		if (fnlvl > 0) {
			struct sexpr* stmt = sexpr_new_empty_list();
			struct sexpr** stmtc = sexpr_get_append_cursor(stmt);
			switch (tt) {
			case T_RETURN: {
				sexpr_append(&stmtc, sexpr_new_atom(t));
				for (;;) {
					struct sexpr* expr = parse_expr_rec(p, 0, depth+1);
					if (expr == NULL) return NULL;
					sexpr_append(&stmtc, expr);
					if (!parser_accept(p, T_COMMA)) break;
				}
			} break;
			case T_FOR: {
				sexpr_append(&stmtc, sexpr_new_atom(t));
				if (!parser_accept(p, T_LCURLY)) {
					struct sexpr* f0 = parse_expr_rec(p, 0, depth+1);
					if (f0 == NULL) return NULL;
					sexpr_append(&stmtc, f0);
					if (!parser_accept(p, T_LCURLY)) {
						for (int i = 0; i < 2; i++) {
							if (!parser_expect(p, T_SEMICOLON)) return NULL;
							struct sexpr* f1 = parse_expr_rec(p, 0, depth+1);
							if (f1 == NULL) return NULL;
							sexpr_append(&stmtc, f1);
						}
						if (!parser_expect(p, T_LCURLY)) return NULL;
					}
				}
				struct sexpr* scope = parse_rec(p, depth+1, fnlvl);
				if (scope == NULL) return NULL;
				sexpr_append(&stmtc, scope);

				parser_expect(p, T_RCURLY);
			} break;
			case T_IF: {
				struct sexpr*** nc = &stmtc;
				struct sexpr** tmp;
				for (;;) {
					sexpr_append(nc, sexpr_new_atom(t));
					struct sexpr* cond = parse_expr_rec(p, 0, depth+1);
					sexpr_append(nc, cond);
					if (!parser_expect(p, T_LCURLY)) return NULL;
					struct sexpr* tscope = parse_rec(p, depth+1, fnlvl);
					if (tscope == NULL) return NULL;
					sexpr_append(nc, tscope);
					if (!parser_expect(p, T_RCURLY)) return NULL;
					if (parser_accept(p, T_ELSE)) {
						if (parser_accept_and_get(p, T_IF, &t)) {
							tt = t.type;
							struct sexpr* fscope1 = sexpr_new_empty_list();
							struct sexpr* fscope0 = sexpr_new_list(fscope1, NULL);
							sexpr_append(nc, fscope0);
							tmp = sexpr_get_append_cursor(fscope1);
							nc = &tmp;
							continue;
						} else {
							if (!parser_expect(p, T_LCURLY)) return NULL;
							struct sexpr* fscope = parse_rec(p, depth+1, fnlvl);
							if (fscope == NULL) return NULL;
							sexpr_append(nc, fscope);
							if (!parser_expect(p, T_RCURLY)) return NULL;
						}
					}
					break;
				}
			} break;
			case T_SWITCH:
				assert(!"TODO switch");
				break;
			case T_BREAK:
			case T_CONTINUE:
			case T_FALLTHROUGH:
				sexpr_append(&stmtc, sexpr_new_atom(t));
				break;
			default:
				parser_rewind(p);
				stmt = parse_expr_rec(p, 0, depth+1);
				if (stmt == NULL) return NULL;
				break;
			}

			sexpr_append(&ssc, stmt);

			if (!parser_expect(p, T_SEMICOLON)) return NULL;

			continue;
		}

		parser_err_unexp(p);
		return NULL;
	}
	return ss;
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
		for (struct sexpr* i = e->list; i; i = i->next) {
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

static void validate(struct parser* p, char* src, struct sexpr* actual_sexpr, char* expected_sexpr_str)
{
	if (actual_sexpr == NULL) {
		printf(FAIL "parse error for '%s'\n", src);
		return;
	}

	char* actual_sexpr_str = sexpr_str(actual_sexpr);
	if (strcmp(actual_sexpr_str, expected_sexpr_str) == 0) {
		printf(OK "%s => %s\n", src, actual_sexpr_str);
	} else {
		printf(FAIL "%s parsed to '%s', expected '%s'\n", src, actual_sexpr_str, expected_sexpr_str);
		n_failed++;
	}
}

static void test_parse_expr(char* src, char* expected_sexpr_str)
{
	struct parser p;
	parser_init(&p, src);
	struct sexpr* actual_sexpr = parse_expr_rec(&p, 0, 0);
	validate(&p, src, actual_sexpr, expected_sexpr_str);
}

static void test_parse(char* src, char* expected_sexpr_str)
{
	struct parser p;
	parser_init(&p, src);
	struct sexpr* actual_sexpr = parse_rec(&p, 0, 0);
	validate(&p, src, actual_sexpr, expected_sexpr_str);
}

static void test_parse_body(char* src, char* expected_sexpr_str)
{
	struct parser p;
	parser_init(&p, src);
	struct sexpr* actual_sexpr = parse_rec(&p, 1, 1);
	validate(&p, src, actual_sexpr, expected_sexpr_str);
}

int main(int argc, char** argv)
{
	#define PSZ(T) printf("sizeof(" #T ") = %zd\n", sizeof(T));
	PSZ(struct sexpr);
	#undef PSZ

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

	test_parse("var x = 5;", "((var x () 5))");
	test_parse("const x = 5;", "((const x () 5))");
	test_parse("var x int = 5;", "((var x (int) 5))");
	test_parse("var x int;", "((var x (int)))");
	test_parse("var x []int;", "((var x ((int))))");
	test_parse("var x [][]int;", "((var x (((int)))))");
	test_parse("var x [4]int;", "((var x ((4 int))))");
	test_parse("var x [5][4]float32;", "((var x ((5 (4 float32)))))");
	test_parse("var x out int;", "((var x (out int)))");
	test_parse("var x [3]out [4]int;", "((var x ((3 out) (4 int))))");
	test_parse("const x [2][3]out [4][5]int = 5;", "((const x ((2 (3 out)) (4 (5 int))) 5))");
	test_parse("var x [N*K]int;", "((var x (((* N K) int))))");
	test_parse("type MyInt int;", "((type MyInt (int)))");
	test_parse("type x y;", "((type x (y)))");
	test_parse("type x [4]y;", "((type x ((4 y))))");
	test_parse("var x; const y;", "((var x ()) (const y ()))");
	test_parse("var x struct { x int; y float; };", "((var x (struct ((x (int)) (y (float))))))");
	test_parse("var x struct { x int; y float };", "((var x (struct ((x (int)) (y (float))))))");
	test_parse("var x struct { x int };", "((var x (struct ((x (int))))))");
	test_parse("var x [4]struct { x int; y float; };", "((var x ((4 struct ((x (int)) (y (float)))))))");
	test_parse("var x struct { x struct { y int; }; };", "((var x (struct ((x (struct ((y (int)))))))))");
	test_parse("type T struct { x struct { y int; }; };", "((type T (struct ((x (struct ((y (int)))))))))");
	test_parse("const x in;", "((const x (in)))");
	test_parse("func fn(){};", "((func fn () () ()))");
	test_parse("func v42() int {return 42;};", "((func v42 () ((int)) ((return 42))))");
	test_parse("func fn() int {var x = 4; return x*x;};", "((func fn () ((int)) ((var x () 4) (return (* x x)))))");
	test_parse("func fn(x int) int {return x*x;};", "((func fn ((x (int))) ((int)) ((return (* x x)))))");
	test_parse("func fn(x int, y int) (int, int) {return x+y,x-y;};", "((func fn ((x (int)) (y (int))) ((int) (int)) ((return (+ x y) (- x y)))))");

	test_parse_body("x=x+1;y=y-1;", "((= x (+ x 1)) (= y (- y 1)))");
	test_parse_body("break;continue;", "((break) (continue))");
	test_parse_body("for{};", "((for ()))");
	test_parse_body("for{x=x+1;};", "((for ((= x (+ x 1)))))");
	test_parse_body("for t {};", "((for t ()))");
	test_parse_body("for a;b;c {};", "((for a b c ()))");
	test_parse_body("for {break;continue;};break;continue;", "((for ((break) (continue))) (break) (continue))");
	test_parse_body("if 1 {};", "((if 1 ()))");
	test_parse_body("if 1 {} else {};", "((if 1 () ()))");
	test_parse_body("if 1 {} else if 2 {};", "((if 1 () ((if 2 ()))))");
	test_parse_body("if 1 {} else if 2 {} else {};", "((if 1 () ((if 2 () ()))))");
	test_parse_body("if 1 {break;} else if 2 {continue;} else {break;};", "((if 1 ((break)) ((if 2 ((continue)) ((break))))))");
	test_parse_body("if 1 { if 2 { break; continue; } else { break; }; };", "((if 1 ((if 2 ((break) (continue)) ((break))))))");

	test_parse_body("1 +\n2\n", "((+ 1 2))");

	if (n_failed) {
		printf("\n %d TEST(S) FAILED\n", n_failed);
		return EXIT_FAILURE;
	} else {
		printf("\n ALL TESTS PASSED\n");
		return EXIT_SUCCESS;
	}
}

#endif
