
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * UTILITY
 */

#ifndef __unused
#define __unused __attribute__((unused))
#endif

#define __initializer __attribute__((constructor))
#define __packed __attribute__((packed))


#define __concat(a,b) a##b
#define concat(a,b) __concat(a,b)

#define defer(x) \
	void concat(__defer, __COUNTER__) () { ({ x; }); }; \
	__attribute__((cleanup(__bar))) int concat(__defervar, __COUNTER__);


/*
 * DEBUGGING
 */

#define DEBUG(fmt, ...) \
	fprintf(stderr, "%s:%d :: " fmt, __FILE__, __LINE__, ##__VA_ARGS__)



/*
 * TIMING
 */

static inline void timespec_diff(const struct timespec *start, const struct timespec *stop, struct timespec *result)
{
    if ((stop->tv_nsec - start->tv_nsec) < 0) {
        result->tv_sec = stop->tv_sec - start->tv_sec - 1;
        result->tv_nsec = stop->tv_nsec - start->tv_nsec + 1000000000;
    } else {
        result->tv_sec = stop->tv_sec - start->tv_sec;
        result->tv_nsec = stop->tv_nsec - start->tv_nsec;
    }
}

#define time_scope(start, end, diff) \
	clock_gettime(CLOCK_MONOTONIC, &start); \
	defer( clock_gettime(CLOCK_MONOTONIC, &end); timespec_diff(&start, &end, &diff); );

#define time_block(diff, x) \
	({ \
	 struct timespec start, end; \
	 clock_gettime(CLOCK_MONOTONIC, &start); \
	 ({ x; }); \
	 clock_gettime(CLOCK_MONOTONIC, &end); \
	 timespec_diff(&start, &end, &diff); \
	 })




/*
 * INTEGER HASHTABLE (chaining)
 */

struct ihelem {
	struct ihelem *next, *prev;
};

struct ihtable {
	int bits;
	bool fl;
	struct ihelem *table[];
};

#define ihtable_size(bits) \
	(sizeof(struct ihtable) + ((1ul << (bits)) + 1) * sizeof(struct ihelem))

#define DECLARE_IHTABLE(name, nbits) \
	struct ihtable name = { \
		.table = { [0 ... (1ul << nbits)] = 0 }, \
		.bits = nbits, \
	}

static inline void ihtable_init(struct ihtable *t, int bits)
{
	for(size_t i=0;i<(1ul << bits);i++) {
		t->table[i] = NULL;
	}
	t->bits = bits;
}

__attribute__((used))
static void _iht_ctor(void *sz, void *obj)
{
	struct ihtable *iht = obj;
	ihtable_init(iht, (long)sz);
}

#define GOLDEN_RATIO_64 0x61C8864680B583EBull

static inline uint64_t hash64(uint64_t val)
{
	return val * GOLDEN_RATIO_64;
}

static inline size_t hash64_sz(uint64_t key, int bits)
{
	return key * GOLDEN_RATIO_64 >> (sizeof(size_t)*8 - bits);
}

static inline size_t hash128_sz(uint128_t key, int bits)
{
	return hash64((uint64_t)key ^ hash64(key >> 64)) >> (sizeof(size_t)*8 - bits);
}

#define ihtable_insert(t, e, key) \
	__ihtable_insert((t), \
		sizeof(key) > 8 ? hash128_sz((key), (t)->bits) : hash64_sz((key), (t)->bits), \
		(e))

static inline void __ihtable_insert(struct ihtable *t, int bucket, struct ihelem *e)
{
	e->next = t->table[bucket];
	if(t->table[bucket]) t->table[bucket]->prev = e;
	t->table[bucket] = e;
	e->prev = NULL;
}

#define ihtable_remove(t, e, key) \
	__ihtable_remove((t), \
		sizeof(key) > 8 ? hash128_sz((key), (t)->bits) : hash64_sz((key), (t)->bits), \
		(e))

static inline void __ihtable_remove(struct ihtable *t, int bucket, struct ihelem *e)
{
	if(e->prev == NULL) {
		t->table[bucket] = e->next;
	} else {
		e->prev->next = e->next;
	}
	if(e->next) {
		e->next->prev = e->prev;
	}
}

#define ihtable_find(t,key,type,memb,keymemb) ({\
		type *ret = NULL; \
		int bucket = sizeof(key) > 8 ? hash128_sz((key), (t)->bits) \
		                             : hash64_sz((key), (t)->bits); \
		for(struct ihelem *e = (t)->table[bucket];e;e=e->next) { \
			type *obj = container_of(e, type, memb); \
			if(obj->keymemb == (key)) { \
				ret = obj; \
				break; \
			} \
		}; \
		ret;})















/* 
 * DOUBLE LINKED LIST
 */

struct list {
	struct list *next, *prev;
};

#define DECLARE_LIST(name) struct list name = { &name, &name }

static inline void list_init(struct list *l)
{
	l->prev = l;
	l->next = l;
}

#define list_empty(l) ((l)->next == (l))

static inline void list_insert(struct list *list, struct list *entry)
{
	entry->prev = list;
	entry->next = list->next;
	entry->prev->next = entry;
	entry->next->prev = entry;
}

static inline void list_remove(struct list *entry)
{
	entry->next->prev = entry->prev;
	entry->prev->next = entry->next;
}

static inline struct list *list_pop(struct list *l)
{
	struct list *next = l->next;
	list_remove(next);
	return next == l ? NULL : next;
}

static inline struct list *list_dequeue(struct list *l)
{
	struct list *prev = l->prev;
	list_remove(prev);
	return prev == l ? NULL : prev;
}

#define list_entry(e, type, memb) \
	container_of(e, type, memb)

#define list_entry_next(item, memb) \
	list_entry((item)->memb.next, typeof(*(item)), memb)

#define list_entry_prev(item, memb) \
	list_entry((item)->memb.prev, typeof(*(item)), memb)

#define list_iter_start(list) \
	(list)->next

#define list_iter_end(list) list

#define list_iter_next(e) (e)->next
#define list_iter_prev(e) (e)->prev

