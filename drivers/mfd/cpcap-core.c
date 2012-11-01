/*
 * Copyright (C) 2007-2010 Motorola, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA
 * 02111-1307, USA
 */

#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/platform_device.h>
#include <linux/regulator/machine.h>
#include <linux/spi/spi.h>
#include <linux/spi/cpcap.h>
#include <linux/spi/cpcap-regbits.h>
#include <linux/uaccess.h>
#include <linux/reboot.h>
#include <linux/notifier.h>
#include <linux/delay.h>
#include <asm/bootinfo.h>
#ifdef CONFIG_POWEROFF_ALARM
#include <linux/of.h>
#endif

struct cpcap_driver_info {
	struct list_head list;
	struct platform_device *pdev;
};

static long ioctl(struct file *file, unsigned int cmd, unsigned long arg);
static int __devinit cpcap_probe(struct spi_device *spi);
static int __devexit cpcap_remove(struct spi_device *spi);

const static struct file_operations cpcap_fops = {
	.owner = THIS_MODULE,
	.unlocked_ioctl = ioctl,
};

static struct miscdevice cpcap_dev = {
	.minor	= MISC_DYNAMIC_MINOR,
	.name	= CPCAP_DEV_NAME,
	.fops	= &cpcap_fops,
};

static struct spi_driver cpcap_driver = {
	.driver = {
		   .name = "cpcap",
		   .bus = &spi_bus_type,
		   .owner = THIS_MODULE,
		   },
	.probe = cpcap_probe,
	.remove = __devexit_p(cpcap_remove),
};

static struct platform_device cpcap_adc_device = {
	.name           = "cpcap_adc",
	.id             = -1,
	.dev.platform_data = NULL,
};


static struct platform_device cpcap_key_device = {
	.name           = "cpcap_key",
	.id             = -1,
	.dev.platform_data = NULL,
};

static struct platform_device cpcap_batt_device = {
	.name           = "cpcap_battery",
	.id             = -1,
	.dev.platform_data = NULL,
};

static struct platform_device cpcap_uc_device = {
	.name           = "cpcap_uc",
	.id             = -1,
	.dev.platform_data = NULL,
};

static struct platform_device cpcap_rtc_device = {
	.name           = "cpcap_rtc",
	.id             = -1,
	.dev.platform_data = NULL,
};

/* List of required CPCAP devices that will ALWAYS be present.
 *
 * DO NOT ADD NEW DEVICES TO THIS LIST! You must use cpcap_driver_register()
 * for any new drivers for non-core functionality of CPCAP.
 */
static struct platform_device *cpcap_devices[] = {
	&cpcap_uc_device,
	&cpcap_adc_device,
	&cpcap_key_device,
	&cpcap_batt_device,
	&cpcap_rtc_device,
};

static struct cpcap_device *misc_cpcap;

static LIST_HEAD(cpcap_device_list);
static DEFINE_MUTEX(cpcap_driver_lock);

void cpcap_set_bit(enum cpcap_reg reg, unsigned short value,
		unsigned short mask)
{
	if (misc_cpcap) {
		int ret = -1;
		ret = cpcap_regacc_write(misc_cpcap, reg, value, mask);
		if (ret)
			pr_info("cpcap set bit failed\n");
	} else
		pr_info("misc_cpcap is null\n");

}
EXPORT_SYMBOL(cpcap_set_bit);

