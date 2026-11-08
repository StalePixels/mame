// license:BSD-3-Clause
// copyright-holders:D. Rimron-Soutter
/***************************************************************************

    Raspberry Pi Zero running NextPi, on the Spectrum Next's Pi UART

    The Pi is QEMU's raspi0 machine, linked into this binary and run on a
    thread of its own.  Its PL011 UART (ttyAMA0) is one end of a socket
    pair; this card is the other.  The kernel, initramfs, device tree and
    command line come from the SD image's boot partition, named by its
    config.txt as the Pi firmware reads it for a Pi Zero.

    Like the Pi firmware, the card applies the dtoverlay and dtparam lines
    of config.txt to the base device tree before the kernel starts, with
    Raspberry Pi's dtoverlay library (vendor/rpi-utils in the pinesy
    repo).  The Next is the Pi's HAT and has no ID EEPROM, so no HAT
    overlay is loaded.

    QEMU can only be started once per process.  It keeps running through
    hard resets and stops when the process exits.  Pi state is not part
    of save states.

***************************************************************************/

#include "emu.h"
#include "nextpi.h"

#if !defined(_WIN32)

// QEMU is the qemu submodule next to this MAME tree in the pinesy repo.
#include "../../../../../qemu/include/pinesy/bridge.h"

// The dtoverlay library is the rpi-utils submodule in the pinesy repo.
// libfdt comes with QEMU.
#include <cstdint>
#include <cstdio>
extern "C" {
#include "../../../../../vendor/rpi-utils/dtmerge/dtoverlay.h"
}

#include "multibyte.h"

#include "corefile.h"
#include "ioprocs.h"
#include "strformat.h"

#include <atomic>
#include <cctype>
#include <cerrno>
#include <cstdarg>
#include <cstdlib>
#include <deque>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <vector>

#include <fcntl.h>
#include <sys/socket.h>
#include <unistd.h>


namespace {

constexpr unsigned IN_HIGH_WATER = 4096;

class nextpi_qemu
{
public:
	~nextpi_qemu()
	{
		if (m_thread.joinable())
		{
			if (!m_done)
				pinesy_bridge_request_stop();
			m_thread.join();
		}
		else if (m_qemu_fd >= 0)
		{
			::close(m_qemu_fd);
		}
		if (m_fd >= 0)
			::close(m_fd);
		if (!m_folder.empty())
		{
			std::error_code ec;
			std::filesystem::remove_all(m_folder, ec);
		}
	}

	bool started() const { return m_thread.joinable(); }
	std::string const &image() const { return m_image; }
	int fd() const { return m_fd; }
	std::vector<std::string> const &args() const { return m_args; }

