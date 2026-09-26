// SPDX-License-Identifier: GPL-2.0-only
/* @canonical simulated_backlight_device -- QEMU-only backlight class fixture.
 * This has no physical illumination effect. Production board profiles never
 * load it; SysfsBacklight exercises the real kernel attribute ABI unchanged.
 */
#include <linux/backlight.h>
#include <linux/module.h>
#include <linux/platform_device.h>

static struct platform_device *bhp_sim_platform;
static struct backlight_device *bhp_sim_backlight;
static int bhp_sim_update_status(struct backlight_device *device)
{
    (void)device;
    return 0;
}
static int bhp_sim_get_brightness(struct backlight_device *device)
{
    return device->props.brightness;
}
static const struct backlight_ops bhp_sim_ops = {
    .update_status = bhp_sim_update_status,
    .get_brightness = bhp_sim_get_brightness,
};
static int __init bhp_sim_init(void)
{
    struct backlight_properties properties = {
        .type = BACKLIGHT_RAW, .max_brightness = 255, .brightness = 0,
    };
    bhp_sim_platform = platform_device_register_simple("bhp-sim-backlight", -1, NULL, 0);
    if (IS_ERR(bhp_sim_platform)) return PTR_ERR(bhp_sim_platform);
    bhp_sim_backlight = backlight_device_register("bhp-sim", &bhp_sim_platform->dev,
                                                 NULL, &bhp_sim_ops, &properties);
    if (IS_ERR(bhp_sim_backlight)) {
        int error = PTR_ERR(bhp_sim_backlight);
        platform_device_unregister(bhp_sim_platform);
        return error;
    }
    return 0;
}
static void __exit bhp_sim_exit(void)
{
    backlight_device_unregister(bhp_sim_backlight);
    platform_device_unregister(bhp_sim_platform);
}
module_init(bhp_sim_init);
module_exit(bhp_sim_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("House panel QEMU backlight fixture: simulation_not_hardware_evidence");
