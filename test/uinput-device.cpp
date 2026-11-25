// SPDX-FileCopyrightText: 2026 Nguyen Hoang Ky <nhktmdzhg@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The server injects keys through a libevdev-uinput keyboard. Describing that keyboard in memory
// needs no privileges, so this test pins the identity and the exact key set the old raw UI_SET_*
// setup produced, without opening /dev/uinput (it passes inside a user namespace as well).
#include "lotus-uinput-device.h"
#include "test-utils.h"

#include <cstdio>
#include <exception>
#include <source_location>
#include <string_view>

namespace {
    void check(const char* name, bool condition, const std::source_location& location = std::source_location::current()) {
        if (!condition) {
            reportFailure(name, "holds", "did not hold", "the uinput keyboard must expose exactly the key set the server injects with", location);
        }
    }

} // namespace

int runUinputDevice() {
    const LibevdevDevice device = buildLotusUinputDevice();
    if (device == nullptr) {
        reportFailure("build the uinput keyboard", "a libevdev device", "libevdev_new returned null", "without the keyboard the server cannot inject any key");
        return 1;
    }

    const char* const name = libevdev_get_name(device.get());
    check("device name", name != nullptr && std::string_view(name) == kLotusUinputName);
    check("bustype is BUS_USB", libevdev_get_id_bustype(device.get()) == BUS_USB);
    check("vendor id", libevdev_get_id_vendor(device.get()) == kLotusUinputVendor);
    check("product id", libevdev_get_id_product(device.get()) == kLotusUinputProduct);
    check("EV_KEY is enabled", libevdev_has_event_type(device.get(), EV_KEY) != 0);
    for (const unsigned int code : kLotusUinputKeys) {
        check("declared key is enabled", libevdev_has_event_code(device.get(), EV_KEY, code) != 0);
    }
    check("no unrelated key", libevdev_has_event_code(device.get(), EV_KEY, KEY_A) == 0);
    check("anchor key enabled", libevdev_has_event_code(device.get(), EV_KEY, KEY_PAUSE) != 0);
    check("no unrelated event type", libevdev_has_event_type(device.get(), EV_REL) == 0);

    return checkExitCode("lotus uinput device: all checks passed");
}

int main() {
    try {
        return runUinputDevice();
    } catch (const std::exception& error) {
        reportFailure("unexpected exception", "no exception escapes the test", error.what(), "an escaped exception would crash the server instead of failing one check");
        return 1;
    }
}
