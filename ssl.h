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

#ifndef _SSL_H_
# define _SSL_H_ 1
#include <sys/types.h>
#include <openssl/rand.h>
#include <openssl/ssl.h>
#include <openssl/err.h>

#define TIMEOUT	 0
#define SSL_PORT 443

typedef struct ssl_conn_s {
	int	bufsz;
	int	rd;
	int	slen;
	int	sock;
	int	state;
#define SSL_STATE_CONNECTED    1
#define SSL_STATE_DISCONNECTED 0
	char	*host;
	char	*lnbuf;
	SSL	*handle;
	SSL_CTX *ctx;
} ssl_conn_t;

extern int	   ssl_read(ssl_conn_t *, int, void *, int); //size_t);
extern int	   ssl_write(ssl_conn_t *, const void *, size_t);
extern char	  *ssl_readln(ssl_conn_t *);
extern void	   ssl_disconnect(ssl_conn_t *);
extern ssl_conn_t *ssl_connect(const char *, u_short);

#endif /* !_SSL_H_ */

