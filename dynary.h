#ifndef DYNARY_h

#ifndef DYNARY_API
#define DYNARY_API
#endif

struct dynary {
	void** ptr;
	int n;
	int element_sz;
	int caplog;
};

DYNARY_API void dynary_init(struct dynary* d, void** ptr, int element_sz);
DYNARY_API int dynary_get_cap(struct dynary* d);
DYNARY_API int dynary_is_valid_index(struct dynary* d, int index);
DYNARY_API void* dynary_clear(struct dynary* d, int index);
DYNARY_API void* dynary_append(struct dynary* d);
DYNARY_API void* dynary_insert(struct dynary* d, int index);
DYNARY_API void dynary_erase(struct dynary* d, int index);


#ifdef DYNARY_IMPLEMENTATION

#ifndef DYNARY_GROW_FACTOR_LOG2
#define DYNARY_GROW_FACTOR_LOG2 (2)
#endif

#ifndef DYNARY_memmove
#include <string.h>
#define DYNARY_memmove memmove
#define DYNARY_memset memset
#endif

#ifndef DYNARY_realloc
#include <stdlib.h>
#define DYNARY_realloc realloc
#endif

#ifndef DYNARY_assert
#include <assert.h>
#define DYNARY_assert assert
#endif

DYNARY_API void dynary_init(struct dynary* d, void** ptr, int element_sz)
{
	*ptr = NULL;
	d->ptr = ptr;
	d->n = 0;
	d->element_sz = element_sz;
	d->caplog = 0;
}

DYNARY_API int dynary_get_cap(struct dynary* d)
{
	return 1 << (d->caplog * DYNARY_GROW_FACTOR_LOG2);
}

DYNARY_API void dynary__set_cap(struct dynary* d, int cap)
{
	int old_caplog = d->caplog;
	while (cap > dynary_get_cap(d)) d->caplog++; // grow
	// TODO shrink?
	if (d->caplog == old_caplog) return;
	*d->ptr = DYNARY_realloc(*d->ptr, dynary_get_cap(d));
	DYNARY_assert(*d->ptr != NULL);
}

DYNARY_API int dynary_is_valid_index(struct dynary* d, int index)
{
	return index >= 0 && index < d->n;
}

DYNARY_API void* dynary_clear(struct dynary* d, int index)
{
	DYNARY_assert(dynary_is_valid_index(d, index));
	void* dst = *d->ptr + index * d->element_sz;
	DYNARY_memset(dst, 0, d->element_sz);
	return dst;
}

DYNARY_API void* dynary_append(struct dynary* d)
{
	d->n++;
	dynary__set_cap(d, d->n * d->element_sz);
	return dynary_clear(d, d->n-1);
}

DYNARY_API void* dynary_insert(struct dynary* d, int index)
{
	d->n++;
	dynary__set_cap(d, d->n * d->element_sz);
	DYNARY_assert(dynary_is_valid_index(d, index));
	DYNARY_memmove(*d->ptr + (index+1) * d->element_sz, *d->ptr + index * d->element_sz, (d->n - 1) * d->element_sz);
	return dynary_clear(d, index);
}

DYNARY_API void dynary_erase(struct dynary* d, int index)
{
	DYNARY_assert(dynary_is_valid_index(d, index));
	DYNARY_memmove(*d->ptr + index * d->element_sz, *d->ptr + (index+1) * d->element_sz, (d->n - 1) * d->element_sz);
	d->n--;
	dynary__set_cap(d, d->n * d->element_sz);
}

#endif

#define DYNARY_h
#endif
