// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2014 Gateworks Corporation
 * Copyright (C) 2011-2012 Freescale Semiconductor, Inc.
 *
 * Author: Tim Harvey <tharvey@gateworks.com>
 */

#include <common.h>
#include <hang.h>
#include <init.h>
#include <log.h>
#include <asm/global_data.h>
#include <asm/io.h>
#include <asm/arch/imx-regs.h>
#include <asm/arch/sys_proto.h>
#include <asm/spl.h>
#include <spl.h>
#include <asm/mach-imx/hab.h>
#include <asm/mach-imx/boot_mode.h>
#include <g_dnl.h>
#include <linux/libfdt.h>
#include <mmc.h>

DECLARE_GLOBAL_DATA_PTR;

__weak int spl_board_boot_device(enum boot_device boot_dev_spl)
{
	switch (boot_dev_spl) {
#if defined(CONFIG_MX7)
	case SD1_BOOT:
	case MMC1_BOOT:
	case SD2_BOOT:
	case MMC2_BOOT:
	case SD3_BOOT:
	case MMC3_BOOT:
		return BOOT_DEVICE_MMC1;
#elif defined(CONFIG_MX7ULP)
        case SD1_BOOT:
        case MMC1_BOOT:
                return BOOT_DEVICE_MMC1;
#elif defined(CONFIG_IMX8)
	case MMC1_BOOT:
		return BOOT_DEVICE_MMC1;
	case SD2_BOOT:
		return BOOT_DEVICE_MMC2_2;
	case SD3_BOOT:
		return BOOT_DEVICE_MMC1;
	case FLEXSPI_BOOT:
		return BOOT_DEVICE_SPI;
#elif defined(CONFIG_IMX8M)
	case SD1_BOOT:
	case MMC1_BOOT:
		return BOOT_DEVICE_MMC1;
	case SD2_BOOT:
	case MMC2_BOOT:
		return BOOT_DEVICE_MMC2;
#endif
	case NAND_BOOT:
		return BOOT_DEVICE_NAND;
	case SPI_NOR_BOOT:
		return BOOT_DEVICE_SPI;
	case QSPI_BOOT:
		return BOOT_DEVICE_NOR;
	case USB_BOOT:
		return BOOT_DEVICE_BOARD;
	default:
		return BOOT_DEVICE_NONE;
	}
}

