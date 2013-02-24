#include <stdio.h>

char *
getfield(char *str)
{
	static char *p, *start = NULL, *result;
	
	if (str != NULL)
		p = start = str;
	if (start == NULL || *start == '\0')
		return (NULL);
	for (p = start; *p != '\0'; p++) {
		if (*p == '\\') {
			if (p[1] == '\0')
				return (NULL);
			(void)memmove(p, p + 1, strlen(p + 1));
			p++;
		} else if (*p == ':') {
			*p = '\0'; result = start; start = p + 1;
			return (result);
		}	
	}
	return (start);
}

contact_t *
read_contact_list()
{
	int	      lnsz, saved_errno;
	char	      *path, *p, *ln;
	FILE	      *fp;
	contact_t     *contacts, *ctp;
	struct passwd *pwd;

	lnsz = 4 * _POSIX2_LINE_MAX;
	if ((ln = malloc(lnsz)) == NULL)
		return (NULL);
	if ((pwd = getpwuid(getuid())) == NULL) {
		free(ln); return (NULL);
	}
 	path = malloc(strlen(pwd->pw_dir) + strlen(PATH_CONTACTS) + 2);
	if (path == NULL) {
		free(ln); return (NULL);
	}
	endpwent();
	if ((fp = fopen(path, "r")) == NULL) {
		free(ln); free(path); return (NULL);
	}
	free(path);

	contacts = NULL;
	while ((fgets(ln, lnsz, fp)) != NULL) {
		if (contacts == NULL) {
			if ((contacts = new_contact_node(NULL)) == NULL)
				goto error;
			ctp = contacts;
		} else if ((ctp = new_contact_node(contacts)) == NULL)
			goto error;
		for (n = 0, p = ln; (p = getfield(p)) != NULL; p = NULL, n++) {
			switch (n) {
			case 0: /* ID */
				ctp->id = strtol(p, NULL, 10);
				break;
			case 1: /* name */
				if ((ctp->name = strdup(p)) == NULL)
					goto error;
				break;
			case 2: /* handle */
				if ((ctp->handle = strdup(p)) == NULL)
					goto error;
				break;
			case 3: /* url */
				if ((ctp->url = strdup(p)) == NULL)
					goto error;
				break;
			case 4: /* avatar */
				if ((ctp->avatar = strdup(p)) == NULL)
					goto error;
				break;
			}
		}
	}
	free(ln);
	return (contacts);
error:
	saved_errno = errno;
	(void)fclose(fp); free(ln); free_contacts(contacts);
	errno = saved_errno;

	return (NULL);
}

int
write_contact_list(contact_t *contacts)
{
	int	      i;
	FILE	      *fp;
	char	      *field[4], *path;
	struct passwd *pwd;

	if ((pwd = getpwuid(getuid())) == NULL)
		return (-1);
 	path = malloc(strlen(pwd->pw_dir) + strlen(PATH_CONTACTS) + 2);
	if (path == NULL)
		return (-1);
	endpwent();
	if ((fp = fopen(path, "w+")) == NULL) {
		free(path);
		return (-1);
	}
	free(path);
	for (; contacts != NULL; contacts = contacts->next) {
		field[0] = contacts->name; field[1] = contacts->handle;
		field[2] = contacts->url;  field[3] = contacts->avatar;

		(void)fprintf(fp, "%d:", contacts->id);
		for (i = 0; i < 4; i++) {
			for (p = field[i]; p != NULL && *p != '\0'; p++) {
				if (*p == ':' || *p == '\\') {
					(void)fputc('\\', fp);
					(void)fputc(*p, fp);
				} else
					(void)fputc(*p, fp);
			}
			if (i < 3)
				(void)fputc(':', fp);
		}
		(void)fputc('\n', fp);
	}
	(void)fclose(fp);

	return (0);
}