static int cpcap_reboot(struct notifier_block *this, unsigned long code,
			void *cmd)
{
	int ret = -1;
	int result = NOTIFY_DONE;
	char *mode = cmd;
	unsigned short value;
	unsigned short counter = 0;
#ifdef CONFIG_POWEROFF_ALARM
	struct device_node *node;
	const void *prop;
	int feature_poweroff_alarm = 0;
	int size;
#endif

	struct cpcap_platform_data *pdata = misc_cpcap->spi->dev.platform_data;

	pdata->usb_mux->configure_switch_muxmode(CPCAP_DM_DP);
	pdata->usb_mux->configure_otg_muxmode(SAFE_MODE);

	/* Disable the USB transceiver */
	ret = cpcap_regacc_write(misc_cpcap, CPCAP_REG_USBC2, 0,
				 CPCAP_BIT_USBXCVREN);

	if (ret) {
		dev_err(&(misc_cpcap->spi->dev),
			"Disable Transciever failure.\n");
		result = NOTIFY_BAD;
	}

	if (code == SYS_RESTART) {
		if (mode != NULL && !strncmp("outofcharge", mode, 12)) {
			/* Set the outofcharge bit in the cpcap */
			ret = cpcap_regacc_write(misc_cpcap, CPCAP_REG_VAL1,
				CPCAP_BIT_OUT_CHARGE_ONLY,
				CPCAP_BIT_OUT_CHARGE_ONLY);
			if (ret) {
				dev_err(&(misc_cpcap->spi->dev),
					"outofcharge cpcap set failure.\n");
				result = NOTIFY_BAD;
			}
			/* Set the soft reset bit in the cpcap */
			cpcap_regacc_write(misc_cpcap, CPCAP_REG_VAL1,
				CPCAP_BIT_SOFT_RESET,
				CPCAP_BIT_SOFT_RESET);
			if (ret) {
				dev_err(&(misc_cpcap->spi->dev),
					"reset cpcap set failure.\n");
				result = NOTIFY_BAD;
			}
		}

#ifdef CONFIG_POWEROFF_ALARM
		if (mode != NULL && !strncmp("outofchargealarm", mode, 17)) {
			/* Set the outofchargealarm bit in the cpcap */
			ret = cpcap_regacc_write(misc_cpcap, CPCAP_REG_VAL1,
				CPCAP_BIT_OUT_CHARGE_ONLY_ALARM,
				CPCAP_BIT_OUT_CHARGE_ONLY_ALARM);
			if (ret) {
				dev_err(&(misc_cpcap->spi->dev),
					"outofchargealarm cpcap set failure.\n");
				result = NOTIFY_BAD;
			}

			printk(KERN_INFO "Set the outofchargealarm bit.\n");
		}
#endif

		/* Check if we are starting recovery mode */
		if (mode != NULL && !strncmp("recovery", mode, 9)) {
			/* Set the fota (recovery mode) bit in the cpcap */
			ret = cpcap_regacc_write(misc_cpcap, CPCAP_REG_VAL1,
				CPCAP_BIT_FOTA_MODE, CPCAP_BIT_FOTA_MODE);
			if (ret) {
				dev_err(&(misc_cpcap->spi->dev),
					"Recovery cpcap set failure.\n");
				result = NOTIFY_BAD;
			}
		} else {
			/* Set the fota (recovery mode) bit in the cpcap */
			ret = cpcap_regacc_write(misc_cpcap, CPCAP_REG_VAL1, 0,
						CPCAP_BIT_FOTA_MODE);
			if (ret) {
				dev_err(&(misc_cpcap->spi->dev),
					"Recovery cpcap clear failure.\n");
				result = NOTIFY_BAD;
			}
		}
		/* Check if we are going into fast boot mode */
		if (mode != NULL && !strncmp("bootloader", mode, 11)) {
			/* Set the bootmode bit in the cpcap */
			ret = cpcap_regacc_write(misc_cpcap, CPCAP_REG_VAL1,
				CPCAP_BIT_BOOT_MODE, CPCAP_BIT_BOOT_MODE);
			if (ret) {
				dev_err(&(misc_cpcap->spi->dev),
					"Boot mode cpcap set failure.\n");
				result = NOTIFY_BAD;
			}
		}
		cpcap_regacc_write(misc_cpcap, CPCAP_REG_MI2, 0, 0xFFFF);
	} else {
		ret = cpcap_regacc_write(misc_cpcap, CPCAP_REG_VAL1,
					 0,
					 CPCAP_BIT_OUT_CHARGE_ONLY);
		if (ret) {
			dev_err(&(misc_cpcap->spi->dev),
				"outofcharge cpcap set failure.\n");
			result = NOTIFY_BAD;
		}
#ifdef CONFIG_POWEROFF_ALARM
		ret = cpcap_regacc_write(misc_cpcap, CPCAP_REG_VAL1,
					 0,
					 CPCAP_BIT_OUT_CHARGE_ONLY_ALARM);
		if (ret) {
			dev_err(&(misc_cpcap->spi->dev),
				"outofchargealarm cpcap set failure.\n");
			result = NOTIFY_BAD;
		}
		printk(KERN_INFO "Clear the outofchargealarm bit\n");
#endif

		/* Clear the soft reset bit in the cpcap */
		ret = cpcap_regacc_write(misc_cpcap, CPCAP_REG_VAL1, 0,
					CPCAP_BIT_SOFT_RESET);
		if (ret) {
			dev_err(&(misc_cpcap->spi->dev),
				"SW Reset cpcap set failure.\n");
			result = NOTIFY_BAD;
		}
		/* Clear the fota (recovery mode) bit in the cpcap */
		ret = cpcap_regacc_write(misc_cpcap, CPCAP_REG_VAL1, 0,
					CPCAP_BIT_FOTA_MODE);
		if (ret) {
			dev_err(&(misc_cpcap->spi->dev),
				"Recovery cpcap clear failure.\n");
			result = NOTIFY_BAD;
		}
	}

	/* Always clear the kpanic bit */
	ret = cpcap_regacc_write(misc_cpcap, CPCAP_REG_VAL1,
		0, CPCAP_BIT_AP_KERNEL_PANIC);
	if (ret) {
		dev_err(&(misc_cpcap->spi->dev),
			"Clear kernel panic bit failure.\n");
		result = NOTIFY_BAD;
	}

	/* Always clear the power cut bit on SW Shutdown*/
	ret = cpcap_regacc_write(misc_cpcap, CPCAP_REG_PC1,
		0, CPCAP_BIT_PC1_PCEN);
	if (ret) {
		dev_err(&(misc_cpcap->spi->dev),
			"Clear Power Cut bit failure.\n");
		result = NOTIFY_BAD;
	}

	cpcap_regacc_write(misc_cpcap, CPCAP_REG_CRM, 0x0300, 0x3FFF);

	(void)cpcap_regacc_read(misc_cpcap, CPCAP_REG_INTS2, &value);
	if (!(value & CPCAP_BIT_VBUSVLD_S)) {
		while ((value & CPCAP_BIT_SESSVLD_S) && (counter < 100)) {
			mdelay(10);
			counter++;
			(void)cpcap_regacc_read(misc_cpcap, CPCAP_REG_INTS2,
						 &value);
		}
	}

	/* Clear the charger and charge path settings to avoid a false turn on
	 * event in caused by CPCAP. After clearing these settings, 100ms is
	 * needed to before SYSRSTRTB is pulled low to avoid the false turn on
	 * event.
	 */
	cpcap_regacc_write(misc_cpcap, CPCAP_REG_CRM, 0, 0x3FFF);

	/* always mask TODAM interrupt due to auto power on issue */
#ifdef CONFIG_POWEROFF_ALARM
	/* do not mask TODAM if support poweroff alarm feature */
	node = of_find_node_by_path("/System@0/Feature@0");

	if (node) {
		prop = of_get_property(node, "feature_poweroff_alarm", &size);
		if (prop && size) {
			feature_poweroff_alarm = *(u8 *)prop;
			pr_debug("%s power off alarm in product",
				feature_poweroff_alarm ? "Enable" : "Disable");
		}
		of_node_put(node);
	}

	if (feature_poweroff_alarm == 0)
#endif
		cpcap_regacc_write(misc_cpcap, CPCAP_REG_INTM3,
			CPCAP_BIT_TODA_M, CPCAP_BIT_TODA_M);
	mdelay(100);

	return result;
}

