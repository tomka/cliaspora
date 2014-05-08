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

#ifndef _JSON_H_
# define _JSON_H_

#include "types.h"

typedef struct json_node_s {
	char type;
#define JSON_TYPE_UNDEF	 0
#define JSON_TYPE_STRING 1
#define JSON_TYPE_NUMBER 2
#define JSON_TYPE_ARRAY  3
#define JSON_TYPE_OBJECT 4
#define JSON_TYPE_BOOL	 5
	char *var;
	void *val;
	struct json_node_s *next;
} json_node_t;

extern void	   free_json_node(json_node_t *);
extern char	   *parse_json(json_node_t *, char *);
extern char	   *json_escape_str(const char *);
json_node_t	   *json_add_node(json_node_t *);
extern json_node_t *new_json_node(void);
extern json_node_t *find_json_node(json_node_t *, const char *);
#endif	/* !_JSON_H */

