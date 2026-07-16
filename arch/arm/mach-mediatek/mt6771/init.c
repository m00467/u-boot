/*
 * bootrom usbdl reboot code source:
 * https://github.com/R0rt1z2/kaeru/blob/main/drivers/usbdl/mtk_usbdl.c
 */
#include <config.h>
#include <asm/global_data.h>
#include <asm/armv8/mmu.h>
#include <init.h>
#include <linux/delay.h>
#include <linux/bitops.h>
#include <linux/sizes.h>
#include <linux/libfdt.h>
#include <fdtdec.h>
#include <fdt_support.h>
#include <vsprintf.h>
#include <asm/io.h>
#include <dm.h>
#include <env.h>
#include <command.h>
#include <power/pmic.h>
#include <power/mt6358.h>
#include <fastboot.h>

DECLARE_GLOBAL_DATA_PTR;

#define SECURITY_AO_BASE 	0x1001a000
#define USBDL_FLAG			0x0080
#define MISC_LOCK_KEY 		0x0100
#define RST_CON 			SECURITY_AO_BASE + 0x0108
#define MISC_LOCK_KEY_MAGIC     0xad98
#define USBDL_BIT_EN            BIT(0)
#define USBDL_BROM              BIT(1)
#define USBDL_TIMEOUT_MASK      GENMASK(15, 2)
#define USBDL_TIMEOUT_MAX       (USBDL_TIMEOUT_MASK >> 2)
#define USBDL_MAGIC             0x444c0000

#define TOPRGU_BASE 	        0x10007000
#define WDT_MODE 		        0x0
#define WDT_RESTART 	        0x8
#define WDT_SWRST 		        0x14
#define WDT_MODE_KEY            0x22000000
#define WDT_MODE_ENABLE   	 	BIT(0)
#define WDT_MODE_EXT_POL    	BIT(1)
#define WDT_MODE_EXTEN      	BIT(2)
#define WDT_MODE_IRQ          	BIT(3)
#define WDT_MODE_AUTO_RESTART 	BIT(4)
#define WDT_MODE_DUAL_MODE  	BIT(6)
#define WDT_MODE_DDR_RESERVE 	BIT(7)
#define WDT_LENGTH_KEY   		0x08
#define WDT_RESTART_KEY  		0x1971
#define WDT_SWRST_KEY   		0x1209

static void mtk_rtc_write_trigger(void)
{
	struct udevice *pmic;
	uclass_get_device(UCLASS_PMIC, 0, &pmic);
	pmic_reg_write(pmic, MT6358_RTC_WRTGR, 1);
	while (pmic_reg_read(pmic, MT6358_RTC_BBPU) & MT6358_RTC_BBPU_CBUSY)
		udelay(10);
}

static void mtk_rtc_write_unlock(void)
{
	struct udevice *pmic;
	uclass_get_device(UCLASS_PMIC, 0, &pmic);
	pmic_reg_write(pmic, MT6358_RTC_PROT, MT6358_RTC_PROT_UNLOCK1);
	mtk_rtc_write_trigger();
	pmic_reg_write(pmic, MT6358_RTC_PROT, MT6358_RTC_PROT_UNLOCK2);
	mtk_rtc_write_trigger();
}

static const char *get_cmdline(void)
{
	const void *fdt_blob = (const void *)get_prev_bl_fdt_addr();
	int node;

	if (!fdt_blob)
		return NULL;
	if (fdt_check_header(fdt_blob))
		return NULL;

	node = fdt_path_offset(fdt_blob, "/chosen");
	return fdt_getprop(fdt_blob, node, "bootargs", NULL);
}

static int get_cmdline_option(const char *cmdline, const char *key, char *out, int out_len)
{
	const char *p, *p_end;
	int len;

	p = strstr(cmdline, key);
	if (!p)
		return -ENOENT;

	p += strlen(key);
	p_end = strstr(p, " ");
	if (!p_end)
		return -ENOENT;

	len = p_end - p;
	if (len > out_len)
		len = out_len;

	strncpy(out, p, len);
	out[len] = '\0';
	return 0;
}

int dram_init(void)
{
	return fdtdec_setup_mem_size_base();
}

ulong get_page_table_size(void)
{
	return 64 * 1024;
}

int dram_init_banksize(void)
{
	return fdtdec_setup_memory_banksize();
}

