// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2019 Foundries.IO
 */

#if (defined(CONFIG_MX6Q) || defined(CONFIG_MX6DL) || defined(CONFIG_MX6QDL))
#define CONFIG_CAAM_IGNORE_KNOWN_HAB_EVENTS 1
#endif

#include <common.h>
#include <config.h>
#include <fuse.h>
#include <mapmem.h>
#include <image.h>
#include <asm/io.h>
#include <asm/system.h>
#include <asm/arch/clock.h>
#include <asm/arch/sys_proto.h>
#include <asm/mach-imx/hab.h>
#include <asm/mach-imx/sys_proto.h>

#if defined(CONFIG_AHAB_BOOT)
#include <asm/arch/sci/sci.h>
#endif

#ifdef CONFIG_MX7ULP
#define SRK_FUSE_LIST								\
{ 5, 0 }, { 5, 1 }, { 5, 2}, { 5, 3 }, { 5, 4 }, { 5, 5}, { 5, 6 }, { 5 ,7 },	\
{ 6, 0 }, { 6, 1 }, { 6, 2}, { 6, 3 }, { 6, 4 }, { 6, 5}, { 6, 6 }, { 6 ,7 },
#define SECURE_FUSE_BANK	(29)
#define SECURE_FUSE_WORD	(6)
#define SECURE_FUSE_VALUE	(0x80000000)
#elif CONFIG_ARCH_MX6
#define SRK_FUSE_LIST								\
{ 3, 0 }, { 3, 1 }, { 3, 2}, { 3, 3 }, { 3, 4 }, { 3, 5}, { 3, 6 }, { 3 ,7 },
#define SECURE_FUSE_BANK	(0)
#define SECURE_FUSE_WORD	(6)
#define SECURE_FUSE_VALUE	(0x00000002)
#elif CONFIG_IMX8M
#define SRK_FUSE_LIST								\
{ 6, 0 }, { 6, 1 }, { 6, 2 }, { 6, 3 }, { 7, 0 }, { 7, 1 }, { 7, 2 }, { 7 , 3 },
#define SECURE_FUSE_BANK	(1)
#define SECURE_FUSE_WORD	(3)
#define SECURE_FUSE_VALUE	(0x2000000)
#elif CONFIG_IMX8QM
#define SRK_FUSE_LIST								\
{ 0, 722 }, { 0, 723 }, { 0, 724 }, { 0, 725 }, { 0, 726 }, { 0, 727 }, \
{ 0, 728 }, { 0, 729 }, { 0, 730 }, { 0, 731 }, { 0, 732 }, { 0, 733 }, \
{ 0, 734 }, { 0, 735 }, { 0, 736 }, { 0, 737 },
#else
#error "SoC not supported"
#endif

#if defined(CONFIG_AHAB_BOOT)
static int hab_status(void)
{
	int err;
	u8 idx = 0U;
	u32 event;
	u16 lc;

	err = sc_seco_chip_info(-1, &lc, NULL, NULL, NULL);
	if (err != SC_ERR_NONE) {
		printf("Error in get lifecycle\n");
		return -EIO;
	}

	err = sc_seco_get_event(-1, idx, &event);
	while (err == SC_ERR_NONE) {
		idx++;
		err = sc_seco_get_event(-1, idx, &event);
	}
	/* No events */
	if (idx == 0)
		return 0;

	return 1;
}

#else /* defined(CONFIG_AHAB_BOOT) */
#ifdef CONFIG_CAAM_IGNORE_KNOWN_HAB_EVENTS
#define RNG_FAIL_EVENT_SIZE 36
/* Known HAB event from imx6d where RNG selftest failed due to ROM issue */
static uint8_t habv4_known_rng_fail_events[][RNG_FAIL_EVENT_SIZE] = {
	{ 0xdb, 0x00, 0x24, 0x42,  0x69, 0x30, 0xe1, 0x1d,
	  0x00, 0x04, 0x00, 0x02,  0x40, 0x00, 0x36, 0x06,
	  0x55, 0x55, 0x00, 0x03,  0x00, 0x00, 0x00, 0x00,
	  0x00, 0x00, 0x00, 0x00,  0x00, 0x00, 0x00, 0x00,
	  0x00, 0x00, 0x00, 0x01 },
};

