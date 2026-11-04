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
#include "main.h"

// OSD headers
#include "osdmac.h"
#include "modules/diagnostics/diagnostics_module.h"

#include <cstdio>
#include <memory>
#include <string>
#include <vector>

// in hostview.mm
void pinesy_attach_host(const pinesy_host *host);


namespace {

// Passes each line of MAME's output to the host, then on to the OSD,
// which prints it as usual.
class host_output : public osd_output
{
public:
	host_output(pinesy_output_fn fn, void *context) : m_fn(fn), m_context(context) { }

	virtual void output_callback(osd_output_channel channel, util::format_argument_pack<char> const &args) override
	{
		chain_output(channel, args);
		const std::string text = util::string_format(args);
		m_fn(m_context, int(channel), text.c_str());
	}

private:
	pinesy_output_fn m_fn;
	void *m_context;
};

// pinesy_run() is running
bool s_running = false;

// an exit was asked for before the machine had a window to take it
bool s_exit_pending = false;

} // anonymous namespace

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

	// the OSD object put itself on the output stack, so this one goes above it
	std::unique_ptr<host_output> output;
	if (host && host->output)
	{
		output = std::make_unique<host_output>(host->output, host->context);
		osd_output::push(output.get());
	}

	s_running = true;
	s_exit_pending = false;
	const int res = emulator_info::start_frontend(options, osd, args);
	s_running = false;
	s_exit_pending = false;

	if (output)
		osd_output::pop(output.get());

	pinesy_attach_host(nullptr);
	return res;
}


//============================================================
//  pinesy_request_exit
//============================================================

extern "C" int pinesy_request_exit(void)
{
	if (!s_running)
		return 0;

	// before the machine has a window, the request waits for one
	if (!MacRequestMachineExit())
		s_exit_pending = true;
	return 1;
}

// Called from the event pump: passes a waiting exit request on once
// the machine can take it.
void pinesy_retry_exit()
{
	if (s_exit_pending && MacRequestMachineExit())
		s_exit_pending = false;
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
