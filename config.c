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
#include <stdlib.h>
#include <ctype.h>
#include <string.h>
#include <limits.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#include <err.h>
#include <pwd.h>
#include <grp.h>

#include "types.h"
#include "config.h"

config_t cfg;

typedef struct var_s {
	char *name;
	enum {
		VAR_STRING,
		VAR_STRINGS,
		VAR_INTEGER,
		VAR_BOOLEAN 
	} type;
	union val_u {
		int   *integer;
		bool  *boolean;
		char  **string;
		char  ***strings;
	} val;
} var_t;
typedef union val_u val_t;

var_t vars[] = {
	{ "host",   VAR_STRING,  (val_t)&cfg.host   },
	{ "user",   VAR_STRING,  (val_t)&cfg.user   },
	{ "cookie", VAR_STRING,  (val_t)&cfg.cookie },
	{ "editor", VAR_STRING,  (val_t)&cfg.editor },
	{ "port",   VAR_INTEGER, (val_t)&cfg.port   }

};
#define NVARS (sizeof(vars) / sizeof(var_t))

static int   write_var(var_t *, FILE *);
static int   parse_line(char *);
static char  *cutok(char *, bool *);
static char  *escape_str(const char *);
static var_t *find_var(const char *, size_t);

/*
 * Extends the string vector at *strv by the given string and terminates
 * the vector with a NULL-pointer.
 *
 * Returns the new string vector.
 */
static char **
add_string(char ***strv, const char *str)
{       
        static int    n;
        static char **p;

	if (*strv == NULL)
		n = 0;
	else {
		for (p = *strv, n = 0; p[n] != NULL; n++)
			;
	}
	n += 2;
	if ((p = realloc(*strv, n * sizeof(char *))) == NULL) {
		free(*strv);
		return (NULL);
	}
	*strv = p;
	if ((p[n - 2] = strdup(str)) == NULL) {
		free(p);
		return (NULL);
	}
	p[n - 1] = NULL;

	return (p);
}

/*
 * Extracts the first (str != NULL) or the next (str == NULL) token from a
 * whitespace  separated  list  of  (quoted) strings, while respecting the
 * escape rules.
 *
 * Returns the start address of the token, or NULL if the string is empty,
 * or a syntax error was found.
 */
static char *
cutok(char *str, bool *error)
{
	static int   quote;
	static char *p, *start = NULL;

	if (str != NULL)
		start = str;
	while (*start != '\0' && isspace(*start))
		start++;
	if (*start == '"') {
		start++; quote = 1;
	} else
		quote = 0;
	if (*start == '\0') {
		if (quote == 1) {
			warnx("Syntax error: Unterminated quoted string:");
			*error = true;
		} else
			*error = false;
		return (NULL);
	}
	*error = true;
	for (p = str = start; *str != '\0'; str++) {
		if (*str == '\\') {
			if (*(str + 1) == '\0') {
				warnx("Syntax error: " \
				    "Incomplete escape sequence:");
				return (NULL);
			}
			*p++ = *++str;
		} else if (*str == '"' || isspace(*str)) {
			if (*str == '"')
				quote ^= 1;
			if (quote == 0) {
				*p++ = '\0'; p = start; start = str + 1;
				*error = false;
				return (p);
			} else
				*p++ = *str;
		} else
			*p++ = *str;
	}
	*p = '\0'; p = start; start = str;
	if (quote == 1) {
		warnx("Syntax error: Unterminated quoted string:");
		return (NULL);
	}
	*error = false;
	return (p);
}

static char *
escape_str(const char *str)
{
	char *p, *esc;

	if ((esc = malloc(2 * strlen(str) + 1)) == NULL)
		return (NULL);
	for (p = esc; *str != '\0'; str++) {
		if (*str == '"' || *str == '\\')
			*p++ = '\\';
		*p++ = *str;
	}
	*p = '\0';

	return (esc);
}