static struct notifier_block cpcap_reboot_notifier = {
	.notifier_call = cpcap_reboot,
};

static int __init cpcap_init(void)
{
	return spi_register_driver(&cpcap_driver);
}

static void cpcap_vendor_read(struct cpcap_device *cpcap)
{
	unsigned short value;

	(void)cpcap_regacc_read(cpcap, CPCAP_REG_VERSC1, &value);

	cpcap->vendor = (enum cpcap_vendor)((value >> 6) & 0x0007);
	cpcap->revision = (enum cpcap_revision)(((value >> 3) & 0x0007) |
						((value << 3) & 0x0038));
}


int cpcap_device_unregister(struct platform_device *pdev)
{
	struct cpcap_driver_info *info;
	struct cpcap_driver_info *tmp;
	int found;


	found = 0;
	mutex_lock(&cpcap_driver_lock);

	list_for_each_entry_safe(info, tmp, &cpcap_device_list, list) {
		if (info->pdev == pdev) {
			list_del(&info->list);

			/*
			 * misc_cpcap != NULL suggests pdev
			 * already registered
			 */
			if (misc_cpcap) {
				pr_info("CPCAP: unregister %s\n", pdev->name);
				platform_device_unregister(pdev);
			}
			info->pdev = NULL;
			kfree(info);
			found = 1;
		}
	}

	mutex_unlock(&cpcap_driver_lock);

	BUG_ON(!found);
	return 0;
}

