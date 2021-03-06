#include <kernel/cpu.h>
#include <kernel/pic.h>
#include <kernel/timer.h>

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <cpuid.h>

#ifndef __cplusplus
#include <stdbool.h>
#endif

#ifndef __build_i386
#error "Cannot build cpu.c driver for non-i386 systems"
#endif

#define MAX_INTERRUPTS 256

#define INTERRUPT_TYPE_PRESENT		0x80
#define INTERRUPT_TYPE_SYSTEM		0x00
#define INTERRUPT_TYPE_USER			0x60
#define INTERRUPT_TYPE_GATE32		0x0E

#define DEVICE_INTERRUPT_BASE	0x20

#define INTERRUPT32_TYPE	(INTERRUPT_TYPE_PRESENT|INTERRUPT_TYPE_SYSTEM|INTERRUPT_TYPE_GATE32)
#define SYSTEM_CODE_SELECTOR	0x08

#define CPUID_BASIC 	0x0
#define CPUID_FEATURES	0x00000001
#define CPUID_EXTENDED 	0x80000000

#define CPUID_FEATURE_FPU_PRESENT	(1<<0)
#define CPUID_FEATURE_APIC_PRESENT 	(1<<9)

typedef void(*isr_handler)();

typedef struct INTERRUPT_DESCRIPTOR_TABLE {
	uint16_t limit;
	uint32_t base;
} __attribute__((packed)) _interrupt_descriptor_table_t;

typedef struct ISR_ENTRY {
	uint16_t offset_low;
	uint16_t selector;
	uint8_t  zero;
	uint8_t  type_attribute;
	uint16_t offset_high;
} __attribute__((packed)) _isr_entry_t;

_interrupt_descriptor_table_t idt_entry;
_isr_entry_t interrupts[MAX_INTERRUPTS];
bool using_apic = false;

/** default interrupt handler. set for all interrupts until initialization is complete */
extern void __attribute__((cdecl))i386_defaulthandler();

/** processor faults / traps / exceptions. the order in which they are listed is the order of the interrupt vector assigned */
extern void __attribute__((cdecl))i386_divbyzero();
extern void __attribute__((cdecl))i386_debugtrap();
extern void __attribute__((cdecl))i386_nmi();
extern void __attribute__((cdecl))i386_breakpoint();
extern void __attribute__((cdecl))i386_overflow();
extern void __attribute__((cdecl))i386_bounderror();
extern void __attribute__((cdecl))i386_badopcode();
extern void __attribute__((cdecl))i386_deviceerror();
extern void __attribute__((cdecl))i386_doublefault();
extern void __attribute__((cdecl))i386_invalidtss();
extern void __attribute__((cdecl))i386_segnotpresent();
extern void __attribute__((cdecl))i386_stacksegfault();
extern void __attribute__((cdecl))i386_genprotectfault();
extern void __attribute__((cdecl))i386_pagefault();
extern void __attribute__((cdecl))i386_fpuexception();
extern void __attribute__((cdecl))i386_aligncheck();
extern void __attribute__((cdecl))i386_machinecheck();
extern void __attribute__((cdecl))i386_simdexception();
extern void __attribute__((cdecl))i386_virtualizeexception();
extern void __attribute__((cdecl))i386_securityexception();

static inline void stop_interrupts() {
	asm volatile("cli");
}

static inline void start_interrupts() {
	asm volatile("sti");
}

static inline void load_idtr() {
	asm volatile("lidt (%0);" :: "m"(idt_entry));
}

/** internal function declarations */
static int install_handler(uint32_t index, uint8_t flags, uint16_t selector, uint32_t routine);
static void install_cpu_exceptions();
static bool has_feature(uint32_t feature);

/**
 * Initialize the CPU. For now, I trust the GDT I set up in bootstub.asm
 * TODO: add code to work with the GDT / LDT
 */
int cpu_driver_init() {
	// interrupts are already off, but just in case...
	stop_interrupts();

	// zero out the interrupt vector table
	memset(&interrupts[0],0,sizeof(_isr_entry_t)*MAX_INTERRUPTS);
	for (uint32_t i = 0; i < MAX_INTERRUPTS; i++) {
		install_handler(i,INTERRUPT32_TYPE,SYSTEM_CODE_SELECTOR,(uint32_t)&i386_defaulthandler);
	}

	idt_entry.base = (uint32_t) &interrupts[0];
	idt_entry.limit = (sizeof(_isr_entry_t)*MAX_INTERRUPTS)-1;

	install_cpu_exceptions();
	load_idtr();

	// TODO: add APIC detection routine here and use 8259a PIC as fallback
	// going to disable the PICs for now so we aren't firing odd interrupts
	pic_8259a_initialize(DEVICE_INTERRUPT_BASE,DEVICE_INTERRUPT_BASE+8);
	pic_8259a_disable();

	start_interrupts();
	return 0;
}

