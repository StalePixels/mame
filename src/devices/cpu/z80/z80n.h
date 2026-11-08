// license:BSD-3-Clause
// copyright-holders:Andrei I. Holub
#ifndef MAME_CPU_Z80_Z80N_H
#define MAME_CPU_Z80_Z80N_H

#pragma once

#include "z80.h"

class z80n_device : public z80_device
{
public:
	// construction/destruction
	z80n_device(const machine_config &mconfig, const char *tag, device_t *owner, u32 clock);

	auto in_nextreg_cb() { return m_in_nextreg_cb.bind(); }
	auto out_nextreg_cb() { return m_out_nextreg_cb.bind(); }
	auto out_retn_seen_cb() { return m_out_retn_seen_cb.bind(); }

	bool nmi_stackless_r() { return m_stackless; }
	void nmi_stackless_w(bool data);

	// T80N debug interface (FPGA cpu/readme.txt): one byte of CPU state by
	// register id 0x00-0x1f.  debug_reg_r only reads, so another thread may
	// call it; it then sees the state at some point of the running
	// instruction.  debug_reg_w is for the emulation thread only.
	u8 debug_reg_r(u8 reg);
	void debug_reg_w(u8 reg, u8 data);

protected:
	virtual void device_start() override ATTR_COLD;
	virtual void device_reset() override ATTR_COLD;

	// device_execute_interface implementation
	virtual void execute_run() override;

	devcb_write8 m_out_retn_seen_cb;
	devcb_read8 m_in_nextreg_cb;
	devcb_write8 m_out_nextreg_cb;

	virtual std::unique_ptr<util::disasm_interface> create_disassembler() override;

private:
	bool m_stackless;
	bool m_stackless_retn_en;

};

DECLARE_DEVICE_TYPE(Z80N, z80n_device)

#endif // MAME_CPU_Z80_Z80N_H
