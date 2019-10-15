#include <version.h>

#include <init/kterm.h>
#include <init/kutils.h>
#include <init/memory/kmemlow.h>
#include <init/memory/paging.h>
#include <init/multiboot/multiboot.h>
#include <init/kerrno.h>

void early_welcome_message() {
    ki_printf("missy kernel %s\n", KERNEL_VERSION_STRING);
#ifdef DEBUG_BUILD
    ki_printf("this is a debug build. debugging output will be shown\n");
#endif
}

char buffer[MAXCHARS];

void kernel_early_init(void *mboot_hdr, unsigned int magic) {
    early_welcome_message();

    int result = multiboot_init(mboot_hdr, magic);
    if (result != 0) {
        ki_printf("[PANIC]: Multiboot data format could not be determined: %d\n", kinit_errno);
        early_panic();
    }

	// we should initialize the block allocator now, then set up for paging
	// the asm block will install the paging handler once this function returns
    kmemlow_init_allocator();
	ki_setup_paging();
}