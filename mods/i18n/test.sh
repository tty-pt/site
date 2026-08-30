#!/bin/sh
set -eu

BASE="${TEST_URL:-http://localhost:8080}"
COOKIE="/tmp/i18n_test_cookie_$$"
trap 'rm -f "$COOKIE"' EXIT INT TERM

echo "=== Testing i18n module ==="

# 1. Default request (English)
html_default=$(curl -s "$BASE/")
if ! printf '%s\n' "$html_default" | grep -q 'lang="en"'; then
	echo "FAIL: Expected default <html lang=\"en\">" >&2
	exit 1
fi
echo "PASS: Default request yields lang=\"en\""

# 2. Accept-Language pt-PT
html_pt=$(curl -s -H "Accept-Language: pt-PT,pt;q=0.9,en;q=0.8" "$BASE/")
if ! printf '%s\n' "$html_pt" | grep -q 'lang="pt"'; then
	echo "FAIL: Expected <html lang=\"pt\"> with Accept-Language pt-PT" >&2
	exit 1
fi
echo "PASS: Accept-Language pt-PT yields lang=\"pt\""

# 3. Auth login with Accept-Language pt-PT
login_pt=$(curl -s -H "Accept-Language: pt-PT" "$BASE/auth/login")
if ! printf '%s\n' "$login_pt" | grep -q 'Iniciar sessão'; then
	echo "FAIL: Expected 'Iniciar sessão' on pt-PT login page" >&2
	exit 1
fi
if ! printf '%s\n' "$login_pt" | grep -q 'Nome de utilizador'; then
	echo "FAIL: Expected 'Nome de utilizador' on pt-PT login page" >&2
	exit 1
fi
if ! printf '%s\n' "$login_pt" | grep -q 'Palavra-passe'; then
	echo "FAIL: Expected 'Palavra-passe' on pt-PT login page" >&2
	exit 1
fi
echo "PASS: Auth login page translates to European Portuguese (pt-PT)"

# 4. Auth login with Accept-Language en
login_en=$(curl -s -H "Accept-Language: en-US,en;q=0.9" "$BASE/auth/login")
if ! printf '%s\n' "$login_en" | grep -q 'Sign in'; then
	echo "FAIL: Expected 'Sign in' on en login page" >&2
	exit 1
fi
echo "PASS: Auth login page renders in English"

# 5. Language switch endpoint /i18n/set
set_code=$(curl -sw "%{http_code}" -o /dev/null -c "$COOKIE" "$BASE/i18n/set?lang=pt&return=/auth/login")
if [ "$set_code" != "303" ]; then
	echo "FAIL: /i18n/set expected 303 redirect, got $set_code" >&2
	exit 1
fi
if ! grep -q 'lang' "$COOKIE"; then
	echo "FAIL: /i18n/set did not set lang cookie in jar" >&2
	exit 1
fi
echo "PASS: /i18n/set sets cookie and redirects"

# 6. Request with Cookie lang=pt
cookie_html=$(curl -s -b "$COOKIE" "$BASE/auth/login")
if ! printf '%s\n' "$cookie_html" | grep -q 'lang="pt"'; then
	echo "FAIL: Cookie lang=pt did not produce lang=\"pt\"" >&2
	exit 1
fi
echo "PASS: Cookie lang=pt persists across requests"

# 7. Query param override ?lang=pt
param_html=$(curl -s "$BASE/auth/login?lang=pt")
if ! printf '%s\n' "$param_html" | grep -q 'Iniciar sessão'; then
	echo "FAIL: ?lang=pt query param did not translate login page" >&2
	exit 1
fi
echo "PASS: ?lang=pt query parameter forces Portuguese translation"

# 8. Song detail page translation (unauthenticated)
song_pt=$(curl -s -H "Accept-Language: pt-PT" "$BASE/song/song1")
if printf '%s\n' "$song_pt" | grep -q 'bud-root'; then
	if ! printf '%s\n' "$song_pt" | grep -q 'Iniciar sessão'; then
		echo "FAIL: Expected 'Iniciar sessão' in song detail menu" >&2
		exit 1
	fi
	if ! printf '%s\n' "$song_pt" | grep -q 'Fechar menu'; then
		echo "FAIL: Expected 'Fechar menu' in song detail layout" >&2
		exit 1
	fi
	echo "PASS: Song detail page menu translates correctly in Portuguese"
fi

# 9. Song detail page translation (authenticated - Me & Logout)
AUTH_COOKIE="/tmp/i18n_auth_cookie_$$"
trap 'rm -f "$COOKIE" "$AUTH_COOKIE"' EXIT INT TERM
I18N_USER="i18n_user_$$"
# Register test user
curl -s -c "$AUTH_COOKIE" -X POST "$BASE/auth/register" \
	-d "username=$I18N_USER&password=pass1234&password2=pass1234&email=i18n@test.com" >/dev/null 2>&1 || true

auth_song_pt=$(curl -s -b "$AUTH_COOKIE" -H "Accept-Language: pt-PT" "$BASE/song/song1")
if printf '%s\n' "$auth_song_pt" | grep -q 'bud-root'; then
	if ! printf '%s\n' "$auth_song_pt" | grep -q 'Perfil'; then
		echo "FAIL: Expected 'Perfil' (Me) in authenticated song detail menu" >&2
		exit 1
	fi
	if ! printf '%s\n' "$auth_song_pt" | grep -q 'Terminar sessão'; then
		echo "FAIL: Expected 'Terminar sessão' (Logout) in authenticated song detail menu" >&2
		exit 1
	fi
	echo "PASS: Authenticated song detail page renders 'Perfil' and 'Terminar sessão'"
fi

echo "i18n module: ALL PASS"
