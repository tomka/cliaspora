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

#ifndef _HTTP_H_
# define _HTTP_H_

#define HTTP_OK			200
#define HTTP_CREATED		201
#define HTTP_NO_CONTENT		204
#define HTTP_FOUND		302
#define HTTP_REDIRECT		302
#define HTTP_UNAUTHORIZED	401	
#define HTTP_POST_TYPE_JSON	1
#define HTTP_POST_TYPE_OCTET	2
#define HTTP_POST_TYPE_FORM	3
#define HTTP_FILESZ_LIMIT	4194304	/* File size-limit in bytes. */

extern int  http_get(ssl_conn_t *, const char *, const char *, const char *,
		     const char *);
extern int  http_post(ssl_conn_t *, const char *, const char *, const char *,
		      const char *, int, const char *);
extern int  http_delete(ssl_conn_t *, const char *, const char *,
		        const char *);
extern int  http_upload(ssl_conn_t *, const char *, const char *, const char *,
			const char *, const char *);
extern int  get_http_status(ssl_conn_t *);
extern char *urlencode(const char *);

#endif /* !_HTTP_H_ */

