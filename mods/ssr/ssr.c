#include <ttypt/ndx-mod.h>
#include <ttypt/ndc.h>

#include "../proxy/proxy.h"

#include <sys/types.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>

static pid_t deno_pid = -1;
static int deno_ready = 0;
static unsigned long long accum_ms = 0;

#define CHECK_INTERVAL_MS   500ULL
#define READY_INTERVAL_MS  5000ULL

#define DENO_LOG "/tmp/deno-ssr.log"

static char repo_root[4096];

static void
start_deno(void)
{
	pid_t setup_pid, pid;
	int status;

	/* Run setup-routes.ts synchronously first */
	setup_pid = fork();
	if (setup_pid < 0) {
		perror("ssr: fork setup");
		return;
	}
	if (setup_pid == 0) {
		int logfd = open(DENO_LOG,
			O_WRONLY | O_CREAT | O_APPEND, 0644);
		if (logfd >= 0) {
			dup2(logfd, STDOUT_FILENO);
			dup2(logfd, STDERR_FILENO);
			close(logfd);
		}
		if (chdir(repo_root) < 0) {
			perror("ssr: chdir setup");
			_exit(1);
		}
		setenv("HOME", "/home/quirinpa", 1);
		char *deno = "/home/quirinpa/.deno/bin/deno";
		char *argv[] = { deno, "run", "--allow-read",
			"--allow-write", "scripts/setup-routes.ts", NULL };
		execv(deno, argv);
		perror("ssr: execvp deno setup");
		_exit(1);
	}
	waitpid(setup_pid, &status, 0);
	if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
		fprintf(stderr, "ssr: setup-routes exited with %d\n",
			WIFEXITED(status) ? WEXITSTATUS(status) : -1);

	/* Now start the server as a long-running process */
	pid = fork();

	if (pid < 0) {
		perror("ssr: fork");
		return;
	}

	if (pid == 0) {
		/* child: own process group so killpg kills all descendants */
		setpgid(0, 0);

		int logfd = open(DENO_LOG,
			O_WRONLY | O_CREAT | O_APPEND, 0644);
		if (logfd >= 0) {
			dup2(logfd, STDOUT_FILENO);
			dup2(logfd, STDERR_FILENO);
			close(logfd);
		}

		if (chdir(repo_root) < 0) {
			perror("ssr: chdir");
			_exit(1);
		}

		setenv("HOME", "/home/quirinpa", 1);
		char *deno = "/home/quirinpa/.deno/bin/deno";
		char *argv[] = { deno, "run", "-A", "main.ts", NULL };
		execv(deno, argv);
		perror("ssr: execvp deno");
		_exit(1);
	}

	/* parent */
	deno_pid = pid;
	deno_ready = 0;
	accum_ms = 0;
	fprintf(stderr, "ssr: started deno pid %d\n", (int)deno_pid);
}

static int
probe_port_3000(void)
{
	int fd, flags, ret = 0;
	struct sockaddr_in addr;
	fd_set wfds;
	struct timeval tv = {0, 100000}; /* 100ms */

	fd = socket(AF_INET, SOCK_STREAM, 0);
	if (fd < 0)
		return 0;

	flags = fcntl(fd, F_GETFL, 0);
	fcntl(fd, F_SETFL, flags | O_NONBLOCK);

	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_port = htons(3000);
	inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);

	if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) == 0) {
		ret = 1;
	} else if (errno == EINPROGRESS) {
		FD_ZERO(&wfds);
		FD_SET(fd, &wfds);
		if (select(fd + 1, NULL, &wfds, NULL, &tv) > 0) {
			int err = 0;
			socklen_t elen = sizeof(err);
			getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &elen);
			ret = (err == 0) ? 1 : 0;
		}
	}

	close(fd);
	return ret;
}

/* Periodic tick — weak symbol, called directly by NDC */
void
on_ndc_update(unsigned long long dt)
{
	int status;
	unsigned long long interval;

	accum_ms += dt;
	interval = deno_ready ? READY_INTERVAL_MS : CHECK_INTERVAL_MS;

	if (accum_ms < interval)
		return;

	accum_ms = 0;

	/* Check if child exited (crash or clean stop) */
	if (deno_pid > 0 &&
	    waitpid(deno_pid, &status, WNOHANG) == deno_pid) {
		if (WIFEXITED(status))
			fprintf(stderr, "ssr: deno exited (code %d), restarting\n",
				WEXITSTATUS(status));
		else if (WIFSIGNALED(status))
			fprintf(stderr, "ssr: deno killed (signal %d), restarting\n",
				WTERMSIG(status));
		deno_pid = -1;
		deno_ready = 0;
		start_deno();
		return;
	}

	/* Probe port 3000 until ready */
	if (!deno_ready && probe_port_3000()) {
		deno_ready = 1;
		proxy_connect("127.0.0.1", 3000);
		fprintf(stderr, "ssr: deno ready, proxy connected\n");
	}
}

NDX_LISTENER(int, on_ndc_exit, int, i)
{
	if (deno_pid > 0) {
		fprintf(stderr, "ssr: stopping deno pid %d\n", (int)deno_pid);
		killpg(deno_pid, SIGTERM);
		waitpid(deno_pid, NULL, 0);
		deno_pid = -1;
	}
	return 0;
}

void
ndx_install(void)
{
	if (!getcwd(repo_root, sizeof(repo_root)))
		repo_root[0] = '\0';

	ndx_load("./mods/proxy/proxy");
	start_deno();
}
