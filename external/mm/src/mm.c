#include "engine.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *default_path(void)
{
	const char *home = getenv("HOME");
	static char buf[1024];
	if (home && home[0])
		snprintf(buf, sizeof(buf), "%s/.mm/memory.qmap", home);
	else
		snprintf(buf, sizeof(buf), ".mm/memory.qmap");
	return buf;
}

static void usage(const char *prog)
{
	fprintf(stderr,
	        "mm — Memory Mipmaps engine (libqmap + libstoma)\n"
	        "\n"
	        "Usage:\n"
	        "  %s store --file PATH --level N --topic T --ts TS\n"
	        "        [--tags T] --text '...'\n"
	        "      Store an entry. level 0=raw, 1=condensed, 2=summary.\n"
	        "      L2 ts is YYYY-MM; L1/L0 ts is an ISO timestamp.\n"
	        "      L0 has no topic.\n"
	        "  %s scan [--file PATH] [--level N] [--topic T] [--prefix P]\n"
	        "        [--q 'tokens' | --q '\"phrase\"'] [--max M]\n"
	        "      Recall entries, newest first. N=-1 = any level.\n"
	        "      Topic routes to 'T@...' keys; prefix zooms to an exact\n"
	        "      key prefix; --q applies full-text search (stoma).\n"
	        "  %s get --file PATH --key K            print one entry\n"
	        "  %s forget --file PATH --key K         delete one entry\n"
	        "  %s reset --file PATH                  delete everything\n"
	        "  %s vec put --file PATH --key K --text '1 2 3'   store vector\n"
	        "  %s vec get --file PATH --key K                 print vector\n"
	        "  %s vec cos --file PATH --key A --key B          cosine similarity\n"
	        "\n"
	        "  Default file: $HOME/.mm/memory.qmap\n",
	        prog, prog, prog, prog, prog, prog, prog, prog);
}

static int opti(int argc, char **argv, int *i, const char *name,
                const char **val)
{
	size_t n = strlen(name);
	if (strncmp(argv[*i], name, n) == 0 && argv[*i][n] == '=') {
		*val = argv[*i] + n + 1;
		return 1;
	}
	if (strcmp(argv[*i], name) == 0 && *i + 1 < argc) {
		*val = argv[++*i];
		return 1;
	}
	return 0;
}

static int cmd_store(int argc, char **argv)
{
	const char *path = NULL, *topic = "", *ts = NULL, *tags = "";
	const char *text = NULL;
	mm_entry_t e = {0};
	mm_t *mm;
	char err[256];
	int i;

	for (i = 2; i < argc; i++) {
		const char *v;
		if (opti(argc, argv, &i, "--file", &v))
			path = v;
		else if (opti(argc, argv, &i, "--topic", &v))
			topic = v;
		else if (opti(argc, argv, &i, "--ts", &v))
			ts = v;
		else if (opti(argc, argv, &i, "--tags", &v))
			tags = v;
		else if (opti(argc, argv, &i, "--text", &v))
			text = v;
		else if (opti(argc, argv, &i, "--level", &v))
			e.level = (uint32_t)strtoul(v, NULL, 10);
		else {
			fprintf(stderr, "mm: unknown store option: %s\n", argv[i]);
			return 2;
		}
	}
	if (!text || !ts) {
		fprintf(stderr, "mm: store requires --ts and --text\n");
		return 2;
	}
	strncpy(e.ts, ts, sizeof(e.ts) - 1);
	strncpy(e.topic, topic, sizeof(e.topic) - 1);
	strncpy(e.tags, tags, sizeof(e.tags) - 1);
	strncpy(e.text, text, sizeof(e.text) - 1);
	if (!path)
		path = default_path();

	mm = mm_open(path, err, sizeof(err));
	if (!mm) {
		fprintf(stderr, "%s\n", err);
		return 1;
	}
	if (mm_store(mm, &e, err, sizeof(err)) < 0) {
		fprintf(stderr, "%s\n", err);
		mm_close(mm);
		return 1;
	}
	mm_close(mm);
	return 0;
}

