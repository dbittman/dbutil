#pragma once

#include "dbu-core.h"
#include <assert.h>

/* Benchmarking framework for microbenchmarking. */

struct timer {
	/* == HOT; first cache line == */

	uint64_t (*get_ns)(const struct timer *t);
	/* calib needs to calculate the time cost of calling get_ns, _and_ the
	 * accuracy of the timer, allowing us to calculate observer effect and
	 * instrumentation error. */

	/* == any cache line == */

	void (*calib)(struct timer *t);
	uint64_t precision_ns; /* smallest unit of time that the timer can measure, in ns. */
	uint64_t get_cost_ns;  /* cost of calling get_ns on this timer, in ns. */
	double instr_err;
	const char *name;
	bool calibrated;
};

struct bench {
	/* == HOT; first cache line == */
	uint64_t start_time_ns;
	int64_t iters_remaining;
	const struct timer *timer;
	bool started;

	/* == any cache line == */
	const char *name;
	void (*fn)(struct bench *);
	struct bench *next;
	uint64_t elapsed;
	double elapsed_err;
	void *arg;
	uint64_t iters_total, iters_complete;
};

static inline bool __bench_continue(struct bench *b, long iters)
{
	if(likely(b->iters_remaining >= iters)) {
		b->iters_remaining -= iters;
		return true;
	}

	if(unlikely(!b->started)) {
		assert(b->timer->calibrated);
		b->started = true;
		b->iters_remaining = b->iters_total;
		b->start_time_ns = b->timer->get_ns(b->timer);
		return true;
	}
	//b->iters_complete += iters;
	//if(likely(b->iters_complete < b->iters_total)) {
	//	return true;
	//}

	uint64_t end = b->timer->get_ns(b->timer);
	b->elapsed = end - b->start_time_ns;
	b->iters_remaining -= iters;
	b->iters_complete = b->iters_total + -b->iters_remaining;
	return false;
}

#define bench_continue(b) __bench_continue(b, 1)

#include <math.h>
#include <time.h>

static inline uint64_t _cgt_get_ns(const struct timer *t)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_nsec + ts.tv_sec * 1e9;
}

static inline bool __stats_normal_quicktest(double mean, double med)
{
	const double thresh = 0.005;
	return fabs(mean - med) / fmax(mean, med) < thresh;
}

#include <stdlib.h>
static int _cmp_dbl(const void *p1, const void *p2)
{
	double a = *(const double *)p1;
	double b = *(const double *)p2;
	return (int)(a - b);
}

#define DIST_NORMAL 0
#define DIST_NOT_NORMAL 1
#define DIST_TOO_MANY_OUTLIERS 2

static void __stats_count_stats(double *base,
  size_t len,
  double *mean,
  double *median,
  double *stddev)
{
	qsort(base, len, sizeof(double), _cmp_dbl);

	double sum = 0, sum2 = 0;
	// double min=UINT64_MAX;
	// double max=0;
	for(size_t i = 0; i < len; i++) {
		sum += base[i];
		sum2 += base[i] * base[i];
		// if(base[i] > max) max = base[i];
		// if(min > base[i]) min = base[i];
	}

	*mean = (double)sum / len;
	*stddev = sqrt(((double)sum2 - (double)sum * sum / len) / (double)(len - 1));
	// med = min + (max - min) / 2.f;
	size_t middle = len / 2;
	if(len % 2) {
		/* odd, median is easy */
		*median = base[middle];
	} else {
		*median = ((double)base[middle] + (double)base[middle - 1]) / 2.f;
	}
}

