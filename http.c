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
#include <ctype.h>
#include <stdlib.h>
#include <limits.h>
#include <err.h>

#include "types.h"
#include "ssl.h"
#include "http.h"
#include "str.h"

#define HTTP_TMPL_POST_RQ	"POST %s HTTP/1.0\r\n"
#define HTTP_TMPL_GET_RQ	"GET %s HTTP/1.0\r\n"
#define HTTP_TMPL_DELETE_RQ	"DELETE %s HTTP/1.0\r\n"
#define HTTP_TMPL_COOKIE	"Cookie: %s\r\n"
#define HTTP_TMPL_CONTENT_TYPE	"Content-Type: %s\r\n"
#define HTTP_TMPL_CONTENT_LEN	"Content-Length: %d\r\n"
#define HTTP_TMPL_USER_AGENT	"User-Agent: %s\r\n"
#define HTTP_TMPL_HOST	  	"Host: %s\r\n"
#define HTTP_TMPL_ACCEPT	"Accept: %s\r\n"
#define HTTP_TMPL_LOCATION	"Location: %s\r\n"
#define HTTP_TMPL_CHARSET	"Charset: %s\r\n"

typedef struct http_req_s {
	int cl;			/* Content length */
	int type;		/* GET, POST, DELETE */
#define HTTP_RQ_TYPE_GET    1
#define HTTP_RQ_TYPE_POST   2
#define HTTP_RQ_TYPE_DELETE 3
	const char *url;
	const char *ua;		/* User agent */
	const char *cs;		/* Charset */
	const char *ct;		/* Content type */
	const char *accept;	/* Accepted document type. */
	const char *host;
	const char *location;
	const char *cookie;
} http_req_t;

char *
http_gen_req(http_req_t *r)
{
	int    i, lc;
	char   *rq, *ln[16];
	size_t rqsz;

	rq = NULL; rqsz = 0; lc = 0;
	switch (r->type) {
	case HTTP_RQ_TYPE_GET:
		ln[lc++] = strduprintf(HTTP_TMPL_GET_RQ, r->url);
		ln[lc++] = strduprintf("X-Requested-With: XMLHttpRequest\r\n");
		break;
	case HTTP_RQ_TYPE_POST:
		ln[lc++] = strduprintf(HTTP_TMPL_POST_RQ, r->url);
		if (r->cl > 0)
			ln[lc++] = strduprintf(HTTP_TMPL_CONTENT_LEN, r->cl);
		break;
	case HTTP_RQ_TYPE_DELETE:
		ln[lc++] = strduprintf(HTTP_TMPL_DELETE_RQ, r->url);
		break;
	default:
		warnx("http_gen_req(): Invalid request type: %d", r->type);
		return (NULL);
	}
	if (r->host != NULL)
		ln[lc++] = strduprintf(HTTP_TMPL_HOST, r->host);
	if (r->location != NULL)
		ln[lc++] = strduprintf(HTTP_TMPL_LOCATION, r->location);
	if (r->cookie != NULL)
		ln[lc++] = strduprintf(HTTP_TMPL_COOKIE, r->cookie);
	if (r->ua != NULL)
		ln[lc++] = strduprintf(HTTP_TMPL_USER_AGENT, r->ua);
	if (r->cs != NULL)
		ln[lc++] = strduprintf(HTTP_TMPL_CHARSET, r->cs);
	else
		ln[lc++] = strduprintf(HTTP_TMPL_CHARSET, "UTF-8");
	if (r->ct != NULL)
		ln[lc++] = strduprintf(HTTP_TMPL_CONTENT_TYPE, r->ct);
	if (r->accept != NULL)
		ln[lc++] = strduprintf(HTTP_TMPL_ACCEPT, r->accept);
	ln[lc++] = strduprintf("Cache-Control: no-cache\r\n\r\n");
	for (i = 0; i < lc; i++) {
		if (ln[i] == NULL)
			goto error;
		if (strdupstrcat(&rq, &rqsz, ln[i], strlen(ln[i])) == NULL)
			goto error;
		free(ln[i]); ln[i] = NULL;
	}
	return (rq);
error:
	while (lc--)
		free(ln[lc]);
	free(rq);
	return (NULL);
}

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
	if ((buf = malloc(buflen)) == NULL)
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
		if (strncmp(p, "HTTP/", 5) == 0) {
			for (q = p; (q = strtok(q, " ")) != NULL; q = NULL) {
				if (isdigit(*q))
					return (strtol(q, NULL, 10));
			}
			warnx("Unexpected server reply: %s", p);
			return (-1);
		}
	}
	return (-1);
}

