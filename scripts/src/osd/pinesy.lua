-- license:BSD-3-Clause
-- copyright-holders:D. Rimron-Soutter

---------------------------------------------------------------------------
--
--   pinesy.lua
--
--   Rules for building MAME as a library for a native macOS host
--
--   The pinesy OSD is the Cocoa OSD (src/osd/mac) with its window
--   layer replaced by src/osd/pinesy/hostview.mm, which draws on a
--   view the host owns. The result is libmame_pinesy.dylib, with
--   the C interface in src/osd/pinesy/pinesy.h.
--
---------------------------------------------------------------------------

dofile("modules.lua")

-- the host owns the view, so there is no OpenGL window for the
-- OpenGL renderer to draw in; frames are software-rendered
_OPTIONS["NO_OPENGL"] = "1"


function maintargetosdoptions(_target,_subtarget)
	osdmodulestargetconf()

	kind "SharedLib"
	targetname "mame_pinesy"
	targetextension ".dylib"
	linkoptions {
		"-install_name @rpath/libmame_pinesy.dylib",
	}

	-- nothing inside MAME calls the C interface, so name each function
	-- to keep the linker from leaving pinesymain.o out of the archive
	for _, fn in ipairs({ "run", "request_exit", "key", "char", "list_machines", "describe" }) do
		linkoptions {
			"-Wl,-u,_pinesy_" .. fn,
		}
	end

	configuration { }
end

BASE_TARGETOS       = "unix"

links {
	"Cocoa.framework",
	"IOKit.framework"
}
linkoptions {
	"-framework QuartzCore",
	"-framework GameController",
	"-weak_framework Metal",
}

project ("qtdbg_" .. _OPTIONS["osd"])
	uuid (os.uuid("qtdbg_" .. _OPTIONS["osd"]))
	kind (LIBTYPE)

	dofile("pinesy_cfg.lua")
	includedirs {
		MAME_DIR .. "src/emu",
		MAME_DIR .. "src/devices", -- accessing imagedev from debugger
		MAME_DIR .. "src/osd",
		MAME_DIR .. "src/lib",
		MAME_DIR .. "src/lib/util",
		MAME_DIR .. "src/osd/modules/render",
		MAME_DIR .. "3rdparty",
	}

	qtdebuggerbuild()

