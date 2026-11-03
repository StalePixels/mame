// license:BSD-3-Clause
// copyright-holders:D. Rimron-Soutter
//============================================================
//
//  pinesymain.cpp - entry points of the pinesy C interface
//
//  The OSD object is the Cocoa OSD's mac_osd_interface. Only
//  the window layer differs, in hostview.mm.
//
//============================================================

#include "pinesy.h"

// MAME headers
#include "emu.h"
#include "drivenum.h"
#include "main.h"

// OSD headers
#include "osdmac.h"
#include "modules/diagnostics/diagnostics_module.h"

#include <cstdio>
#include <string>
#include <vector>

// in hostview.mm
void pinesy_attach_host(const pinesy_host *host);

extern "C" int MacRequestMachineExit();        // in mac/window.cpp
extern "C" void MacKeyboardEvent(int vk, int down); // in modules/input/input_mac.cpp
extern "C" void MacCharEvent(unsigned int ch);


//============================================================
//  pinesy_run
//============================================================

extern "C" int pinesy_run(int argc, const char *const argv[], const pinesy_host *host)
{
	std::vector<std::string> args;
	args.emplace_back("pinesy");
	for (int i = 0; i < argc; i++)
		args.emplace_back(argv[i]);

	// disable I/O buffering
	setvbuf(stdout, nullptr, _IONBF, 0);
	setvbuf(stderr, nullptr, _IONBF, 0);

	diagnostics_module::get_instance()->init_crash_diagnostics();

	pinesy_attach_host(host);

	mac_options options;
	mac_osd_interface osd(options);
	osd.register_options();

	// the host's view takes a software-rendered frame; -video still overrides
	options.set_default_value(OSDOPTION_VIDEO, "soft");

	const int res = emulator_info::start_frontend(options, osd, args);

	pinesy_attach_host(nullptr);
	return res;
}


//============================================================
//  pinesy_request_exit
//============================================================

extern "C" int pinesy_request_exit(void)
{
	return MacRequestMachineExit();
}


//============================================================
//  keyboard
//============================================================

extern "C" void pinesy_key(int keycode, int down)
{
	MacKeyboardEvent(keycode, down);
}

extern "C" void pinesy_char(unsigned int ch)
{
	MacCharEvent(ch);
}


//============================================================
//  machine list
//============================================================

static const std::vector<int> &machine_indexes()
{
	static const std::vector<int> s_indexes = []
	{
		std::vector<int> result;
		for (int i = 0; i < driver_list::total(); i++)
		{
			// skip the placeholder driver MAME runs when no machine is given
			if (&driver_list::driver(i) != &GAME_NAME(___empty))
				result.push_back(i);
		}
		return result;
	}();
	return s_indexes;
}

extern "C" int pinesy_machine_count(void)
{
	return int(machine_indexes().size());
}

extern "C" const char *pinesy_machine_name(int index)
{
	if ((index < 0) || (index >= pinesy_machine_count()))
		return nullptr;
	return driver_list::driver(machine_indexes()[index]).name;
}

extern "C" const char *pinesy_machine_description(int index)
{
	if ((index < 0) || (index >= pinesy_machine_count()))
		return nullptr;
	return driver_list::driver(machine_indexes()[index]).type.fullname();
}
