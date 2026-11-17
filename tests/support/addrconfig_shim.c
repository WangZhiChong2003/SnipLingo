/*
 * Test-only LD_PRELOAD shim: clears AI_ADDRCONFIG from getaddrinfo hints.
 *
 * libdbus uses AI_ADDRCONFIG for its TCP transport. In a restricted network namespace that has only
 * a loopback interface (some sandboxes, some CI containers) the resolver then refuses even numeric
 * addresses, so a private D-Bus daemon on 127.0.0.1 is unreachable. The AT-SPI integration test
 * needs exactly that connection; this shim makes the lookup behave like a normal machine. It
 * affects nothing but the test process tree.
 */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <netdb.h>

int getaddrinfo(
    const char *node, const char *service, const struct addrinfo *hints, struct addrinfo **res) {
    static int (*real_getaddrinfo)(
        const char *, const char *, const struct addrinfo *, struct addrinfo **);
    if (!real_getaddrinfo)
        real_getaddrinfo = (int (*)(const char *, const char *, const struct addrinfo *,
            struct addrinfo **))dlsym(RTLD_NEXT, "getaddrinfo");

    struct addrinfo patched;
    if (hints) {
        patched = *hints;
        patched.ai_flags &= ~AI_ADDRCONFIG;
        hints = &patched;
    }
    return real_getaddrinfo(node, service, hints, res);
}
