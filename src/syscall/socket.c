/* -*- c-set-style: "K&R"; c-basic-offset: 8 -*-
 *
 * This file is part of PRoot.
 *
 * Copyright (C) 2015 STMicroelectronics
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301 USA.
 */

#include <stddef.h>      /* offsetof(3), */
#include <strings.h>     /* bzero(3), */
#include <string.h>      /* strncpy(3), strlen(3), */
#include <assert.h>      /* assert(3), */
#include <errno.h>       /* E*, */
#include <sys/socket.h>  /* struct sockaddr_un, AF_UNIX, */
#include <sys/un.h>      /* struct sockaddr_un, */
#include <sys/param.h>   /* MIN(), MAX(), */
#include <sys/uio.h>     /* UIO_MAXIOV, */
#include <stdint.h>      /* uint32_t, */
#include <stdbool.h>     /* bool, */
#include <talloc.h>      /* talloc_*, */

#include "syscall/socket.h"
#include "tracee/tracee.h"
#include "tracee/mem.h"
#include "tracee/abi.h"
#include "path/binding.h"
#include "path/temp.h"
#include "path/path.h"
#include "arch.h"

#include "compat.h"

/* The sockaddr_un structure has exactly the same layout on all
 * architectures.  */
static const off_t offsetof_path = offsetof(struct sockaddr_un, sun_path);
extern struct sockaddr_un sockaddr_un__;
static const size_t sizeof_path  = sizeof(sockaddr_un__.sun_path);

/**
 * Copy in @sockaddr the struct sockaddr_un stored in the @tracee
 * memory at the given @address.  Also, its pathname is copied to the
 * null-terminated @path.  Only @size bytes are read from the @tracee
 * memory (should be <= @max_size <= sizeof(struct sockaddr_un)).
 * This function returns -errno if an error occurred, 0 if the
 * structure was not found (not a sockaddr_un or @size > @max_size),
 * otherwise 1.
 */
static int read_sockaddr_un(Tracee *tracee, struct sockaddr_un *sockaddr, word_t max_size,
			char path[PATH_MAX], word_t address, int size)
{
	int status;

	assert(max_size <= sizeof(struct sockaddr_un));

	/* Nothing to do if the sockaddr has an unexpected size.  */
	if (size <= offsetof_path || (word_t) size > max_size)
		return 0;

	bzero(sockaddr, sizeof(struct sockaddr_un));
	status = read_data(tracee, sockaddr, address, size);
	if (status < 0)
		return status;

	/* Nothing to do if it's not a named Unix domain socket.  */
	if ((sockaddr->sun_family != AF_UNIX)
	    || sockaddr->sun_path[0] == '\0')
		return 0;

	/* Be careful: sun_path doesn't have to be null-terminated.  */
	assert(sizeof_path < PATH_MAX - 1);
	strncpy(path, sockaddr->sun_path, sizeof_path);
	path[sizeof_path] = '\0';

	return 1;
}

/**
 * Translate the pathname of the struct sockaddr_un currently stored
 * in the @tracee memory at the given @address.  See the documentation
 * of read_sockaddr_un() for the meaning of the @size parameter.
 * Also, the new address of the translated sockaddr_un is put in the
 * @address parameter.  A translated path too long to fit the sun_path
 * array is bound to a shorter one if @bind_long_path is true, it is
 * refused with -ENAMETOOLONG otherwise.  This function returns -errno
 * if an error occurred, 0 if there was nothing to translate,
 * otherwise 1.
 */
