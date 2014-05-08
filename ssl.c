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

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <err.h>
#include <errno.h>
#include <limits.h>

#include "types.h"
#include "ssl.h"

ssl_conn_t *
ssl_connect(const char *host, u_short port)
{
	int	s;
	SSL	*handle;
	SSL_CTX *ctx;
	ssl_conn_t	   *cp;
	static int	   init = 1;
	struct sockaddr_in target;

	errno = 0;
	if (init == 1) {
		SSL_load_error_strings();
		(void)SSL_library_init();
		init = 0;
	}
        if ((s = socket(AF_INET, SOCK_STREAM, 0)) == -1)
		return (NULL);
	(void)memset(&target, 0, sizeof(target));
	target.sin_family = AF_INET;
	target.sin_port = htons(port);
	if ((target.sin_addr.s_addr = inet_addr(host)) == INADDR_NONE) {
		struct hostent *t_info;

		if ((t_info = gethostbyname(host)) == NULL) {
			herror("gethostbyname()");
			return (NULL);
		}
		(void)memcpy((char *)&target.sin_addr.s_addr, t_info->h_addr,
		    t_info->h_length);
	}
	if (connect(s, (struct sockaddr *)&target,
	    sizeof(struct sockaddr)) == -1)
		return (NULL);
	if ((ctx = SSL_CTX_new(SSLv23_client_method())) == NULL) {
		ERR_print_errors_fp(stderr);
		return (NULL);
	}
	if ((handle = SSL_new(ctx)) == NULL) {
		ERR_print_errors_fp(stderr);
		return (NULL);
	}
	if (SSL_set_fd(handle, s) == 0) {
		ERR_print_errors_fp(stderr);
		(void)close(s); SSL_free(handle);
		return (NULL);
	}
	if (SSL_connect(handle) != 1) {
		ERR_print_errors_fp(stderr);
		(void)close(s); SSL_free(handle);
		return (NULL);
	}
	if ((cp = malloc(sizeof(ssl_conn_t))) == NULL) {
		warn("malloc()"); (void)close(s); SSL_free(handle);
		return (NULL);
	}
	cp->ctx	   = ctx;
	cp->sock   = s;
	cp->handle = handle;
	cp->lnbuf  = NULL;
	cp->state  = SSL_STATE_CONNECTED;
	cp->slen   = cp->bufsz = cp->rd = 0;
	cp->host   = strdup(host);
	if (cp->host == NULL) {
		warn("strdup()"); ssl_disconnect(cp);
		return (NULL);
	}
	return (cp);
}

void
ssl_disconnect(ssl_conn_t *cp)
{
	int saved_errno;

	saved_errno = errno;
	(void)close(cp->sock);
	SSL_shutdown(cp->handle);
	SSL_free(cp->handle);
	SSL_CTX_free(cp->ctx);
	free(cp->host);
	free(cp->lnbuf);
	free(cp);
	errno = saved_errno;
}

int
ssl_read(ssl_conn_t *cp, int waitsecs, void *buf, int size)
{
	int	       n, ec;
	fd_set         rset;
	struct timeval tv;
	
	if (SSL_pending(cp->handle) > 0) {
		while ((n = SSL_read(cp->handle, buf, size)) == -1) {
			if (errno != EINTR) {
				ERR_print_errors_fp(stderr);
				return (-1);
			}
		}
		return (n);
	}
	for (n = -1; n < 0;) {
		tv.tv_sec = waitsecs; tv.tv_usec = 0;
		FD_ZERO(&rset); FD_SET(cp->sock, &rset);
		while (select(cp->sock + 1, &rset, NULL, NULL, &tv) == -1) {
			if (errno != EINTR) {
				warn("ssl_read(): select()");
				return (-1);
			}
		}
		if (!FD_ISSET(cp->sock, &rset)) {
			warnx("ssl_read(): Timeout");
			return (TIMEOUT);
		} 
		if ((n = SSL_read(cp->handle, buf, size)) < 0) {
			switch ((ec = SSL_get_error(cp->handle, n))) {
			case SSL_ERROR_WANT_READ:
			case SSL_ERROR_WANT_WRITE:
				/* FALLTHROUGH */
				break;
			default:
				if (errno != 0) {
					warn("SSL_read() returned with %d," \
					     "SSL-error code %d", n, ec);
				} else {
					warnx("SSL_read() returned with %d," \
					      "SSL-error code %d", n, ec);
				}
				ERR_print_errors_fp(stderr);
				return (-1);
			}
		}
	}
	if (n == 0)
		cp->state = SSL_STATE_DISCONNECTED;
	return (n);
}