#if defined(CONFIG_MX6)
/* determine boot device from SRC_SBMR1 (BOOT_CFG[4:1]) or SRC_GPR9 register */
u32 spl_boot_device(void)
{
	unsigned int bmode = readl(&src_base->sbmr2);
	u32 reg = imx6_src_get_boot_mode();

	/*
	 * Check for BMODE if serial downloader is enabled
	 * BOOT_MODE - see IMX6DQRM Table 8-1
	 */
	if (((bmode >> 24) & 0x03) == 0x01) /* Serial Downloader */
#ifdef CONFIG_SPL_RAM_DEVICE
		return BOOT_DEVICE_RAM;
#else
		return BOOT_DEVICE_BOARD;
#endif

	/*
	 * The above method does not detect that the boot ROM used
	 * serial downloader in case the boot ROM decided to use the
	 * serial downloader as a fall back (primary boot source failed).
	 *
	 * Infer that the boot ROM used the USB serial downloader by
	 * checking whether the USB PHY is currently active... This
	 * assumes that SPL did not (yet) initialize the USB PHY...
	 */
	if (is_usbotg_phy_active())
#ifdef CONFIG_SPL_RAM_DEVICE
		return BOOT_DEVICE_RAM;
#else
		return BOOT_DEVICE_BOARD;
#endif

	/* BOOT_CFG1[7:4] - see IMX6DQRM Table 8-8 */
	switch ((reg & IMX6_BMODE_MASK) >> IMX6_BMODE_SHIFT) {
	 /* EIM: See 8.5.1, Table 8-9 */
	case IMX6_BMODE_EIM:
		/* BOOT_CFG1[3]: NOR/OneNAND Selection */
		switch ((reg & IMX6_BMODE_EIM_MASK) >> IMX6_BMODE_EIM_SHIFT) {
		case IMX6_BMODE_ONENAND:
			return BOOT_DEVICE_ONENAND;
		case IMX6_BMODE_NOR:
			return BOOT_DEVICE_NOR;
		break;
		}
	/* Reserved: Used to force Serial Downloader */
	case IMX6_BMODE_RESERVED:
		return BOOT_DEVICE_BOARD;
	/* SATA: See 8.5.4, Table 8-20 */
#if !defined(CONFIG_MX6UL) && !defined(CONFIG_MX6ULL)
	case IMX6_BMODE_SATA:
		return BOOT_DEVICE_SATA;
#endif
	/* Serial ROM: See 8.5.5.1, Table 8-22 */
	case IMX6_BMODE_SERIAL_ROM:
		/* BOOT_CFG4[2:0] */
		switch ((reg & IMX6_BMODE_SERIAL_ROM_MASK) >>
			IMX6_BMODE_SERIAL_ROM_SHIFT) {
		case IMX6_BMODE_ECSPI1:
		case IMX6_BMODE_ECSPI2:
		case IMX6_BMODE_ECSPI3:
		case IMX6_BMODE_ECSPI4:
		case IMX6_BMODE_ECSPI5:
			return BOOT_DEVICE_SPI;
		case IMX6_BMODE_I2C1:
		case IMX6_BMODE_I2C2:
		case IMX6_BMODE_I2C3:
			return BOOT_DEVICE_I2C;
		}
		break;
	/* SD/eSD: 8.5.3, Table 8-15  */
	case IMX6_BMODE_SD:
	case IMX6_BMODE_ESD:
		return BOOT_DEVICE_MMC1;
	/* MMC/eMMC: 8.5.3 */
	case IMX6_BMODE_MMC:
	case IMX6_BMODE_EMMC:
		return BOOT_DEVICE_MMC1;
	/* NAND Flash: 8.5.2, Table 8-10 */
	case IMX6_BMODE_NAND_MIN ... IMX6_BMODE_NAND_MAX:
		return BOOT_DEVICE_NAND;
#if defined(CONFIG_MX6UL) || defined(CONFIG_MX6ULL)
	/* QSPI boot */
	case IMX6_BMODE_QSPI:
		return BOOT_DEVICE_SPI;
#endif
	}
	return BOOT_DEVICE_NONE;
}

#elif defined(CONFIG_MX7) || defined(CONFIG_MX7ULP) || defined(CONFIG_IMX8M) || defined(CONFIG_IMX8) || defined(CONFIG_IMX9)
/* Translate iMX7/i.MX8M boot device to the SPL boot device enumeration */
u32 spl_boot_device(void)
{
#if defined(CONFIG_MX7)
	unsigned int bmode = readl(&src_base->sbmr2);

	/*
	 * Check for BMODE if serial downloader is enabled
	 * BOOT_MODE - see IMX7DRM Table 6-24
	 */
	if (((bmode >> 24) & 0x03) == 0x01) /* Serial Downloader */
		return BOOT_DEVICE_BOARD;
#endif

#if defined(CONFIG_MX7) || defined(CONFIG_MX7ULP)
	/*
	 * The above method does not detect that the boot ROM used
	 * serial downloader in case the boot ROM decided to use the
	 * serial downloader as a fall back (primary boot source failed).
	 *
	 * Infer that the boot ROM used the USB serial downloader by
	 * checking whether the USB PHY is currently active... This
	 * assumes that SPL did not (yet) initialize the USB PHY...
	 */
	if (is_boot_from_usb())
		return BOOT_DEVICE_BOARD;
#endif

	enum boot_device boot_device_spl = get_boot_device();

	return spl_board_boot_device(boot_device_spl);
}
#endif /* CONFIG_MX7 || CONFIG_MX7ULP || CONFIG_IMX8M || CONFIG_IMX8 */

#ifdef CONFIG_SPL_USB_GADGET
int g_dnl_bind_fixup(struct usb_device_descriptor *dev, const char *name)
{
	put_unaligned(0x0151, &dev->idProduct);

	return 0;
}