int translate_socketcall_enter2(Tracee *tracee, word_t *address, int size, bool bind_long_path)
{
	struct sockaddr_un sockaddr;
	char user_path[PATH_MAX];
	char host_path[PATH_MAX];
	int status;

	if (*address == 0)
		return 0;

	status = read_sockaddr_un(tracee, &sockaddr, sizeof(sockaddr), user_path, *address, size);
	if (status <= 0)
		return status;

	status = translate_path(tracee, host_path, AT_FDCWD, user_path, true);
	if (status < 0)
		return status;

	/* Be careful: sun_path doesn't have to be null-terminated.  */
	if (strlen(host_path) > sizeof_path) {
		char *shorter_host_path;
		Binding *binding;

		if (!bind_long_path)
			return -ENAMETOOLONG;

		/* The translated path is too long to fit the sun_path
		 * array, so let's bind it to a shorter path.  */
		shorter_host_path = create_temp_name(tracee->ctx, "proot");
		if (shorter_host_path == NULL || strlen(shorter_host_path) > sizeof_path)
			return -EINVAL;

		(void) mktemp(shorter_host_path);

		if (strlen(shorter_host_path) > sizeof_path)
			return -EINVAL;

		/* Ensure the guest path of this new binding is
		 * canonicalized, as it is always assumed.  */
		strcpy(user_path, host_path);
		status = detranslate_path(tracee, user_path, NULL);
		if (status < 0)
			return -EINVAL;

		/* Bing the guest path to a shorter host path.  */
		binding = insort_binding3(tracee, tracee->ctx, shorter_host_path, user_path);
		if (binding == NULL)
			return -EINVAL;

		/* This temporary file (shorter_host_path) will be removed once the
		 * binding is destroyed.  */
		talloc_reparent(tracee->ctx, binding, shorter_host_path);

		/* Let's use this shorter path now.  */
		strcpy(host_path, shorter_host_path);
	}
	strncpy(sockaddr.sun_path, host_path, sizeof_path);

	/* Push the updated sockaddr to a newly allocated space.  */
	*address = alloc_mem(tracee, sizeof(sockaddr));
	if (*address == 0)
		return -EFAULT;

	status = write_data(tracee, *address, &sockaddr, sizeof(sockaddr));
	if (status < 0)
		return status;

	return 1;
}

/**
 * c.f. function above, a too long path is bound to a shorter one.
 */
int translate_socketcall_enter(Tracee *tracee, word_t *address, int size)
{
	return translate_socketcall_enter2(tracee, address, size, true);
}

/* A struct msghdr is seven words long, msg_name and then msg_namelen
 * first, and a struct mmsghdr is a struct msghdr followed by msg_len,
 * eight words long, on all ABIs.  */
#define SIZEOF_MSGHDR(tracee)	(7 * sizeof_word(tracee))
#define SIZEOF_MMSGHDR(tracee)	(8 * sizeof_word(tracee))

/**
 * Translate the pathname of the struct sockaddr_un pointed to by the
 * msg_name field of the struct msghdr currently stored in the @tracee
 * memory at the given @address, and put the address of the translated
 * sockaddr_un in @name.  This function returns -errno if an error
 * occurred, 0 if there was nothing to translate, otherwise 1.
 */
static int translate_msg_name(Tracee *tracee, word_t address, word_t *name)
{
	int namelen;

	*name = peek_word(tracee, address);
	if (errno != 0)
		return -errno;

	/* Nothing to do if no address was specified, as for a
	 * connected socket.  */
	if (*name == 0)
		return 0;

	namelen = peek_int32(tracee, address + sizeof_word(tracee));
	if (errno != 0)
		return -errno;

	/* See PR_sendto in translate_syscall_enter().  */
	return translate_socketcall_enter2(tracee, name, namelen, false);
}

/**
 * Make the struct msghdr currently stored in the @tracee memory at
 * the given @address point to the translated sockaddr_un at @name.
 * This function returns -errno if an error occurred, otherwise 0.
 */
static int set_msg_name(Tracee *tracee, word_t address, word_t name)
{
	poke_word(tracee, address, name);
	if (errno != 0)
		return -errno;

	poke_int32(tracee, address + sizeof_word(tracee), sizeof(struct sockaddr_un));
	if (errno != 0)
		return -errno;

	return 0;
}

/**
 * Push a copy of the @size bytes currently stored in the @tracee
 * memory at the given @address to a newly allocated space, and put
 * the address of this copy in the @address parameter.  This function
 * returns -errno if an error occurred, otherwise 0.
 */