// 0x54400000 0x20000
int board_late_init(void)
{
	char serial[64];
	const char *cmdline = get_cmdline();
	struct udevice *pmic;
	int ret;

	get_cmdline_option(cmdline, "androidboot.serialno=", serial, sizeof(serial));
	env_set("serial#", serial);

	ret = uclass_get_device(UCLASS_PMIC, 0, &pmic);
	if (ret) {
		printf("%s: failed to get mt6358 pmic dev: %d\n", __func__, ret);
		return ret;
	}

	env_set("board", CONFIG_SYS_BOARD);
	writel(0x22000000, TOPRGU_BASE + WDT_MODE);
	pmic_clrsetbits(pmic, MT6358_TOP_RST_MISC, GENMASK(13, 12) | BIT(9), (3 << 12) | BIT(9));
	pmic_clrsetbits(pmic, MT6358_VIBR_ANA_CON0, GENMASK(11, 8), 0);
	return 0;
}

#if 0
int fdt_copy_resv_mem_node(const void *src, void *dst)
{
	u32 phandle;
	struct fdt_memory pmp_mem;
	fdt_addr_t addr, size;
	char basename[32] = {0};
	int offset, node, err, rmem_offset, bname_len, max_len = sizeof(basename);
	const char *name;
	char *temp;

	offset = fdt_path_offset(src, "/reserved-memory");
	if (offset < 0) {
		log_debug("No reserved memory region found in source FDT\n");
		return 0;
	}

	/*
	 * Extend the FDT by the following estimated size:
	 *
	 * Each PMP memory region entry occupies 64 bytes.
	 * With 16 PMP memory regions we need 64 * 16 = 1024 bytes.
	 */
	fdt_for_each_subnode(node, src, offset) {
		name = fdt_get_name(src, node, NULL);

		/* skip copying useless stuff */
		if (name && (strstr(name, "log") ||
			     strstr(name, "minirdump") ||
			     strstr(name, "pstore") ||
			     strstr(name, "console"))) {
			printf("skipping resv node %s\n", name);
			continue;
		}

		/* TODO: nodes that get skipped here MAY BE important !!!!!!!!!! */
		addr = fdtdec_get_addr_size_auto_parent(src, offset, node, "reg", 0, &size, false);
		if (addr == FDT_ADDR_T_NONE) {
			printf("bad node %s\n", name);
			continue;
		}

		strncpy(basename, name, max_len);
		temp = strchr(basename, '@');
		if (temp) {
			bname_len = strnlen(basename, max_len) - strnlen(temp, max_len);
			*(basename + bname_len) = '\0';
		}
		pmp_mem.start = addr;
		pmp_mem.end = addr + size - 1;
		err = fdtdec_add_reserved_memory(dst, basename, &pmp_mem,
						 NULL, 0, &phandle, 0);
		if (err < 0 && err != -FDT_ERR_EXISTS) {
			log_err("failed to add reserved memory: %d\n", err);
			return err;
		}
		if (fdt_getprop(src, node, "no-map", NULL)) {
			rmem_offset = fdt_node_offset_by_phandle(dst, phandle);
			fdt_setprop_empty(dst, rmem_offset, "no-map");
		}
	}

	return 0;
}

int ft_board_setup(void *blob, struct bd_info *bd)
{
	const void *lk_fdt = (const void *)get_prev_bl_fdt_addr();
	int lk_node, dst_node, len;
	fdt_addr_t addr, size;
	const void *seed;

	if (!lk_fdt || fdt_check_header(lk_fdt)) {
		panic("lk fdt is invalid!\n");
		return -EINVAL;
	}

	fdt_set_totalsize(blob, fdt_totalsize(blob) + 4096);
	lk_node = fdt_path_offset(lk_fdt, "/memory");
	if (lk_node > 0) {
		addr = fdtdec_get_addr_size_auto_noparent(lk_fdt, lk_node, "reg", 0, &size, true);
		if (addr == FDT_ADDR_T_NONE)
			panic("failed to setup /memory address + size");

		fdt_fixup_memory(blob, addr, size);
	}

	/* Add kaslr entropy */
	lk_node = fdt_path_offset(lk_fdt, "/chosen");
	if (lk_node > 0) {
		seed = fdt_getprop(lk_fdt, lk_node, "kaslr-seed", &len);
		if (seed && len > 0) {
			dst_node = fdt_path_offset(blob, "/chosen");
			if (dst_node < 0)
				dst_node = fdt_add_subnode(blob, 0, "chosen");

			if (dst_node >= 0)
				fdt_setprop(blob, dst_node, "kaslr-seed", seed, len);
			else
				printf("failed to add kaslr-seed\n");
		}
	}

	return fdt_copy_resv_mem_node(lk_fdt, blob);
}
#endif

