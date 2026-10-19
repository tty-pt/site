#include <ttypt/xy-mod.h>
#include <ttypt/axil.h>
int axil_tty_attach(socket_t fd);
int axil_tty_owns(socket_t fd);
int axil_tty_active(socket_t fd);
int axil_tty_input(socket_t fd, unsigned char *input, int nread);
#include <arpa/telnet.h>
#include <string.h>
#include <nd/xy.h>
#include <nd/world-events.h>

void nd_session_register_commands(void);
int nd_world_init(int argc, char **argv);
void nd_set_auth_ready(int ready);
unsigned nd_connect(int fd);
void nd_disconnect(int fd);
void nd_update(double dt);
void nd_password_line(int fd, const char *name, const char *password);
void nd_io_attach(unsigned fd, unsigned player_ref);
void nd_io_reset(unsigned fd);

#define ND_TTY_ROUTE "/tty"

static int
is_http_method(const char *s)
{
  static const char *const methods[] = {
    "GET", "POST", "HEAD", "PUT", "OPTIONS", "DELETE", "PATCH", "PRI"
  };
  size_t i;

  while (*s == ' ')
    s++;
  for (i = 0; i < sizeof(methods) / sizeof(methods[0]); i++) {
    size_t n = strlen(methods[i]);

    if (strncmp(s, methods[i], n) == 0 && s[n] == ' ')
      return 1;
  }
  return 0;
}

static int
strip_telnet(unsigned char *input, int nread)
{
  int in = 0, out = 0;

  while (in < nread) {
    if (input[in] == IAC) {
      if (in + 1 >= nread)
        break;
      switch (input[in + 1]) {
      case WILL:
      case WONT:
      case DO:
      case DONT:
        if (in + 2 >= nread)
          return out;
        in += 3;
        break;
      case SB:
        in += 2;
        while (in + 1 < nread &&
               !(input[in] == IAC && input[in + 1] == SE))
          in++;
        if (in + 1 < nread)
          in += 2;
        break;
      default:
        in += 2;
      }
    } else {
      input[out++] = input[in++];
    }
  }
  return out;
}

static int
nd_tty_owned(socket_t fd)
{
  if (axil_tty_owns(fd))
    return 1;
  char doc_uri[BUFSIZ] = {0};
  if (axil_env_get(fd, doc_uri, sizeof(doc_uri), "DOCUMENT_URI") == 0 &&
      strcmp(doc_uri, ND_TTY_ROUTE) == 0)
    return 1;
  return 0;
}

XY_IMPL(int, on_axil_connect, socket_t, fd)
{
  if (nd_tty_owned(fd))
    return 0;

  axil_tty_attach(fd);

  unsigned player_ref = nd_connect(fd);
  if (!player_ref || player_ref == (unsigned)-1) {
    axil_close(fd);
    return 0;
  }
  nd_io_attach(fd, player_ref);
  char msg[] = "hello from engine";
  on_demo(player_ref, msg);
  on_enter(player_ref, player_ref);
  nd_flush(player_ref);
  return 0;
}

XY_IMPL(int, on_axil_parse,
    socket_t, fd,
    unsigned char *, input,
    int, nread)
{
  {
    char pending[64];
    if (axil_env_get(fd, pending, sizeof(pending), "ND_PWPEND") == 0 &&
        *pending) {
      char line[128];
      size_t o = 0;
      int i = 0;
      while (i < nread && o + 1 < sizeof(line)) {
        unsigned char c = input[i];
        if (c == '\r' || c == '\n' || c == '\0')
          break;
        if (c == 255 && i + 1 < nread) {
          unsigned char opt = input[i + 1];
          if (opt == 250) {
            i += 2;
            while (i + 1 < nread &&
                   !(input[i] == 255 && input[i + 1] == 240))
              i++;
            if (i + 1 < nread)
              i += 2;
          } else {
            i += 2;
          }
          continue;
        }
        line[o++] = (char)c;
        i++;
      }
      line[o] = '\0';
      nd_password_line(fd, pending, line);
      memset(line, 0, sizeof(line));
      return -1;
    }
  }

  if (nd_tty_owned(fd))
    return -1;

  if (axil_flags(fd) & DF_WEBSOCKET) {
    if (axil_tty_active(fd))
      return -1;
    nd_io_reset(fd);
    return nread;
  }

  if (is_http_method((const char *)input))
    return nread;

  int used = axil_tty_input(fd, input, nread);
  if (used < 0)
    return -1;

  if (used > 0) {
    int rest = nread - used;
    memmove(input, input + used, (size_t)rest);
    memset(input + rest, '\n', (size_t)used);
    nread = rest;
  }

  int len = strip_telnet(input, nread);

  char nego[8] = {0};
  if (len > 0 && !is_http_method((const char *)input) &&
      axil_env_get(fd, nego, sizeof(nego), "RAW_NEGO") != 0) {
    axil_env_put(fd, "RAW_NEGO", "1");
    axil_tty_attach(fd);
    axil_write(fd, "Connect with: connect <name>\n",
               strlen("Connect with: connect <name>\n"));
  }

  return len;
}

XY_IMPL(int, on_axil_disconnect, socket_t, fd)
{
  if (nd_tty_owned(fd))
    return 0;
  nd_disconnect(fd);
  return 0;
}

XY_IMPL(int, on_axil_update, unsigned long long, dt)
{
  nd_update(dt);
  return 0;
}

XY_MODULE_API void
xy_install(void)
{
	WARN("nd-session: session lifecycle module installed from '%s'\n", xy.module_path);
	nd_world_init(0, NULL);
	nd_session_register_commands();
	xy_load("axil-tty");
	if (xy_load("libaxil-auth") == 0)
		nd_set_auth_ready(1);
}
