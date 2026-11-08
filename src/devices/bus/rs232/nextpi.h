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


// The Pi's SPI0 master on chip select 0, which RS-232 lacks; a host finds
// it on the slot card.  The Pi is the master and clocks the bytes at its
// own rate, so the handlers run on the Pi's thread, not the emulation
// thread, at any time while the machine exists.  They must not wait for
// the emulation thread, and must share state with it only in ways that
// are safe across threads.  The select handler is called when chip
// select 0 changes (1 = active).  The transfer handler gets the bytes
// the Pi sends on MOSI, in order, and fills in the bytes it receives on
// MISO at the same time; each MISO byte may depend on the MOSI bytes
// before it.  With no handlers set, MISO reads 0xff.
class device_pi_spi_interface : public device_interface
{
public:
	using select_delegate = named_delegate<void (int state)>;
	using transfer_delegate = named_delegate<void (u8 const *mosi, u8 *miso, u32 length)>;

	void set_spi_handlers(select_delegate select, transfer_delegate transfer) { m_spi_select = select; m_spi_transfer = transfer; }

protected:
	device_pi_spi_interface(machine_config const &mconfig, device_t &device) : device_interface(device, "pi_spi") { }

	select_delegate m_spi_select;
	transfer_delegate m_spi_transfer;
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
