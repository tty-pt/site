#include "engine.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#define VEC_MAX 512

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

/* Parse "d0 d1 ..." into out; *n = count. Returns 0 on success. */
static int parse_vec(const char *s, float *out, size_t max, size_t *n)
{
	size_t cnt = 0;
	*n = 0;
	while (*s && cnt < max) {
		char *end;
		out[cnt++] = strtof(s, &end);
		if (end == s)
			break;
		s = end;
		while (*s == ' ')
			s++;
	}
	*n = cnt;
	return cnt == 0 ? -1 : 0;
}

/* Extract the first JSON array of floats ("[0.1, 0.2, ...]") from `json`.
 * Returns the count, or -1 if none found. */
static int extract_floats(const char *json, float *out, size_t max)
{
	const char *p = json;
	while (*p) {
		const char *open, *q;
		size_t cnt = 0;
		open = strchr(p, '[');
		if (!open)
			break;
		q = open + 1;
		while (*q == ' ' || *q == '\t' || *q == '\n' || *q == '\r')
			q++;
		while (cnt < max) {
			char *end;
			out[cnt] = strtof(q, &end);
			if (end == q)
				break;
			cnt++;
			q = end;
			while (*q == ' ' || *q == '\t' || *q == '\n' || *q == '\r' ||
			       *q == ',')
				q++;
			if (*q == ']')
				return (int)cnt;
		}
		p = open + 1;
	}
	return -1;
}

/* JSON-escape text into buf (under bufsz, little end-of-string slack). */
static size_t json_escape(const char *text, char *buf, size_t bufsz)
{
	size_t w = 0;
	while (*text && w + 8 < bufsz) {
		unsigned char c = (unsigned char)*text;
		if (c == '"' || c == '\\') {
			buf[w++] = '\\';
			buf[w++] = (char)c;
		} else if (c == '\n') {
			buf[w++] = '\\';
			buf[w++] = 'n';
		} else if (c < 0x20) {
			buf[w++] = ' ';
		} else {
			buf[w++] = (char)c;
		}
		text++;
	}
	buf[w] = '\0';
	return w;
}

/* Build the curl argv array for one embeddings POST. */
static void build_curl(char **pp, const char *url, const char *auth,
                       const char *body)
{
	*pp++ = "curl";
	*pp++ = "-sS";
	*pp++ = "-X";
	*pp++ = "POST";
	*pp++ = "-H";
	*pp++ = "Content-Type: application/json";
	if (auth && auth[0]) {
		*pp++ = "-H";
		*pp++ = (char *)auth;
	}
	*pp++ = "-d";
	*pp++ = (char *)body;
	*pp++ = (char *)url;
	*pp = NULL;
}

/* Query the embeddings provider (config-only, via system curl).
 * POSTs {"input":"<text>","model":N} to $MM_EMBED_URL with
 * Authorization: Bearer $MM_EMBED_KEY. Returns 0 and fills out and n on
 * success; on failure writes a message into err. */
