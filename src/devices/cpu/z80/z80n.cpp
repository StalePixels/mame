// license:BSD-3-Clause
// copyright-holders:Andrei I. Holub
/***************************************************************************
    Z80N
***************************************************************************/

#include "emu.h"
#include "z80n.h"
#include "z80ndasm.h"

#include "z80.inc"

#define LOG_INT   (1U << 1) // z80.lst

//#define VERBOSE (LOG_INT)
#include "logmacro.h"


DEFINE_DEVICE_TYPE(Z80N, z80n_device, "z80n", "Z80N")

std::unique_ptr<util::disasm_interface> z80n_device::create_disassembler()
{
	return std::make_unique<z80n_disassembler>();
}

z80n_device::z80n_device(const machine_config &mconfig, const char *tag, device_t *owner, u32 clock)
	: z80_device(mconfig, Z80N, tag, owner, clock)
	, m_out_retn_seen_cb(*this)
	, m_in_nextreg_cb(*this, 0)
	, m_out_nextreg_cb(*this)
{
}

void z80n_device::execute_run()
{
	#include "cpu/z80/z80n.hxx"
}

void z80n_device::nmi_stackless_w(bool data)
{
	if (!data)
		m_stackless_retn_en = false;
	m_stackless = data;
}

// Ids from cpu/t80n-debug/t80n_debug.vhd: 0x00-0x19 are A F B C D E H L,
// the same for the alternate set, IXH IXL IYH IYL SPH SPL PCH PCL I R.  The
// others read IM, Halt_FF, IFF2 and IFF1; only a write to 0x1b changes
// them, and only Halt_FF, IFF2 and IFF1.
u8 z80n_device::debug_reg_r(u8 reg)
{
	switch (reg & 0x1f)
	{
	case 0x00: return m_af.b.h;
	case 0x01: return get_f();
	case 0x02: return m_bc.b.h;
	case 0x03: return m_bc.b.l;
	case 0x04: return m_de.b.h;
	case 0x05: return m_de.b.l;
	case 0x06: return m_hl.b.h;
	case 0x07: return m_hl.b.l;
	case 0x08: return m_af2.b.h;
	case 0x09: return m_af2.b.l;
	case 0x0a: return m_bc2.b.h;
	case 0x0b: return m_bc2.b.l;
	case 0x0c: return m_de2.b.h;
	case 0x0d: return m_de2.b.l;
	case 0x0e: return m_hl2.b.h;
	case 0x0f: return m_hl2.b.l;
	case 0x10: return m_ix.b.h;
	case 0x11: return m_ix.b.l;
	case 0x12: return m_iy.b.h;
	case 0x13: return m_iy.b.l;
	case 0x14: return m_sp.b.h;
	case 0x15: return m_sp.b.l;
	case 0x16: return m_pc.b.h;
	case 0x17: return m_pc.b.l;
	case 0x18: return m_i;
	case 0x19: return (m_r & 0x7f) | (m_r2 & 0x80);
	default: return (BIT(m_im, 0, 2) << 4) | ((m_halt ? 1 : 0) << 2) | ((m_iff2 ? 1 : 0) << 1) | (m_iff1 ? 1 : 0);
	}
}

void z80n_device::debug_reg_w(u8 reg, u8 data)
{
	switch (reg & 0x1f)
	{
	case 0x00: m_af.b.h = data; break;
	case 0x01: set_f(data); break;
	case 0x02: m_bc.b.h = data; break;
	case 0x03: m_bc.b.l = data; break;
	case 0x04: m_de.b.h = data; break;
	case 0x05: m_de.b.l = data; break;
	case 0x06: m_hl.b.h = data; break;
	case 0x07: m_hl.b.l = data; break;
	case 0x08: m_af2.b.h = data; break;
	case 0x09: m_af2.b.l = data; break;
	case 0x0a: m_bc2.b.h = data; break;
	case 0x0b: m_bc2.b.l = data; break;
	case 0x0c: m_de2.b.h = data; break;
	case 0x0d: m_de2.b.l = data; break;
	case 0x0e: m_hl2.b.h = data; break;
	case 0x0f: m_hl2.b.l = data; break;
	case 0x10: m_ix.b.h = data; break;
	case 0x11: m_ix.b.l = data; break;
	case 0x12: m_iy.b.h = data; break;
	case 0x13: m_iy.b.l = data; break;
	case 0x14: m_sp.b.h = data; break;
	case 0x15: m_sp.b.l = data; break;
	case 0x16: m_pc.b.h = data; break;
	case 0x17: m_pc.b.l = data; break;
	case 0x18: m_i = data; break;
	case 0x19: m_r = data & 0x7f; m_r2 = data & 0x80; break;
	case 0x1b:
		if (BIT(data, 2))
			halt();
		else
			leave_halt();
		m_iff2 = BIT(data, 1);
		m_iff1 = BIT(data, 0);
		break;
	default: break;
	}
}

void z80n_device::device_start()
{
	z80_device::device_start();
	save_item(NAME(m_stackless));
	save_item(NAME(m_stackless_retn_en));
}

void z80n_device::device_reset()
{
	z80_device::device_reset();
	m_stackless = false;
	m_stackless_retn_en = false;
}
