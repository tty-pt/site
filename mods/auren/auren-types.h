#ifndef AUREN_TYPES_H
#define AUREN_TYPES_H

#include <stddef.h>
#include <stdint.h>

#define AUREN_DEMO_MAX_MSGS 32
#define AUREN_DEMO_CONTENT_MAX 4096

typedef struct {
	char role[16];   /* "user" or "assistant" */
	char content[AUREN_DEMO_CONTENT_MAX];
} auren_demo_msg_t;

typedef struct {
	char model[64];
	char status[64];
	int  webgpu_available;
	int  cached;
	int  percent;
	int  streaming;
	size_t msg_count;
	auren_demo_msg_t msgs[AUREN_DEMO_MAX_MSGS];
	char current_response[AUREN_DEMO_CONTENT_MAX];
} auren_demo_state_t;

#endif /* AUREN_TYPES_H */
