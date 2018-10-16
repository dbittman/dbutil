#pragma

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define likely(b) __builtin_expect((bool)(b), true)
#define unlikely(b) __builtin_expect((bool)(b), false)

#define lengthof(x) ({ sizeof((x)) / sizeof((x)[0]); })

#ifndef __unused
#define __unused __attribute__((unused))
#endif

#define __initializer __attribute__((constructor))
#define __packed __attribute__((packed))


#define ___concat(a,b) a##b
#define __concat(a,b) __concat(a,b)

#define defer(x) \
	void __concat(__defer, __COUNTER__) () { ({ x; }); }; \
	__attribute__((cleanup(__bar))) int __concat(__defervar, __COUNTER__);

/*
 * DEBUGGING
 */

#define DEBUG(fmt, ...) \
	fprintf(stderr, "%s:%d :: " fmt, __FILE__, __LINE__, ##__VA_ARGS__)