static int
parse_line(char *str)
{
	int   i;
	bool  error;
	char  *var, *val, *p, **sv, **pp;
	
	for (var = str; *var != '\0' && isspace(*var); var++)
		;
	if (*var == '\0' || *var == '#')
		return (0);
	for (val = var + strcspn(var, " =\t"); *val != '=' && *val != '\0';
	    val++)
		*val = '\0';
	if (*val != '=') {
		warnx("Syntax error: Missing '=':");
		return (-1);
	}
	*val++ = '\0'; val += strspn(val, " \t\n");
	if (*val == '\0' || isspace(*val)) {
		warnx("Syntax error: Missing value:");
		return (-1);
	}
	for (i = 0; i < sizeof(vars) / sizeof(vars[0]); i++) {
		if (strcmp(var, vars[i].name) != 0)
			continue;
		switch (vars[i].type) {
		case VAR_STRINGS:
			for (pp = *vars[i].val.strings;
			    pp != NULL && *pp != NULL; pp++)
				free(*pp);
			for (p = val; (p = cutok(p, &error)) != NULL;
			    p = NULL) {
				sv = *vars[i].val.strings;
				if ((sv = add_string(&sv, p)) == NULL)
					err(EXIT_FAILURE, "add_string()");
				*vars[i].val.strings = sv;
			}
			if (error)
				return (-1);
			break;
		case VAR_STRING:
			if ((p = cutok(val, &error)) == NULL)
				return (-1);
			free(*vars[i].val.string);
			if ((*vars[i].val.string = strdup(p)) == NULL)
				err(EXIT_FAILURE, "strdup()");
			break;
		case VAR_BOOLEAN:
			if ((p = cutok(val, &error)) == NULL)
				return (-1);
			if (strcasecmp(p, "false") == 0 ||
			    strcasecmp(p, "no") == 0  ||
			    (strchr("0123456789", p[0]) != NULL &&
			     strtol(p, NULL, 10) == 0))
				*vars[i].val.boolean = false;
			else
				*vars[i].val.boolean = true;
			break;
		case VAR_INTEGER:
			if ((p = cutok(val, &error)) == NULL)
				return (-1);
			*vars[i].val.integer = strtol(p, NULL, 10);
			break;
		}
		return (0);
	}
	warnx("Unknown variable '%s':", var);
	return (-1);
}

char *
cfgpath()
{
	int	      len;
	char	      *path;
	struct passwd *pw;
	
	if ((pw = getpwuid(getuid())) == NULL) {
		warnx("Couldn't find you in the password file");
		return (NULL);
	}
	endpwent();
	len = strlen(pw->pw_dir) + sizeof(PATH_CONFIG) + 1;
	if ((path = malloc(len)) == NULL) {
		warn("malloc()");
		return (NULL);
	}
	(void)snprintf(path, len, "%s/%s", pw->pw_dir, PATH_CONFIG);

	return (path);
}

int
read_config()
{
	int   lc, i;
	FILE *fp;
	char *buf, *p, **pp, *path;
	
	(void)memset(&cfg, 0, sizeof(cfg));

	for (i = 0; i < NVARS; i++) {
		if (vars[i].type == VAR_STRING) {
			if (*vars[i].val.string == NULL)
				continue;
			p = strdup(*vars[i].val.string);
			if (p == NULL)
				return (-1);
			*vars[i].val.string = p;
		} else if (vars[i].type == VAR_STRINGS) {
			if (*vars[i].val.strings == NULL)
				continue;
			for (pp = *vars[i].val.strings; *pp != NULL; pp++) {
				if ((p = strdup(*pp)) == NULL)
					return (-1);
				*pp = p;
			}
		}
	}
	if ((path = cfgpath()) == NULL)
		return (-1);
	if ((fp = fopen(path, "r")) == NULL) {
		warn("fopen(%s)", path); free(path);
		return (-1);
	}
	free(path);

	if ((buf = malloc(_POSIX2_LINE_MAX)) == NULL) {
		warn("malloc()"); (void)fclose(fp);
		return (-1);
	}
	for (lc = 1; fgets(buf, _POSIX2_LINE_MAX, fp) != NULL; lc++) {
		(void)strtok(buf, "\n");
		if (parse_line(buf) == -1) {
			(void)fprintf(stderr, "%s, line %d\n", path, lc);
			(void)fclose(fp); free(buf);
			return (-1);
		}
	}
	free(buf); (void)fclose(fp);
	return (0);
}