	std::string start(std::string const &image, std::vector<u8> const &kernel, std::vector<u8> const &initrd, std::vector<u8> const &dtb, std::vector<u8> const &cmdline);

private:
	std::thread m_thread;
	std::atomic<bool> m_done = false;
	std::string m_image;
	std::filesystem::path m_folder;
	std::vector<std::string> m_args;
	std::vector<char *> m_argv;
	int m_fd = -1;
	int m_qemu_fd = -1;
};

nextpi_qemu s_qemu;


bool write_file(std::filesystem::path const &path, std::vector<u8> const &data)
{
	std::ofstream file(path, std::ios::binary | std::ios::trunc);
	file.write(reinterpret_cast<char const *>(data.data()), data.size());
	return bool(file);
}


// One dtoverlay line and the dtparam lines after it, in file order.  An
// empty overlay name holds parameters for the base device tree: those
// before the first dtoverlay line, and those after "dtoverlay=".
struct nextpi_dt_step
{
	std::string overlay;
	std::vector<std::string> params;
};

// Boot file names and device tree settings from config.txt, as the Pi
// firmware reads them for a Pi Zero.  Only the [pi0] and [all] sections
// apply; [none] and every other filter switch the lines after them off.
// Keys the card has no use for are skipped.  An empty dtb means
// "device_tree=": no device tree.
struct nextpi_boot_names
{
	std::string kernel = "kernel.img";
	std::string initrd;
	std::string dtb = "bcm2708-rpi-zero.dtb";
	std::string cmdline = "cmdline.txt";
	std::string os_prefix;
	std::string overlay_prefix = "overlays/";
	std::vector<nextpi_dt_step> dt_steps;
	bool dtdebug = false;
};

std::string_view trim(std::string_view text)
{
	auto const first = text.find_first_not_of(" \t\r\n");
	if (first == std::string_view::npos)
		return std::string_view();
	auto const last = text.find_last_not_of(" \t\r\n");
	return text.substr(first, last - first + 1);
}

std::string lower(std::string_view text)
{
	std::string result(text);
	for (char &c : result)
		c = char(std::tolower(u8(c)));
	return result;
}

std::vector<std::string> split_list(std::string_view text)
{
	std::vector<std::string> result;
	while (true)
	{
		size_t const comma = text.find(',');
		std::string_view const item = trim(text.substr(0, comma));
		if (!item.empty())
			result.emplace_back(item);
		if (comma == std::string_view::npos)
			return result;
		text.remove_prefix(comma + 1);
	}
}

std::string read_config(util::random_read &image, u64 offset, std::string_view name, unsigned depth, nextpi_boot_names &names)
{
	if (depth > 8)
		return util::string_format("%s: too many nested include lines", name);

	std::vector<u8> data;
	std::string const msg = nextpi_fat32_read(image, offset, name, data);
	if (!msg.empty())
		return depth ? msg : std::string(); // with no config.txt the firmware uses its defaults

	std::string_view const text(reinterpret_cast<char const *>(data.data()), data.size());
	bool active = true;
	size_t start = 0;
	while (start < text.length())
	{
		size_t end = text.find('\n', start);
		if (end == std::string_view::npos)
			end = text.length();
		std::string_view line = text.substr(start, end - start);
		start = end + 1;

		line = trim(line.substr(0, line.find('#')));
		if (line.empty())
			continue;

		if (line.front() == '[')
		{
			std::string const filter = lower(trim(line.substr(1, line.find(']') - 1)));
			active = (filter == "all") || (filter == "pi0");
			continue;
		}
		if (!active)
			continue;

		// key=value, except include and initramfs, which take a space
		size_t const split = line.find_first_of("= \t");
		std::string const key = lower(line.substr(0, split));
		std::string_view const value = (split == std::string_view::npos) ? std::string_view() : trim(line.substr(split + 1));

		if (key == "include")
		{
			std::string const err = read_config(image, offset, value, depth + 1, names);
			if (!err.empty())
				return err;
		}
		else if (key == "initramfs")
		{
			// the load address (or followkernel) after the name means nothing to QEMU
			names.initrd = value.substr(0, value.find_first_of(" \t"));
		}
		else if ((key == "dtoverlay") || (key == "device_tree_overlay"))
		{
			// "dtoverlay=" ends the overlay scope: later parameters go to the base
			size_t const comma = value.find(',');
			nextpi_dt_step &step = names.dt_steps.emplace_back();
			step.overlay = trim(value.substr(0, comma));
			if (comma != std::string_view::npos)
				step.params = split_list(value.substr(comma + 1));
		}
		else if ((key == "dtparam") || (key == "device_tree_param"))
		{
			if (names.dt_steps.empty())
				names.dt_steps.emplace_back();
			for (std::string &param : split_list(value))
				names.dt_steps.back().params.emplace_back(std::move(param));
		}
		else if (key == "device_tree")
		{
			names.dtb = value;
		}
		else if (key == "os_prefix")
		{
			names.os_prefix = value;
		}
		else if (key == "overlay_prefix")
		{
			names.overlay_prefix = value;
		}
		else if (key == "dtdebug")
		{
			names.dtdebug = std::atoi(std::string(value).c_str()) != 0;
		}
		else if (!value.empty())
		{
			if (key == "kernel")
				names.kernel = value;
			else if (key == "cmdline")
				names.cmdline = value;
		}
	}
	return std::string();
}


// A file named in config.txt: os_prefix comes first, unless the name
// starts with '/'
std::string os_file(std::string const &prefix, std::string const &name)
{
	return (!name.empty() && (name.front() == '/')) ? name : (prefix + name);
}


// Room for the device trees to grow as overlays and parameters change them
constexpr int DT_BASE_PADDING = 1 << 20;
constexpr int DT_OVERLAY_PADDING = 64 << 10;

// The card loading an image, for messages from the dtoverlay library
device_t const *s_dt_log_device = nullptr;

void dt_log(dtoverlay_logging_type_t type, char const *fmt, va_list args)
{
	char text[512];
	std::vsnprintf(text, sizeof(text), fmt, args);
	if (s_dt_log_device)
		s_dt_log_device->logerror("dtoverlay %s: %s\n", (type == DTOVERLAY_ERROR) ? "error" : (type == DTOVERLAY_WARN) ? "warning" : "debug", text);
}

DTBLOB_T *dt_load(std::vector<u8> &data, int padding)
{
	if (data.empty())
		return nullptr;
	FILE *const fp = fmemopen(data.data(), data.size(), "rb");
	if (!fp)
		return nullptr;
	return dtoverlay_load_dtb_from_fp(fp, DTOVERLAY_PADDING(padding)); // closes fp
}

// A parameter goes to the overlay in scope if it has one by that name,
// otherwise to the base.  With no value it means "on".
void dt_apply_param(device_t const &dev, DTBLOB_T *base, DTBLOB_T *overlay, std::string_view param)
{
	size_t const equals = param.find('=');
	std::string const name(trim(param.substr(0, equals)));
	std::string const value = (equals == std::string_view::npos) ? std::string("on") : std::string(trim(param.substr(equals + 1)));

	DTBLOB_T *target = overlay;
	int length = 0;
	char const *data = overlay ? dtoverlay_find_override(overlay, name.c_str(), &length) : nullptr;
	if (!data)
	{
		target = base;
		data = dtoverlay_find_override(base, name.c_str(), &length);
	}
	if (!data)
		dev.logerror("device tree parameter %s is unknown, skipped\n", name);
	else if (dtoverlay_apply_override(target, name.c_str(), data, length, value.c_str()))
		dev.logerror("device tree parameter %s=%s cannot be applied, skipped\n", name, value);
}

// Apply the dtoverlay and dtparam lines to the base device tree, in file
// order, as the Pi firmware does.  Missing overlays, overlays that do not
// resolve against the base and bad parameters are logged and skipped.  A
// base device tree that cannot be loaded, or an overlay that cannot be
// merged, makes the firmware start the kernel with no device tree, which
// Linux cannot boot from: here it is an error.
std::string dt_merge(device_t const &dev, util::random_read &image, u64 offset, nextpi_boot_names const &names, std::string const &dtb_name, std::string const &overlays, std::vector<u8> &dtb)
{
	struct log_scope
	{
		log_scope(device_t const &dev) { s_dt_log_device = &dev; }
		~log_scope() { s_dt_log_device = nullptr; }
	} const scope(dev);
	dtoverlay_set_logging_func(dt_log);
	dtoverlay_enable_debug(names.dtdebug ? 1 : 0);

	DTBLOB_T *const base = dt_load(dtb, DT_BASE_PADDING);
	if (!base)
		return util::string_format("%s is not a valid device tree", dtb_name);
	if (names.dt_steps.empty())
	{
		// nothing to change: the kernel gets the file as it is
		dtoverlay_free_dtb(base);
		return std::string();
	}

	// the overlay map is read from the overlays folder, for the platform
	// named by the base device tree
	int compatible_length = 0;
	char const *const compatible = static_cast<char const *>(dtoverlay_get_property(base, dtoverlay_find_node(base, "/", 1), "compatible", &compatible_length));
	std::vector<u8> map;
	FILE *map_fp = nullptr;
	if (compatible && nextpi_fat32_read(image, offset, overlays.substr(0, overlays.rfind('/') + 1) + "overlay_map.dtb", map).empty() && !map.empty())
		map_fp = fmemopen(map.data(), map.size(), "rb");
	dtoverlay_init_map_from_fp(map_fp, compatible, compatible_length);

	std::string err;
	for (nextpi_dt_step const &step : names.dt_steps)
	{
		if (step.overlay.empty())
		{
			for (std::string const &param : step.params)
				dt_apply_param(dev, base, nullptr, param);
			continue;
		}

		// the map can name another overlay, with parameters after it
		char const *const mapped = dtoverlay_remap_overlay(step.overlay.c_str());
		if (!mapped)
		{
			dev.logerror("overlay %s skipped\n", step.overlay);
			continue;
		}
		std::string_view const entry(mapped);
		size_t const comma = entry.find(',');
		std::string const file = overlays + std::string(entry.substr(0, comma)) + ".dtbo";

		std::vector<u8> data;
		std::string const msg = nextpi_fat32_read(image, offset, file, data);
		if (!msg.empty())
		{
			dev.logerror("%s, overlay skipped\n", msg);
			continue;
		}
		DTBLOB_T *const overlay = dt_load(data, DT_OVERLAY_PADDING);
		if (!overlay)
		{
			dev.logerror("%s is not a valid device tree overlay, skipped\n", file);
			continue;
		}
		// an overlay that refers to labels the base does not have is
		// skipped; the base is not changed yet
		if (dtoverlay_fixup_overlay(base, overlay))
		{
			dtoverlay_free_dtb(overlay);
			dev.logerror("%s does not fit this device tree, skipped\n", file);
			continue;
		}

		std::vector<std::string> params;
		if (comma != std::string_view::npos)
			params = split_list(entry.substr(comma + 1));
		params.insert(params.end(), step.params.begin(), step.params.end());
		for (std::string const &param : params)
			dt_apply_param(dev, base, overlay, param);

		int const merged = dtoverlay_merge_overlay(base, overlay);
		dtoverlay_free_dtb(overlay);
		if (merged)
		{
			err = util::string_format("cannot merge %s into the device tree", file);
			break;
		}
	}

	if (err.empty())
	{
		dtoverlay_pack_dtb(base);
		u8 const *const fdt = static_cast<u8 const *>(base->fdt);
		dtb.assign(fdt, fdt + dtoverlay_dtb_totalsize(base));
		u8 const *const trailer = static_cast<u8 const *>(dtoverlay_dtb_trailer(base));
		dtb.insert(dtb.end(), trailer, trailer + dtoverlay_dtb_trailer_len(base));
	}
	dtoverlay_free_dtb(base);
	return err;
}


std::string nextpi_qemu::start(std::string const &image, std::vector<u8> const &kernel, std::vector<u8> const &initrd, std::vector<u8> const &dtb, std::vector<u8> const &cmdline)
{
	std::error_code ec;
	std::filesystem::path const folder = std::filesystem::temp_directory_path(ec) / util::string_format("pinesy-pi-%d", int(::getpid()));
	if (ec || (!std::filesystem::create_directories(folder, ec) && ec))
		return "cannot create the temporary folder for the Pi boot files";
	m_folder = folder;

	if (!write_file(folder / "kernel.img", kernel) || !write_file(folder / "cmdline.txt", cmdline))
		return util::string_format("cannot write the Pi boot files to %s", folder.string());
	if (!dtb.empty() && !write_file(folder / "bcm2708-rpi-zero.dtb", dtb))
		return util::string_format("cannot write the Pi boot files to %s", folder.string());
	if (!initrd.empty() && !write_file(folder / "initrd.img", initrd))
		return util::string_format("cannot write the Pi boot files to %s", folder.string());

	std::string append(cmdline.begin(), cmdline.end());
	auto const first = append.find_first_not_of(" \t\r\n");
	auto const last = append.find_last_not_of(" \t\r\n");
	append = (first == std::string::npos) ? std::string() : append.substr(first, last - first + 1);

	int fds[2];
	if (::socketpair(AF_UNIX, SOCK_STREAM, 0, fds) < 0)
		return "cannot create the socket pair for the Pi UART";
	m_fd = fds[0];
	m_qemu_fd = fds[1];
	::fcntl(m_fd, F_SETFL, ::fcntl(m_fd, F_GETFL) | O_NONBLOCK);
#if defined(SO_NOSIGPIPE)
	int const one = 1;
	::setsockopt(m_fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof(one));
#endif

	// -drive takes key=value options, where a comma is written twice
	std::string drive_file;
	for (char c : image)
	{
		drive_file += c;
		if (c == ',')
			drive_file += c;
	}

	m_image = image;
	m_args = {
			"pinesy-qemu",
			"-M", "raspi0",
			"-nodefaults",
			"-display", "none",
			"-kernel", (folder / "kernel.img").string(),
			"-append", append,
			"-drive", "if=sd,format=raw,file=" + drive_file,
			"-chardev", util::string_format("socket,id=pi0,fd=%d", m_qemu_fd),
			"-serial", "chardev:pi0" };
	if (!initrd.empty())
	{
		m_args.emplace_back("-initrd");
		m_args.emplace_back((folder / "initrd.img").string());
	}
	if (!dtb.empty())
	{
		m_args.emplace_back("-dtb");
		m_args.emplace_back((folder / "bcm2708-rpi-zero.dtb").string());
	}
	for (std::string &arg : m_args)
		m_argv.push_back(arg.data());
	m_argv.push_back(nullptr);

	m_thread = std::thread(
			[this] ()
			{
				pinesy_bridge_run(int(m_argv.size() - 1), m_argv.data());
				m_done = true;
			});
	return std::string();
}


class nextpi_device : public buffered_rs232_device<16>, public device_image_interface
{
public:
	nextpi_device(machine_config const &mconfig, char const *tag, device_t *owner, u32 clock);

