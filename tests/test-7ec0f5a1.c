/* recvfrom(2) and recvmsg(2) must report the sender of a datagram
 * with its guest path, not the host path the kernel knows it by.
 * Unnamed and abstract senders are reported as is.  */

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>

#define DIR       "/tmp/proot-test-7ec0f5a1"
#define RECEIVER  DIR "/r"
#define SENDER    DIR "/s"

static int failures = 0;

static void fail(const char *what, const struct sockaddr_un *addr, socklen_t length)
{
	fprintf(stderr, "%s: got length %u, path \"%.*s\"\n", what, (unsigned) length,
		length > offsetof(struct sockaddr_un, sun_path)
		? (int) (length - offsetof(struct sockaddr_un, sun_path)) : 0,
		addr->sun_path);
	failures++;
}

static int bound_socket(const char *path)
{
	struct sockaddr_un addr;
	int fd;

	fd = socket(AF_UNIX, SOCK_DGRAM, 0);
	if (fd < 0) {
		perror("socket");
		exit(EXIT_FAILURE);
	}

	if (path == NULL)
		return fd;

	(void) unlink(path);
	memset(&addr, 0, sizeof(addr));
	addr.sun_family = AF_UNIX;
	strcpy(addr.sun_path, path);
	if (bind(fd, (struct sockaddr *) &addr, sizeof(addr)) < 0) {
		perror("bind");
		exit(EXIT_FAILURE);
	}

	return fd;
}

static void send_to(int fd, const char *path)
{
	struct sockaddr_un addr;

	memset(&addr, 0, sizeof(addr));
	addr.sun_family = AF_UNIX;
	strcpy(addr.sun_path, path);
	if (sendto(fd, "x", 1, 0, (struct sockaddr *) &addr, sizeof(addr)) != 1) {
		perror("sendto");
		exit(EXIT_FAILURE);
	}
}

/* Receive one datagram on @receiver with recvfrom(2), or recvmsg(2)
 * if @use_msg, into a @max_size-byte address buffer.  */
static void receive(int receiver, int use_msg, struct sockaddr_un *addr,
		    socklen_t max_size, socklen_t *length)
{
	char byte;
	ssize_t status;

	memset(addr, 0x55, sizeof(*addr));

	if (use_msg) {
		struct iovec iov = { &byte, 1 };
		struct msghdr msg;

		memset(&msg, 0, sizeof(msg));
		msg.msg_name    = addr;
		msg.msg_namelen = max_size;
		msg.msg_iov     = &iov;
		msg.msg_iovlen  = 1;
		status = recvmsg(receiver, &msg, 0);
		*length = msg.msg_namelen;
	}
	else {
		*length = max_size;
		status = recvfrom(receiver, &byte, 1, 0, (struct sockaddr *) addr, length);
	}

	if (status != 1) {
		perror(use_msg ? "recvmsg" : "recvfrom");
		exit(EXIT_FAILURE);
	}
}

static void check_path(int receiver, int sender, int use_msg, const char *path,
		       socklen_t truncate)
{
	const char *name = use_msg ? "recvmsg" : "recvfrom";
	const socklen_t full = offsetof(struct sockaddr_un, sun_path) + strlen(path) + 1;
	struct sockaddr_un addr;
	socklen_t length;

	send_to(sender, RECEIVER);
	receive(receiver, use_msg, &addr, sizeof(addr), &length);
	if (length != full || addr.sun_family != AF_UNIX
	    || strcmp(addr.sun_path, path) != 0)
		fail(name, &addr, length);

	if (truncate == 0)
		return;

	/* Truncated to the caller's buffer, full length reported.  The
	 * buffer is large enough for the name the kernel knows the
	 * sender by (a short link, see test-50c4e7a1), not for its
	 * guest path.  */
	send_to(sender, RECEIVER);
	receive(receiver, use_msg, &addr, offsetof(struct sockaddr_un, sun_path) + truncate,
		&length);
	if (length != full || addr.sun_family != AF_UNIX
	    || memcmp(addr.sun_path, path, truncate) != 0
	    || (unsigned char) addr.sun_path[truncate] != 0x55)
		fail(name, &addr, length);
}

int main(void)
{
	const char abstract[] = "\0proot-test-7ec0f5a1";
	struct sockaddr_un addr;
	socklen_t length;
	int receiver, sender, unnamed, abstract_sender;
	int use_msg;

	(void) mkdir("/tmp", 0777);
	(void) mkdir(DIR, 0700);

	receiver    = bound_socket(RECEIVER);
	sender      = bound_socket(SENDER);
	unnamed     = bound_socket(NULL);

	abstract_sender = socket(AF_UNIX, SOCK_DGRAM, 0);
	memset(&addr, 0, sizeof(addr));
	addr.sun_family = AF_UNIX;
	memcpy(addr.sun_path, abstract, sizeof(abstract) - 1);
	if (abstract_sender < 0
	    || bind(abstract_sender, (struct sockaddr *) &addr,
		    offsetof(struct sockaddr_un, sun_path) + sizeof(abstract) - 1) < 0) {
		perror("abstract bind");
		exit(EXIT_FAILURE);
	}

	for (use_msg = 0; use_msg <= 1; use_msg++) {
		const char *name = use_msg ? "recvmsg (unnamed)" : "recvfrom (unnamed)";

		check_path(receiver, sender, use_msg, SENDER, 0);

		/* An unbound sender has no name at all.  */
		send_to(unnamed, RECEIVER);
		receive(receiver, use_msg, &addr, sizeof(addr), &length);
		if (length != 0)
			fail(name, &addr, length);

		/* An abstract name is not a path.  */
		name = use_msg ? "recvmsg (abstract)" : "recvfrom (abstract)";
		send_to(abstract_sender, RECEIVER);
		receive(receiver, use_msg, &addr, sizeof(addr), &length);
		if (length != offsetof(struct sockaddr_un, sun_path) + sizeof(abstract) - 1
		    || memcmp(addr.sun_path, abstract, sizeof(abstract) - 1) != 0)
			fail(name, &addr, length);
	}

	(void) unlink(RECEIVER);
	(void) unlink(SENDER);
	(void) rmdir(DIR);

	if (failures != 0)
		return EXIT_FAILURE;

	puts("recvfrom/recvmsg report guest sender paths");
	return EXIT_SUCCESS;
}
