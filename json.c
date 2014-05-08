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
#include <ctype.h>
#include <errno.h>
#include <err.h>
#include <sys/types.h>

#include "types.h"
#include "json.h"

static int	  uctoutf8(u_int, u_char *);
static char	  *get_string(const char *, bool *, int *);
static u_int	  utf16touc(u_short, u_short);
static const char *skip_ws(const char *);

static u_int
utf16touc(u_short hs, u_short ls)
{
	hs -= (54 << 10); ls -= (55 << 10);

	return (((ls | (hs << 10)) + 0x10000) & 0xfffff);
}

static int
uctoutf8(u_int u, u_char *buf)
{
	int n;

	if (u > 0xffff) {
		n = 4;
		buf[0] = 0xf0 + ((u >> 18) & 0x07);
		buf[1] = 0x80 + ((u >> 12) & 0x3f);
		buf[2] = 0x80 + ((u >>  6) & 0x3f);
		buf[3] = 0x80 + (u & 0x3f);
	} else if (u > 0xff) {
		n = 3;
		buf[0] = 0xe0 + ((u >> 12) & 0x0f);
		buf[1] = 0x80 + ((u >>  6) & 0x3f);
		buf[2] = 0x80 + (u & 0x3f);
	} else if (u > 0x7f) {
		n = 2;
		buf[0] = 0xc0 + ((u >>  6) & 0x1f);
		buf[1] = 0x80 + (u & 0x3f);
	} else {
		n = 1;
		buf[0] = u;
	}
	return (n);
}

bool
isutf8(const u_char *str)
{
	while (*str != '\0') {
		if ((*str >> 4) == 0x0f) {
			if (strlen((char *)str) < 4)
				return (false);
			if ((str[1] >> 6) != 0x02 || (str[2] >> 6) != 0x02 ||
			    (str[3] >> 6) != 0x02)
				return (false);
			str += 4;
		} else if ((*str >> 5) == 0x07) {
			if (strlen((char *)str) < 3)
				return (false);
			if ((str[1] >> 6) != 0x02 || (str[2] >> 6) != 0x02)
				return (false);
			str += 3;
		} else if ((*str >> 6) == 0x03) {
			if (strlen((char *)str) < 2)
				return (false);
			if ((str[1] >> 6) != 0x02)
				return (false);
			str += 2;
		} else if (*str > 0x7f)
			return (false);
		else
			str++;
	}
	return (true);
}

static char *
get_string(const char *str, bool *error, int *curpos)
{
	u_short	    hs, ls;
	static int  n, quote, quoted, buflen = 0;
	const  char *start;
	static char *p, num[8], *buf = NULL;

	if (buflen < strlen(str)) {
		if ((p = realloc(buf, strlen(str))) == NULL) {
			*error = true;
			return (NULL);
		}
		buflen = strlen(str);
		buf = p;
	}
	start = str;
	while (*str != '\0' && isspace(*str))
		str++;
	if (*str == '"') {
		str++; quote = 1; quoted = 1;
	} else
		quote = quoted = 0;
	if (*str == '\0') {
		if (quote == 1) {
			warnx("Syntax error: Unterminated quoted string:");
			*error = true;
		} else
			*error = false;
		return (NULL);
	}
	*error = true;
	for (p = buf; *str != '\0'; str++) {
		if (*str == '\\') {
			if (*(str + 1) == '\0') {
				warnx("Syntax error: " \
				    "Incomplete escape sequence:");
				return (NULL);
			}
			switch (*++str) {
			case '\\':
				*p++ = '\\';
				break;
			case 'n':
				*p++ = '\n';
				break;
			case 't':
				*p++ = '\t';
				break;
			case 'v':
				*p++ = '\v';
				break;
			case 'r':
				*p++ = '\r';
				break;
			case 'u':
				if (strlen(++str) < 4) {
					*error = true;
					return (NULL);
				}
				(void)memcpy(num, str, 4); num[5] = '\0';
				if ((strtol(num, NULL, 16) >> 8) == 0xd8) {
					hs = (u_short)strtol(num, NULL, 16);
					str += 4;
					if (str[0] != '\\' || str[1] != 'u') {
						*error = true;
						return (NULL);
					}
					if (strlen(str) < 4) {
						*error = true;
						return (NULL);
					}
					str += 2;
					(void)memcpy(num, str, 4);
					ls = (u_short)strtol(num, NULL, 16);
					n = uctoutf8(utf16touc(hs, ls),
					    (u_char *)p);
				} else {
					n = uctoutf8(strtol(num, NULL, 16),
					    (u_char *)p);
				}
				p += n; str += 3;
				break;
			default:
				*p++ = *str;
			}
		} else if (strchr(",:{}[]\" \n\t", *str) != NULL) {
			if (*str == '"')
				quote ^= 1;
			if (quote == 0) {
				*p++ = '\0'; 
				if (!quoted)
					*curpos = str - start;
				else
					*curpos = str - start + 1;
				*error = false;
				return (buf);
			} else
				*p++ = *str;
		} else
			*p++ = *str;
	}
	*p = '\0'; *curpos = str - start;
	if (quote == 1) {
		warnx("Syntax error: Unterminated quoted string:");
		return (NULL);
	}
	*error = false;
	return (buf);
}

static const char *
skip_ws(const char *str)
{
	while (str != NULL && *str != '\0' && isspace(*str))
		str++;
	return (str);
}

