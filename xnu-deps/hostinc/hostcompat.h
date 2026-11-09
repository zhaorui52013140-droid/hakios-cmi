/* Force-included into XNU's SETUP host tools when building on Linux */
#ifndef XNU_HOSTCOMPAT_H
#define XNU_HOSTCOMPAT_H
#ifndef _GNU_SOURCE
#define _GNU_SOURCE 1
#endif
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
extern char *program_invocation_short_name;
static inline const char *getprogname(void) { return program_invocation_short_name; }
#ifndef __unused
#define __unused __attribute__((unused))
#endif
#ifndef __dead2
#define __dead2 __attribute__((noreturn))
#endif
#ifndef __printflike
#define __printflike(a,b) __attribute__((format(printf,a,b)))
#endif
#include <libgen.h>
#include <sys/param.h>
typedef uintptr_t vm_offset_t;
typedef uintptr_t vm_size_t;
typedef uintptr_t vm_address_t;
typedef int boolean_t;
typedef int kern_return_t;
#ifndef TRUE
#define TRUE 1
#define FALSE 0
#endif
#define KERN_SUCCESS 0
#include <byteswap.h>
#define OSSwapInt16(x) bswap_16(x)
#define OSSwapInt32(x) bswap_32(x)
#define OSSwapInt64(x) bswap_64(x)
#include <uuid/uuid.h>
static inline void host_uuid_generate(unsigned char *out) {
	FILE *f = fopen("/dev/urandom", "rb");
	if (!f || fread(out, 1, 16, f) != 16) abort();
	fclose(f);
	out[6] = (out[6] & 0x0f) | 0x40; out[8] = (out[8] & 0x3f) | 0x80;
}
#define uuid_generate(x) host_uuid_generate(x)
/* BSD setmode/getmode: only absolute octal modes are used by the XNU build */
static inline void *setmode(const char *s) {
	char *end; long v = strtol(s, &end, 8);
	if (*end || v < 0) return NULL;
	mode_t *m = malloc(sizeof *m); *m = (mode_t)v; return m;
}
static inline mode_t getmode(const void *set, mode_t omode) { (void)omode; return *(const mode_t *)set; }
#ifndef O_EXLOCK
#define O_EXLOCK 0
#endif
#ifndef O_SHLOCK
#define O_SHLOCK 0
#endif
#ifndef O_SYMLINK
#define O_SYMLINK 0
#endif
#ifndef EFTYPE
#define EFTYPE EINVAL
#endif
/* glibc has neither strlcat nor strlcpy; XNU's SETUP tools use both */
static inline size_t
host_strlcat(char *dst, const char *src, size_t size)
{
	size_t dl = strnlen(dst, size);
	size_t sl = strlen(src);
	size_t room;
	if (dl == size)
		return size + sl;
	room = size - dl - 1;
	if (sl <= room) {
		memcpy(dst + dl, src, sl + 1);
		return dl + sl;
	}
	memcpy(dst + dl, src, room);
	dst[dl + room] = '\0';
	return dl + sl;
}
static inline size_t
host_strlcpy(char *dst, const char *src, size_t size)
{
	size_t sl = strlen(src);
	if (size != 0) {
		size_t n = (sl < size - 1) ? sl : (size - 1);
		memcpy(dst, src, n);
		dst[n] = '\0';
	}
	return sl;
}
#ifndef strlcat
#define strlcat host_strlcat
#endif
#ifndef strlcpy
#define strlcpy host_strlcpy
#endif
#endif