static int __stats_get_normal(double *base,
  size_t *start,
  size_t *end,
  double *mean,
  double *median,
  double *stddev)
{
	__stats_count_stats(base, *end, mean, median, stddev);
	/* find and remove outliers */

	bool found_ok = false;
	size_t outliers = 0;
	size_t orig_samp_sz = *end;
	*start = 0;
	for(size_t i = 0; i < *end; i++) {
		if(fabs(base[i] - *mean) > 3.f * (*stddev)) {
			if(found_ok) {
				(*end)--;
			} else {
				(*start)++;
			}

			outliers++;
		} else {
			found_ok = true;
		}
	}

	if((double)outliers / (double)orig_samp_sz > 0.25) {
		return DIST_TOO_MANY_OUTLIERS;
	}

	__stats_count_stats(base + *start, *end - *start, mean, median, stddev);

	return __stats_normal_quicktest(*mean, *median) ? DIST_NORMAL : DIST_NOT_NORMAL;
}

static inline void __timer_generic_calibrate(struct timer *t)
{
	bool done = false;
	uint64_t max, min;
	double mean, std, med;
	int iters;
	const int max_iters = 1000;
	for(int i = 0; i < 100; i++)
		t->get_ns(t);
	for(iters = 100; iters < max_iters; iters += 100) {
		int bcount = 1024;

		uint64_t *buckets = malloc(sizeof(uint64_t) * bcount);
		for(int i = 0; i < bcount * iters; i++) {
			uint64_t ns = t->get_ns(t);
			buckets[i & (bcount - 1)] = ns;
		}

		size_t smsz = bcount - 1;
		double *dbuck = malloc(sizeof(double) * smsz);
		for(int i = 0; i < bcount - 1; i++) {
			dbuck[i] = (double)buckets[i + 1] - buckets[i];
		}

		size_t start = 0;
		size_t end = bcount - 1;
		int r = __stats_get_normal(dbuck, &start, &end, &mean, &med, &std);

		free(dbuck);
		free(buckets);
		/* if we got back a normal distribution, we're done */
		if(r == DIST_NORMAL)
			break;
		/* otherwise, consider: we're dealing with integer timers, so if the
		 * median (which will always be an integer!) and mean are within the
		 * precision of each other, we're also okay. Though if we got too many
		 * outliers, we'll probably want to rerun the tests. */
		if(r != DIST_TOO_MANY_OUTLIERS && fabs(mean - med) < t->precision_ns)
			break;
	}

	if(iters >= max_iters) {
		return;
	}

	t->get_cost_ns = mean + std * 1.96;

	t->instr_err = ((double)t->precision_ns + std * 1.96 + t->get_cost_ns / 2.f) * 2.f;
	t->calibrated = true;
}

static inline void _cgt_calib(struct timer *t)
{
	struct timespec ts;
	clock_getres(CLOCK_MONOTONIC, &ts);
	t->precision_ns = ts.tv_nsec + ts.tv_sec * 1e9;

	t->calibrated = false;
	__timer_generic_calibrate(t);
}

#define TIMER(nm, _name, get, cal)                                                                 \
	struct timer __timer_##nm = {                                                                  \
		.name = #_name,                                                                            \
		.get_ns = get,                                                                             \
		.calib = cal,                                                                              \
	};                                                                                             \
	__attribute__((constructor)) static inline void __timer_##nm##_##_name##_init(void)            \
	{                                                                                              \
		__timer_##nm.calib(&__timer_##nm);                                                         \
	}

static TIMER(cgt, cgt_mon, _cgt_get_ns, _cgt_calib);

#define bench_default_timer (&__timer_cgt)

static struct bench *__bench_list;

#define BENCHARG(_fn, nm, _arg)                                                                    \
	struct bench __bench_##_fn##_##nm = {                                                          \
		.timer = bench_default_timer,                                                              \
		.name = #nm,                                                                               \
		.fn = _fn,                                                                                 \
		.arg = (void *)_arg,                                                                       \
	};                                                                                             \
	__attribute__((constructor)) static inline void __bench_##_fn##_##nm##_init(void)              \
	{                                                                                              \
		struct bench *mb = &__bench_##_fn##_##nm;                                                  \
		mb->next = __bench_list;                                                                   \
		__bench_list = mb;                                                                         \
	}

#define BENCH(f, n) BENCHARG(f, n, NULL)

