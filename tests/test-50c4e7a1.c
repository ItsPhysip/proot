/* A Unix socket whose translated path doesn't fit sun_path: bind(2)
 * must create it at its real place, connect(2) must reach it, and
 * getsockname(2) must return the guest path.  With the argument
 * "keep", the socket is left in place for test-50c4e7a2.sh.  */

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>

#define DIR  "/tmp/proot-test-50c4e7a1-xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"
#define PATH DIR "/s"

int main(int argc, char *argv[])
{
	int keep = argc > 1 && strcmp(argv[1], "keep") == 0;
	struct sockaddr_un addr, name;
	socklen_t length = sizeof(name);
	struct stat statl;
	int server, client;
	int result = EXIT_FAILURE;

	if (sizeof(PATH) > sizeof(addr.sun_path))
		return 125;

	(void) mkdir("/tmp", 0777);
	(void) mkdir(DIR, 0700);
	(void) unlink(PATH);

	memset(&addr, 0, sizeof(addr));
	addr.sun_family = AF_UNIX;
	strcpy(addr.sun_path, PATH);

	server = socket(AF_UNIX, SOCK_STREAM, 0);
	client = socket(AF_UNIX, SOCK_STREAM, 0);
	if (server < 0 || client < 0) {
		perror("socket");
		goto end;
	}

	if (bind(server, (struct sockaddr *) &addr, sizeof(addr)) < 0) {
		perror("bind");
		goto end;
	}

	if (listen(server, 1) < 0) {
		perror("listen");
		goto end;
	}

	/* The socket is at its guest path, not somewhere else.  */
	if (lstat(PATH, &statl) < 0 || !S_ISSOCK(statl.st_mode)) {
		fprintf(stderr, "%s is not a socket\n", PATH);
		goto end;
	}

	if (connect(client, (struct sockaddr *) &addr, sizeof(addr)) < 0) {
		perror("connect");
		goto end;
	}

	if (getsockname(server, (struct sockaddr *) &name, &length) < 0) {
		perror("getsockname");
		goto end;
	}

	if (strcmp(name.sun_path, PATH) != 0) {
		fprintf(stderr, "getsockname: %s, expected %s\n", name.sun_path, PATH);
		goto end;
	}

	result = EXIT_SUCCESS;
end:
	if (!keep || result != EXIT_SUCCESS) {
		(void) unlink(PATH);
		(void) rmdir(DIR);
	}
	return result;
}