static int embed(const char *text, float *out, size_t max, size_t *n,
                 char *err, size_t errsz)
{
	const char *url = getenv("MM_EMBED_URL");
	const char *key = getenv("MM_EMBED_KEY");
	const char *model = getenv("MM_EMBED_MODEL");
	char body[8192];
	char auth[1600];
	char resp[65536];
	const char *argv[16];
	int pfd[2];
	pid_t pid;
	ssize_t got;
	size_t total = 0;
	int status;
	int cnt;
	char esc[8192];

	if (!url || !url[0]) {
		snprintf(err, errsz,
		         "mm: no embeddings provider configured (set MM_EMBED_URL; "
		         "or pass --vec / --like to search with a stored vector)");
		return -1;
	}
	json_escape(text, esc, sizeof(esc));
	if (model && model[0])
		snprintf(body, sizeof(body),
		         "{\"input\":\"%s\",\"model\":\"%s\"}", esc, model);
	else
		snprintf(body, sizeof(body), "{\"input\":\"%s\"}", esc);
	snprintf(auth, sizeof(auth), "Authorization: Bearer %s", key ? key : "");
	build_curl((char **)argv, url, auth, body);

	if (pipe(pfd) < 0) {
		snprintf(err, errsz, "mm: pipe failed");
		return -1;
	}
	pid = fork();
	if (pid < 0) {
		snprintf(err, errsz, "mm: fork failed");
		close(pfd[0]);
		close(pfd[1]);
		return -1;
	}
	if (pid == 0) {
		dup2(pfd[1], STDOUT_FILENO);
		close(pfd[0]);
		close(pfd[1]);
		execvp("curl", (char *const *)argv);
		_exit(127);
	}
	close(pfd[1]);
	while (total + 1 < sizeof(resp)) {
		got = read(pfd[0], resp + total, sizeof(resp) - total - 1);
		if (got <= 0)
			break;
		total += (size_t)got;
	}
	close(pfd[0]);
	resp[total] = '\0';
	waitpid(pid, &status, 0);
	if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
		cnt = extract_floats(resp, out, max);
		if (cnt > 0) {
			*n = (size_t)cnt;
			return 0;
		}
		snprintf(err, errsz, "mm: no embedding in provider response: %s",
		         resp);
		return -1;
	}
	if (WIFEXITED(status) && WEXITSTATUS(status) == 127)
		snprintf(err, errsz, "mm: curl not found (set MM_EMBED_URL and install curl)");
	else
		snprintf(err, errsz, "mm: embeddings request failed: %s", resp);
	return -1;
}

