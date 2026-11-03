// license:BSD-3-Clause
// copyright-holders:D. Rimron-Soutter
#ifndef MAME_BUS_RS232_NEXTPI_H
#define MAME_BUS_RS232_NEXTPI_H

#pragma once

#include "rs232.h"

#if !defined(_WIN32)

#include <string>
#include <string_view>
#include <vector>


// Read a file from the root folder of the FAT32 file system that starts at
// byte offset in image (nextpi_fat32.cpp).  Returns an empty string on
// success, otherwise a message.
std::string nextpi_fat32_read(util::random_read &image, u64 offset, std::string_view name, std::vector<u8> &data);

DECLARE_DEVICE_TYPE(NEXTPI, device_rs232_port_interface)

#endif // !defined(_WIN32)

#endif // MAME_BUS_RS232_NEXTPI_H