static int cmd_scan(int argc, char **argv)
{
	const char *path = NULL, *topic = "", *prefix = "", *textq = NULL;
	long level = -1, max = 0;
	mm_t *mm;
	char err[256];
	mm_hit_t *hits;
	size_t n, i;
	int c;

	for (c = 2; c < argc; c++) {
		const char *v;
		if (opti(argc, argv, &c, "--file", &v))
			path = v;
		else if (opti(argc, argv, &c, "--topic", &v))
			topic = v;
		else if (opti(argc, argv, &c, "--prefix", &v))
			prefix = v;
		else if (opti(argc, argv, &c, "--q", &v))
			textq = v;
		else if (opti(argc, argv, &c, "--level", &v))
			level = strtol(v, NULL, 10);
		else if (opti(argc, argv, &c, "--max", &v))
			max = strtol(v, NULL, 10);
		else {
			fprintf(stderr, "mm: unknown scan option: %s\n", argv[c]);
			return 2;
		}
	}
	if (!path)
		path = default_path();
	mm = mm_open(path, err, sizeof(err));
	if (!mm) {
		fprintf(stderr, "%s\n", err);
		return 1;
	}
	hits = mm_scan(mm, topic[0] ? topic : NULL, prefix[0] ? prefix : NULL,
	               textq, (int)level, (size_t)max, &n);
	if (!hits) {
		mm_close(mm);
		return 0;
	}
	for (i = 0; i < n; i++) {
		printf("%s\t%u\t%s\t%s\n", hits[i].key, hits[i].level,
		       hits[i].ts, hits[i].topic);
		if (hits[i].text && hits[i].text[0])
			printf("%s\n", hits[i].text);
		printf("\n");
	}
	mm_hits_free(hits, n);
	mm_close(mm);
	return 0;
}

static int cmd_get(int argc, char **argv)
{
	const char *path = NULL, *key = NULL;
	mm_t *mm;
	char err[256];
	mm_entry_t e;
	int i;

	for (i = 2; i < argc; i++) {
		const char *v;
		if (opti(argc, argv, &i, "--file", &v))
			path = v;
		else if (opti(argc, argv, &i, "--key", &v))
			key = v;
		else {
			fprintf(stderr, "mm: unknown get option: %s\n", argv[i]);
			return 2;
		}
	}
	if (!key) {
		fprintf(stderr, "mm: get requires --key\n");
		return 2;
	}
	if (!path)
		path = default_path();
	mm = mm_open(path, err, sizeof(err));
	if (!mm) {
		fprintf(stderr, "%s\n", err);
		return 1;
	}
	if (!mm_get(mm, key, &e)) {
		mm_close(mm);
		return 1;
	}
	printf("key\t%s\nlevel\t%u\nts\t%s\ntopic\t%s\ntags\t%s\ntext\n%s\n",
	       key, e.level, e.ts, e.topic, e.tags, e.text);
	mm_close(mm);
	return 0;
}

static int cmd_forget(int argc, char **argv)
{
	const char *path = NULL, *key = NULL;
	mm_t *mm;
	char err[256];
	int i;

	for (i = 2; i < argc; i++) {
		const char *v;
		if (opti(argc, argv, &i, "--file", &v))
			path = v;
		else if (opti(argc, argv, &i, "--key", &v))
			key = v;
		else {
			fprintf(stderr, "mm: unknown forget option: %s\n", argv[i]);
			return 2;
		}
	}
	if (!key) {
		fprintf(stderr, "mm: forget requires --key\n");
		return 2;
	}
	if (!path)
		path = default_path();
	mm = mm_open(path, err, sizeof(err));
	if (!mm) {
		fprintf(stderr, "%s\n", err);
		return 1;
	}
	if (mm_forget(mm, key, err, sizeof(err)) < 0) {
		fprintf(stderr, "%s\n", err);
		mm_close(mm);
		return 1;
	}
	mm_close(mm);
	return 0;
}

