// SPDX-License-Identifier: GPL-2.0+

#pragma once

#include <libusb-1.0/libusb.h>

/// libusb_exit() aborts on libusb 1.0.25 ("usbi_hotplug_exit: Assertion dev->parent_dev != next_dev
/// failed"), the version shipped by Ubuntu/Pop!_OS 22.04; fixed in 1.0.26. On that version the context is left
/// for the OS to release at process exit (devices are already closed), so closing a program does not end in
/// "Abortado (imagem do núcleo gravada)".
inline void exitLibUsb(libusb_context *context) {
    const libusb_version *v = libusb_get_version();
    if (v->major == 1 && v->minor == 0 && v->micro == 25) return;
    libusb_exit(context);
}
