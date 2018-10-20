#pragma once

#include "dbu-core.h"

enum {
	OPTION_INT,
	OPTION_BOOL,
	OPTION_STRING,
};

struct option {
	void *p;
	int type;
	const char *shrt, *desc;
	bool seen;
};

#define OPTION(t, _p, so, dsc)                                                                     \
	(struct option)                                                                                \
	{                                                                                              \
		.type = t, .p = _p, .shrt = so, .desc = dsc                                                \
	}

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static inline void db_options_usage(struct option *opts, size_t num)
{
	fprintf(stderr, "SWITCH     :    DEFAULT : DESCRIPTION\n");
	for(int i = 0; i < num; i++) {
		fprintf(stderr, "-%s ", opts[i].shrt);
		switch(opts[i].type) {
			case OPTION_BOOL:
				fprintf(stderr,
				  "        : %10s : %s\n",
				  *(bool *)opts[i].p ? "true" : "false",
				  opts[i].desc);
				break;
			case OPTION_INT:
				fprintf(stderr, "INTEGER : %10d : %s\n", *(int *)opts[i].p, opts[i].desc);
				break;
			case OPTION_STRING:
				fprintf(stderr, "STRING  : %10s : %s\n", *(char **)opts[i].p, opts[i].desc);
				break;
		}
	}
}

static inline void db_options_parse(int argc, char **argv, struct option *opts, size_t num)
{
	char *str = calloc(num * 2 + 1, sizeof(char));
	strcat(str, "h");
	for(int i = 0; i < num; i++) {
		strcat(str, opts[i].shrt);
		if(opts[i].type != OPTION_BOOL) {
			strcat(str, ":");
		}
	}
	int c;
	while((c = getopt(argc, argv, str)) != EOF) {
		bool found = false;
		if(c == 'h') {
			db_options_usage(opts, num);
			continue;
		}
		for(int i = 0; i < num; i++) {
			if(c == opts[i].shrt[0]) {
				switch(opts[i].type) {
					case OPTION_INT:
						/* TODO: deal with errors */
						*(int *)opts[i].p = strtol(optarg, NULL, 0);
						break;
					case OPTION_BOOL:
						if(!opts[i].seen)
							*(bool *)opts[i].p = !*(bool *)opts[i].p;
						break;
					case OPTION_STRING:
						*(char **)opts[i].p = strdup(optarg);
						break;
				}
				opts[i].seen = found = true;
				goto next;
			}
		}
	next : {
	}
	}
	free(str);
}
