#ifndef LSL4D_H

#include "dynary.h"

#if 0
struct l4d_deck {
};
#endif

struct l4d_node;

struct l4d_code {
	struct dynary code_dy;
	char* code;

	int refcount;
	struct l4d_code* next;
};

struct l4d_container {
	struct dynary nodes_dy;
	struct l4d_node* nodes;

	int refcount;
	struct l4d_container* next;
};

struct l4d_nodemeta {
	int x;
	int y;
	int iusr0;
};

struct l4d_node {
	int type;
	union {
		struct l4d_code* code;
		struct l4d_container* container;
	};
	struct l4d_nodemeta meta;
};

struct l4d {
	#if 0
	struct dynary decks_dy;
	struct l4d_deck* decks;
	#endif

	struct l4d_code* codes;
	struct l4d_container* containers;
	struct l4d_container* root_container;
};

void l4d_init(struct l4d* d);

#define LSL4D_H
#endif