static int copy_to_new_mem(Tracee *tracee, word_t *address, size_t size)
{
	void *data;
	int status;

	data = talloc_size(tracee->ctx, size);
	if (data == NULL)
		return -ENOMEM;

	status = read_data(tracee, data, *address, size);
	if (status < 0)
		return status;

	*address = alloc_mem(tracee, size);
	if (*address == 0)
		return -EFAULT;

	return write_data(tracee, *address, data, size);
}

/**
 * Translate the pathname of the struct sockaddr_un pointed to by the
 * msg_name field of the struct msghdr currently stored in the @tracee
 * memory at the given @address: sendmsg(2) sends to it as sendto(2)
 * sends to its destination address.  The tracee might use its struct
 * msghdr again, so it is left untouched: a copy of it that points to
 * the translated sockaddr_un is pushed to a newly allocated space,
 * whose address is put in the @address parameter.  This function
 * returns -errno if an error occurred, 0 if there was nothing to
 * translate, otherwise 1.
 */
int translate_msghdr_enter(Tracee *tracee, word_t *address)
{
	word_t name;
	int status;

	if (*address == 0)
		return 0;

	status = translate_msg_name(tracee, *address, &name);
	if (status <= 0)
		return status;

	status = copy_to_new_mem(tracee, address, SIZEOF_MSGHDR(tracee));
	if (status < 0)
		return status;

	status = set_msg_name(tracee, *address, name);
	if (status < 0)
		return status;

	return 1;
}

/**
 * Translate the msg_name of each of the *@vlen struct mmsghdr
 * currently stored in the @tracee memory at the given @address, since
 * sendmmsg(2) sends each of these messages as sendmsg(2) does.  As
 * with translate_msghdr_enter(), a copy of this vector is used when
 * any name is translated, and its address is put in the @address
 * parameter; the kernel then reports the length it sent of each
 * message in this copy, see translate_mmsghdr_exit().  Like the
 * kernel, send only the messages before the first one whose name
 * can't be read or translated: *@vlen is lowered to their number, or
 * the error is returned if there is none.  This function returns
 * -errno if an error occurred, 0 if there was nothing to change,
 * otherwise 1.
 */
int translate_mmsghdr_enter(Tracee *tracee, word_t *address, word_t *vlen)
{
	bool translated = false;
	word_t *names;
	word_t count;
	word_t i;
	int status = 0;

	if (*address == 0 || *vlen == 0)
		return 0;

	/* The kernel doesn't send more messages at once.  */
	count = MIN(*vlen, UIO_MAXIOV);

	names = talloc_zero_array(tracee->ctx, word_t, count);
	if (names == NULL)
		return -ENOMEM;

	for (i = 0; i < count; i++) {
		status = translate_msg_name(tracee, *address + i * SIZEOF_MMSGHDR(tracee), &names[i]);
		if (status < 0)
			break;

		if (status > 0)
			translated = true;
		else
			names[i] = 0;
	}

	if (status < 0) {
		if (i == 0)
			return status;

		count = i;
		*vlen = count;
	}
	else if (!translated)
		return 0;

	if (translated) {
		status = copy_to_new_mem(tracee, address, count * SIZEOF_MMSGHDR(tracee));
		if (status < 0)
			return status;

		for (i = 0; i < count; i++) {
			if (names[i] == 0)
				continue;

			status = set_msg_name(tracee, *address + i * SIZEOF_MMSGHDR(tracee), names[i]);
			if (status < 0)
				return status;
		}
	}

	return 1;
}

/**
 * Report the msg_len the kernel wrote in the copy at @copy of the
 * vector of struct mmsghdr stored in the @tracee memory at the given
 * @address, for its first @count messages, the ones it sent; see
 * translate_mmsghdr_enter().  This function returns -errno if an
 * error occurred, otherwise 0.
 */
int translate_mmsghdr_exit(Tracee *tracee, word_t address, word_t copy, word_t count)
{
	const word_t offsetof_len = SIZEOF_MSGHDR(tracee);
	uint32_t len;
	word_t i;

	for (i = 0; i < count; i++) {
		len = peek_uint32(tracee, copy + i * SIZEOF_MMSGHDR(tracee) + offsetof_len);
		if (errno != 0)
			return -errno;

		poke_uint32(tracee, address + i * SIZEOF_MMSGHDR(tracee) + offsetof_len, len);
		if (errno != 0)
			return -errno;
	}

	return 0;
}