static bool is_known_rng_fail_event(const uint8_t *data, size_t len)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(habv4_known_rng_fail_events); i++) {
		if (memcmp(data, habv4_known_rng_fail_events[i],
			   min_t(size_t, len, RNG_FAIL_EVENT_SIZE)) == 0) {
			return true;
		}
	}

	return false;
}
#endif

static hab_rvt_report_status_t *hab_check;

static int hab_status(void)
{
	hab_check = (hab_rvt_report_status_t *) HAB_RVT_REPORT_STATUS;
	enum hab_config config = 0;
	enum hab_state state = 0;

	if (hab_check(&config, &state) != HAB_SUCCESS) {
/* check to see if the only events are known failures */
#ifdef CONFIG_CAAM_IGNORE_KNOWN_HAB_EVENTS
		hab_rvt_report_event_t *hab_event_f;
		hab_event_f =  (hab_rvt_report_event_t *)HAB_RVT_REPORT_EVENT;
		uint32_t index = 0; /* Loop index */
		uint8_t event_data[128]; /* Event data buffer */
		size_t bytes = sizeof(event_data); /* Event size in bytes */

		/* Check for known failure to ingore */
		while (hab_event_f(HAB_STS_ANY, index++, event_data, &bytes) ==
		       HAB_SUCCESS) {
			if (!is_known_rng_fail_event(event_data, bytes)) {
				printf("HAB events active error\n");
				printf("Make sure the SPL is correctly signed and the board is fused\n");
				return 1;
			}
		}

		printf("Ignoring known HAB failures, no other events found.\n");
		return 0;
#else
		printf("HAB events active error\n");
		printf("Make sure the SPL is correctly signed and the board is fused\n");
		return 1;
#endif
	}

	return 0;
}
#endif /* if defined(CONFIG_IMX8QM) */

/* The fuses must have been programmed and their values set in the environment.
 * The fuse read operation returns a shadow value so a board reset is required
 * after the SRK fuses have been written.
 */
static int do_fiohab_close(struct cmd_tbl *cmdtp, int flag, int argc,
			   char *const argv[])
{
	struct srk_fuse {
		u32 bank;
		u32 word;
	} const srk_fuses[] = { SRK_FUSE_LIST };
	char fuse_name[20] = { '\0' };
	uint32_t fuse, fuse_env;
	int i, ret;

	if (argc != 1) {
		cmd_usage(cmdtp);
		return 1;
	}

	/* if secure boot is already enabled, there is nothing to do */
	if (boot_mode_is_closed()) {
		printf("secure boot already enabled\n");
		return 0;
	}

	/* if there are pending HAB errors, we cant close the board */
	if (hab_status())
		return 1;

	for (i = 0; i < ARRAY_SIZE(srk_fuses); i++) {
		ret = fuse_read(srk_fuses[i].bank, srk_fuses[i].word, &fuse);
		if (ret) {
			printf("Secure boot fuse read error\n");
			return 1;
		}

		/**
		 * if the fuses are not in in the environemnt or hold the wrong
		 * values, then we cant close the board
		 */
		sprintf(fuse_name, "srk_%d", i);
		fuse_env = (uint32_t) env_get_hex(fuse_name, 0);
		if (!fuse_env) {
			printf("%s not in environment\n", fuse_name);
			return 1;
		}

		if (fuse_env != fuse) {
			printf("%s - programmed: 0x%x != expected: 0x%x \n",
				fuse_name, fuse, fuse_env);
			return 1;
		}
	}
#if defined(CONFIG_IMX8QM)
		ret = ahab_close();
#else
		ret = fuse_prog(SECURE_FUSE_BANK, SECURE_FUSE_WORD, SECURE_FUSE_VALUE);
#endif
	if (ret) {
		printf("Error closing device");
		return 1;
	}

	return 0;
}

U_BOOT_CMD(fiohab_close, CONFIG_SYS_MAXARGS, 1, do_fiohab_close,
	   "Close the board for HABv4/AHAB","");