json_node_t *
new_json_node()
{
	json_node_t *node;

	if ((node = malloc(sizeof(json_node_t))) == NULL)
		return (NULL);
	node->var  = NULL;
	node->val  = NULL;
	node->next = NULL;
	node->type = JSON_TYPE_UNDEF;

	return (node);
}

void
free_json_node(json_node_t *node)
{
	json_node_t *next;

	while (node != NULL) {
		if (node->type == JSON_TYPE_OBJECT ||
		    node->type == JSON_TYPE_ARRAY) {
			free_json_node(node->val);
			return;
		}
		next = node->next;
		free(node->var); free(node->val);
		free(node);
		node = next;
	}
}

char *
parse_json(json_node_t *node, char *str)
{
	int  curpos;
	char *p;
	bool error;

	for (; str != NULL;) {
		str = (char *)skip_ws(str);
		if (*str == '\0') {
			node->next = NULL;
			return (str);
		}
		if (*str == '"') {
			p = get_string(str, &error, &curpos);
			if (p == NULL && error)
				return (NULL);
			str = (char *)skip_ws(str + curpos);
			if (*str == ':') {
				node->var = strdup(p);
				str++;
			} else {
				node->type = JSON_TYPE_STRING;
				if ((node->val = strdup(p)) == NULL)
					return (NULL);
			}
		} else if (isdigit(*str)) {
			p = get_string(str, &error, &curpos);
			if (p == NULL && error)
				return (NULL);
			node->type = JSON_TYPE_NUMBER;
			if ((node->val = malloc(sizeof(int))) == NULL)
				return (NULL);
			*(int *)node->val = strtol(p, NULL, 10);
			str += curpos;
		} else if (strncmp(str, "null",  4) == 0) {
			p = get_string(str, &error, &curpos);
			if (p == NULL && error)
				return (NULL);
			node->type = JSON_TYPE_NUMBER;
			if ((node->val = malloc(sizeof(int))) == NULL)
				return (NULL);
			*(int *)node->val  = 0;
			str += curpos;
		} else if (strncmp(str, "true",  4) == 0) {
			p = get_string(str, &error, &curpos);
			if (p == NULL && error)
				return (NULL);
			node->type = JSON_TYPE_BOOL;
			if ((node->val = malloc(sizeof(bool))) == NULL)
				return (NULL);
			*(bool *)node->val  = true;
			str += curpos;
		} else if (strncmp(str, "false", 5) == 0) {
			p = get_string(str, &error, &curpos);
			if (p == NULL && error)
				return (NULL);
			node->type = JSON_TYPE_BOOL;
			if ((node->val = malloc(sizeof(bool))) == NULL)
				return (NULL);
			*(bool *)node->val  = false;
			str += curpos;
		} else if (*str == '{') {
			if ((node->val = new_json_node()) == NULL)
				return (NULL);
			node->type = JSON_TYPE_OBJECT;
			str = parse_json(node->val, ++str);
		} else if (*str == '[') {
			if ((node->val = new_json_node()) == NULL)
				return (NULL);
			node->type = JSON_TYPE_ARRAY;
			str = parse_json(node->val, ++str);
		} else if (*str == ',') {
			if ((node->next = new_json_node()) == NULL)
				return (NULL);
			++str; node = node->next;
		} else if (*str == '\0') {
			node->next = NULL;
			return (str);
		} else if (*str == '}' || *str == ']') {
			node->next = NULL;
			return (++str);
		} else {
			warnx("Syntax error in JSON string.");
			return (NULL);
		}
	}
	return (str);
}

json_node_t *
find_json_node(json_node_t *node, const char *name)
{
	json_node_t *np;

	while (node != NULL) {
		if (node->var != NULL && strcmp(node->var, name) == 0)
			return (node);
		else if (node->type == JSON_TYPE_OBJECT ||
		    node->type == JSON_TYPE_ARRAY) {
			if ((np = find_json_node(node->val, name)) != NULL)
				return (np);
		}
		node = node->next;
	}	
	return (NULL);
}

json_node_t *
json_add_node(json_node_t *node)
{
	for (; node != NULL && node->next != NULL; node = node->next)
		;
	if (node == NULL)
		return (NULL);
	return ((node->next = new_json_node()));
}

char *
json_escape_str(const char *str)
{
	char *p, *buf;

	if ((buf = malloc(strlen(str) * 2 + 1)) == NULL)
		return (NULL);
	for (p = buf; str != NULL && *str != '\0'; str++) {
		switch (*str) {
		case '\r':
			*p++ = '\\'; *p++ = 'r';
			break;
		case '\b':
			*p++ = '\\'; *p++ = 'b';
			break;
		case '\f':
			*p++ = '\\'; *p++ = 'f';
			break;
		case '\n':
			*p++ = '\\'; *p++ = 'n';
			break;
		case '\t':
			*p++ = '\\'; *p++ = 't';
			break;
		case '\v':
			*p++ = '\\'; *p++ = 'v';
			break;
		case '\\':
			*p++ = '\\'; *p++ = '\\';
			break;
		case '/':
			*p++ = '\\'; *p++ = '/';
			break;
		case '"':
			*p++ = '\\'; *p++ = '"';
			break;
		default:
			*p++ = *str;
		}
	}
	*p = '\0';
	return (buf);
}