static int cmd_reset(int argc, char **argv)
{
	const char *path = NULL;
	mm_t *mm;
	char err[256];
	int i;

	for (i = 2; i < argc; i++) {
		const char *v;
		if (opti(argc, argv, &i, "--file", &v))
			path = v;
		else {
			fprintf(stderr, "mm: unknown reset option: %s\n", argv[i]);
			return 2;
		}
	}
	if (!path)
		path = default_path();
	mm = mm_open(path, err, sizeof(err));
	if (!mm) {
		fprintf(stderr, "%s\n", err);
		return 1;
	}
	if (mm_reset(mm, err, sizeof(err)) < 0) {
		fprintf(stderr, "%s\n", err);
		mm_close(mm);
		return 1;
	}
	mm_close(mm);
	return 0;
}

static int cmd_vec(int argc, char **argv)
{
	const char *path = NULL, *key = NULL, *key2 = NULL, *text = NULL;
	const char *sub = (argc > 2) ? argv[2] : "";
	mm_t *mm;
	char err[256];
	int i;

	for (i = 3; i < argc; i++) {
		const char *v;
		if (opti(argc, argv, &i, "--file", &v))
			path = v;
		else if (opti(argc, argv, &i, "--key", &v)) {
			if (!key)
				key = v;
			else
				key2 = v;
		} else if (opti(argc, argv, &i, "--text", &v))
			text = v;
		else {
			fprintf(stderr, "mm: unknown vec option: %s\n", argv[i]);
			return 2;
		}
	}
	if (!path)
		path = default_path();
	mm = mm_open(path, err, sizeof(err));
	if (!mm) {
		fprintf(stderr, "%s\n", err);
		return 1;
	}
	if (strcmp(sub, "put") == 0) {
		float vals[512];
		size_t n = 0;
		const char *p = text;
		if (!key || !text) {
			fprintf(stderr, "mm: vec put requires --key and --text\n");
			mm_close(mm);
			return 2;
		}
		while (*p && n < 512) {
			char *end;
			vals[n++] = strtof(p, &end);
			while (*end == ' ')
				end++;
			p = end;
		}
		if (mm_vec_put(mm, key, vals, n) < 0) {
			mm_close(mm);
			return 1;
		}
	} else if (strcmp(sub, "get") == 0) {
		float vals[512];
		size_t n, j;
		if (!key) {
			fprintf(stderr, "mm: vec get requires --key\n");
			mm_close(mm);
			return 2;
		}
		n = mm_vec_get(mm, key, vals, 512);
		for (j = 0; j < n; j++)
			printf("%s%f", j ? " " : "", (double)vals[j]);
		printf("\n");
	} else if (strcmp(sub, "cos") == 0) {
		float a[512], b[512];
		size_t na, nb;
		if (!key || !key2) {
			fprintf(stderr, "mm: vec cos requires two --key\n");
			mm_close(mm);
			return 2;
		}
		na = mm_vec_get(mm, key, a, 512);
		nb = mm_vec_get(mm, key2, b, 512);
		if (na != nb || na == 0) {
			fprintf(stderr, "mm: vectors differ or missing (%zu vs %zu)\n",
			        na, nb);
			mm_close(mm);
			return 1;
		}
		printf("%f\n", (double)mm_cosine(a, b, na));
	} else {
		fprintf(stderr, "mm: unknown vec subcommand: %s\n", sub);
		mm_close(mm);
		return 2;
	}
	mm_close(mm);
	return 0;
}

int main(int argc, char **argv)
{
	const char *cmd = argc > 1 ? argv[1] : "";

	if (strcmp(cmd, "store") == 0)
		return cmd_store(argc, argv);
	if (strcmp(cmd, "scan") == 0)
		return cmd_scan(argc, argv);
	if (strcmp(cmd, "get") == 0)
		return cmd_get(argc, argv);
	if (strcmp(cmd, "forget") == 0)
		return cmd_forget(argc, argv);
	if (strcmp(cmd, "reset") == 0)
		return cmd_reset(argc, argv);
	if (strcmp(cmd, "vec") == 0)
		return cmd_vec(argc, argv);
	if (strcmp(cmd, "help") == 0 || strcmp(cmd, "-h") == 0) {
		usage(argv[0]);
		return 0;
	}
	usage(argv[0]);
	return 2;
}