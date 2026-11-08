// license:BSD-3-Clause
// copyright-holders:D. Rimron-Soutter
#ifndef MAME_BUS_RS232_NEXTPI_H
#define MAME_BUS_RS232_NEXTPI_H

#pragma once

#include "rs232.h"


// The Pi's I2S output, which RS-232 lacks; a host finds it on the slot
// card.  The Pi is the I2S master: the card calls the handler once per
// frame, in emulated time at the Pi's frame rate, with the left and right
// samples signed and MSB-aligned in 32 bits.
class device_pi_i2s_interface : public device_interface
{
public:
	using frame_delegate = named_delegate<void (s32 left, s32 right)>;

	void set_i2s_frame_handler(frame_delegate handler) { m_i2s_frame = handler; }

protected:
	device_pi_i2s_interface(machine_config const &mconfig, device_t &device) : device_interface(device, "pi_i2s") { }

	frame_delegate m_i2s_frame;
};


#if !defined(_WIN32)

#include <string>
#include <string_view>
#include <vector>


// Read a file from the FAT32 file system that starts at byte offset in
// image (nextpi_fat32.cpp).  The path names folders with '/' and matches
// long or short names without regard to case.  Returns an empty string on
// success, otherwise a message.
std::string nextpi_fat32_read(util::random_read &image, u64 offset, std::string_view path, std::vector<u8> &data);

// Whether that file exists
bool nextpi_fat32_exists(util::random_read &image, u64 offset, std::string_view path);

DECLARE_DEVICE_TYPE(NEXTPI, device_rs232_port_interface)

#endif // !defined(_WIN32)

#endif // MAME_BUS_RS232_NEXTPI_H