static void usage(const char *prog)
{
	fprintf(stderr,
	        "mm — Memory Mipmaps engine (libqmap + libstoma)\n"
	        "\n"
	        "Usage:\n"
	        "  %s store --file PATH --level N --topic T --ts TS\n"
	        "        [--tags T] --text '...' [--embed]\n"
	        "      Store an entry. level 0=raw, 1=condensed, 2=summary.\n"
	        "      L2 ts is YYYY-MM; L1/L0 ts is an ISO timestamp.\n"
	        "      L0 has no topic. --embed (optional) stores an embedding\n"
	        "      for the new entry via $MM_EMBED_URL (requires curl).\n"
	        "  %s scan [--file PATH] [--level N] [--topic T] [--prefix P]\n"
	        "        [--q 'tokens' | --q '\"phrase\"'] [--max M]\n"
	        "        [--vec 'v1 ...' | --like K | --embed 'text']\n"
	        "        [--min-sim F]\n"
	        "      Recall entries. Without a vector flag: newest first.\n"
	        "      With --vec/--like/--embed: cosine top-k by similarity\n"
	        "      (entries without a stored vector, or with a different\n"
	        "      dimension, are excluded). --vec takes the query vector as\n"
	        "      space-separated floats (dimension = count); --min-sim sets\n"
	        "      a threshold on the score column.\n"
	        "  %s get --file PATH --key K            print one entry\n"
	        "  %s forget --file PATH --key K         delete one entry\n"
	        "  %s reset --file PATH                  delete everything\n"
	        "  %s vec put --file PATH --key K --text '1 2 3'   store vector\n"
	        "  %s vec get --file PATH --key K                 print vector\n"
	        "  %s vec cos --file PATH --key A --key B          cosine similarity\n"
	        "\n"
	        "  Default file: $HOME/.mm/memory.qmap\n"
	        "\n"
	        "  Embeddings: optional, never required.  Configure via:\n"
	        "    MM_EMBED_URL   — OpenAI-compatible /v1/embeddings endpoint\n"
	        "    MM_EMBED_KEY   — API key (optional)\n"
	        "    MM_EMBED_MODEL — model name (optional, omit for provider default)\n"
	        "  Without a config, --vec and --like still work fully offline.\n",
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
	int do_embed = 0;

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
		else if (strcmp(argv[i], "--embed") == 0)
			do_embed = 1;
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
	if (do_embed) {
		float ev[VEC_MAX];
		size_t edim = 0;
		char key[MM_KEY_LEN];
		if (embed(e.text, ev, VEC_MAX, &edim, err, sizeof(err)) < 0) {
			fprintf(stderr, "%s\n", err);
			mm_close(mm);
			return 1;
		}
		if (mm_key_make(&e, key, sizeof(key)) == 0) {
			if (mm_vec_put(mm, key, ev, edim) < 0)
				fprintf(stderr, "mm: vec_put after store failed\n");
		}
	}
	mm_close(mm);
	return 0;
}

static int cmd_scan(int argc, char **argv)
{
	const char *path = NULL, *topic = "", *prefix = "", *textq = NULL;
	const char *vec_str = NULL, *like_key = NULL, *embed_text = NULL;
	long level = -1, max = 0;
	double min_sim = 0.0;
	float q[VEC_MAX];
	size_t qdim = 0;
	int semantic = 0;
	mm_t *mm;
	char err[256];
	mm_hit_t *hits;
	size_t n, i;

	for (i = 2; i < (size_t)argc; i++) {
		const char *v;
		if (opti(argc, argv, (int *)&i, "--file", &v))
			path = v;
		else if (opti(argc, argv, (int *)&i, "--topic", &v))
			topic = v;
		else if (opti(argc, argv, (int *)&i, "--prefix", &v))
			prefix = v;
		else if (opti(argc, argv, (int *)&i, "--q", &v))
			textq = v;
		else if (opti(argc, argv, (int *)&i, "--level", &v))
			level = strtol(v, NULL, 10);
		else if (opti(argc, argv, (int *)&i, "--max", &v))
			max = strtol(v, NULL, 10);
		else if (opti(argc, argv, (int *)&i, "--vec", &v))
			vec_str = v;
		else if (opti(argc, argv, (int *)&i, "--like", &v))
			like_key = v;
		else if (opti(argc, argv, (int *)&i, "--embed", &v))
			embed_text = v;
		else if (opti(argc, argv, (int *)&i, "--min-sim", &v))
			min_sim = strtod(v, NULL);
		else {
			fprintf(stderr, "mm: unknown scan option: %s\n",
			        argv[(int)i]);
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
	if (vec_str) {
		if (parse_vec(vec_str, q, VEC_MAX, &qdim) < 0 || qdim == 0) {
			fprintf(stderr, "mm: --vec expects d space-separated floats\n");
			mm_close(mm);
			return 2;
		}
		semantic = 1;
	} else if (like_key) {
		qdim = mm_vec_get(mm, like_key, q, VEC_MAX);
		if (qdim == 0) {
			fprintf(stderr,
			        "mm: no stored vector for --like key '%s'\n",
			        like_key);
			mm_close(mm);
			return 1;
		}
		semantic = 1;
	} else if (embed_text) {
		if (embed(embed_text, q, VEC_MAX, &qdim, err, sizeof(err)) < 0) {
			fprintf(stderr, "%s\n", err);
			mm_close(mm);
			return 1;
		}
		semantic = 1;
	}

	hits = mm_semantic_scan(mm,
	                        topic[0] ? topic : NULL,
	                        prefix[0] ? prefix : NULL,
	                        textq, (int)level, (size_t)max,
	                        semantic ? q : NULL, semantic ? qdim : 0,
	                        min_sim, &n);
	if (!hits) {
		mm_close(mm);
		return 0;
	}
	for (i = 0; i < n; i++) {
		printf("%s\t%u\t%s\t%s", hits[i].key, hits[i].level,
		       hits[i].ts, hits[i].topic);
		if (semantic)
			printf("\t%.4f", (double)hits[i].score);
		printf("\n");
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