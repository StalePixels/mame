// license:BSD-3-Clause
// copyright-holders:D. Rimron-Soutter
/***************************************************************************

    Raspberry Pi Zero running NextPi, on the Spectrum Next's Pi UART

    The Pi is QEMU's raspi0 machine, linked into this binary and run on a
    thread of its own.  Its PL011 UART (ttyAMA0) is one end of a socket
    pair; this card is the other.  The kernel, device tree and command
    line come from the SD image's boot partition.

    QEMU can only be started once per process.  It keeps running through
    hard resets and stops when the process exits.  Pi state is not part
    of save states.

***************************************************************************/

#include "emu.h"
#include "nextpi.h"

#if !defined(_WIN32)

// QEMU is the qemu submodule next to this MAME tree in the pinesy repo.
#include "../../../../../qemu/include/mane/mane.h"

#include "multibyte.h"

#include "corefile.h"
#include "ioprocs.h"
#include "strformat.h"

#include <atomic>
#include <cerrno>
#include <deque>
#include <filesystem>
#include <fstream>
#include <string>
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
				mane_qemu_request_stop();
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

	std::string start(std::string const &image, std::vector<u8> const &kernel, std::vector<u8> const &dtb, std::vector<u8> const &cmdline);

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


std::string nextpi_qemu::start(std::string const &image, std::vector<u8> const &kernel, std::vector<u8> const &dtb, std::vector<u8> const &cmdline)
{
	std::error_code ec;
	std::filesystem::path const folder = std::filesystem::temp_directory_path(ec) / util::string_format("mane-pi-%d", int(::getpid()));
	if (ec || (!std::filesystem::create_directories(folder, ec) && ec))
		return "cannot create the temporary folder for the Pi boot files";
	m_folder = folder;

	if (!write_file(folder / "kernel.img", kernel) || !write_file(folder / "bcm2708-rpi-zero.dtb", dtb) || !write_file(folder / "cmdline.txt", cmdline))
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
			"mane-qemu",
			"-M", "raspi0",
			"-nodefaults",
			"-display", "none",
			"-kernel", (folder / "kernel.img").string(),
			"-dtb", (folder / "bcm2708-rpi-zero.dtb").string(),
			"-append", append,
			"-drive", "if=sd,format=raw,file=" + drive_file,
			"-chardev", util::string_format("socket,id=pi0,fd=%d", m_qemu_fd),
			"-serial", "chardev:pi0" };
	for (std::string &arg : m_args)
		m_argv.push_back(arg.data());
	m_argv.push_back(nullptr);

	m_thread = std::thread(
			[this] ()
			{
				mane_qemu_run(int(m_argv.size() - 1), m_argv.data());
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

		std::vector<u8> kernel, dtb, cmdline;
		for (auto [name, data] : { std::make_pair("kernel.img", &kernel), std::make_pair("bcm2708-rpi-zero.dtb", &dtb), std::make_pair("cmdline.txt", &cmdline) })
		{
			std::string const msg = nextpi_fat32_read(*image, offset, name, *data);
			if (!msg.empty())
				return std::make_pair(image_error::INVALIDIMAGE, msg);
		}

		std::string const msg = s_qemu.start(path, kernel, dtb, cmdline);
		if (!msg.empty())
			return std::make_pair(image_error::UNSPECIFIED, msg);

		std::string line;
		for (std::string const &arg : s_qemu.args())
			line += " " + arg;
		logerror("starting QEMU:%s\n", line);
	}
	else if (s_qemu.image() != path)
	{
		return std::make_pair(image_error::UNSUPPORTED, "restart MaNE to change the Pi image");
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
