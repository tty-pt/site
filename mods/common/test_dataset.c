#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#define QM_STR 1

#define COMMON_IMPL
#include "common.h"
#include "common_internal.h"
#undef COMMON_IMPL

/* Mock NDC functions */
void ndc_env_get(int fd, char *buf, const char *name)
{
	if (strcmp(name, "PATTERN_PARAM_DATASET_ID") == 0) {
		strcpy(buf, "test_dataset");
	}
}

const char *get_request_user(int fd)
{
	return "testuser";
}

int respond_json(int fd, int status, const char *msg)
{
	return 0;
}

int respond_error(int fd, int status, const char *msg)
{
	printf("MOCK respond_error: status=%d msg=%s\n", status, msg);
	return 0;
}

int server_error(int fd, const char *msg)
{
	printf("MOCK server_error: %s\n", msg);
	return 0;
}

void ndc_header_set(int fd, const char *name, const char *value)
{}
void ndc_respond(int fd, int status, const char *msg)
{}
int ndc_respond_plain(int fd, int status, const char *msg)
{
	return 0;
}
void ndc_header_get(int fd, const char *name, char *buf, size_t len)
{}
int ndc_query_parse(char *query)
{
	return 0;
}
int ndc_query_param(const char *name, char *buf, size_t len)
{
	return -1;
}
int csrf_validate(int fd, const char *token)
{
	return 0;
}
void ndc_register_handler(const char *pattern, int (*handler)(int, char *))
{}

/* Mock qmap */
unsigned qmap_open(
        const char *path,
        const char *name,
        int ktype,
        int vtype,
        int flags,
        int mode)
{
	return 1;
}
void qmap_close(unsigned hd)
{}
int qmap_put(unsigned hd, const char *key, const char *val)
{
	return 0;
}

typedef struct {
	const char *key;
	const char *value;
} mock_kv_t;

static mock_kv_t mock_data[] = { { "key1", "val1" },
	                         { "key2", "val2" },
	                         { NULL, NULL } };

static int mock_iter_idx = 0;

unsigned qmap_iter(unsigned hd, const void **key, int flags)
{
	mock_iter_idx = 0;
	return 1; // dummy handle
}

int qmap_next(const void **key, const void **value, unsigned cur)
{
	if (mock_data[mock_iter_idx].key == NULL)
		return 0;
	*key = mock_data[mock_iter_idx].key;
	*value = mock_data[mock_iter_idx].value;
	mock_iter_idx++;
	return 1;
}

/* Include the implementations */
#define json_escape mock_json_escape
int mock_json_escape(const char *in, char *out, size_t outlen)
{
	strncpy(out, in, outlen);
	return 0;
}

#include "common_json.c"
#include "common_dataset.c"

/* Mock dataset callbacks */
static int test_row_json_cb(
        json_object_t *row, const char *key, const void *value, void *user)
{
	json_object_kv_str(row, "id", key);
	json_object_kv_str(row, "name", (const char *)value);
	return 0;
}

/* Test functions */
void test_registration()
{
	static dataset_field_t fields[] = {
		{ "id", DATASET_FIELD_STRING, 0 },
		{ "name", DATASET_FIELD_STRING, 1 }
	};

	static dataset_def_t def = { .id = "test_dataset",
		                     .key_field = "id",
		                     .access_policy = DATASET_ACCESS_PUBLIC,
		                     .fields = fields,
		                     .field_count = 2,
		                     .source_hd = 1, // dummy
		                     .row_json_cb = test_row_json_cb };

	int rc = dataset_register(&def);
	assert(rc == 0);
	printf("Registration test passed\n");
}

void test_get_json()
{
	char *json = NULL;
	int rc = dataset_get_json(0, "test_dataset", NULL, &json);
	assert(rc == 0);
	assert(json != NULL);

	assert(strstr(json, "\"dataset\":\"test_dataset\"") != NULL);
	assert(strstr(json, "\"key1\"") != NULL);
	assert(strstr(json, "\"val1\"") != NULL);
	assert(strstr(json, "\"key2\"") != NULL);
	assert(strstr(json, "\"val2\"") != NULL);

	free(json);
	printf("Get JSON test passed\n");
}

int main()
{
	printf("Starting Dataset Unit Tests...\n");
	test_registration();
	test_get_json();
	printf("All Dataset Unit Tests Passed!\n");
	return 0;
}