project ("osd_" .. _OPTIONS["osd"])
	uuid (os.uuid("osd_" .. _OPTIONS["osd"]))
	kind (LIBTYPE)

	dofile("pinesy_cfg.lua")
	osdmodulesbuild()

	includedirs {
		MAME_DIR .. "src/emu",
		MAME_DIR .. "src/devices", -- accessing imagedev from debugger
		MAME_DIR .. "src/osd",
		MAME_DIR .. "src/lib",
		MAME_DIR .. "src/lib/util",
		MAME_DIR .. "src/osd/modules/file",
		MAME_DIR .. "src/osd/modules/render",
		MAME_DIR .. "3rdparty",
		MAME_DIR .. "src/osd/mac",
		MAME_DIR .. "src/osd/pinesy",
	}

	files {
		MAME_DIR .. "src/osd/modules/debugger/debugosx.mm",
		MAME_DIR .. "src/osd/modules/debugger/osx/breakpointsview.mm",
		MAME_DIR .. "src/osd/modules/debugger/osx/breakpointsview.h",
		MAME_DIR .. "src/osd/modules/debugger/osx/consoleview.mm",
		MAME_DIR .. "src/osd/modules/debugger/osx/consoleview.h",
		MAME_DIR .. "src/osd/modules/debugger/osx/debugcommandhistory.mm",
		MAME_DIR .. "src/osd/modules/debugger/osx/debugcommandhistory.h",
		MAME_DIR .. "src/osd/modules/debugger/osx/debugconsole.mm",
		MAME_DIR .. "src/osd/modules/debugger/osx/debugconsole.h",
		MAME_DIR .. "src/osd/modules/debugger/osx/debugview.mm",
		MAME_DIR .. "src/osd/modules/debugger/osx/debugview.h",
		MAME_DIR .. "src/osd/modules/debugger/osx/debugwindowhandler.mm",
		MAME_DIR .. "src/osd/modules/debugger/osx/debugwindowhandler.h",
		MAME_DIR .. "src/osd/modules/debugger/osx/deviceinfoviewer.mm",
		MAME_DIR .. "src/osd/modules/debugger/osx/deviceinfoviewer.h",
		MAME_DIR .. "src/osd/modules/debugger/osx/devicesviewer.mm",
		MAME_DIR .. "src/osd/modules/debugger/osx/devicesviewer.h",
		MAME_DIR .. "src/osd/modules/debugger/osx/disassemblyview.mm",
		MAME_DIR .. "src/osd/modules/debugger/osx/disassemblyviewer.mm",
		MAME_DIR .. "src/osd/modules/debugger/osx/disassemblyviewer.h",
		MAME_DIR .. "src/osd/modules/debugger/osx/errorlogview.mm",
		MAME_DIR .. "src/osd/modules/debugger/osx/errorlogview.h",
		MAME_DIR .. "src/osd/modules/debugger/osx/disassemblyview.h",
		MAME_DIR .. "src/osd/modules/debugger/osx/errorlogviewer.mm",
		MAME_DIR .. "src/osd/modules/debugger/osx/errorlogviewer.h",
		MAME_DIR .. "src/osd/modules/debugger/osx/exceptionpointsview.mm",
		MAME_DIR .. "src/osd/modules/debugger/osx/exceptionpointsview.h",
		MAME_DIR .. "src/osd/modules/debugger/osx/memoryview.mm",
		MAME_DIR .. "src/osd/modules/debugger/osx/memoryview.h",
		MAME_DIR .. "src/osd/modules/debugger/osx/memoryviewer.mm",
		MAME_DIR .. "src/osd/modules/debugger/osx/memoryviewer.h",
		MAME_DIR .. "src/osd/modules/debugger/osx/pointsviewer.mm",
		MAME_DIR .. "src/osd/modules/debugger/osx/pointsviewer.h",
		MAME_DIR .. "src/osd/modules/debugger/osx/registerpointsview.mm",
		MAME_DIR .. "src/osd/modules/debugger/osx/registerpointsview.h",
		MAME_DIR .. "src/osd/modules/debugger/osx/registersview.mm",
		MAME_DIR .. "src/osd/modules/debugger/osx/registersview.h",
		MAME_DIR .. "src/osd/modules/debugger/osx/watchpointsview.mm",
		MAME_DIR .. "src/osd/modules/debugger/osx/watchpointsview.h",
		MAME_DIR .. "src/osd/modules/debugger/osx/debugosx.h",
	}

	-- the Cocoa OSD, without its own windows and application shell
	files {
		MAME_DIR .. "src/osd/mac/macmain.cpp",
		MAME_DIR .. "src/osd/mac/video.cpp",
		MAME_DIR .. "src/osd/mac/window.cpp",
		MAME_DIR .. "src/osd/mac/window.h",
		MAME_DIR .. "src/osd/modules/input/input_macgame.mm",
		MAME_DIR .. "src/osd/modules/input/input_macjoy.cpp",
		MAME_DIR .. "src/osd/modules/render/drawmacsoft.cpp",
		MAME_DIR .. "src/osd/modules/osdwindow.cpp",
		MAME_DIR .. "src/osd/modules/osdwindow.h",
	}

	files {
		MAME_DIR .. "src/osd/pinesy/pinesy.h",
		MAME_DIR .. "src/osd/pinesy/pinesymain.cpp",
		MAME_DIR .. "src/osd/pinesy/hostview.mm",
		MAME_DIR .. "src/osd/pinesy/pinesyinfo.cpp",
	}

	-- pinesyinfo.cpp reads the ini files as the frontend does
	includedirs {
		MAME_DIR .. "src/frontend/mame",
	}


project ("ocore_" .. _OPTIONS["osd"])
	uuid (os.uuid("ocore_" .. _OPTIONS["osd"]))
	kind (LIBTYPE)

	removeflags {
		"SingleOutputDir",
	}

	dofile("pinesy_cfg.lua")

	includedirs {
		MAME_DIR .. "src/emu",
		MAME_DIR .. "src/osd",
		MAME_DIR .. "src/lib",
		MAME_DIR .. "src/lib/util",
		MAME_DIR .. "src/osd/mac",
		ext_includedir("asio"),
	}

	files {
		MAME_DIR .. "src/osd/asio.cpp",
		MAME_DIR .. "src/osd/asio.h",
		MAME_DIR .. "src/osd/osdcore.cpp",
		MAME_DIR .. "src/osd/osdcore.h",
		MAME_DIR .. "src/osd/osdfile.h",
		MAME_DIR .. "src/osd/strconv.cpp",
		MAME_DIR .. "src/osd/strconv.h",
		MAME_DIR .. "src/osd/osdsync.cpp",
		MAME_DIR .. "src/osd/osdsync.h",
		MAME_DIR .. "src/osd/modules/osdmodule.cpp",
		MAME_DIR .. "src/osd/modules/osdmodule.h",
		MAME_DIR .. "src/osd/modules/lib/osdlib_macosx.cpp",
		MAME_DIR .. "src/osd/modules/lib/osdlib.h",
		MAME_DIR .. "src/osd/modules/file/posixdir.cpp",
		MAME_DIR .. "src/osd/modules/file/posixfile.cpp",
		MAME_DIR .. "src/osd/modules/file/posixfile.h",
		MAME_DIR .. "src/osd/modules/file/posixptty.cpp",
		MAME_DIR .. "src/osd/modules/file/posixsocket.cpp",
	}
