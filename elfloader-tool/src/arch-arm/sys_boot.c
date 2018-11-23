/*
 * Copyright 2017, Data61
 * Commonwealth Scientific and Industrial Research Organisation (CSIRO)
 * ABN 41 687 119 230.
 *
 * This software may be distributed and modified according to the terms of
 * the GNU General Public License version 2. Note that NO WARRANTY is provided.
 * See "LICENSE_GPLv2.txt" for details.
 *
 * @TAG(DATA61_GPL)
 */

#include <autoconf.h>

#include <printf.h>
#include <types.h>
#include <abort.h>
#include <strops.h>
#include <cpuid.h>
#include <platform.h>

#include <binaries/efi/efi.h>
#include <elfloader.h>

/* 0xd00dfeed in big endian */
#define DTB_MAGIC (0xedfe0dd0)

ALIGN(BIT(PAGE_BITS)) VISIBLE
char core_stack_alloc[CONFIG_MAX_NUM_NODES][BIT(PAGE_BITS)];

struct image_info kernel_info;
struct image_info user_info;

/*
 * Entry point.
 *
 * Unpack images, setup the MMU, jump to the kernel.
 */
void main(UNUSED void *arg)
{
    int num_apps;
    uint32_t dtb_size;
    void *dtb;

#ifdef CONFIG_IMAGE_UIMAGE
    if (arg) {
        uint32_t magic = *(uint32_t *)arg;
        /*
         * This might happen on ancient bootloaders which
         * still think Linux wants atags instead of a
         * device tree.
         */
        if (magic != DTB_MAGIC) {
            printf("Bootloader did not supply a valid device tree!\n");
            arg = NULL;
        }
    }
    dtb = arg;
#else
    dtb = NULL;
#endif

#ifdef CONFIG_IMAGE_EFI
    if (efi_exit_boot_services() != EFI_SUCCESS) {
        printf("Unable to exit UEFI boot services!\n");
        abort();
    }

	/* TODO: get DTB from EFI like Linux does. */
#endif

    /* Print welcome message. */
    platform_init();
    printf("\nELF-loader started on ");
    print_cpuid();

    printf("  paddr=[%p..%p]\n", _start, _end - 1);

    /*
     * U-Boot will either pass us a DTB, or (if we're being booted via bootelf)
     * pass '0' in argc.
     */
    if (dtb) {
        printf("  dtb=%p\n", dtb);
    } else {
        printf("No DTB found!\n");
    }

    /* Unpack ELF images into memory. */
    load_images(&kernel_info, &user_info, 1, &num_apps, &dtb, &dtb_size);
    if (num_apps != 1) {
        printf("No user images loaded!\n");
        abort();
    }

#if (defined(CONFIG_ARCH_ARM_V7A) || defined(CONFIG_ARCH_ARM_V8A)) && !defined(CONFIG_ARM_HYPERVISOR_SUPPORT)
    if (is_hyp_mode()) {
        extern void leave_hyp(void);
        leave_hyp();
    }
#endif
    /* Setup MMU. */
    if (is_hyp_mode()) {
#ifdef CONFIG_ARCH_AARCH64
        extern void disable_caches_hyp();
        disable_caches_hyp();
#endif
        init_hyp_boot_vspace(&kernel_info);
    } else {
        /* If we are not in HYP mode, we enable the SV MMU and paging
         * just in case the kernel does not support hyp mode. */
        init_boot_vspace(&kernel_info);
    }

#if CONFIG_MAX_NUM_NODES > 1
    smp_boot();
#endif /* CONFIG_MAX_NUM_NODES */

    if (is_hyp_mode()) {
        printf("Enabling hypervisor MMU and paging\n");
        arm_enable_hyp_mmu();
    } else {
        printf("Enabling MMU and paging\n");
        arm_enable_mmu();
    }

    /* Enter kernel. */
    if (UART_PPTR < kernel_info.virt_region_start) {
        printf("Jumping to kernel-image entry point...\n\n");
    } else {
        /* Our serial port is no longer accessible */
    }

    ((init_arm_kernel_t)kernel_info.virt_entry)(user_info.phys_region_start,
                                                user_info.phys_region_end, user_info.phys_virt_offset,
                                                user_info.virt_entry, (paddr_t)dtb, dtb_size);

    /* We should never get here. */
    printf("Kernel returned back to the elf-loader.\n");
    abort();
}