int cpcap_device_register(struct platform_device *pdev)
{
	int retval;
	struct cpcap_driver_info *info;

	retval = 0;

	info = kzalloc(sizeof(*info), GFP_KERNEL);
	if (!info) {
		pr_err("Cannot save device %s\n", pdev->name);
		return -ENOMEM;
	}

	mutex_lock(&cpcap_driver_lock);

	info->pdev = pdev;
	list_add_tail(&info->list, &cpcap_device_list);

	/* If misc_cpcap is valid, the CPCAP driver has already been probed.
	 * Therefore, call platform_device_register() to probe the device.
	 */
	if (misc_cpcap) {
		dev_dbg(&(misc_cpcap->spi->dev),
			 "Probing CPCAP device %s\n", pdev->name);

		/*
		 * platform_data is non-empty indicates
		 * CPCAP client devices need to pass their own data
		 * In that case we put cpcap data in driver_data
		 */
		if (pdev->dev.platform_data != NULL)
			platform_set_drvdata(pdev, misc_cpcap);
		else
			pdev->dev.platform_data = misc_cpcap;
		retval = platform_device_register(pdev);
	} else
		pr_debug("CPCAP: delaying %s probe\n", pdev->name);
	mutex_unlock(&cpcap_driver_lock);

	return retval;
}

static int __devinit cpcap_probe(struct spi_device *spi)
{
	int retval = -EINVAL;
	struct cpcap_device *cpcap;
	struct cpcap_platform_data *data;
	int i;
	struct cpcap_driver_info *info;

	cpcap = kzalloc(sizeof(*cpcap), GFP_KERNEL);
	if (cpcap == NULL)
		return -ENOMEM;

	cpcap->spi = spi;
	data = spi->dev.platform_data;
	spi_set_drvdata(spi, cpcap);

	retval = cpcap_regacc_init(cpcap);
	if (retval < 0)
		goto free_mem;

	retval = cpcap_irq_init(cpcap);
	if (retval < 0)
		goto free_mem;

	if (bi_powerup_reason() != PU_REASON_CHARGER) {
		/* Set Kpanic bit, which will be cleared at normal reboot */
		cpcap_regacc_write(cpcap, CPCAP_REG_VAL1,
			CPCAP_BIT_AP_KERNEL_PANIC, CPCAP_BIT_AP_KERNEL_PANIC);

		/* Set the soft reset bit in the cpcap */
		cpcap_regacc_write(cpcap, CPCAP_REG_VAL1,
				   CPCAP_BIT_SOFT_RESET, CPCAP_BIT_SOFT_RESET);
	}

	cpcap_vendor_read(cpcap);

	for (i = 0; i < ARRAY_SIZE(cpcap_devices); i++)
		cpcap_devices[i]->dev.platform_data = cpcap;

	retval = misc_register(&cpcap_dev);
	if (retval < 0)
		goto free_cpcap_irq;

	/* loop twice becuase cpcap_regulator_probe may refer to other devices
	 * in this list to handle dependencies between regulators.  Create them
	 * all and then add them */
	for (i = 0; i < CPCAP_NUM_REGULATORS; i++) {
		struct platform_device *pdev;

		pdev = platform_device_alloc("cpcap-regltr", i);
		if (!pdev) {
			dev_err(&(spi->dev), "Cannot create regulator\n");
			continue;
		}

		pdev->dev.parent = &(spi->dev);
		pdev->dev.platform_data = &data->regulator_init[i];
		platform_set_drvdata(pdev, cpcap);
		cpcap->regulator_pdev[i] = pdev;
	}

	for (i = 0; i < CPCAP_NUM_REGULATORS; i++) {
		/* vusb has to be added after sw5 so skip it for now,
		 * it will be added from probe of sw5 */
		if (i == CPCAP_VUSB)
			continue;
		platform_device_add(cpcap->regulator_pdev[i]);
	}

	platform_add_devices(cpcap_devices, ARRAY_SIZE(cpcap_devices));

	mutex_lock(&cpcap_driver_lock);
	misc_cpcap = cpcap;  /* kept for misc device */

	list_for_each_entry(info, &cpcap_device_list, list) {
		int ret = 0;
		dev_dbg(&(spi->dev), "Probing CPCAP device %s\n",
			 info->pdev->name);
		if (info->pdev->dev.platform_data != NULL)
			platform_set_drvdata(info->pdev, cpcap);
		else
			info->pdev->dev.platform_data = cpcap;
		ret = platform_device_register(info->pdev);
	}
	mutex_unlock(&cpcap_driver_lock);

	register_reboot_notifier(&cpcap_reboot_notifier);

	return 0;

free_cpcap_irq:
	cpcap_irq_shutdown(cpcap);
free_mem:
	kfree(cpcap);
	return retval;
}