#define SDPV_BCD_DEVICE 0x500
int g_dnl_get_board_bcd_device_number(int gcnum)
{
	return SDPV_BCD_DEVICE;
}
#endif

#if defined(CONFIG_SPL_MMC)
/* called from spl_mmc to see type of boot mode for storage (RAW or FAT) */
u32 spl_mmc_boot_mode(const u32 boot_device)
{
#if defined(CONFIG_MX7) || defined(CONFIG_IMX8M) || defined(CONFIG_IMX8)
	switch (get_boot_device()) {
	/* for MMC return either RAW or FAT mode */
	case SD1_BOOT:
	case SD2_BOOT:
	case SD3_BOOT:
		if (IS_ENABLED(CONFIG_SPL_FS_FAT))
			return MMCSD_MODE_FS;
		else
			return MMCSD_MODE_RAW;
	case MMC1_BOOT:
	case MMC2_BOOT:
	case MMC3_BOOT:
		if (IS_ENABLED(CONFIG_SPL_FS_FAT))
			return MMCSD_MODE_FS;
		else if (IS_ENABLED(CONFIG_SUPPORT_EMMC_BOOT))
			return MMCSD_MODE_EMMCBOOT;
		else
			return MMCSD_MODE_RAW;
	default:
		puts("spl: ERROR:  unsupported device\n");
		hang();
	}
#else
	switch (boot_device) {
	/* for MMC return either RAW or FAT mode */
	case BOOT_DEVICE_MMC1:
	case BOOT_DEVICE_MMC2:
	case BOOT_DEVICE_MMC2_2:
		if (IS_ENABLED(CONFIG_SPL_FS_FAT))
			return MMCSD_MODE_FS;
		else if (IS_ENABLED(CONFIG_SUPPORT_EMMC_BOOT))
			return MMCSD_MODE_EMMCBOOT;
		else
			return MMCSD_MODE_RAW;
	default:
		puts("spl: ERROR:  unsupported device\n");
		hang();
	}
#endif
}
#endif

#if defined(CONFIG_SPL_IMX_HAB)

/*
 * +------------+  0x0 (DDR_UIMAGE_START) -
 * |   Header   |                          |
 * +------------+  0x40                    |
 * |            |                          |
 * |            |                          |
 * |            |                          |
 * |            |                          |
 * | Image Data |                          |
 * .            |                          |
 * .            |                           > Stuff to be authenticated ----+
 * .            |                          |                                |
 * |            |                          |                                |
 * |            |                          |                                |
 * +------------+                          |                                |
 * |            |                          |                                |
 * | Fill Data  |                          |                                |
 * |            |                          |                                |
 * +------------+ Align to ALIGN_SIZE      |                                |
 * |    IVT     |                          |                                |
 * +------------+ + IVT_SIZE              -                                 |
 * |            |                                                           |
 * |  CSF DATA  | <---------------------------------------------------------+
 * |            |
 * +------------+
 * |            |
 * | Fill Data  |
 * |            |
 * +------------+ + CSF_PAD_SIZE
 */

__weak void __noreturn jump_to_image_no_args(struct spl_image_info *spl_image)
{
	typedef void __noreturn (*image_entry_noargs_t)(void);
	uint32_t offset;

	image_entry_noargs_t image_entry =
		(image_entry_noargs_t)(unsigned long)spl_image->entry_point;

	debug("image entry point: 0x%lX\n", spl_image->entry_point);

	if (spl_image->flags & SPL_FIT_FOUND) {
		image_entry();
	} else {
		/*
		 * HAB looks for the CSF at the end of the authenticated
		 * data therefore, we need to subtract the size of the
		 * CSF from the actual filesize
		 */
		offset = spl_image->size - CONFIG_CSF_SIZE;
		if (!imx_hab_authenticate_image(spl_image->load_addr,
						offset + IVT_SIZE +
						CSF_PAD_SIZE, offset)) {
			image_entry();
		} else {
			panic("spl: ERROR:  image authentication fail\n");
		}
	}
}

