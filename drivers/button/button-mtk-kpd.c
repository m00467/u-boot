#include <button.h>
#include <dm.h>
#include <dm/lists.h>
#include <dm/read.h>
#include <dm/device-internal.h>
#include <asm/io.h>
#include <power/pmic.h>
#include <dt-bindings/input/linux-event-codes.h>
#include <power/mt6358.h>

struct mtk_kpd_priv {
	void __iomem *kpd_base;
	struct udevice *pmic;
	u32 bit;
	int code;
	bool button_is_pmic;
};

static enum button_state_t mtk_kpd_get_state(struct udevice *dev)
{
	struct mtk_kpd_priv *priv = dev_get_priv(dev);
	if (priv->button_is_pmic) {
		int val = pmic_reg_read(priv->pmic, MT6358_TOPSTATUS);
		return (!(val & BIT(priv->bit))) ? BUTTON_ON : BUTTON_OFF;
	}
	/* the key is kpd: */
	return (!!(readl(priv->kpd_base) & BIT(priv->bit))) ? BUTTON_ON : BUTTON_OFF;
}

static int mtk_kpd_get_code(struct udevice *dev)
{
	struct mtk_kpd_priv *priv = dev_get_priv(dev);
	return priv->code;
}

static int mtk_kpd_probe(struct udevice *dev)
{
	struct mtk_kpd_priv *priv = dev_get_priv(dev);
	struct button_uc_plat *uc_plat = dev_get_uclass_plat(dev);
	ofnode node = dev_ofnode(dev);

	if (strcmp(dev->name, "power") != 0 &&
	    strcmp(dev->name, "vol_up") != 0 &&
	    strcmp(dev->name, "vol_down") != 0)
		return 0;

	if (uc_plat &&
	    (!strcmp(dev->name, "power") ||
	     !strcmp(dev->name, "vol_up") ||
	     !strcmp(dev->name, "vol_down")))
		uc_plat->label = dev->name;

	priv->kpd_base = (void __iomem *)dev_read_addr(dev->parent);
	/* CONFIG_BUTTON_REMAP_KEYS */
	if (strcmp(dev->name, "power") == 0) {
		priv->code = KEY_POWER;
		priv->button_is_pmic = true;
		priv->bit = 1; /* PWRKEY_DEB */
	} else if (strcmp(dev->name, "vol_up") == 0) {
		priv->code = KEY_VOLUMEUP;
		if (ofnode_read_bool(node, "mediatek,have-pmic-up")) {
			priv->button_is_pmic = true;
			priv->bit = 3; /* HOMEKEY_DEB */
		} else {
			ofnode_read_u32(node, "mediatek,kpd-up-bit", &priv->bit);
		}
	} else if (strcmp(dev->name, "vol_down") == 0) {
		priv->code = KEY_VOLUMEDOWN;
		ofnode_read_u32(node, "mediatek,kpd-down-bit", &priv->bit);
	}

	if (priv->button_is_pmic)
		uclass_get_device(UCLASS_PMIC, 0, &priv->pmic);

	return 0;
}

static bool mtk_kpd_is_button(struct udevice *dev)
{
	return !strcmp(dev->name, "power") || !strcmp(dev->name, "vol_up") || !strcmp(dev->name, "vol_down");
}

static int mtk_kpd_bind(struct udevice *parent)
{
	ofnode node = dev_ofnode(parent);
	struct udevice *dev;

	if (mtk_kpd_is_button(parent))
		return 0;

	/* Assume every device has pmic pwrkey. */
	device_bind_driver_to_node(parent, "mtk_kpd", "power", node, &dev);
	if (ofnode_read_bool(node, "mediatek,have-kpd-up") || ofnode_read_bool(node, "mediatek,have-pmic-up"))
		device_bind_driver_to_node(parent, "mtk_kpd", "vol_up", node, &dev);
	if (ofnode_read_bool(node, "mediatek,have-kpd-down"))
		device_bind_driver_to_node(parent, "mtk_kpd", "vol_down", node, &dev);

	return 0;
}

static int mtk_kpd_of_to_plat(struct udevice *dev)
{
	struct button_uc_plat *uc_plat = dev_get_uclass_plat(dev);

	if (!uc_plat)
		return 0;
	if (!strcmp(dev->name, "power") ||
	    !strcmp(dev->name, "vol_up") ||
	    !strcmp(dev->name, "vol_down"))
		uc_plat->label = dev->name;

	return 0;
}

static const struct button_ops mtk_kpd_ops = {
	.get_state = mtk_kpd_get_state,
	.get_code = mtk_kpd_get_code,
};

static const struct udevice_id mtk_kpd_ids[] = {
	{ .compatible = "mediatek,mt6771-kpd" },
	{ /* sentinel */ },
};

U_BOOT_DRIVER(mtk_kpd) = {
	.name = "mtk_kpd",
	.id = UCLASS_BUTTON,
	.of_match = mtk_kpd_ids,
	.bind = mtk_kpd_bind,
	.probe = mtk_kpd_probe,
	.of_to_plat = mtk_kpd_of_to_plat,
	.ops = &mtk_kpd_ops,
	.priv_auto = sizeof(struct mtk_kpd_priv),
};
