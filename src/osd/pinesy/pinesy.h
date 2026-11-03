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

typedef struct pinesy_host
{
	// NSView that shows the first screen. MAME renders at the view's
	// backing size and sets the frame as the contents of its layer.
	// The host keeps the view alive for the whole run. May be null:
	// then MAME renders at its own size and only calls frame.
	void *view;

	pinesy_frame_fn frame;
	void *context;
} pinesy_host;

// Runs MAME with a command line, as `mame` would get it without the
// program name: for example { "tbblue", "-pi", "nextpi.img" }.
// Main thread only. Returns when MAME exits, with MAME's exit code.
// host may be null.
int pinesy_run(int argc, const char *const argv[], const pinesy_host *host);

// Asks the running machine to exit. Returns 1 if a machine is running.
int pinesy_request_exit(void);

// Keyboard input. keycode is a macOS virtual key code (NSEvent
// keyCode, kVK_* in Carbon). Modifier keys are sent as key events too.
void pinesy_key(int keycode, int down);

// Typed characters, for text entry in MAME's own menus.
void pinesy_char(unsigned int ch);

// The machines built into this library. index runs from 0 to
// pinesy_machine_count() - 1. The strings are static.
int pinesy_machine_count(void);
const char *pinesy_machine_name(int index);
const char *pinesy_machine_description(int index);

#ifdef __cplusplus
}
#endif

#endif // MAME_OSD_PINESY_PINESY_H
