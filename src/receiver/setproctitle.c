/* ==========================================================================
 * setproctitle.c - Linux/Darwin setproctitle.
 * --------------------------------------------------------------------------
 * Copyright (C) 2010  William Ahern
 * Adaptation (C) 2022  Deep Thought <deeptho@gmail.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to permit
 * persons to whom the Software is furnished to do so, subject to the
 * following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN
 * NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
 * USE OR OTHER DEALINGS IN THE SOFTWARE.
 * ==========================================================================
 */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <stddef.h>	/* NULL size_t */
#include <stdarg.h>	/* va_list va_start va_end */
#include <stdlib.h>	/* malloc(3) setenv(3) clearenv(3) setproctitle(3) getprogname(3) */
#include <stdio.h>	/* vsnprintf(3) snprintf(3) */

#include <string.h>	/* strlen(3) strchr(3) strdup(3) memset(3) memcpy(3) */

#include <errno.h>	/* errno program_invocation_name program_invocation_short_name */




static struct {
	/* original value */
	const char *arg0;

	/* title space available */
	char *base, *end;

	 /* pointer to original nul character within base */
	char *nul;

	char reset;
	int error;
} SPT;


#ifndef SPT_MIN
#define SPT_MIN(a, b) (((a) < (b))? (a) : (b))
#endif

static inline size_t spt_min(size_t a, size_t b) {
	return SPT_MIN(a, b);
} /* spt_min() */



static int spt_copyenv() {
	extern char **environ;
	char *eq;
	int error;

	char * saved = malloc(SPT.end - SPT.base);
	char *pstart = (environ[0] - SPT.base) + saved;
	char *pend = (SPT.end -SPT.base) + saved;
	char* p;

	memcpy(saved, SPT.base, SPT.end - SPT.base);
	for(p = pstart; p[0] != 0 && p <pend ; p+=strlen(p) +1) {
		if (!(eq = strchr(p, '=')))
			setenv(p, "", 1);
		else {
			*eq = '\0';
			error = (0 != setenv(p, eq + 1, 1))? errno : 0;
			*eq = '=';
			if (error)
				goto error;
		}
	}

	return 0;
error:
	return error;
}


static int spt_copyargs(int argc, char *argv[]) {
	char *tmp;
	int i;

	for (i = 1; i < argc || (i >= argc && argv[i]); i++) {
		if (!argv[i])
			continue;

		if (!(tmp = strdup(argv[i])))
			return errno;

		argv[i] = tmp;
	}

	return 0;
} /* spt_copyargs() */

#if 1
void spt_init(int argc, char *argv[]) __attribute__((constructor));
#endif
extern char** environ;
static char* get_start() {
	int len = strlen(program_invocation_name);
	char *pstart = environ[0] - 4096 + len;
	char *pend = environ[0];
	char *p;
	for(p = pend -1 ; p >= pstart; --p) {
		if(*p ==0) {
			if(strncmp(p -len, program_invocation_name, len)==0) {
				return p-len;
			}
		}
	}
	return NULL;
}

static char* get_end() {
	char *pstart = environ[0];
	char *pend = pstart + 16*4096;
	char *p;
	for(p = pstart; p[0] != 0 && p <pend ; p+=strlen(p) +1) {
		/* do nothing*/ ;
	}
	return p-1;
}


void spt_init(int argc, char *argv[]) {
	char* tmp;
	int error;
	char* start = get_start();
	char* end = get_end();
	if (!start || ! end)
		return;
	if (!(SPT.arg0 = strdup(start))) {
		SPT.error = errno;
		return;
	}

	SPT.nul  = start + strlen(start);
	SPT.base = start;
	SPT.end  = end;
	if (!(tmp = strdup(program_invocation_name))) {
		SPT.error = errno;
		return;
	}

	program_invocation_name = tmp;

	if (!(tmp = strdup(program_invocation_short_name))) {
		SPT.error = errno;
		return;
	}


	program_invocation_short_name = tmp;


	if ((error = spt_copyenv())) {
		SPT.error = error;
		return;
	}
	if ((error = spt_copyargs(argc, argv))) {
		SPT.error = error;
		return;
	}


	return;
}


#ifndef SPT_MAXTITLE
#define SPT_MAXTITLE 255
#endif

__attribute__((no_sanitize_address))
void setproctitle(const char *fmt, ...) {
	char buf[SPT_MAXTITLE + 1]; /* use buffer in case argv[0] is passed */
	va_list ap;
	int len;

	if (!SPT.base)
		return;

	if (fmt) {
		va_start(ap, fmt);
		len = vsnprintf(buf, sizeof buf, fmt, ap);
		va_end(ap);
	} else {
		len = snprintf(buf, sizeof buf, "%s", SPT.arg0);
	}

	if (len <= 0) {
		SPT.error = errno;
		return;
	}

	if (!SPT.reset) {
		memset(SPT.base, 0, SPT.end - SPT.base);
		SPT.reset = 1;
	} else {
		memset(SPT.base, 0, spt_min(sizeof buf, SPT.end - SPT.base));
	}

	len = spt_min(len, spt_min(sizeof buf, SPT.end - SPT.base) - 1);
	memcpy(SPT.base, buf, len);
	return;
}
