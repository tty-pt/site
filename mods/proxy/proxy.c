#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>

#include <ttypt/ndc.h>
#include <ttypt/ndx-mod.h>

static char req_buf[16384];
static size_t req_len = 0;
static int proxy_fd = -1;
static int proxy_headers_sent = 0;
static char proxy_host[1024];
static unsigned proxy_port;

/* --- Internal Helper: The Read/Forward Loop --- */
static int proxy_await_response(int fd)
{
  if (proxy_fd < 0) goto upstream_err;
  char buf[4096];
  ssize_t n;
  int headers_done = 0;
  char headbuf[16384];
  size_t head_used = 0;
  long content_length_val = -1;
  size_t body_received = 0;

  while ((n = read(proxy_fd, buf, sizeof(buf))) > 0) {
    size_t rn = (size_t)n;

    if (!headers_done) {
      size_t to_copy = (head_used + rn >= sizeof(headbuf)) ? (sizeof(headbuf) - 1 - head_used) : rn;
      memcpy(headbuf + head_used, buf, to_copy);
      head_used += to_copy;
      headbuf[head_used] = '\0';

      char *sep = strstr(headbuf, "\r\n\r\n");
      if (!sep) {
        if (head_used == sizeof(headbuf) - 1) goto upstream_err;
        continue;
      }

      size_t header_len = (size_t)(sep + 4 - headbuf);
      char saved_byte = headbuf[header_len];
      headbuf[header_len] = '\0';

      char *save = NULL;
      char *line = strtok_r(headbuf, "\r\n", &save);
      int status = 200;
      if (line) {
        char proto[32];
        if (sscanf(line, "%31s %d", proto, &status) != 2) status = 200;
      }

      while ((line = strtok_r(NULL, "\r\n", &save)) != NULL) {
        char *colon = strchr(line, ':');
        if (!colon) continue;
        *colon = '\0';
        char *key = line;
        char *val = colon + 1;
        while (*val == ' ') val++;

        if (!strcasecmp(key, "Content-Length")) content_length_val = atol(val);

        /* Filter hop-by-hop; Keep-Alive is handled at the transport layer */
        if (!strcasecmp(key, "Connection") || !strcasecmp(key, "Keep-Alive") ||
            !strcasecmp(key, "Proxy-Connection") || !strcasecmp(key, "Transfer-Encoding"))
          continue;

        ndc_header(fd, key, val);
      }

      ndc_head(fd, status);
      headbuf[header_len] = saved_byte;
      headers_done = 1;

      size_t initial_body = head_used - header_len;
      if (initial_body > 0) {
        ndc_write(fd, headbuf + header_len, initial_body);
        body_received += initial_body;
      }
      if (to_copy < rn) {
        ndc_write(fd, buf + to_copy, rn - to_copy);
        body_received += (rn - to_copy);
      }
    } else {
      ndc_write(fd, buf, rn);
      body_received += rn;
    }

    /* Check for completion to keep connection alive */
    if (content_length_val >= 0 &&
        body_received >= (size_t) content_length_val)

      break;
  }

  return 0;

upstream_err:
  ndc_header(fd, "Content-Type", "text/plain");
  ndc_head(fd, 502);
  ndc_body(fd, "Upstream Error");
  return -1;
}

NDX_DEF(int, proxy_init,
    const char *, method,
    const char *, path)
{
  struct sockaddr_in addr;
  int flags;
  fd_set wfds;
  struct timeval tv;

  /* Open a fresh connection to upstream per request */
  if (proxy_fd >= 0) {
    close(proxy_fd);
    proxy_fd = -1;
  }

  proxy_fd = socket(AF_INET, SOCK_STREAM, 0);
  if (proxy_fd < 0)
    return -1;

  flags = fcntl(proxy_fd, F_GETFL, 0);
  fcntl(proxy_fd, F_SETFL, flags | O_NONBLOCK);

  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_port = htons(proxy_port);
  inet_pton(AF_INET, proxy_host, &addr.sin_addr);

  if (connect(proxy_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
    if (errno != EINPROGRESS) {
      close(proxy_fd);
      proxy_fd = -1;
      return -1;
    }

    /* Wait up to 1s for connection */
    FD_ZERO(&wfds);
    FD_SET(proxy_fd, &wfds);
    tv.tv_sec = 0;
    tv.tv_usec = 0;

    if (select(proxy_fd + 1, NULL, &wfds, NULL, &tv) <= 0) {
      close(proxy_fd);
      proxy_fd = -1;
      return -1;
    }

    /* Check for connect error */
    int err = 0;
    socklen_t elen = sizeof(err);
    getsockopt(proxy_fd, SOL_SOCKET, SO_ERROR, &err, &elen);
    if (err) {
      close(proxy_fd);
      proxy_fd = -1;
      return -1;
    }
  }

  /* Restore blocking mode for read/write */
  fcntl(proxy_fd, F_SETFL, flags);

  req_len = snprintf(req_buf, sizeof(req_buf),
      "%s %s HTTP/1.1\r\n"
      "Host: %s:%d\r\n",
      method, path, proxy_host, proxy_port);

  proxy_headers_sent = 0;
  return 0;
}

NDX_DEF(int, proxy_header,
    const char *, name,
    const char *, val)
{
  if (proxy_headers_sent) return 1;
  size_t remain = sizeof(req_buf) - req_len;
  req_len += snprintf(req_buf + req_len, remain, "%s: %s\r\n", name, val);
  return 0;
}

/* Streams raw data upstream.
 * Automatically finalizes headers on first call.
 */
NDX_DEF(int, proxy_write,
    const char *, data,
    size_t, len)
{
  if (!proxy_headers_sent) {
    if (proxy_fd < 0) return -1;
    proxy_header("Connection", "keep-alive");

    if (req_len + 2 >= sizeof(req_buf))
      return -1;

    strcat(req_buf + req_len, "\r\n");
    req_len += 2;

    if (write(proxy_fd, req_buf, req_len) < 0)
      return -1;

    proxy_headers_sent = 1;
  }

  if (data && len > 0)
    return write(proxy_fd, data, len);

  return 0;
}

/* Finalizes headers, sends body data,
 * and starts the response read loop */
NDX_DEF(int, proxy_body,
    int, fd,
    const char *, data,
    size_t, len)
{
  if (data && len > 0) {
    char clen[32];
    snprintf(clen, sizeof(clen), "%zu", len);
    proxy_header("Content-Length", clen);
  }

  proxy_write(data, len);
  int ret = proxy_await_response(fd);
  if (proxy_fd >= 0) { close(proxy_fd); proxy_fd = -1; }
  return ret;
}

/* For GET or requests with no body */
NDX_DEF(int, proxy_head, int, fd)
{
  return proxy_body(fd, NULL, 0);
}

NDX_DEF(int, proxy_connect,
    const char *, host,
    unsigned, port)
{
  strlcpy(proxy_host, host, sizeof(proxy_host));
  proxy_port = port;
  return 0;
}

void ndx_install(void) {}
void ndx_open(void) {}
