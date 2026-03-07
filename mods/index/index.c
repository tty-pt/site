#include <stdio.h>
#include <dirent.h>
#include <ttypt/qmap.h>
#include <ttypt/ndx-mod.h>

#include "./../ssr/ssr.h"

NDX_DEF(unsigned, index_open,
		const char *, path,
		unsigned, mask)
{
	unsigned hd = qmap_open(NULL, "hd", QM_STR, QM_STR,
			mask ? mask : 0x3FF, QM_SORTED);

	struct dirent *entry;
	DIR *dir = opendir(path);

	if (!dir) {
		perror("opendir");
		return QM_MISS;
	}

	while ((entry = readdir(dir)) != NULL) {
		char title_path[PATH_MAX], title[256];
		FILE *f;

		printf("%s\n", entry->d_name);
		snprintf(title_path, sizeof(title_path),
				"%s/%s/title", path,
				entry->d_name);

		f = fopen(title_path, "r");
		if (!f)
			continue;

		if (fgets(title, sizeof(title), f)) {
			title[strcspn(title, "\n")] = 0;
			qmap_put(hd, entry->d_name, title);
		}

		fclose(f);
	}

	closedir(dir);
	return hd;
}

NDX_DEF(unsigned, index_put,
		unsigned, hd,
		char *, key,
		char *, value)
{
	return qmap_put(hd, key, value);
}

NDX_DEF(unsigned, index_get,
		unsigned, hd,
		char *, value,
		size_t, len,
		char *, key)
{
	const void *val = qmap_get(hd, key);
	size_t rlen = qmap_len(QM_STR, val);

	if (rlen > len)
		return 1;

	memcpy(value, val, rlen);
	return 0;
}

NDX_DEF(int, index_page,
		unsigned, fd,
		unsigned, hd,
		char *, path,
		char *, title)
{
	register size_t total = 0;
	unsigned cur = qmap_iter(hd, NULL, 0);
	const void *key, *val;
	char *body, *s;

	// count body size
	while (qmap_next(&key, &val, cur)) {
		register size_t klen, vlen;
		klen = qmap_len(QM_STR, key);
		vlen = qmap_len(QM_STR, val);
		total += klen + vlen + 3;
	}

	total++;
	body = malloc(total);
	s = body;

	cur = qmap_iter(hd, NULL, 0);
	while (qmap_next(&key, &val, cur)) {
		s += sprintf(s, "%s %s\r\n",
				(char *) key,
				(char *) val);
	}

	*s = '\0';
	return call_ssr_proxy_post(fd, path, body, total);
}

MODULE_API void
ndx_install(void)
{
	ndx_load("./mods/ssr/ssr");
}

MODULE_API void
ndx_open(void)
{
}
