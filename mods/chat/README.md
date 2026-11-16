# chat — Local LLM Chat & Streaming Sessions

Multi-session conversational AI interface backed by `axil-auren` (`127.0.0.1:4242`), persistent DSV session transcripts, and real-time WebSocket token streaming.

## Overview

The `chat` module provides a full-featured local LLM chat interface at `/chat` that works both with and without JavaScript:
- **No-JS / SSR**: Full server-side rendering of session lists and message history, with standard `POST` form actions for creating sessions, sending prompts, and deleting conversations.
- **Progressive WebSocket + WASM**: Upgrades `/chat` connections to WebSocket for real-time token streaming and hydrates the UI via `htdocs/chat.wasm` (`ux/chat_fe.c` + `ux/chat_ui.c`).

## Routes & Endpoints

| Method | Route | Description |
|--------|-------|-------------|
| `GET` | `/chat` | Render chat workspace (or upgrade to WebSocket for live streaming) |
| `GET` | `/chat/{session_id}` | Render specific chat session transcript |
| `POST` | `/chat/send` | Submit a prompt in a session and stream/complete via `axil-auren` |
| `POST` | `/chat/new` | Create a new chat session |
| `POST` | `/chat/delete` | Delete an existing chat session |
| `GET` | `/api/chat/sessions` | JSON endpoint listing sessions for the current user |

## Storage Format

Chat sessions are persisted under `var/chat/sessions/{id}.dsv` using single-line pipe-escaped DSV records:

```text
META|<id>|<user>|<title>|<model>|<created_at>|<updated_at>
MSG|<role>|<timestamp>|<escaped_content>
```

## XY Exports (`chat.h` / `chat-types.h`)

- `chat_save_session(const chat_session_t *s)` — Persist session metadata and messages to `var/chat/sessions/{id}.dsv`.
- `chat_get_session(const char *id, chat_session_t *out)` — Load a session by ID.

## Dependencies

- `mods/common/common` — Page shell (`site_ui_respond_page`) and shared layout helpers.
- `libcorm` — In-memory map tracking active WebSocket file descriptors to session IDs (`g_ws_session_map`).
- `libbud` — Isomorphic SSR and browser WASM UI rendering (`ux/chat_ui.c`, `ux/chat_fe.c`).

## Building & Testing

```sh
make -C mods/chat
make test-mod MOD=chat
```