#if !defined(CONFIG_SPL_FIT_SIGNATURE)
ulong board_spl_fit_size_align(ulong size)
{
	/*
	 * HAB authenticate_image requests the IVT offset is
	 * aligned to 0x1000
	 */

	size = ALIGN(size, 0x1000);
	size += CONFIG_CSF_SIZE;

	return size;
}

#endif
#endif

void *board_spl_fit_buffer_addr(ulong fit_size, int sectors, int bl_len)
{
	int align_len = ARCH_DMA_MINALIGN - 1;
	uintptr_t base_addr;

	/* Some devices like SDP, NOR, NAND, SPI are using bl_len =1, so their fit address
	 * is different with SD/MMC, this cause mismatch with signed address. Thus, adjust
	 * the bl_len to align with SD/MMC.
	 */
	if (bl_len < 512)
		bl_len = 512;

	if (is_imx8qm() || is_imx8qxp() || is_imx8dxl())
		base_addr = 0x83000000;
	else
		base_addr = CONFIG_SYS_TEXT_BASE;

	return (void *)((base_addr - fit_size - bl_len -
			align_len) & ~align_len);
}

#if defined(CONFIG_MX6) && (defined(CONFIG_SPL_OS_BOOT) || \
		defined(CONFIG_SPL_ATF) || defined(CONFIG_SPL_OPTEE))
int dram_init_banksize(void)
{
	gd->bd->bi_dram[0].start = CONFIG_SYS_SDRAM_BASE;
	gd->bd->bi_dram[0].size = imx_ddr_size();

	return 0;
}
#endif

/*
 * read the address where the IVT header must sit
 * from IVT image header, loaded from SPL into
 * an malloced buffer and copy the IVT header
 * to this address
 */
void *spl_load_simple_fit_fix_load(const void *fit)
{
	struct ivt *ivt;
	unsigned long new;
	unsigned long offset;
	unsigned long size;
	u8 *tmp = (u8 *)fit;

	offset = ALIGN(fdt_totalsize(fit), 0x1000);
	size = ALIGN(fdt_totalsize(fit), 4);
	size = board_spl_fit_size_align(size);
	tmp += offset;
	ivt = (struct ivt *)tmp;
	if (ivt->hdr.magic != IVT_HEADER_MAGIC) {
		debug("no IVT header found\n");
		return (void *)fit;
	}
	debug("%s: ivt: %p offset: %lx size: %lx\n", __func__, ivt, offset, size);
	debug("%s: ivt self: %x\n", __func__, ivt->self);
	new = ivt->self;
	new -= offset;
	debug("%s: new %lx\n", __func__, new);
	memcpy((void *)new, fit, size);

	return (void *)new;
}

#if defined(CONFIG_IMX8MP) || defined(CONFIG_IMX8MN)
int board_handle_rdc_config(void *fdt_addr, const char *config_name, void *dst_addr)
{
	int node = -1, size = 0, ret = 0;
	uint32_t *data = NULL;
	const struct fdt_property *prop;

	node = fdt_node_offset_by_compatible(fdt_addr, -1, "imx8m,mcu_rdc");
	if (node < 0) {
		printf("Failed to find node!, err: %d!\n", node);
		ret = -1;
		goto exit;
	}

	/*
	 * Before MCU core starts we should set the rdc config for it,
	 * then restore the rdc config after it stops.
	 */
	prop = fdt_getprop(fdt_addr, node, config_name, &size);
	if (!prop) {
		printf("Failed to find property %s!\n", config_name);
		ret = -1;
		goto exit;
	}
	if (!size || size % (5 * sizeof(uint32_t))) {
		printf("Config size is wrong! size:%d\n", size);
		ret = -1;
		goto exit;
	}
	data = malloc(size);
	if (fdtdec_get_int_array(fdt_addr, node, config_name,
				 data, size/sizeof(int))) {
		printf("Failed to parse rdc config!\n");
		ret = -1;
		goto exit;
	} else {
		/* copy the rdc config */
		memcpy(dst_addr, data, size);
		ret = 0;
	}

exit:
	if (data)
		free(data);

	/* Invalidate the buffer if no valid config found. */
	if (ret < 0)
		memset(dst_addr, 0, sizeof(uint32_t));

	return ret;
}
#endif

