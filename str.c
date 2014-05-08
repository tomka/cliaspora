/*-
 * Copyright (c) 2014 Marcel Kaiser. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifdef __linux__
# define _GNU_SOURCE
#endif

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <ctype.h>
#include <err.h>
#include <errno.h>

char *
strdupstrcat(char **buf, size_t *remain, const char *str, size_t len)
{
	char   *p;
	size_t sz;

	if (len + 1 >= *remain) {
		if (*buf != NULL)
			sz = len + 32 + strlen(*buf) + *remain;
		else
			sz = len + 32;
		if ((p = realloc(*buf, sz)) == NULL) {
			warn("realloc()"); return (NULL);
		}
		if (*buf == NULL)
			*p = '\0';
		*buf = p; *remain += len + 32;
	}
	*remain -= (len + 1);
	(void)strncat(*buf, str, len);
	return (*buf);
}

char *
strduprintf(const char *fmt, ...)
{
	char	*str, *buf, tmp[64];
	size_t  bufsz;
	va_list ap;

	va_start(ap, fmt); buf = NULL; bufsz = 0;
	while (*fmt != '\0') {
		if (*fmt == '%') {
			if (*++fmt == '\0') {
				va_end(ap); return (buf);
			}
		} else {
			if (strdupstrcat(&buf, &bufsz, fmt++, 1) == NULL)
				goto error;
			continue;
		}
		switch(*fmt++) {
		case '%':
			if (strdupstrcat(&buf, &bufsz, "%", 1) == NULL)
				goto error;
			break;	
		case 'c':
			(void)snprintf(tmp, sizeof(tmp), "%c",
			    va_arg(ap, int));
			if (strdupstrcat(&buf, &bufsz, tmp, 1) == NULL)
				goto error;
			break;
		case 'd':
			(void)snprintf(tmp, sizeof(tmp), "%d",
			    va_arg(ap, int));
			if (strdupstrcat(&buf, &bufsz, tmp,
			    strlen(tmp)) == NULL)
				goto error;
			break;
		case 's':
			str = va_arg(ap, char *);
			if (strdupstrcat(&buf, &bufsz, str,
			    strlen(str)) == NULL)
				goto error;
			break;
		}
	}
	va_end(ap);

	return (buf);
error:
	va_end(ap); free(buf);
	return (NULL);
}


