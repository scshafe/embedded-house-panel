#pragma once
#include "media_broker/media_result.h"
#include <cstdint>
#include <functional>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>
namespace media_broker
{
/**
 * One vendor IN control transfer to the device (bmRequestType 0xC0, bRequest 0).
 * Returns the bytes read, or an empty vector on a transport failure.
 */
using XvfControlTransfer =
	std::function<std::vector<std::uint8_t>(std::uint16_t value, std::uint16_t index, std::uint16_t length)>;

enum class XvfValueType { Uint8, Char, Int32, Float };

/** A read-only XVF3800 parameter: its resource ID, command ID, element count and type. */
struct XvfParameter {
	const char *name;
	std::uint8_t resource;
	std::uint8_t command;
	std::uint8_t count;
	XvfValueType type;
};

/** The firmware identity and the processing settings voice-lab's capture profile names. */
const std::vector<XvfParameter> &xvf3800_identity_parameters();

/**
 * @canonical xvf3800_control -- read-only parameter reads from the XVF3800.
 *
 * The XMOS host-control read protocol, as Seeed's public host code uses it:
 * wValue = 0x80 | command, wIndex = resource, and a reply whose first byte is
 * a status (0 success, 64 retry). Retries up to 100 times. No write exists.
 */
class Xvf3800Control final
{
public:
	explicit Xvf3800Control(XvfControlTransfer transfer) : transfer_(std::move(transfer)) {}
	/** A JSON array of the values, or MEDIA_DEVICE.CONTROL_* on failure. */
	MediaResult<nlohmann::json> read(const XvfParameter &parameter) const;
	/**
	 * {"firmware": {...}, "settings": {...}, "routing": {...}}; a parameter that
	 * cannot be read is recorded as null with its code under "unread".
	 */
	nlohmann::json read_identity() const;

private:
	XvfControlTransfer transfer_;
};

/** The usbdevfs transport for the USB device at /dev/bus/usb/BUS/DEVICE. */
MediaResult<XvfControlTransfer> usbdevfs_control_transfer(const std::string &device_node);

/**
 * Finds the array in sysfs (vendor 2886, "XVF3800" in the product) and describes
 * it: model, vendor and product IDs, serial and its usbdevfs node.
 */
MediaResult<nlohmann::json> find_xvf3800_usb_device(const std::string &sysfs_usb_devices = "/sys/bus/usb/devices");
} // namespace media_broker