int
write_config()
{
	int   i, fd, varlen;
	bool  var_written[NVARS];
	char  *p, *path, *ln, tmpl[] = "/tmp/XXXXXX";
	FILE  *fp, *tmpfp;
	var_t *var;

	if ((path = cfgpath()) == NULL)
		return (-1);
	if ((fp = fopen(path, "r+")) == NULL && errno == ENOENT) {
		if ((fp = fopen(path, "w+")) == NULL) {
			warn("fopen(%s)", path); free(path);
			return (-1);
		}
		(void)fchmod(fileno(fp), S_IRUSR|S_IWUSR);
	} else if (fp == NULL) {
		warn("fopen(%s)", path); free(path);
		return (-1);
	}
	free(path);

	if ((fd = mkstemp(tmpl)) == -1) {
		warn("mkstemp()"); (void)fclose(fp);
		return (-1);
	}
	if ((tmpfp = fdopen(fd, "r+")) == NULL) {
		warn("fdopen()"); (void)fclose(fp); (void)close(fd);
		return (-1);
	}
	if ((ln = malloc(_POSIX2_LINE_MAX)) == NULL) {
		warn("malloc()"); (void)fclose(fp);
		(void)fclose(tmpfp); (void)remove(tmpl);
		return (-1);
	}
	for (i = 0; i < NVARS; i++)
		var_written[i] = false;
	(void)fseek(fp, 0, SEEK_SET);
	while (fgets(ln, _POSIX2_LINE_MAX, fp) != NULL) {
		for (p = ln; isspace(*p); p++)
			;
		if (*p == '\0' || *p == '#')
			(void)fputs(ln, tmpfp);
		else {
			varlen = strcspn(p, " =\t");
			if ((var = find_var(p, varlen)) == NULL)
				(void)fputs(ln, tmpfp);
			else {
				for (i = 0; i < NVARS; i++) {
					if (var == &vars[i])
						var_written[i] = true;
				}
				(void)write_var(var, tmpfp);
			}
		}
	}
	for (i = 0; i < NVARS; i++) {
		if (!var_written[i])
			(void)write_var(&vars[i], tmpfp);
	}
	(void)fflush(tmpfp);
	(void)fseek(fp, 0, SEEK_SET); (void)fseek(tmpfp, 0, SEEK_SET);

	while (fgets(ln, _POSIX2_LINE_MAX, tmpfp) != NULL)
		(void)fputs(ln, fp);
	(void)fclose(tmpfp); (void)remove(tmpl);

	(void)fflush(fp);
        (void)ftruncate(fileno(fp), ftell(fp));
        (void)fclose(fp); free(ln);

	return (0);
}

static var_t *
find_var(const char *name, size_t len)
{
	int i;

	for (i = 0; i < NVARS; i++) {
		if (strncmp(vars[i].name, name, len) == 0)
			return (&vars[i]);
	}
	return (NULL);
}

static int
write_var(var_t *var, FILE *fp)
{
	char *p, **s;

	if (var == NULL)
		return (0);

	switch (var->type) {
	case VAR_STRING:
		if (*var->val.string == NULL)
			return (0);
		p = escape_str(*var->val.string);
		if (p == NULL)
			return (-1);
		(void)fprintf(fp, "%s = \"%s\"\n",
		    var->name, p);
		free(p);
		break;
	case VAR_STRINGS:
		(void)fprintf(fp, "%s = ", var->name);
		for (s = *var->val.strings;
		    s != NULL && *s != NULL; s++) {
			if (s != *var->val.strings)
				(void)fputc(' ', fp);
			p = escape_str(*s);
			if (p == NULL)
				return (-1);
			(void)fprintf(fp, "\"%s\"", p);
			free(p);
		}
		(void)fputc('\n', fp);
		break;
	case VAR_INTEGER:
		(void)fprintf(fp, "%s = %d\n", var->name, *var->val.integer);
		break;
	case VAR_BOOLEAN:
		(void)fprintf(fp, "%s = %s\n", var->name,
		    *var->val.boolean ? "true" : "false");
		break;
	}
	return (0);
}

