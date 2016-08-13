#include <stdlib.h>
#include <string.h>

#include "l4d.h"

static struct l4d_container* new_container(struct l4d* d)
{
	struct l4d_container* c = calloc(1, sizeof(*c));
	dynary_init(&c->nodes_dy, (void**) &c->nodes, sizeof(*c->nodes));

	// XXX test
	for (int i = 0; i < 20; i++) {
		struct l4d_node* n = dynary_append(&c->nodes_dy);
		n->meta.x = i * 50;
		n->meta.y = i * 25;
	}

	c->next = d->containers;
	d->containers = c;

	return c;
}

void l4d_init(struct l4d* d)
{
	memset(d, 0, sizeof(*d));
	d->root_container = new_container(d);
	d->root_container->refcount++;
}