void board_spl_fit_post_load(const void *fit, struct spl_image_info *spl_image)
{
	if (CONFIG_IS_ENABLED(IMX_HAB) && !(spl_image->flags & SPL_FIT_BYPASS_POST_LOAD)) {
		u32 offset = ALIGN(fdt_totalsize(fit), 0x1000);

		if (imx_hab_authenticate_image((uintptr_t)fit,
					       offset + IVT_SIZE + CSF_PAD_SIZE,
					       offset)) {
			panic("spl: ERROR:  image authentication unsuccessful\n");
		}
	}
#if defined(CONFIG_IMX8MP) || defined(CONFIG_IMX8MN)
#define MCU_RDC_MAGIC "mcu_rdc"
	if (!(spl_image->flags & SPL_FIT_BYPASS_POST_LOAD)) {
		memcpy((void *)CONFIG_IMX8M_MCU_RDC_START_CONFIG_ADDR, MCU_RDC_MAGIC, ALIGN(strlen(MCU_RDC_MAGIC), 4));
		memcpy((void *)CONFIG_IMX8M_MCU_RDC_STOP_CONFIG_ADDR, MCU_RDC_MAGIC, ALIGN(strlen(MCU_RDC_MAGIC), 4));
		board_handle_rdc_config(spl_image->fdt_addr, "start-config",
					(void *)(CONFIG_IMX8M_MCU_RDC_START_CONFIG_ADDR + ALIGN(strlen(MCU_RDC_MAGIC), 4)));
		board_handle_rdc_config(spl_image->fdt_addr, "stop-config",
					(void *)(CONFIG_IMX8M_MCU_RDC_STOP_CONFIG_ADDR + ALIGN(strlen(MCU_RDC_MAGIC), 4)));
	}
#endif
}

#ifdef CONFIG_IMX_TRUSTY_OS
int check_rollback_index(struct spl_image_info *spl_image, struct mmc *mmc);
int check_rpmb_blob(struct mmc *mmc);

int mmc_image_load_late(struct spl_image_info *spl_image, struct mmc *mmc)
{
	/* Check the rollback index of next stage image */
	if (check_rollback_index(spl_image, mmc) < 0)
		return -1;

	/* Check the rpmb key blob for trusty enabled platfrom. */
	return check_rpmb_blob(mmc);
}
#endif

#if defined(CONFIG_SECONDARY_BOOT_RUNTIME_DETECTION) && \
    defined(CONFIG_SYS_MMCSD_RAW_MODE_U_BOOT_USE_SECTOR) && \
    !defined(CONFIG_SPL_LOAD_IMX_CONTAINER)
unsigned long spl_mmc_get_uboot_raw_sector(struct mmc *mmc,
					   unsigned long raw_sect)
{
	int boot_secondary = boot_mode_getprisec();
	unsigned long offset = CONFIG_SYS_MMCSD_RAW_MODE_U_BOOT_SECTOR;
#if defined(CONFIG_iMX8MN)
	#define UBOOT_MMC2_RAW_SECTOR_OFFSET 0x40
	if (spl_boot_device() == BOOT_DEVICE_MMC2)
			offset -= UBOOT_MMC2_RAW_SECTOR_OFFSET;
#endif
	if (boot_secondary) {
		offset += CONFIG_SECONDARY_BOOT_SECTOR_OFFSET;
		printf("SPL: Booting secondary boot path: using 0x%lx offset "
		       "for next boot image\n", offset);
	} else {
		printf("SPL: Booting primary boot path: using 0x%lx offset "
		       "for next boot image\n", offset);
	}

	return offset;
}

#if defined(CONFIG_IMX8QM) || defined(CONFIG_IMX8QXP) || defined(CONFIG_IMX8ULP)
int spl_mmc_emmc_boot_partition(struct mmc *mmc)
{
	return boot_mode_getprisec() ? 2 : 1;
}
#endif
#endif