	static constexpr flags_type emulation_flags() { return flags::SAVE_UNSUPPORTED; }

	void update_serial(int state);

	// device_image_interface
	virtual std::pair<std::error_condition, std::string> call_load() override;
	virtual void call_unload() override;
	virtual bool is_readable() const noexcept override { return true; }
	virtual bool is_writeable() const noexcept override { return false; }
	virtual bool is_creatable() const noexcept override { return false; }
	virtual bool is_reset_on_load() const noexcept override { return false; }
	virtual bool core_opens_image_file() const noexcept override { return false; }
	virtual char const *file_extensions() const noexcept override { return "img"; }
	virtual char const *image_type_name() const noexcept override { return "pi"; }
	virtual char const *image_brief_type_name() const noexcept override { return "pi"; }

protected:
	virtual ioport_constructor device_input_ports() const override ATTR_COLD;
	virtual void device_start() override ATTR_COLD;
	virtual void device_reset() override ATTR_COLD;

	virtual void tra_complete() override;
	virtual void received_byte(u8 byte) override;

private:
	TIMER_CALLBACK_MEMBER(poll);
	void pump();

	required_ioport m_baud_config;
	emu_timer *m_poll_timer;
	int m_fd;
	std::deque<u8> m_in;
	std::deque<u8> m_out;
};


INPUT_PORTS_START(nextpi)
	PORT_RS232_BAUD("RS232_BAUD", RS232_BAUD_115200, "Baud", nextpi_device, update_serial)
INPUT_PORTS_END


nextpi_device::nextpi_device(machine_config const &mconfig, char const *tag, device_t *owner, u32 clock)
	: buffered_rs232_device<16>(mconfig, NEXTPI, tag, owner, clock)
	, device_image_interface(mconfig, *this)
	, m_baud_config(*this, "RS232_BAUD")
	, m_poll_timer(nullptr)
	, m_fd(-1)
{
}


ioport_constructor nextpi_device::device_input_ports() const
{
	return INPUT_PORTS_NAME(nextpi);
}


void nextpi_device::device_start()
{
	m_poll_timer = timer_alloc(FUNC(nextpi_device::poll), this);
}


void nextpi_device::device_reset()
{
	update_serial(0);

	output_rxd(1);
	output_dcd(0);
	output_dsr(0);
	output_cts(0);

	if (m_fd >= 0)
		m_poll_timer->adjust(attotime::from_msec(1), 0, attotime::from_msec(1));
}


void nextpi_device::update_serial(int state)
{
	set_data_frame(1, 8, PARITY_NONE, STOP_BITS_1);
	set_rate(convert_baud(m_baud_config->read()));
}


std::pair<std::error_condition, std::string> nextpi_device::call_load()
{
	// The core would hash the whole image on every load (image_checkhash)
	util::core_file::ptr image;
	std::error_condition const filerr = util::core_file::open(filename(), OPEN_FLAG_READ, image);
	if (filerr)
		return std::make_pair(filerr, std::string());

	// QEMU's SD card model takes no other sizes
	u64 size = 0;
	image->length(size);
	bool const size_ok = (size <= (u64(2) << 30))
			? (size && !(size & (size - 1)))
			: !(size % (512 * 1024));
	if (!size_ok)
		return std::make_pair(image_error::INVALIDLENGTH, "SD image size must be a power of 2 up to 2 GiB, or a multiple of 512 KiB above 2 GiB");

	std::error_code ec;
	std::string const path = std::filesystem::absolute(filename(), ec).lexically_normal().string();
	if (ec)
		return std::make_pair(image_error::UNSPECIFIED, "cannot find the absolute path of the SD image");

	if (!s_qemu.started())
	{
		u8 mbr[512];
		auto const [err, actual] = util::read_at(*image, 0, mbr, sizeof(mbr));
		if (err || (actual != sizeof(mbr)) || (mbr[510] != 0x55) || (mbr[511] != 0xaa))
			return std::make_pair(image_error::INVALIDIMAGE, "SD image has no MBR");
		u8 const *const part = &mbr[446];
		if ((part[4] != 0x0b) && (part[4] != 0x0c))
			return std::make_pair(image_error::INVALIDIMAGE, "partition 1 of the SD image is not FAT32");
		u64 const offset = u64(get_u32le(&part[8])) * 512;

		nextpi_boot_names names;
		std::string const cfgerr = read_config(*image, offset, "config.txt", 0, names);
		if (!cfgerr.empty())
			return std::make_pair(image_error::INVALIDIMAGE, cfgerr);

		// os_prefix is used only when the kernel and device tree are found
		// with it.  Overlays are shared unless the prefixed overlay folder
		// has a README.
		std::string prefix = names.os_prefix;
		if (!prefix.empty() && (!nextpi_fat32_exists(*image, offset, os_file(prefix, names.kernel)) || (!names.dtb.empty() && !nextpi_fat32_exists(*image, offset, os_file(prefix, names.dtb)))))
		{
			logerror("os_prefix %s ignored: the kernel or device tree is not found with it\n", prefix);
			prefix.clear();
		}
		std::string overlays = names.overlay_prefix;
		if (!prefix.empty() && nextpi_fat32_exists(*image, offset, prefix + names.overlay_prefix + "README"))
			overlays = prefix + names.overlay_prefix;

		std::vector<u8> kernel, initrd, dtb, cmdline;
		for (auto [name, data] : { std::make_pair(&names.kernel, &kernel), std::make_pair(&names.initrd, &initrd), std::make_pair(&names.dtb, &dtb), std::make_pair(&names.cmdline, &cmdline) })
		{
			if (name->empty())
				continue;
			std::string const msg = nextpi_fat32_read(*image, offset, os_file(prefix, *name), *data);
			if (!msg.empty())
				return std::make_pair(image_error::INVALIDIMAGE, msg);
		}

		if (!names.dtb.empty())
		{
			std::string const dterr = dt_merge(*this, *image, offset, names, os_file(prefix, names.dtb), overlays, dtb);
			if (!dterr.empty())
				return std::make_pair(image_error::INVALIDIMAGE, dterr);
		}

		std::string const msg = s_qemu.start(path, kernel, initrd, dtb, cmdline);
		if (!msg.empty())
			return std::make_pair(image_error::UNSPECIFIED, msg);

		std::string line;
		for (std::string const &arg : s_qemu.args())
			line += " " + arg;
		logerror("starting QEMU:%s\n", line);
	}
	else if (s_qemu.image() != path)
	{
		return std::make_pair(image_error::UNSUPPORTED, "restart Pinesy to change the Pi image");
	}

	m_fd = s_qemu.fd();
	m_in.clear();
	m_out.clear();
	m_poll_timer->adjust(attotime::from_msec(1), 0, attotime::from_msec(1));
	return std::make_pair(std::error_condition(), std::string());
}


void nextpi_device::call_unload()
{
	m_fd = -1;
	m_in.clear();
	m_out.clear();
	m_poll_timer->adjust(attotime::never);
}


TIMER_CALLBACK_MEMBER(nextpi_device::poll)
{
	if (m_fd < 0)
		return;

	while (!m_out.empty())
	{
		u8 const byte = m_out.front();
		if (::write(m_fd, &byte, 1) != 1)
			break;
		m_out.pop_front();
	}

	while (m_in.size() < IN_HIGH_WATER)
	{
		u8 buffer[256];
		ssize_t const actual = ::read(m_fd, buffer, sizeof(buffer));
		if (actual <= 0)
			break;
		m_in.insert(m_in.end(), buffer, buffer + actual);
	}

	pump();
}


void nextpi_device::pump()
{
	while (!m_in.empty() && !fifo_full())
	{
		transmit_byte(m_in.front());
		m_in.pop_front();
	}
}


void nextpi_device::tra_complete()
{
	buffered_rs232_device<16>::tra_complete();
	pump();
}


void nextpi_device::received_byte(u8 byte)
{
	if (m_fd < 0)
		return;

	if (m_out.empty() && (::write(m_fd, &byte, 1) == 1))
		return;
	m_out.push_back(byte);
}

} // anonymous namespace


DEFINE_DEVICE_TYPE_PRIVATE(NEXTPI, device_rs232_port_interface, nextpi_device, "nextpi", "Raspberry Pi Zero running NextPi (QEMU)")

#endif // !defined(_WIN32)
