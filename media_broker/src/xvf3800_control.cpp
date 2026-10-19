#include "media_broker/xvf3800_control.h"
#include <cerrno>
#include <chrono>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <fstream>
#include <linux/usbdevice_fs.h>
#include <memory>
#include <sys/ioctl.h>
#include <thread>
#include <unistd.h>
namespace media_broker
{
namespace
{
constexpr std::uint8_t kStatusSuccess = 0;
constexpr std::uint8_t kStatusRetry = 64;
constexpr int kMaximumAttempts = 100;

unsigned width(const XvfValueType type)
{
	return type == XvfValueType::Int32 || type == XvfValueType::Float ? 4 : 1;
}

std::string first_line(const std::string &path)
{
	std::ifstream file(path);
	std::string line;
	std::getline(file, line);
	return line;
}

template <typename Value> MediaResult<Value> fail(const char *code, const char *message, const char *operation)
{
	return MediaResult<Value>::failure(media_error(code, message, operation));
}
} // namespace

const std::vector<XvfParameter> &xvf3800_identity_parameters()
{
	// Resource and command IDs from Seeed's xvf_host.py PARAMETERS table.
	static const std::vector<XvfParameter> parameters{
		{"VERSION", 48, 0, 3, XvfValueType::Uint8},
		{"BLD_MSG", 48, 1, 50, XvfValueType::Char},
		{"BLD_REPO_HASH", 48, 3, 40, XvfValueType::Char},
		{"SHF_BYPASS", 33, 70, 1, XvfValueType::Uint8},
		{"AEC_HPFONOFF", 33, 1, 1, XvfValueType::Int32},
		{"PP_AGCONOFF", 17, 10, 1, XvfValueType::Int32},
		{"PP_AGCGAIN", 17, 13, 1, XvfValueType::Float},
		{"PP_LIMITONOFF", 17, 19, 1, XvfValueType::Int32},
		{"PP_MIN_NS", 17, 21, 1, XvfValueType::Float},
		{"PP_MIN_NN", 17, 22, 1, XvfValueType::Float},
		{"PP_ECHOONOFF", 17, 23, 1, XvfValueType::Int32},
		{"PP_NLATTENONOFF", 17, 27, 1, XvfValueType::Int32},
		{"AUDIO_MGR_OP_L", 35, 15, 2, XvfValueType::Uint8},
		{"AUDIO_MGR_OP_R", 35, 19, 2, XvfValueType::Uint8},
	};
	return parameters;
}

MediaResult<nlohmann::json> Xvf3800Control::read(const XvfParameter &parameter) const
{
	const auto length = static_cast<std::uint16_t>(1 + parameter.count * width(parameter.type));
	std::vector<std::uint8_t> reply;
	for (int attempt = 0; attempt < kMaximumAttempts; ++attempt) {
		reply = transfer_(static_cast<std::uint16_t>(0x80U | parameter.command), parameter.resource, length);
		if (reply.empty())
			return fail<nlohmann::json>("MEDIA_DEVICE.CONTROL_TRANSFER_FAILED",
						    "Control transfer to the array failed", "xvf3800.read");
		if (reply[0] != kStatusRetry)
			break;
		std::this_thread::sleep_for(std::chrono::milliseconds(10));
	}
	if (reply[0] == kStatusRetry)
		return fail<nlohmann::json>("MEDIA_DEVICE.CONTROL_BUSY", "Array kept asking for a retry",
					    "xvf3800.read");
	if (reply[0] != kStatusSuccess || reply.size() != length)
		return fail<nlohmann::json>("MEDIA_DEVICE.CONTROL_REFUSED", "Array refused the read", "xvf3800.read");
	if (parameter.type == XvfValueType::Char) {
		std::string text(reply.begin() + 1, reply.end());
		text = text.substr(0, text.find('\0'));
		for (const char c : text)
			if (static_cast<unsigned char>(c) < 0x20 || static_cast<unsigned char>(c) > 0x7e)
				return fail<nlohmann::json>("MEDIA_DEVICE.CONTROL_REFUSED", "Array returned unprintable text",
							    "xvf3800.read");
		return MediaResult<nlohmann::json>::success(text);
	}
	nlohmann::json values = nlohmann::json::array();
	for (unsigned element = 0; element < parameter.count; ++element) {
		const std::uint8_t *at = reply.data() + 1 + element * width(parameter.type);
		if (parameter.type == XvfValueType::Uint8) {
			values.push_back(at[0]);
			continue;
		}
		std::uint32_t bits = static_cast<std::uint32_t>(at[0]) | static_cast<std::uint32_t>(at[1]) << 8 |
				     static_cast<std::uint32_t>(at[2]) << 16 | static_cast<std::uint32_t>(at[3]) << 24;
		if (parameter.type == XvfValueType::Int32) {
			std::int32_t value;
			std::memcpy(&value, &bits, sizeof value);
			values.push_back(value);
		} else {
			float value;
			std::memcpy(&value, &bits, sizeof value);
			values.push_back(static_cast<double>(value));
		}
	}
	return MediaResult<nlohmann::json>::success(values);
}

nlohmann::json Xvf3800Control::read_identity() const
{
	nlohmann::json identity = {{"firmware", nlohmann::json::object()},
				   {"settings", nlohmann::json::object()},
				   {"routing", nlohmann::json::object()},
				   {"unread", nlohmann::json::object()}};
	for (const auto &parameter : xvf3800_identity_parameters()) {
		const std::string name = parameter.name;
		const char *group = parameter.resource == 48 ? "firmware" : parameter.resource == 35 ? "routing" : "settings";
		const auto value = read(parameter);
		identity[group][name] = value ? value.value() : nlohmann::json(nullptr);
		if (!value)
			identity["unread"][name] = value.error().code;
	}
	return identity;
}

MediaResult<XvfControlTransfer> usbdevfs_control_transfer(const std::string &device_node)
{
	// usbdevfs needs a writable descriptor for any ioctl; only IN reads are sent.
	const int descriptor = ::open(device_node.c_str(), O_RDWR | O_CLOEXEC);
	if (descriptor < 0)
		return fail<XvfControlTransfer>("MEDIA_DEVICE.CONTROL_UNAVAILABLE", "Array control node did not open",
						"xvf3800.open");
	const auto shared = std::shared_ptr<int>(new int(descriptor), [](const int *fd) {
		::close(*fd);
		delete fd;
	});
	return MediaResult<XvfControlTransfer>::success(
		[shared](const std::uint16_t value, const std::uint16_t index, const std::uint16_t length) {
			std::vector<std::uint8_t> reply(length);
			usbdevfs_ctrltransfer transfer{};
			transfer.bRequestType = 0xC0;
			transfer.bRequest = 0;
			transfer.wValue = value;
			transfer.wIndex = index;
			transfer.wLength = length;
			transfer.timeout = 1000;
			transfer.data = reply.data();
			const int got = ioctl(*shared, USBDEVFS_CONTROL, &transfer);
			if (got < 0)
				return std::vector<std::uint8_t>{};
			reply.resize(static_cast<std::size_t>(got));
			return reply;
		});
}

MediaResult<nlohmann::json> find_xvf3800_usb_device(const std::string &sysfs_usb_devices)
{
	DIR *directory = opendir(sysfs_usb_devices.c_str());
	if (directory == nullptr)
		return fail<nlohmann::json>("MEDIA_DEVICE.ABSENT", "USB devices are unreadable", "xvf3800.find");
	nlohmann::json found;
	while (const dirent *entry = readdir(directory)) {
		const std::string base = sysfs_usb_devices + "/" + entry->d_name;
		if (first_line(base + "/idVendor") != "2886")
			continue;
		const std::string product = first_line(base + "/product");
		if (product.find("XVF3800") == std::string::npos)
			continue;
		const auto bus = first_line(base + "/busnum");
		const auto device = first_line(base + "/devnum");
		const auto product_id = first_line(base + "/idProduct");
		if (bus.empty() || device.empty() || product_id.size() != 4)
			continue;
		char node[64];
		std::snprintf(node, sizeof node, "/dev/bus/usb/%03d/%03d", std::atoi(bus.c_str()),
			      std::atoi(device.c_str()));
		const auto serial = first_line(base + "/serial");
		found = {{"model", product},
			 {"usb_vendor_id", 0x2886},
			 {"usb_product_id", std::stoi(product_id, nullptr, 16)},
			 {"serial", serial.empty() ? nlohmann::json(nullptr) : nlohmann::json(serial)},
			 {"usb_node", node}};
		break;
	}
	closedir(directory);
	if (found.is_null())
		return fail<nlohmann::json>("MEDIA_DEVICE.ABSENT", "No XVF3800 array on USB", "xvf3800.find");
	return MediaResult<nlohmann::json>::success(found);
}
} // namespace media_broker
