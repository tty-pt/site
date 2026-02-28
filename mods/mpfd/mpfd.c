#include <ttypt/ndx-mod.h>

#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <ttypt/ndc.h>
#include <ttypt/ndx.h>

#define CD "Content-Disposition: form-data; name=\""

static void
parse_multipart(char *body, char *content_type, char *doc_root)
{
	char boundary[128] = { 0 };
	char mpfd_path[64];
	char mpfd_file_path[512];
	char key[256] = { 0 };
	int body_state = 0;
	FILE *fp = NULL;
	int filecount = 0;
	int file = 0;
	char *keyf = NULL;

	char *boundary_start = strstr(content_type, "boundary=");
	if (!boundary_start)
		return;
	boundary_start += 9;
	
	while (*boundary_start == ' ')
		boundary_start++;
	
	char *boundary_end = strchr(boundary_start, ';');
	if (boundary_end) {
		snprintf(boundary, sizeof(boundary), "--%.*s", (int)(boundary_end - boundary_start), boundary_start);
	} else {
		snprintf(boundary, sizeof(boundary), "--%s", boundary_start);
	}

	snprintf(mpfd_path, sizeof(mpfd_path), "%s/tmp/mpfd", doc_root[0] ? doc_root : ".");

	char *line_start = body;
	
	while (*line_start) {
		char *line_end = strchr(line_start, '\r');
		if (line_end && line_end[1] == '\n') {
			*line_end = '\0';
			
			if (body_state) {
				if (strncmp(line_start, boundary, strlen(boundary)) == 0) {
					body_state = 0;
					if (fp) {
						fclose(fp);
						fp = NULL;
					}
					if (keyf) {
						snprintf(mpfd_file_path, sizeof(mpfd_file_path),
							 "%s/%s-count", mpfd_path, key);
						fp = fopen(mpfd_file_path, "w");
						if (fp) {
							fprintf(fp, "%d", filecount);
							fclose(fp);
							fp = NULL;
						}
					}
				} else if (*line_start) {
					if (fp) {
						fprintf(fp, "%s\n", line_start);
					}
				}
			} else {
				if (*line_start == '\0') {
					body_state = 1;
				} else if (!strncmp(CD, line_start, sizeof(CD) - 1)) {
					char *limit;
					strncpy(key, line_start + sizeof(CD) - 1, sizeof(key) - 1);
					key[sizeof(key) - 1] = '\0';
					limit = strchr(key, '"');
					if (limit)
						*limit = '\0';

					if (limit && limit[1] == ';') {
						keyf = strchr(key, '[');
						if (keyf) {
							*keyf = '\0';
							limit = keyf + 1;
						}
						if (limit && strncmp(limit, "; filename=", 11) == 0) {
							char *filename = limit + 11;
							filename = strchr(filename, '"');
							if (filename) {
								filename++;
								char *filename_end = strchr(filename, '"');
								if (filename_end)
									*filename_end = '\0';
								
								file = 1;
								if (keyf)
									snprintf(mpfd_file_path, sizeof(mpfd_file_path),
										 "%s/%s%u-name", mpfd_path, key, filecount);
								else
									snprintf(mpfd_file_path, sizeof(mpfd_file_path),
										 "%s/%s-name", mpfd_path, key);
								fp = fopen(mpfd_file_path, "w");
								if (fp) {
									fprintf(fp, "%s", filename);
									fclose(fp);
									fp = NULL;
								}
							}
						}
					} else {
						keyf = NULL;
						file = 0;
					}

					if (keyf)
						snprintf(mpfd_file_path, sizeof(mpfd_file_path),
							"%s/%s%d", mpfd_path, key, filecount);
					else
						snprintf(mpfd_file_path, sizeof(mpfd_file_path),
							"%s/%s", mpfd_path, key);

					fp = fopen(mpfd_file_path, "w");
				}
			}
			
			line_start = line_end + 2;
		} else {
			break;
		}
	}

	if (fp)
		fclose(fp);
}

static void
mpfd_handler(int fd, char *body)
{
	char method[16] = { 0 };
	char content_type[256] = { 0 };
	char doc_root[256] = { 0 };

	ndc_env_get(fd, method, "REQUEST_METHOD");
	ndc_env_get(fd, content_type, "CONTENT_TYPE");
	ndc_env_get(fd, doc_root, "DOCUMENT_ROOT");

	if (strcmp(method, "POST") != 0) {
		ndc_header(fd, "Content-Type", "text/plain");
		ndc_head(fd, 405);
		ndc_body(fd, "Method not allowed");
		return;
	}

	if (!strstr(content_type, "multipart/form-data")) {
		ndc_header(fd, "Content-Type", "text/plain");
		ndc_head(fd, 415);
		ndc_body(fd, "Expected multipart/form-data");
		return;
	}

	parse_multipart(body, content_type, doc_root);

	ndc_header(fd, "Content-Type", "text/plain");
	ndc_head(fd, 200);
	ndc_body(fd, "Parsed");
}

MODULE_API void
ndx_install(void)
{
	ndc_register_handler("POST:/mpfd", mpfd_handler);
}

MODULE_API void
ndx_open(void)
{
}
