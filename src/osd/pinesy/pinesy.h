// license:BSD-3-Clause
// copyright-holders:D. Rimron-Soutter
//============================================================
//
//  pinesy.h - C interface between MAME and a native macOS host
//
//  The host is a Cocoa application. It owns NSApp, the windows
//  and the menus. MAME runs on the main thread inside
//  pinesy_run() and keeps its own main loop; while it runs it
//  pumps the host's AppKit events, so the host's UI stays live,
//  also while the debugger has the machine stopped.
//
//============================================================

#ifndef MAME_OSD_PINESY_PINESY_H
#define MAME_OSD_PINESY_PINESY_H

#ifdef __cplusplus
extern "C" {
#endif

// Called after each frame is drawn. pixels is BGRX, 4 bytes per
// pixel, top row first, pitch bytes per row. It is valid only
// during the call. May be null.
typedef void (*pinesy_frame_fn)(void *context, const void *pixels, int width, int height, int pitch);

// Receives one complete UTF-8 JSON document. Valid only during the call.
typedef void (*pinesy_text_fn)(void *context, const char *text);

// Receives one line of MAME output. channel is MAME's osd_output_channel:
// 0 error, 1 warning, 2 info, 3 debug, 4 verbose, 5 log.
typedef void (*pinesy_output_fn)(void *context, int channel, const char *text);

// Receives a size for the host's window, in points.
typedef void (*pinesy_size_fn)(void *context, int width, int height);

typedef struct pinesy_host
{
	// NSView that shows the first screen. MAME renders at the view's
	// backing size and sets the frame as the contents of its layer.
	// The host keeps the view alive for the whole run. May be null:
	// then MAME renders at its own size and only calls frame.
	void *view;

	pinesy_frame_fn frame;

	// Receives MAME's output while it runs. May be null. MAME still
	// prints the output as it does without a host.
	pinesy_output_fn output;

	// The shape MAME wants for the view: aspect gets the ratio to keep
	// when the user resizes (0, 0: any shape), size the view's size.
	// MAME calls size with its own window size when it takes the view,
	// and again when the view's shape does not fit the picture. Either
	// may be null.
	pinesy_size_fn aspect;
	pinesy_size_fn size;

	void *context;
} pinesy_host;

// Runs MAME with a command line, as `mame` would get it without the
// program name: for example { "tbblue", "-pi", "nextpi.img" }.
// Main thread only. Returns when MAME exits, with MAME's exit code.
// host may be null.
int pinesy_run(int argc, const char *const argv[], const pinesy_host *host);

// Asks the running machine to exit. Returns 1 while pinesy_run() runs.
// A request made before the machine has started is kept, and the
// machine exits as soon as it has.
int pinesy_request_exit(void);

// Keyboard input. keycode is a macOS virtual key code (NSEvent
// keyCode, kVK_* in Carbon). Modifier keys are sent as key events too.
void pinesy_key(int keycode, int down);

// Typed characters, for text entry in MAME's own menus.
void pinesy_char(unsigned int ch);

// Every driver in the library, as one JSON array. Each driver has
// name, description, manufacturer, year, cloneof (or null),
// sourcefile, unofficial, status (good, imperfect or preliminary,
// as in -listxml), biossets, devices (the sorted device type names
// of the default configuration, without the driver itself) and
// slots (as pinesy_describe gives them, for the default
// configuration). A driver MAME cannot describe is left out.
// Returns 0.
int pinesy_list_machines(pinesy_text_fn fn, void *context);

// The machine a command line describes, as one JSON object. argv is
// a command line as pinesy_run() takes it: a driver, then options.
// MAME parses it as for a run (ini files are read only when the
// command line allows it) and builds the machine configuration
// without starting it. The object has driver, bios, biossets, slots
// and media; errors, with MAME's error text; and warnings, with
// MAME's warning text, which do not stop a run. Both are empty when
// there is none. Returns 0, or MAME's error code.
int pinesy_describe(int argc, const char *const argv[], pinesy_text_fn fn, void *context);

#ifdef __cplusplus
}
#endif

#endif // MAME_OSD_PINESY_PINESY_H