static int __devexit cpcap_remove(struct spi_device *spi)
{
	struct cpcap_device *cpcap = spi_get_drvdata(spi);
	struct cpcap_driver_info *info;
	int i;

	unregister_reboot_notifier(&cpcap_reboot_notifier);

	mutex_lock(&cpcap_driver_lock);
	list_for_each_entry(info, &cpcap_device_list, list) {
		dev_dbg(&(spi->dev), "Removing CPCAP device %s\n",
			 info->pdev->name);
		platform_device_unregister(info->pdev);
	}
	misc_cpcap = NULL;
	mutex_unlock(&cpcap_driver_lock);

	for (i = ARRAY_SIZE(cpcap_devices); i > 0; i--)
		platform_device_unregister(cpcap_devices[i-1]);

	for (i = 0; i < CPCAP_NUM_REGULATORS; i++)
		platform_device_unregister(cpcap->regulator_pdev[i]);

	misc_deregister(&cpcap_dev);
	cpcap_irq_shutdown(cpcap);
	kfree(cpcap);
	return 0;
}


static int test_ioctl(unsigned int cmd, unsigned long arg)
{
	int retval = -EINVAL;
	struct cpcap_regacc read_data;
	struct cpcap_regacc write_data;

	switch (cmd) {
	case CPCAP_IOCTL_TEST_READ_REG:
	case CPCAP_IOCTL_TEST_SEC_READ_REG:
		if (copy_from_user((void *)&read_data, (void *)arg,
				   sizeof(read_data)))
			return -EFAULT;

		if (cmd == CPCAP_IOCTL_TEST_SEC_READ_REG)
			retval = cpcap_regacc_read_secondary(misc_cpcap,
							     read_data.reg,
							     &read_data.
							     value);
		else
			retval = cpcap_regacc_read(misc_cpcap, read_data.reg,
						   &read_data.value);
		if (retval < 0)
			return retval;
		if (copy_to_user((void *)arg, (void *)&read_data,
				 sizeof(read_data)))
			return -EFAULT;
		return 0;
		break;

	case CPCAP_IOCTL_TEST_WRITE_REG:
	case CPCAP_IOCTL_TEST_SEC_WRITE_REG:
		if (copy_from_user((void *) &write_data,
				   (void *) arg,
				   sizeof(write_data)))
			return -EFAULT;

		if (cmd == CPCAP_IOCTL_TEST_SEC_WRITE_REG)
			retval = cpcap_regacc_write_secondary(misc_cpcap,
							      write_data.reg,
							      write_data.value,
							      write_data.mask);
		else
			retval = cpcap_regacc_write(misc_cpcap,
						    write_data.reg,
						    write_data.value,
						    write_data.mask);
		break;

	default:
		retval = -ENOTTY;
		break;
	}

	return retval;
}

static int adc_ioctl(unsigned int cmd, unsigned long arg)
{
	int retval = -EINVAL;
	struct cpcap_adc_phase phase;

	switch (cmd) {
	case CPCAP_IOCTL_ADC_PHASE:
		if (copy_from_user((void *) &phase, (void *) arg,
				   sizeof(phase)))
			return -EFAULT;

		cpcap_adc_phase(misc_cpcap, &phase);
		retval = 0;
		break;

	default:
		retval = -ENOTTY;
		break;
	}

	return retval;
}