void reset_cpu(void)
{
	u32 val;

	/* reset, bypass pwrkey */
	writel(WDT_RESTART_KEY, TOPRGU_BASE + WDT_RESTART);
	val = readl(TOPRGU_BASE + WDT_MODE);
	val &= ~(WDT_MODE_IRQ);
	val |= WDT_MODE_KEY | WDT_MODE_ENABLE | WDT_MODE_EXTEN | WDT_MODE_AUTO_RESTART;
	writel(val, TOPRGU_BASE + WDT_MODE);
	udelay(100);

	writel(WDT_SWRST_KEY, TOPRGU_BASE + WDT_SWRST);
	for (;;) {}
}

extern void g_dnl_trigger_detach(void);
int fastboot_set_reboot_flag(enum fastboot_reboot_reason reason)
{
	struct udevice *pmic;
	u16 pdn1;

	uclass_get_device(UCLASS_PMIC, 0, &pmic);
	if (reason == FASTBOOT_REBOOT_REASON_FASTBOOTD) {
		printf("reboot reason unsupported!\n");
		return -ENOTSUPP;
	}

	/* usb pulldown */
	g_dnl_trigger_detach();
	mdelay(800);

	/* TODO: recovery/fastboot*/
	mtk_rtc_write_unlock();
	pdn1 = pmic_reg_read(pmic, MT6358_RTC_PDN1);
	switch (reason) {
	case FASTBOOT_REBOOT_REASON_BOOTLOADER:
		printf("Rebooting the device to LK bootloader ... \n");
		pdn1 |= BIT(13); /* LK fast_boot bit */
		break;
	case FASTBOOT_REBOOT_REASON_RECOVERY:
		printf("Rebooting the device to LK recovery ... \n");
		pdn1 |= BIT(4); /* LK recovery bit */
		break;
	default:
		break;
	}
	pmic_reg_write(pmic, MT6358_RTC_PDN1, pdn1);
	mtk_rtc_write_trigger();

	reset_cpu();
	return 0;
}

int do_poweroff(struct cmd_tbl *cmdtp, int flag, int argc, char *const argv[])
{
	struct udevice *pmic;
	u32 reg;

	uclass_get_device(UCLASS_PMIC, 0, &pmic);

	printf("%s: waiting until usb plug-out !\n", __func__);
	while (1) {
		reg = pmic_reg_read(pmic, MT6358_CHR_CON0);
		if (!((reg >> 5) & 1))
			break;

		mdelay(40);
		printf(".");
	}
	printf("\n%s: usb unplugged - powering off ...\n", __func__);
	mdelay(500);

	mtk_rtc_write_unlock();
	/* lpsd */
	pmic_reg_write(pmic, MT6358_RTC_BBPU, MT6358_RTC_BBPU_KEY | MT6358_RTC_BBPU_CLR | MT6358_RTC_BBPU_PWREN);
	pmic_reg_write(pmic, MT6358_RTC_AL_MASK, MT6358_RTC_AL_MASK_DOW);
	mtk_rtc_write_trigger();
	mdelay(10);
	pmic_clrsetbits(pmic, MT6358_RTC_BBPU, MT6358_RTC_BBPU_KEY | MT6358_RTC_BBPU_RELOAD, MT6358_RTC_BBPU_KEY | MT6358_RTC_BBPU_RELOAD);
	mtk_rtc_write_trigger();
	mdelay(10);

	pmic_clrsetbits(pmic, MT6358_PPCCTL0, 1, 0);
	/* NOTREACHED - poweroff */
	return CMD_RET_SUCCESS;
}

int do_brom_rst(struct cmd_tbl *cmdtp, int flag, int argc, char *const argv[])
{
	u32 reg = USBDL_MAGIC | USBDL_BIT_EN | (USBDL_TIMEOUT_MASK & (USBDL_TIMEOUT_MAX << 2));

	writel(MISC_LOCK_KEY_MAGIC, SECURITY_AO_BASE + MISC_LOCK_KEY);
	setbits_le32(RST_CON, BIT(0));
	writel(0, SECURITY_AO_BASE + MISC_LOCK_KEY);
	writel(reg, SECURITY_AO_BASE + USBDL_FLAG);
	reset_cpu();
	return CMD_RET_SUCCESS;
}

U_BOOT_CMD(poweroff, 1, 0, do_poweroff, "", "");
U_BOOT_CMD(brom_rst, 1, 0, do_brom_rst, "", "");
