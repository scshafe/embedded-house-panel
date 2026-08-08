// SPDX-License-Identifier: GPL-2.0-only
/*
 * canonical: beagle_house_pir_input_driver -- experimental PIR GPIO input.
 *
 * This is an experimental house-panel driver. Runtime GPIO events do not prove
 * that an AM335x can wake from DeepSleep0; that path also depends on the chosen
 * pad, GPIO bank, parent IRQ, WKUP-M3 firmware, PM configuration, and board.
 */

#include <linux/device.h>
#include <linux/gpio/consumer.h>
#include <linux/input.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pm_wakeirq.h>
#include <linux/pm_wakeup.h>
#include <linux/property.h>

#define BEAGLE_HOUSE_PIR_DEFAULT_NAME "Beagle House PIR custom experiment"
#define BEAGLE_HOUSE_PIR_PHYS_PATH "beagle-house-pir/input0"

struct beagle_house_pir {
	struct device *device;
	struct gpio_desc *presence_gpio;
	struct input_dev *input;
	int irq;
};

/*
 * Report the logical GPIO state. Descriptor polarity makes one mean
 * "presence asserted" for either active-high or active-low hardware.
 */
static int beagle_house_pir_report_state(struct beagle_house_pir *pir)
{
	int state;

	state = gpiod_get_value_cansleep(pir->presence_gpio);
	if (state < 0)
		return state;

	input_report_switch(pir->input, SW_FRONT_PROXIMITY, !!state);
	input_sync(pir->input);
	dev_dbg(pir->device, "reported logical presence state=%d\n", !!state);

	return 0;
}

static irqreturn_t beagle_house_pir_irq_thread(int irq, void *driver_data)
{
	struct beagle_house_pir *pir = driver_data;
	int error;

	if (unlikely(irq != pir->irq))
		return IRQ_NONE;

	if (device_may_wakeup(pir->device))
		pm_wakeup_event(pir->device, 0);

	error = beagle_house_pir_report_state(pir);
	if (error)
		dev_err_ratelimited(pir->device,
				    "failed to read presence GPIO after IRQ %d: %d\n",
				    irq, error);

	return IRQ_HANDLED;
}

static void beagle_house_pir_clear_wakeup(void *driver_data)
{
	struct device *device = driver_data;

	dev_pm_clear_wake_irq(device);
	device_init_wakeup(device, false);
}

static int beagle_house_pir_configure_wakeup(struct beagle_house_pir *pir)
{
	struct device *device = pir->device;
	int error;

	if (!device_property_read_bool(device, "wakeup-source"))
		return 0;

	if (!IS_ENABLED(CONFIG_PM_SLEEP))
		return dev_err_probe(device, -EOPNOTSUPP,
				     "wakeup-source requires CONFIG_PM_SLEEP\n");

	error = device_init_wakeup(device, true);
	if (error)
		return dev_err_probe(device, error,
				     "failed to initialize system wakeup\n");

	error = dev_pm_set_wake_irq(device, pir->irq);
	if (error) {
		device_init_wakeup(device, false);
		return dev_err_probe(device, error,
				     "failed to attach IRQ %d as the wake IRQ\n",
				     pir->irq);
	}

	error = devm_add_action_or_reset(device,
					 beagle_house_pir_clear_wakeup, device);
	if (error)
		return dev_err_probe(device, error,
				     "failed to register wakeup cleanup\n");

	dev_warn(device,
		 "experimental wake path armed; AM335x DeepSleep0 wake remains unverified on target hardware\n");

	return 0;
}

static int beagle_house_pir_probe(struct platform_device *platform_device)
{
	struct device *device = &platform_device->dev;
	const char *input_name = BEAGLE_HOUSE_PIR_DEFAULT_NAME;
	struct beagle_house_pir *pir;
	unsigned long irq_flags;
	int error;

	pir = devm_kzalloc(device, sizeof(*pir), GFP_KERNEL);
	if (!pir)
		return dev_err_probe(device, -ENOMEM,
				     "failed to allocate driver state\n");

	pir->device = device;
	pir->presence_gpio = devm_gpiod_get(device, "presence", GPIOD_IN);
	if (IS_ERR(pir->presence_gpio))
		return dev_err_probe(device, PTR_ERR(pir->presence_gpio),
				     "failed to acquire required presence GPIO\n");

	pir->irq = gpiod_to_irq(pir->presence_gpio);
	if (pir->irq < 0)
		return dev_err_probe(device, pir->irq,
				     "failed to map presence GPIO to an IRQ\n");

	pir->input = devm_input_allocate_device(device);
	if (!pir->input)
		return dev_err_probe(device, -ENOMEM,
				     "failed to allocate input device\n");

	if (device_property_present(device, "label")) {
		error = device_property_read_string(device, "label", &input_name);
		if (error)
			return dev_err_probe(device, error,
					     "label property is not a valid string\n");
	}

	pir->input->name = input_name;
	pir->input->phys = BEAGLE_HOUSE_PIR_PHYS_PATH;
	pir->input->id.bustype = BUS_HOST;
	pir->input->id.vendor = 0x0001;
	pir->input->id.product = 0x0001;
	pir->input->id.version = 0x0100;
	input_set_capability(pir->input, EV_SW, SW_FRONT_PROXIMITY);

	platform_set_drvdata(platform_device, pir);

	/*
	 * Register before the IRQ can run: input event buffers are initialized by
	 * input_register_device(). The post-request sample closes the state gap.
	 */
	error = input_register_device(pir->input);
	if (error)
		return dev_err_probe(device, error,
				     "failed to register input device\n");

	irq_flags = IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING | IRQF_ONESHOT;
	error = devm_request_threaded_irq(device, pir->irq, NULL,
					  beagle_house_pir_irq_thread,
					  irq_flags, dev_name(device), pir);
	if (error)
		return dev_err_probe(device, error,
				     "failed to request both-edge IRQ %d\n", pir->irq);

	error = beagle_house_pir_report_state(pir);
	if (error)
		return dev_err_probe(device, error,
				     "failed to read initial presence GPIO state\n");

	error = beagle_house_pir_configure_wakeup(pir);
	if (error)
		return error;

	dev_info(device,
		 "registered GPIO state input on IRQ %d (both edges, switch code %u)\n",
		 pir->irq, SW_FRONT_PROXIMITY);

	return 0;
}

static const struct of_device_id beagle_house_pir_of_match[] = {
	{ .compatible = "beagle,house-pir" },
	{ }
};
MODULE_DEVICE_TABLE(of, beagle_house_pir_of_match);

static struct platform_driver beagle_house_pir_driver = {
	.probe = beagle_house_pir_probe,
	.driver = {
		.name = "beagle-house-pir",
		.of_match_table = beagle_house_pir_of_match,
	},
};
module_platform_driver(beagle_house_pir_driver);

MODULE_AUTHOR("Beagle House Panel maintainers");
MODULE_DESCRIPTION("Experimental GPIO PIR presence input and wake driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:beagle-house-pir");