int
http_get(ssl_conn_t *cp, const char *url, const char *cookie,
	 const char *accept, const char *agent)
{
	char	   *rq;
	http_req_t hdr;

	(void)memset(&hdr, 0, sizeof(hdr));
	hdr.ua       = agent;
	hdr.url	     = url;
	hdr.type     = HTTP_RQ_TYPE_GET;
	hdr.host     = cp->host;
	hdr.cookie   = cookie;
	hdr.accept   = accept;
	hdr.location = url; 

	if ((rq = http_gen_req(&hdr)) == NULL)
		return (-1);
	if (ssl_write(cp, rq, strlen(rq)) == -1) {
		free(rq); return (-1);
	}
	free(rq);
	return (get_http_status(cp));
}

int
http_post(ssl_conn_t *cp, const char *url, const char *cookie,
	 const char *accept, const char *agent, int type, const char *content)
{
	char	   *rq;
	http_req_t hdr;

	(void)memset(&hdr, 0, sizeof(hdr));
	hdr.url      = url;
	hdr.ua       = agent;
	hdr.host     = cp->host;
	hdr.type     = HTTP_RQ_TYPE_POST;
	hdr.cookie   = cookie;
	hdr.accept   = accept;
	hdr.location = url;

	if (type == HTTP_POST_TYPE_JSON)
		hdr.ct = "application/json; charset=UTF-8";
	else if (type == HTTP_POST_TYPE_OCTET)
		hdr.ct = "application/octet-stream";
	else {
		hdr.ct = "application/x-www-form-" \
			 "urlencoded;charset=utf-8";
	}
	if (content != NULL)
		hdr.cl = strlen(content);
	if ((rq = http_gen_req(&hdr)) == NULL)
		return (-1);
	if (ssl_write(cp, rq, strlen(rq)) == -1) {
		free(rq); return (-1);
	}
	free(rq);
	if (content != NULL) {
		if (ssl_write(cp, content, hdr.cl) == -1)
			return (-1);
	}
	return (get_http_status(cp));
}

int
http_delete(ssl_conn_t *cp, const char *url, const char *cookie,
	    const char *agent)
{
	char	   *rq;
	http_req_t hdr;

	(void)memset(&hdr, 0, sizeof(hdr));
	hdr.url	     = url;
	hdr.cookie   = cookie;
	hdr.ua	     = agent;
	hdr.type     = HTTP_RQ_TYPE_DELETE;
	hdr.host     = cp->host;
	hdr.location = url;

	if ((rq = http_gen_req(&hdr)) == NULL)
		return (-1);
	if (ssl_write(cp, rq, strlen(rq)) == -1) {
		free(rq); return (-1);
	}
	free(rq);
	return (get_http_status(cp));
}

int
http_upload(ssl_conn_t *cp, const char *url, const char *cookie,
	    const char *accept, const char *agent, const char *file)
{
	int	   n;
	long	   len;
	FILE	   *fp;
	char	   *rq, buf[1024];
	http_req_t hdr;

	if ((fp = fopen(file, "r")) == NULL) {
		warn("%s", file); return (-1);
	}
	if (fseek(fp, 0, SEEK_END) == -1) {
		warn("fseek()"); (void)fclose(fp); return (-1);
	}
	if ((len = ftell(fp)) == -1) {
		warn("ftell()"); (void)fclose(fp); return (-1);
	}
	if (len > HTTP_FILESZ_LIMIT) {
		warnx("'%s' exceeds file size-limit of %d MB", file,
		    HTTP_FILESZ_LIMIT / (1024 * 1024));
		(void)fclose(fp); return (-1);
	}
	(void)memset(&hdr, 0, sizeof(hdr));
	hdr.ua	   = agent;
	hdr.cl	   = (int)len;
	hdr.ct	   = "application/octet-stream";
	hdr.url	   = url;
	hdr.type   = HTTP_RQ_TYPE_POST;
	hdr.host   = cp->host;
	hdr.cookie = cookie;
	hdr.accept = accept;

	if ((rq = http_gen_req(&hdr)) == NULL) {
		(void)fclose(fp); return (-1);
	}
	if (ssl_write(cp, rq, strlen(rq)) == -1) {
		free(rq); (void)fclose(fp); return (-1);
	}
	free(rq);

	rewind(fp);
	while ((n = fread(buf, 1, sizeof(buf), fp)) > 0) {
		if (ssl_write(cp, buf, n) == -1) {
			(void)fclose(fp); return (-1);
		}
	}
	(void)fclose(fp);

	return (get_http_status(cp));
}

