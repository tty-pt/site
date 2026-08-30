# axil-hyle — Axil to Hyle HTTP Connector

Bridge library connecting Axil HTTP request handling with Hyle dataset queries and schema endpoints.

## Overview

`libaxil-hyle` mounts standard RESTful endpoints and picker hot-swap endpoints:
- `GET /api/dataset/:id` — Execute Hyle queries over HTTP, returning JSON results
- `GET /pick/:id/options` — Return rendered HTML picker slot fragments for debounced client-side omni-dropdowns

## Usage

```c
#include <ttypt/axil-hyle.h>

void xy_install(void)
{
    // Mount dataset query and picker fragment endpoints
    axil_hyle_install_routes();
}
```

## Dependencies

- `external/axil` — HTTP request and route registration
- `external/hyle` — Core query parser and search engine
- `external/hyle/c/libhyle-source` — Dataset persistence
- `external/hyle/c/libhyle-bud` — Picker component rendering
- `external/bud` — HTML AST generation