static int accy_ioctl(unsigned int cmd, unsigned long arg)
{
	int retval = -EINVAL;

	struct cpcap_whisper_request read_data;
	struct cpcap_charger_request chrg_data;
	switch (cmd) {
	case CPCAP_IOCTL_ACCY_WHISPER:
		if (copy_from_user((void *) &read_data, (void *) arg,
				   sizeof(read_data)))
			return -EFAULT;
		read_data.dock_id[CPCAP_WHISPER_ID_SIZE - 1] = '\0';
		read_data.dock_prop[CPCAP_WHISPER_PROP_SIZE - 1] = '\0';
		retval = cpcap_accy_whisper(misc_cpcap, &read_data);
		break;
	case CPCAP_IOCTL_ACCY_CHARGER:
		if (copy_from_user((void *) &chrg_data, (void *) arg,
				   sizeof(chrg_data)))
			return -EFAULT;
		retval = cpcap_accy_charger(misc_cpcap, &chrg_data);
		break;
	default:
		retval = -ENOTTY;
		break;
	}

	return retval;
}

struct regulator *audio_reg;

static int audio_pwr_ioctl(unsigned int cmd, unsigned long arg)
{
	int retval = -EINVAL;
	if (!audio_reg) {
		audio_reg = regulator_get(NULL, "vaudio");
		if (IS_ERR(audio_reg)) {
			dev_err(&(misc_cpcap->spi->dev),
			"Cannot open audio regulator\n");
			return -ENOTTY;
		}
	}
	switch (cmd) {
	case CPCAP_IOCTL_AUDIO_PWR_MODE:
		  retval = regulator_set_mode(audio_reg, (unsigned int) arg);
	break;
	case CPCAP_IOCTL_AUDIO_PWR_ENABLE:
		if (arg) {
			/* Enable vaudio  regulator */
			retval = regulator_enable(audio_reg);
			retval = regulator_set_mode(audio_reg,
						    REGULATOR_MODE_NORMAL);
		} else {
			retval = regulator_set_mode(audio_reg,
						    REGULATOR_MODE_STANDBY);
			retval = regulator_disable(audio_reg);
		}
	break;
	default:
		retval = -ENOTTY;
	break;
	}

	return retval;
}

#if defined(CONFIG_LEDS_FLASH_RESET)
int cpcap_direct_misc_write(unsigned short reg, unsigned short value,\
						unsigned short mask)
{
	int retval = -EINVAL;

	retval = cpcap_regacc_write(misc_cpcap, reg, value, mask);

	return retval;
}
#endif

static long ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	long retval = -ENOTTY;
	unsigned int cmd_num;
	unsigned int i;
	static const struct {
		unsigned int low_cmd;
		unsigned int high_cmd;
		int (*handler)(unsigned int, unsigned long);
	} ioctl_fcn_tb[] = {
		{ CPCAP_IOCTL_NUM_TEST__START,
		  CPCAP_IOCTL_NUM_TEST__END,
		  test_ioctl      },
		{ CPCAP_IOCTL_NUM_ADC__START,
		  CPCAP_IOCTL_NUM_ADC__END,
		  adc_ioctl       },
		{ CPCAP_IOCTL_NUM_ACCY__START,
		  CPCAP_IOCTL_NUM_ACCY__END,
		  accy_ioctl      },
		{ CPCAP_IOCTL_NUM_AUDIO_PWR__START,
		  CPCAP_IOCTL_NUM_AUDIO_PWR__END,
		  audio_pwr_ioctl },
		{ CPCAP_IOCTL_NUM_TEST_SEC__START,
		  CPCAP_IOCTL_NUM_TEST_SEC__END,
		  test_ioctl      },
	};

	cmd_num = _IOC_NR(cmd);

	for (i = 0; i < ARRAY_SIZE(ioctl_fcn_tb); i++) {
		if ((cmd_num < ioctl_fcn_tb[i].high_cmd) &&
			(cmd_num > ioctl_fcn_tb[i].low_cmd))
			retval = ioctl_fcn_tb[i].handler(cmd, arg);
	}

	return retval;
}

static void cpcap_shutdown(void)
{
	spi_unregister_driver(&cpcap_driver);
}

void cpcap_accy_whisper_audio_switch_spdif_state(bool state)
{
	whisper_toggle_audio_switch_for_spdif(misc_cpcap, state);
}

subsys_initcall(cpcap_init);
module_exit(cpcap_shutdown);

MODULE_ALIAS("platform:cpcap");
MODULE_DESCRIPTION("CPCAP driver");
MODULE_AUTHOR("Motorola");
MODULE_LICENSE("GPL");