/**
 * Tell the CPU to listen for interrupt requests from the given IRQ
 * Execute the handler at fn_address when the IRQ is raised
 */
void cpu_install_device(uint32_t irq, void *fn_address) {
	uint32_t interrupt_idx = irq + DEVICE_INTERRUPT_BASE;
	uint32_t address = (uint32_t) fn_address;
	
	// need to pause interrupts while making IDT changes
	stop_interrupts();
	install_handler(interrupt_idx,INTERRUPT32_TYPE,SYSTEM_CODE_SELECTOR,address);
	start_interrupts();
	
	if (!using_apic) {
		pic_8259a_unmask_irq((uint8_t) irq);
	} else {
		// TODO: add APIC routine here
	}
}

/** internal functions */

/**
 * install the given interrupt handler at the given index in the IDT
 */
static int install_handler(uint32_t index, uint8_t flags, uint16_t selector, uint32_t routine) {
	if (index > MAX_INTERRUPTS) return -1;

	interrupts[index].offset_low = (uint16_t)(routine & 0xffff);
	interrupts[index].offset_high = (uint16_t)(routine >> 16);
	interrupts[index].selector = selector;
	interrupts[index].type_attribute = flags;
	return 0;
}

/**
 * install the CPU default exception handlers. This is only called once,
 * so it could be inline'd?
 */
static void install_cpu_exceptions() {
	// install the system exception handlers
	install_handler(0,INTERRUPT32_TYPE,SYSTEM_CODE_SELECTOR,(uint32_t)&i386_divbyzero);
	install_handler(1,INTERRUPT32_TYPE,SYSTEM_CODE_SELECTOR,(uint32_t)&i386_debugtrap);
	install_handler(2,INTERRUPT32_TYPE,SYSTEM_CODE_SELECTOR,(uint32_t)&i386_nmi);
	install_handler(3,INTERRUPT32_TYPE,SYSTEM_CODE_SELECTOR,(uint32_t)&i386_breakpoint);
	install_handler(4,INTERRUPT32_TYPE,SYSTEM_CODE_SELECTOR,(uint32_t)&i386_overflow);
	install_handler(5,INTERRUPT32_TYPE,SYSTEM_CODE_SELECTOR,(uint32_t)&i386_bounderror);
	install_handler(6,INTERRUPT32_TYPE,SYSTEM_CODE_SELECTOR,(uint32_t)&i386_badopcode);
	install_handler(7,INTERRUPT32_TYPE,SYSTEM_CODE_SELECTOR,(uint32_t)&i386_deviceerror);
	install_handler(8,INTERRUPT32_TYPE,SYSTEM_CODE_SELECTOR,(uint32_t)&i386_doublefault);
	install_handler(10,INTERRUPT32_TYPE,SYSTEM_CODE_SELECTOR,(uint32_t)&i386_invalidtss);
	install_handler(11,INTERRUPT32_TYPE,SYSTEM_CODE_SELECTOR,(uint32_t)&i386_segnotpresent);
	install_handler(12,INTERRUPT32_TYPE,SYSTEM_CODE_SELECTOR,(uint32_t)&i386_stacksegfault);
	install_handler(13,INTERRUPT32_TYPE,SYSTEM_CODE_SELECTOR,(uint32_t)&i386_genprotectfault);
	install_handler(14,INTERRUPT32_TYPE,SYSTEM_CODE_SELECTOR,(uint32_t)&i386_pagefault);
	install_handler(16,INTERRUPT32_TYPE,SYSTEM_CODE_SELECTOR,(uint32_t)&i386_fpuexception);
	install_handler(17,INTERRUPT32_TYPE,SYSTEM_CODE_SELECTOR,(uint32_t)&i386_aligncheck);
	install_handler(18,INTERRUPT32_TYPE,SYSTEM_CODE_SELECTOR,(uint32_t)&i386_machinecheck);
	install_handler(19,INTERRUPT32_TYPE,SYSTEM_CODE_SELECTOR,(uint32_t)&i386_simdexception);
	install_handler(20,INTERRUPT32_TYPE,SYSTEM_CODE_SELECTOR,(uint32_t)&i386_virtualizeexception);
	install_handler(30,INTERRUPT32_TYPE,SYSTEM_CODE_SELECTOR,(uint32_t)&i386_securityexception);
}

/**
 * Check EDX for specific CPUID feature
 */
static bool has_feature(uint32_t feature) {
	if (__get_cpuid_max(CPUID_BASIC,NULL) > 0) {
		unsigned int reg_eax, reg_ebx, reg_ecx, reg_edx;
		__get_cpuid(CPUID_FEATURES, &reg_eax, &reg_ebx, &reg_ecx, &reg_edx);
		if (reg_edx & feature) {
			return true;
		}
	}
	return false;
}