static inline void __bench_baseline_loop(struct bench *b)
{
	while(likely(bench_continue(b)))
		;
}

#include <stdio.h>
static inline void __bench_pretty_print(struct bench *b)
{
	double per_iter = (double)b->elapsed / (double)b->iters_complete;
	const char *units[] = {
		"ns",
		"us",
		"ms",
		"s",
	};
	int unit = 0;
	while(per_iter > 1000.f && unit < (sizeof(units) / sizeof(units[0])) - 1) {
		per_iter /= 1000.f;
		unit++;
	}

	fprintf(stderr,
	  "[b] %10s %10ld %10.4lf %10.4lg%s err %6.4lfns\n",
	  b->name,
	  b->iters_complete,
	  (double)b->elapsed / (double)1e9,
	  per_iter,
	  units[unit],
	  b->elapsed_err / b->iters_complete);
}

static inline void bench_timer_pretty_print(struct timer *t)
{
	fprintf(stderr,
	  "[t] %10s %8ldns %8ldns %8.3lfns\n",
	  t->name,
	  t->precision_ns,
	  t->get_cost_ns,
	  t->instr_err);
}

static inline void __bench_do_run(struct bench *b)
{
	int iters = 100;
	while(true) {
		b->iters_total = iters;
		b->iters_remaining = 0;
		b->started = false;
		b->fn(b);
		if(b->elapsed > 2 * b->timer->instr_err && b->elapsed > 1e6) {
			b->elapsed_err = (b->timer->instr_err * 2.f);
			break;
		}
		iters *= 10;
	}
}

static inline void bench_escopt(void *p)
{
	asm volatile("" ::"g"(p) : "memory");
}

#ifdef BENCH_DRIVER

#include "dbu-opts.h"

struct __bench_opt {
	bool print_timers, print_empty;
	int num_runs;
};

static struct __bench_opt __bench_opt = {
	.print_timers = false,
	.print_empty = false,
	.num_runs = 1,
};

struct option __bench_driver_options[] = {
	OPTION(OPTION_BOOL, &__bench_opt.print_timers, "t", "Print timers information"),
	OPTION(OPTION_BOOL, &__bench_opt.print_empty, "e", "Print empty loop information"),
	OPTION(OPTION_INT, &__bench_opt.num_runs, "r", "Number of runs for each benchmark"),
};

static inline void bench_run(void)
{
	struct bench empty = {
		.fn = __bench_baseline_loop,
		.name = "empty-loop",
		.timer = bench_default_timer,
	};
	if(__bench_opt.print_timers) {
		fprintf(stderr, "[t]      TIMER  PRECISION   GET-COST      ERROR\n");
		bench_timer_pretty_print(bench_default_timer);
		fprintf(stderr, "[t] --------------------------------\n");
	}

	__bench_do_run(&empty);
	double loop_cost = (double)empty.elapsed / empty.iters_complete;
	double loop_cost_err = (double)empty.timer->instr_err / empty.iters_complete;

	fprintf(stderr, "[b]       NAME      ITERS       TIME                 TIME/iter\n");
	if(__bench_opt.print_empty) {
		__bench_pretty_print(&empty);
	}
	for(int i = 0; i < __bench_opt.num_runs; i++) {
		for(struct bench *b = __bench_list; b; b = b->next) {
			__bench_do_run(b);
			if(b->elapsed > loop_cost * b->iters_complete)
				b->elapsed -= loop_cost * b->iters_complete;
			else
				b->elapsed = 0;
			/* errors add in quadrature */
			b->elapsed_err =
			  sqrt(b->elapsed_err * b->elapsed_err
			       + loop_cost_err * loop_cost_err * b->iters_complete * b->iters_complete);
			__bench_pretty_print(b);
		}
	}
}

int main(int argc, char **argv)
{
	db_options_parse(argc, argv, __bench_driver_options, lengthof(__bench_driver_options));
	bench_run();
}

#endif
