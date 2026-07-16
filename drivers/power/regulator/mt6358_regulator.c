// SPDX-License-Identifier: GPL-2.0-only
/*
 * MT6358 regulator driver
 *
 * Copyright (c) 2026 BayLibre, SAS.
 * Author: Julien Masson <jmasson@baylibre.com>
 */

#include <dm.h>
#include <power/regulator.h>
#include <power/mt6358.h>
#include <power/pmic.h>

enum mt6358_regulator_type {
	MT6358_REG_TYPE_RANGE,
	MT6358_REG_TYPE_TABLE,
	MT6358_REG_TYPE_FIXED,
};

struct mt6358_regulator_desc {
	const char *name;
	const char *of_match;
	enum mt6358_regulator_type type;
	int id;
	unsigned int n_voltages;
	const unsigned int *volt_table;
	const unsigned int *index_table;
	unsigned int n_table;
	unsigned int vsel_reg;
	unsigned int vsel_mask;
	unsigned int vsel_shift;
	unsigned int enable_reg;
	unsigned int enable_mask;
};

struct mt6358_regulator_info {
	struct mt6358_regulator_desc desc;
};

#define MT6358_LDO(match, vreg, ldo_volt_table, ldo_index_table,	\
		   enreg, vosel, vosel_mask, vosel_shift)		\
	[MT6358_ID_##vreg] = {						\
		.desc = {						\
			.name = #vreg,					\
			.of_match = of_match_ptr(match),		\
			.type = MT6358_REG_TYPE_TABLE,			\
			.id = MT6358_ID_##vreg,				\
			.n_voltages = ARRAY_SIZE(ldo_volt_table),	\
			.volt_table = ldo_volt_table,			\
			.index_table = ldo_index_table,			\
			.n_table = ARRAY_SIZE(ldo_index_table),		\
			.vsel_reg = vosel,				\
			.vsel_mask = vosel_mask,			\
			.vsel_shift = vosel_shift,			\
			.enable_reg = enreg,				\
			.enable_mask = BIT(0),				\
		},							\
	}

static const unsigned int vmch_voltages[] = {
	2900000,
	3000000,
	3300000,
};

static const unsigned int vmch_idx[] = {
	2, 3, 5,
};

static const unsigned int vmc_voltages[] = {
	1800000,
	2900000,
	3000000,
	3300000,
};

static const unsigned int vmc_idx[] = {
	4, 10, 11, 13,
};

static struct mt6358_regulator_info mt6358_regulators[] = {
	MT6358_LDO("ldo_vmch", VMCH, vmch_voltages, vmch_idx,
		   MT6358_LDO_VMCH_CON0, MT6358_VMCH_ANA_CON0, 0x700, 8),
	MT6358_LDO("ldo_vmc", VMC, vmc_voltages, vmc_idx,
		   MT6358_LDO_VMC_CON0, MT6358_VMC_ANA_CON0, 0xf00, 8),
};

static int mt6358_get_enable(struct udevice *dev)
{
	struct mt6358_regulator_info *info = dev_get_priv(dev);
	int ret;

	ret = pmic_reg_read(dev->parent, info->desc.enable_reg);
	if (ret < 0)
		return ret;

	return ret & info->desc.enable_mask ? true : false;
}

static int mt6358_set_enable(struct udevice *dev, bool enable)
{
	struct mt6358_regulator_info *info = dev_get_priv(dev);
	return pmic_clrsetbits(dev->parent, info->desc.enable_reg,
			       info->desc.enable_mask,
			       enable ? info->desc.enable_mask : 0);
}

static int mt6358_get_value(struct udevice *dev)
{
	struct mt6358_regulator_info *info = dev_get_priv(dev);
	int selector, idx, ret;

	ret = pmic_reg_read(dev->parent, info->desc.vsel_reg);
	if (ret < 0)
		return ret;

	selector = (ret & info->desc.vsel_mask) >> info->desc.vsel_shift;
	for (idx = 0; idx < info->desc.n_voltages; idx++) {
		if (info->desc.index_table[idx] == selector)
			return info->desc.volt_table[idx];
	}

	return -EINVAL;
}

static int mt6358_set_value(struct udevice *dev, int uvolt)
{
	struct mt6358_regulator_info *info = dev_get_priv(dev);
	int idx;

	for (idx = 0; idx < info->desc.n_voltages; idx++) {
		if (info->desc.volt_table[idx] == uvolt) {
			return pmic_clrsetbits(dev->parent,
					       info->desc.vsel_reg,
					       info->desc.vsel_mask,
					       info->desc.index_table[idx]
					         << info->desc.vsel_shift);
		}
	}

	return -EINVAL;
}

static int mt6358_regulator_probe(struct udevice *dev)
{
	struct mt6358_regulator_info *priv = dev_get_priv(dev);
	int i;

	for (i = 0; i < ARRAY_SIZE(mt6358_regulators); i++) {
		if (!mt6358_regulators[i].desc.of_match)
			continue;

		if (!strcmp(dev->name, mt6358_regulators[i].desc.of_match)) {
			*priv = mt6358_regulators[i];
			return 0;
		}
	}

	return -ENOENT;
}

static const struct dm_regulator_ops mt6358_regulator_ops = {
	.get_value = mt6358_get_value,
	.set_value = mt6358_set_value,
	.get_enable = mt6358_get_enable,
	.set_enable = mt6358_set_enable,
};

U_BOOT_DRIVER(mt6358_regulator) = {
	.name = MT6358_REGULATOR_DRIVER,
	.id = UCLASS_REGULATOR,
	.ops = &mt6358_regulator_ops,
	.probe = mt6358_regulator_probe,
	.priv_auto = sizeof(struct mt6358_regulator_info),
};