#include <kernel/version.h>

#include <init/kterm.h>
#include <init/kutils.h>
#include <init/memory/kmemlow.h>
#include <init/memory/paging.h>
#include <init/multiboot/multiboot.h>
#include <init/kerrno.h>
#include <init/kparams.h>

#include <stddef.h>
#include <stdint.h>

void early_welcome_message() {
    ki_printf("missy kernel %s\n", KERNEL_VERSION_STRING);
#ifdef DEBUG_BUILD
    ki_printf("this is a debug build. debugging output will be shown\n");
#endif
}

char buffer[MAXCHARS];

void kernel_early_init(void *mboot_hdr, unsigned int magic, _kernel_params_t *kparams) {
    early_welcome_message();

    int result = multiboot_init(mboot_hdr, magic);
    if (result != 0) {
        ki_printf("[PANIC]: Multiboot data format could not be determined: %d\n", kinit_errno);
        early_panic();
    }

	// preserve the rsdt address
	void *rsdt_ptr =  multiboot_get_rsdt_ptr();
	if (!rsdt_ptr) {
		ki_printf("[PANIC]: Couuld not retrieve the RSDT pointer from multiboot data\n");
		early_panic();
	}

	_acpi2_rsdp_t rsdp;
	if (multiboot_get_rsdp(&rsdp) == -1) {
		ki_printf("[WARNING]: Could not find ACPI information\n");
		early_panic();
	}

	

	// we should initialize the block allocator now, then set up for paging
	// the asm block will install the paging handler once this function returns
    kmemlow_init_allocator();
	ki_setup_paging();

	ki_memset(kparams,0,sizeof(_kernel_params_t));

	kparams->kernel_stack = ki_get_kernel_stack_top();
	kparams->kernel_heap  = ki_get_kernel_end_virtual();
	kparams->kernel_memory_bitmap = (uint32_t*) KERNEL_PHYSICAL_TO_VIRTUAL(kmemlow_get_bitmap_ptr());
	kparams->allocator_total_blocks = kmemlow_get_total_blocks();
	kparams->allocator_used_blocks = kmemlow_get_used_blocks();
	kparams->rsdt_address = rsdt_address;

	ki_printf("kparams struct:\n");
	ki_printf("kernel_stack - 0x%x\n", (unsigned int) kparams->kernel_stack);
	ki_printf("kernel_heap - 0x%x\n", (unsigned int) kparams->kernel_heap);
	ki_printf("kernel_memory_bitmap - 0x%x\n", (unsigned int) kparams->kernel_memory_bitmap);
	ki_printf("kernel memory total blocks: %d\n", kparams->allocator_total_blocks);
	ki_printf("kernel memory used blocks: %d\n", kparams->allocator_used_blocks);
}