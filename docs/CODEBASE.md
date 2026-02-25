# Site Codebase Documentation

## Architecture Overview

### Request Flow

```
Client → ndc (port 8080) → ndx loads modules → handlers
                                              ↓
                                         ssr.so → Deno SSR (port 3000) → React → HTML
```

### Key Components

#### Core (`src/core/`)
- `main.c` - Loads modules from `mods.load` file via ndx

#### SSR Module (`mods/ssr/`)
- `ssr.c` - HTTP proxy that forwards requests to Deno SSR server, parses responses, manages sessions via cookies
- `server.ts` - Deno SSR that loads module SSR components dynamically and renders React
- `ui.tsx` - Shared Layout/Menu components

#### Auth Module (`mods/auth/`)
- `auth.c` - User registration, login, logout, session management with qmap database
- `ssr/index.tsx` - Login, Register, Confirm SSR pages
- `ssr/components/Login.tsx` - Login form
- `ssr/components/Register.tsx` - Registration form
- `ssr/components/ConfirmUI.tsx` - Confirmation UI

#### Poem Module (`mods/poem/`)
- `poem.c` - Handles poem uploads (multipart form data) to `items/poem/`
- `ssr/index.tsx` - Poem list and detail views
- `ssr/components/PoemList.tsx` - List of poems
- `ssr/components/PoemAdd.tsx` - Add poem form
- `ssr/components/PoemDetail.tsx` - Poem detail view

#### MPFD Module (`mods/mpfd/`)
- `mpfd.c` - Parses multipart/form-data, writes to `tmp/mpfd/`

#### Index API Module (`mods/index_api/`)
- `index_api.c` - Returns JSON list from `items/index-en.db`

### Data Flow
- Sessions stored in `sessions/` directory (auth)
- Users stored in `auth.qmap` (qmap database)
- Poems stored in `items/poem/`
- Index stored in `items/index-en.db`

### Module Dependencies

```
ssr (no deps)
auth -> ssr
mpfd (no deps)
index_api (no deps)
poem -> ssr, mpfd
```

### Testing
- Unit tests: `make unit-tests` - runs `test.sh` in each module
- Page smoke tests: `make pages-test` - starts site, runs `tests/pages/10-pages-render.sh`

## File Structure

```
/home/quirinpa/site/
├── src/core/main.c           # Core entry point
├── mods/
│   ├── ssr/
│   │   ├── ssr.c            # SSR proxy handler
│   │   ├── ssr.so          # Compiled module
│   │   ├── server.ts       # Deno SSR server
│   │   ├── ui.tsx          # Shared UI components
│   │   ├── test.sh         # SSR tests
│   │   └── Makefile
│   ├── auth/
│   │   ├── auth.c          # Auth handlers (login/register/logout)
│   │   ├── auth.so         # Compiled module
│   │   ├── ssr/index.tsx   # Auth SSR pages
│   │   ├── ssr/components/ # React components
│   │   ├── test.sh
│   │   └── Makefile
│   ├── poem/
│   │   ├── poem.c           # Poem upload handler
│   │   ├── poem.so
│   │   ├── ssr/index.tsx   # Poem SSR pages
│   │   ├── ssr/components/
│   │   ├── test.sh
│   │   └── Makefile
│   ├── mpfd/
│   │   ├── mpfd.c          # Multipart parser
│   │   ├── mpfd.so
│   │   ├── test.sh
│   │   └── Makefile
│   └── index_api/
│       ├── index_api.c     # Index API handler
│       ├── index_api.so
│       ├── test.sh
│       └── Makefile
├── htdocs/
│   └── styles.css          # Tailwind CSS
├── tests/
│   ├── pages/
│   │   ├── 10-pages-render.sh
│   │   └── 00-helpers.sh
│   └── integration/
│       ├── run_all.sh
│       └── 01-auth-poem-flow.sh
├── scripts/
│   └── gen_mods.py         # Generates module.db and mods.load
├── start.sh                # Starts ndc + Deno SSR
├── Makefile                # Build system
├── module.db               # Module metadata (qmap)
├── mods.load               # List of .so files to load
└── auth.qmap               # User database
```

## API Endpoints

### Backend (C modules)

| Method | Path | Handler |
|--------|------|---------|
| GET | `/api/session` | auth.c - get current session |
| POST | `/login` | auth.c - login |
| POST | `/register` | auth.c - register |
| GET | `/logout` | auth.c - logout |
| GET | `/confirm` | auth.c - confirm registration |
| POST | `/poem/add` | poem.c - upload poem |
| POST | `/mpfd` | mpfd.c - parse multipart |
| GET | `/api/index` | index_api.c - list items |

### SSR (Deno/React)

| Path | Handler |
|------|---------|
| `/` | server.ts - index page |
| `/login` | auth/ssr/index.tsx |
| `/register` | auth/ssr/index.tsx |
| `/confirm` | auth/ssr/index.tsx |
| `/poem` | poem/ssr/index.tsx |
| `/poem/add` | poem/ssr/index.tsx |
| `/poem/:id` | poem/ssr/index.tsx |

## Building and Running

```sh
# Build everything
make all

# Build and run all tests
make test

# Generate module database
make module.db

# Start site
./start.sh

# Run only module unit tests
make unit-tests

# Run page smoke tests
make pages-test
```

## Key Technologies

- **ndc** - HTTP server framework (C)
- **ndx** - Dynamic module loader (C)
- **qmap** - Key-value database (C)
- **Deno** - TypeScript runtime
- **React 18** - SSR rendering
- **Tailwind CSS** - Styling (pre-compiled)
