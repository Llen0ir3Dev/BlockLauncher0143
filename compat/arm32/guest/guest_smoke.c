#include <stdio.h>

/*
 * Minimal statically linked ARM32 bionic guest.
 *
 * Purpose: prove that the bundled runtime can execute genuine ARM32 guest
 * code under QEMU user-mode on any host, with no dependency on a device
 * bionic linker.  Built by tools/build_guest_arm32.sh.
 */
int main(void) {
    printf("NEXUS ARM32 guest: static bionic runtime OK\n");
    return 0;
}
