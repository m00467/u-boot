// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (c) 2015 National Instruments
 */

#include <dm.h>
#include <serial.h>

static int nulldev_serial_setbrg(struct udevice *dev, int baudrate)
{
	return 0;
}

static int nulldev_serial_getc(struct udevice *dev)
{
	return -EAGAIN;
}

static int nulldev_serial_pending(struct udevice *dev, bool input)
{
	return 0;
}

static int nulldev_serial_putc(struct udevice *dev, const char ch)
{
	return 0;
}

static void null_setbrg(void){}
static int null_init(void){return 0;}
static struct serial_device nulldev_serial_device = {
	.name = "nulldev",
	.start = null_init,
	.stop = NULL,
	.setbrg = null_setbrg
};
__weak struct serial_device *default_serial_console(void)
{
	return &nulldev_serial_device;
}

static const struct udevice_id nulldev_serial_ids[] = {
	{ .compatible = "nulldev-serial" },
	{ }
};

const struct dm_serial_ops nulldev_serial_ops = {
	.putc = nulldev_serial_putc,
	.pending = nulldev_serial_pending,
	.getc = nulldev_serial_getc,
	.setbrg = nulldev_serial_setbrg,
};

U_BOOT_DRIVER(serial_nulldev) = {
	.name	= "serial_nulldev",
	.id	= UCLASS_SERIAL,
	.of_match = nulldev_serial_ids,
	.ops	= &nulldev_serial_ops,
};
