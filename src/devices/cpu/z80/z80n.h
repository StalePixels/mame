// license:BSD-3-Clause
// copyright-holders:Andrei I. Holub
#ifndef MAME_CPU_Z80_Z80N_H
#define MAME_CPU_Z80_Z80N_H

#pragma once

#include "z80.h"
#include "z80ndasm.h"

class z80n_device : public z80_device, public z80n_disassembler::config
{
public:
	// construction/destruction
	z80n_device(const machine_config &mconfig, const char *tag, device_t *owner, u32 clock);

	auto in_nextreg_cb() { return m_in_nextreg_cb.bind(); }
	auto out_nextreg_cb() { return m_out_nextreg_cb.bind(); }
	auto out_retn_seen_cb() { return m_out_retn_seen_cb.bind(); }

	bool nmi_stackless_r() { return m_stackless; }
	void nmi_stackless_w(bool data);

	// Core 3.03.03 changed the Z80N: it adds NEXTREG A,n (ED C7)
	void core_30303_w(bool data) { m_core_30303 = data; }

	// Turbo mode (core 3.03.03, nextreg $07 bit 2 at 28MHz) leaves out most
	// of the internal cycles that standard Z80 timing adds. Memory, I/O and
	// nextreg cycles keep their length. Only the opcodes in TURBO_OPCODES
	// (z80n.cpp) get turbo timing.
	void turbo_w(bool data) { m_turbo = data; }

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

	// z80n_disassembler::config implementation
	virtual bool core_30303() const override { return m_core_30303; }

private:
	bool m_stackless;
	bool m_stackless_retn_en;
	bool m_core_30303;
	bool m_turbo;
	bool m_turbo_op; // turbo timing for the instruction that runs now

	static bool turbo_opcode(u32 opcode);

};

DECLARE_DEVICE_TYPE(Z80N, z80n_device)

#endif // MAME_CPU_Z80_Z80N_H
