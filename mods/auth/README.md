# auth - Authentication Module

User registration, login, logout, and session management.

## Overview

The auth module provides complete user authentication functionality including registration with email confirmation, login/logout, and session management via cookies.

Confirmation is required by default. Set `AUTH_SKIP_CONFIRM=1` to skip confirmation in non-production environments.

## Endpoints

### POST /login

Authenticate a user with username and password.

**Parameters (form-encoded):**
- `username` (required)
- `password` (required)
- `ret` (optional) - Redirect URL after successful login

**Responses:**
- `303 See Other` - Success, redirects to `ret` parameter or `/`
- `400 Bad Request` - Missing credentials or invalid username/password
- `401 Unauthorized` - Account not activated (email not confirmed)

**Sets cookie:** `QSESSION=<token>` on success

### POST /logout

Log out the current user.

**Responses:**
- `303 See Other` - Always redirects to `/`

**Clears cookie:** Removes session token

### POST /register

Register a new user account.

**Parameters (form-encoded):**
- `username` (required)
- `email` (required)
- `password` (required)

**Responses:**
- `303 See Other` - Success, redirects to `/`
- `400 Bad Request` - Missing fields or username already exists

**Default behavior:** Account is inactive until email is confirmed. A confirmation code is generated and logged (in production, this would be emailed).

**Opt-out for non-production:** Set `AUTH_SKIP_CONFIRM=1` to activate the account immediately and keep the current auto-login behavior.

### GET /confirm

Confirm email address with code.

**Parameters (query string):**
- `u` (required) - Username
- `r` (required) - Confirmation code

**Responses:**
- `303 See Other` - Success, redirects to `/login`
- `400 Bad Request` - Invalid username or confirmation code

**Effect:** Activates the user account by removing the pending `rcode`

### GET /api/session

Get the current logged-in username.

**Responses:**
- `200 OK` - Returns username (or empty string if not logged in)

**Content-Type:** `text/plain`

## SSR Routes

The module provides server-side rendered pages:

- `/login` - Login form
- `/register` - Registration form
- `/confirm` - Email confirmation result page

## Storage

Uses `auth.qmap` with two qmap tables:

### users table

Stores user records by username:

```c
struct user {
    char active;            // 0 = needs confirmation, 1 = activated
    char hash[64];          // Password hash (crypt format)
    char email[128];        // Email address
    char confirm_code[64];  // Email confirmation code
};
```

### sessions table

Maps session tokens to usernames:
- Key: Session token (hex string from /dev/urandom)
- Value: Username (string)

## Session Management

### Cookie-Based Sessions

Sessions are managed via the `QSESSION` cookie:

1. User logs in with valid credentials
2. Server generates random token from `/dev/urandom`
3. Token → username mapping stored in sessions table
4. Cookie set: `QSESSION=<token>`
5. Subsequent requests include cookie
6. Server looks up username via token

### Token Generation

Tokens are 32+ character hex strings generated from `/dev/urandom`:

```c
static void generate_token(char *buf, size_t len)
{
    FILE *f = fopen("/dev/urandom", "r");
    // Read random bytes, encode as hex
    // Fallback to timestamp if /dev/urandom unavailable
}
```

### Session Lifetime

- Sessions persist until explicit logout
- No automatic expiration currently implemented
- Logout removes token from sessions table and clears cookie

## Exported API

Other modules can check authentication status:

### get_session_user

```c
const char *get_session_user(const char *token)
```

Look up username by session token.

**Returns:** Username string, or NULL if invalid/expired token

**Example usage from other modules:**
```c
char cookie[256] = {0};
char token[64] = {0};

ndc_env_get(fd, cookie, "HTTP_COOKIE");
get_cookie(cookie, token, sizeof(token));

const char *username = get_session_user(token);
if (username) {
    // User is logged in
} else {
    // Anonymous user
}
```

## Dependencies

- `mods/common/common` - For `query_param()` utility function

Declared in `ndx_deps[]` and loaded in `ndx_install()`.

## Security Features

### Password Hashing

Passwords are hashed using the system `crypt()` function:

```c
#include <crypt.h>
const char *hash = crypt(password, "$6$rounds=5000$...");
```

Never stores plaintext passwords.

### Email Confirmation

New accounts require email confirmation:
1. Registration creates inactive account by writing `./users/<user>/rcode`
2. Confirmation code generated and logged
3. User must visit `/confirm?u=<user>&r=<code>`
4. Confirmation removes `rcode`, which marks the account as active

Set `AUTH_SKIP_CONFIRM=1` only when you intentionally want to bypass this requirement outside production.

**Note:** In production, confirmation codes should be emailed, not logged.

### Token Security

- Tokens generated from cryptographically secure `/dev/urandom`
- Sufficient entropy (32+ hex characters = 128+ bits)
- Fallback to timestamp if /dev/urandom unavailable (less secure)

## Implementation Details

**Backend:** `auth.c`
- Lines 36-40: `get_session_user()` - Session lookup
- Lines 43-59: `generate_token()` - Random token generation
- Lines 80-127: `handle_login()` - Login logic
- Lines 129-150: `handle_logout()` - Logout logic
- Lines 152-228: `handle_register()` - Registration logic
- Lines 230-277: `handle_confirm()` - Email confirmation logic

**SSR:** `ssr/index.tsx`
- Routes: `/login`, `/register`, `/confirm`
- Components: `Login`, `Register`, `ConfirmUI`

## Testing

**Unit Tests:** `./test.sh` or `make -C mods/auth test`

Covers:
1. Registration flow
2. Login with unconfirmed account (expects 401)
3. Email confirmation
4. Login with confirmed account (expects 303)
5. Session persistence
6. Duplicate registration prevention

## Usage Example

From `mods/ssr/ssr.c`:

```c
// Get session token from cookie
char cookie[256] = {0};
char token[64] = {0};
ndc_env_get(fd, cookie, "HTTP_COOKIE");
get_cookie(cookie, token, sizeof(token));

// Check if user is logged in
const char *username = get_session_user(token);

// Forward username to SSR via X-Remote-User header
if (username) {
    snprintf(user_header, sizeof(user_header), 
             "X-Remote-User: %s\r\n", username);
}
```

## Troubleshooting

### Login fails with 401 "Account not activated"

**Cause:** Email not confirmed after registration.

**Solution:** Visit the confirmation URL logged during registration, or check the confirmation code in logs.

### Sessions don't persist

**Cause:** Cookie not being set or sent.

**Debug:**
```sh
# Check if cookie is set in response
curl -i -X POST http://localhost:8080/login -d "username=test&password=pass"

# Check if cookie is sent in request
curl -b "QSESSION=<token>" http://localhost:8080/api/session
```

### auth.qmap permission errors

**Cause:** File permissions or ownership issues.

**Solution:**
```sh
# Check permissions
ls -l auth.qmap

# If needed, fix ownership
chown $USER:$USER auth.qmap
chmod 644 auth.qmap
```

## See Also

- [mods/common/README.md](../common/README.md) - Shared utility functions
- [mods/ssr/README.md](../ssr/README.md) - How SSR uses authentication
- [AGENTS.md](../../AGENTS.md) - Development guidelines