int
ssl_write(ssl_conn_t *cp, const void *buf, size_t size)
{
	int	       n, ec;
	fd_set	       wset;
	struct timeval tv;

	for (n = -1; n < 0;) {
		tv.tv_sec = 20; tv.tv_usec = 0;
		FD_ZERO(&wset); FD_SET(cp->sock, &wset);
		while (select(cp->sock + 1, NULL, &wset, NULL, &tv) == -1) {
			if (errno != EINTR) {
				warn("ssl_write(): select()");
				return (-1);
			}
		}
		if (!FD_ISSET(cp->sock, &wset)) {
			warnx("ssl_write(): Timeout");
			return (TIMEOUT);
		}
		if ((n = SSL_write(cp->handle, buf, size)) < 0) {
			switch ((ec = SSL_get_error(cp->handle, n))) {
			case SSL_ERROR_WANT_READ:
			case SSL_ERROR_WANT_WRITE:
				/* FALLTHROUGH */
				break;
			default:
				if (errno != 0) {
					warn("SSL_write() returned with %d," \
					     "SSL-error code %d", n, ec);
				} else {
					warnx("SSL_write() returned with %d," \
					      "SSL-error code %d", n, ec);
				}
				ERR_print_errors_fp(stderr);
				return (-1);
			}
		}
	}
	if (n == 0)
		cp->state = SSL_STATE_DISCONNECTED;
	return (n);
}

char *
ssl_readln(ssl_conn_t *cp)
{
	int  i, n;
	char *p;
	
	if (cp->lnbuf == NULL) {
		if ((cp->lnbuf = malloc(_POSIX2_LINE_MAX)) == NULL)
			return (NULL);
		cp->bufsz = _POSIX2_LINE_MAX;
	}
	n = 0;
	do {
		cp->rd += n;
		if (cp->slen > 0) {
			for (i = 0; i < cp->rd - cp->slen; i++)
				cp->lnbuf[i] = cp->lnbuf[i + cp->slen];
		}
		cp->rd	-= cp->slen;
		cp->slen = 0;
		for (i = 0; i < cp->rd && cp->lnbuf[i] != '\n'; i++)
			;
		if (i < cp->rd && cp->lnbuf[i] == '\n') {
			if (i > 0 && cp->lnbuf[i - 1] == '\r')
				cp->lnbuf[i - 1] = '\0';
			cp->slen = i + 1;
			if (cp->slen >= cp->bufsz)
				cp->slen = cp->rd = 0;
			cp->lnbuf[i] = '\0';
			return (cp->lnbuf);
		}
		if (cp->rd >= cp->bufsz) {
			p = realloc(cp->lnbuf, cp->bufsz + _POSIX2_LINE_MAX);
			if (p == NULL)
				return (NULL);
			cp->lnbuf  = p;
			cp->bufsz += _POSIX2_LINE_MAX;
		}
	} while ((n = ssl_read(cp, 20, cp->lnbuf + cp->rd,
	    cp->bufsz - cp->rd)) > 0);
	if (cp->rd > 0) {
		cp->lnbuf[cp->rd] = '\0';
		cp->slen = cp->rd = 0;
		return (cp->lnbuf);
	}
	cp->slen = cp->rd = 0;

	return (NULL);
}

