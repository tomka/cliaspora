/*-
 * Copyright (c) 2013 Marcel Kaiser. All rights reserved.
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
#include <ctype.h>
#include <stdlib.h>
#include <limits.h>
#include <err.h>

#include "types.h"
#include "ssl.h"
#include "http.h"

char *
urlencode(const char *url)
{
	int	   n, buflen = 0;
	char	   *buf = NULL;
	const char nores[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ" \
			     "abcdefghijklmnopqrstuvwxyz" \
			     "0123456789-_.~";

	if (url == NULL)
		return (NULL);
	buflen = strlen(url) * 3 + 1;
	if ((buf = realloc(buf, buflen)) == NULL)
		return (NULL);
	for (n = 0; *url != '\0'; n++, url++) {
		if (strchr(nores, (int)*url) == NULL) {
			(void)snprintf(&buf[n], buflen - n, "%%%02x",
			    (u_char)*url);
			n += 2;
		} else
			buf[n] = *url;
	}
	buf[n] = '\0';
	return (buf);
}

int
get_http_status(ssl_conn_t *cp)
{
	char *p, *q;

	while ((p = ssl_readln(cp)) != NULL) {
		if (strncmp(p, "Status:", 7) != 0)
			continue;	
		for (q = p; (q = strtok(q, " ")) != NULL; q = NULL) {
			if (isdigit(*q))
				return (strtol(q, NULL, 10));
		}
	}
	return (-1);
}

int
http_get(ssl_conn_t *cp, const char *url, const char *cookie,
	 const char *accept, const char *agent)
{
	int  len, n;
	char *rq;

	if (url == NULL) {
		warnx("http_get(): url == NULL");
		return (-1);
	}
	len = strlen(url);
	if (cookie != NULL)
		len += strlen(cookie);
	if (accept != NULL)
		len += strlen(accept);
	if (agent != NULL)
		len += strlen(agent);
	len += strlen("GET HTTP/1.0\nCookie: \nAccept: " \
		      "\nUser-Agent: \nHost: \nLocation\n\n") + 64;
	len += strlen("Accept-Charset: utf-8\n");
	len += strlen("X-Requested-With: XMLHttpRequest\n");
	if ((rq = malloc(len)) == NULL) {
		warn("malloc()");
		return (-1);
	}
	(void)snprintf(rq, len, "GET %s HTTP/1.0\n", url);
	n = strlen(rq);
	(void)snprintf(rq + n, len - n, "Location: %s\n", url);
	n = strlen(rq);
	(void)snprintf(rq + n, len - n, "Host: %s\n", cp->host);
	n = strlen(rq);

	if (cookie != NULL) {
		(void)snprintf(rq + n, len - n, "Cookie: %s\n", cookie);
		n = strlen(rq);
	}
	if (accept != NULL) {
		(void)snprintf(rq + n, len - n, "Accept: %s\n", accept);
		n = strlen(rq);
	}
	if (agent != NULL) {
		(void)snprintf(rq + n, len - n, "User-Agent: %s\n", agent);
		n = strlen(rq);
	}
	(void)snprintf(rq + n, len - n, "Accept-Charset: utf-8\n" \
	    "X-Requested-With: XMLHttpRequest\n");
	n = strlen(rq);
	(void)snprintf(rq + n, len - n, "\n");
	if (ssl_write(cp, rq, strlen(rq)) == -1) {
		free(rq);
		return (-1);
	}
	free(rq);

	return (get_http_status(cp));
}

int
http_post(ssl_conn_t *cp, const char *url, const char *cookie,
	 const char *accept, const char *agent, int type, const char *request)
{
	int  len, n;
	char *rq, *ct;

	if (url == NULL) {
		warnx("http_get(): url == NULL");
		return (-1);
	}
	len = strlen(url);
	if (request != NULL)
		len += strlen(request);
	if (cookie != NULL)
		len += strlen(cookie);
	if (accept != NULL)
		len += strlen(accept);
	if (agent != NULL)
		len += strlen(agent);
	if (type == HTTP_POST_TYPE_JSON)
		ct = "Content-Type: application/json; charset=UTF-8\n";
	else {
		ct = "Content-type: application/x-www-form-" \
			      "urlencoded;charset=utf-8\n";
	}
	len += strlen(ct);
	len += strlen("POST HTTP/1.0\nCookie: \nAccept: \nUser-Agent: \n\n");
	len += strlen("Content-Length: 1234567890\nHost: \nLocation: \n") + 8;
	if ((rq = malloc(len)) == NULL) {
		warn("malloc()");
		return (-1);
	}
	(void)snprintf(rq, len, "POST %s HTTP/1.0\n", url);
	n = strlen(rq);
	(void)snprintf(rq + n, len - n, "Host: %s\n", cp->host);
	n = strlen(rq);
	(void)snprintf(rq + n, len - n, "Location: %s\n", url);
	n = strlen(rq);
	if (cookie != NULL) {
		(void)snprintf(rq + n, len - n, "Cookie: %s\n", cookie);
		n = strlen(rq);
	}
	if (accept != NULL) {
		(void)snprintf(rq + n, len - n, "Accept: %s\n", accept);
		n = strlen(rq);
	}
	if (agent != NULL) {
		(void)snprintf(rq + n, len - n, "User-Agent: %s\n", agent);
		n = strlen(rq);
	}
	if (request != NULL) {
		(void)snprintf(rq + n, len - n, "Content-Length: %d\n%s",
		    (int)strlen(request), ct);
		n = strlen(rq);
		(void)snprintf(rq + n, len - n, "\n%s", request);
	} else
		(void)snprintf(rq + n, len - n, "\n");
	if (ssl_write(cp, rq, strlen(rq)) == -1) {
		free(rq);
		return (-1);
	}
	free(rq);

	return (get_http_status(cp));
}