/**
 * Detranslate the pathname of the struct sockaddr_un currently stored
 * in the @tracee memory at the given @sock_addr.  See the
 * documentation of read_sockaddr_un() for the meaning of the
 * @size_addr and @max_size parameters.  This function returns -errno
 * if an error occurred, otherwise 0.
 */
int translate_socketcall_exit(Tracee *tracee, word_t sock_addr, word_t size_addr, word_t max_size)
{
	struct sockaddr_un sockaddr;
	bool is_truncated = false;
	char path[PATH_MAX];
	int status;
	int size;

	if (sock_addr == 0)
		return 0;

	size = peek_int32(tracee, size_addr);
	if (errno != 0)
		return -errno;

	max_size = MIN(max_size, sizeof(sockaddr));
	status = read_sockaddr_un(tracee, &sockaddr, max_size, path, sock_addr, size);
	if (status <= 0)
		return status;

	status = detranslate_path(tracee, path, NULL);
	if (status < 0)
		return status;

	/* Be careful: sun_path doesn't have to be null-terminated.  */
	size = offsetof_path + strlen(path) + 1;
	if (size < 0 || (word_t) size > max_size) {
		size = max_size;
		is_truncated = true;
	}
	strncpy(sockaddr.sun_path, path, sizeof_path);

	/* Overwrite the sockaddr and socklen parameters.  */
	status = write_data(tracee, sock_addr, &sockaddr, size);
	if (status < 0)
		return status;

	/* If sockaddr is truncated (because the buffer provided is
	 * too small), addrlen will return a value greater than was
	 * supplied to the call.  See man 2 accept. */
	if (is_truncated)
		size = max_size + 1;

	poke_int32(tracee, size_addr, size);
	if (errno != 0)
		return -errno;

	return 0;
}

/**
 * Detranslate the source address the kernel wrote at @sock_addr (its
 * length at @size_addr) at the exit stage of a successful recvfrom(2)
 * or recvmsg(2): a named AF_UNIX sender is reported with its host
 * path, which is turned back into the guest path.  At most @max_size
 * bytes, the length the tracee allowed, are written, and the full
 * length is reported at @size_addr even when the address had to be
 * truncated, like the kernel does.  Abstract and unnamed addresses,
 * and other families, are left untouched.  An address the kernel
 * already truncated is left untouched too: its host path is lost.
 * This function returns -errno if an error occurred, otherwise 0.
 */
int translate_recv_name_exit(Tracee *tracee, word_t sock_addr, word_t size_addr, word_t max_size)
{
	struct sockaddr_un sockaddr;
	char path[PATH_MAX];
	size_t length;
	word_t full_size;
	int status;
	int size;

	if (sock_addr == 0 || size_addr == 0)
		return 0;

	size = peek_int32(tracee, size_addr);
	if (errno != 0)
		return -errno;

	status = read_sockaddr_un(tracee, &sockaddr, MIN(max_size, sizeof(sockaddr)),
				path, sock_addr, size);
	if (status <= 0)
		return status;

	status = detranslate_path(tracee, path, NULL);
	if (status < 0)
		return status;

	/* Like the kernel: the path and its terminating null byte,
	 * the latter omitted when the path fills sun_path.  */
	length = strlen(path);
	if (length > sizeof_path)
		length = sizeof_path;
	full_size = offsetof_path + MIN(length + 1, sizeof_path);

	memset(sockaddr.sun_path, 0, sizeof_path);
	memcpy(sockaddr.sun_path, path, length);

	/* Writing at least what the kernel wrote also clears the rest of
	 * a host path longer than the guest one.  */
	status = write_data(tracee, sock_addr, &sockaddr,
			    MIN(MAX(full_size, (word_t) size), max_size));
	if (status < 0)
		return status;

	poke_int32(tracee, size_addr, full_size);
	if (errno != 0)
		return -errno;

	return 0;
}
