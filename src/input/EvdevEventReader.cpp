#include "input/EvdevEventReader.h"
#include "Logger.h"
#include <libevdev/libevdev.h>
#include <linux/input.h>
#include <fcntl.h>
#include <unistd.h>
#include <cerrno>
#include <cstring>
#include <sys/ioctl.h>

bool EvdevEventReader::open(const std::string& devicePath) {
    fd = ::open(devicePath.c_str(), O_RDONLY | O_NONBLOCK);
    if (fd < 0) {
        Logger::error("open input device: " + std::string(strerror(errno)));
        return false;
    }
    if (libevdev_new_from_fd(fd, &dev) < 0) {
        Logger::error("libevdev init failed");
        return false;
    }
    return true;
}

std::optional<RawEvent> EvdevEventReader::read() {
    if (!dev) return std::nullopt;

    struct input_event ev{};
    int rc = libevdev_next_event(dev, LIBEVDEV_READ_FLAG_NORMAL, &ev);

    if (rc == LIBEVDEV_READ_STATUS_SUCCESS) {
        if (ev.type == EV_SYN) return std::nullopt;
        return RawEvent{ ev.type, ev.code, ev.value };
    }
    if (rc == -EAGAIN) return std::nullopt;

    readFailed = true;
    Logger::error("Read error: " + std::string(strerror(-rc)));
    return std::nullopt;
}

bool EvdevEventReader::failed() const {
    return readFailed;
}

float EvdevEventReader::normalizeAxis(int code, int rawValue) const {
    if (!dev) return 0.0f;
    const struct input_absinfo* info = libevdev_get_abs_info(dev, code);
    if (!info) return 0.0f;

    int min = info->minimum;
    int max = info->maximum;
    int mid = (min + max) / 2;
    int range = (max - min) / 2;
    if (range == 0) return 0.0f;

    float normalized = static_cast<float>(rawValue - mid) / static_cast<float>(range);

    const float DEADZONE = 0.05f;
    if (normalized > -DEADZONE && normalized < DEADZONE) return 0.0f;
    if (normalized > 1.0f) normalized = 1.0f;
    if (normalized < -1.0f) normalized = -1.0f;

    return normalized;
}

float EvdevEventReader::normalizeTrigger(int code, int rawValue) const {
    if (!dev) return 0.0f;
    const struct input_absinfo* info = libevdev_get_abs_info(dev, code);
    if (!info) return 0.0f;

    int min = info->minimum;
    int max = info->maximum;
    if (max == min) return 0.0f;

    float normalized = static_cast<float>(rawValue - min) / static_cast<float>(max - min);
    if (normalized < 0.0f) normalized = 0.0f;
    if (normalized > 1.0f) normalized = 1.0f;
    return normalized;
}

std::string EvdevEventReader::deviceName() const {
    return dev ? libevdev_get_name(dev) : "";
}

const char* EvdevEventReader::codeName(int type, int code) const {
    return libevdev_event_code_get_name(type, code);
}

std::vector<DeviceCapability> EvdevEventReader::listButtonCapabilities() const {
    std::vector<DeviceCapability> caps;
    if (!dev) return caps;
    for (int code = 0; code < KEY_MAX; ++code) {
        if (libevdev_has_event_code(dev, EV_KEY, code) == 1) {
            const char* n = libevdev_event_code_get_name(EV_KEY, code);
            caps.push_back({ code, n ? n : "" });
        }
    }
    return caps;
}

std::vector<DeviceCapability> EvdevEventReader::listAxisCapabilities() const {
    std::vector<DeviceCapability> caps;
    if (!dev) return caps;
    for (int code = 0; code < ABS_MAX; ++code) {
        if (libevdev_has_event_code(dev, EV_ABS, code) == 1) {
            const char* n = libevdev_event_code_get_name(EV_ABS, code);
            caps.push_back({ code, n ? n : "" });
        }
    }
    return caps;
}

bool EvdevEventReader::grab(bool exclusive) {
    if (fd < 0) return false;
    if (::ioctl(fd, EVIOCGRAB, exclusive ? 1 : 0) == 0) {
        Logger::info(std::string(exclusive ? "Grabbed" : "Released") + " device " + deviceName());
        return true;
    }
    Logger::error("EVIOCGRAB failed: " + std::string(strerror(errno)));
    return false;
}

DeviceIdentity EvdevEventReader::identity() const {
    DeviceIdentity id;
    id.name = deviceName();
    if (dev) {
        id.vendorId = static_cast<uint16_t>(libevdev_get_id_vendor(dev));
        id.productId = static_cast<uint16_t>(libevdev_get_id_product(dev));
        const char* u = libevdev_get_uniq(dev);
        id.serial = u ? u : "";
    }
    return id;
}

EvdevEventReader::~EvdevEventReader() {
    if (dev) libevdev_free(dev);
    if (fd >= 0) close(fd);
}