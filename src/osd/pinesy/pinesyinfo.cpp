// license:BSD-3-Clause
// copyright-holders:D. Rimron-Soutter
//============================================================
//
//  pinesyinfo.cpp - machine descriptions for the pinesy host
//
//  pinesy_list_machines() and pinesy_describe() report MAME's
//  own data as JSON: the drivers in the library, and the machine
//  configuration a command line builds. They read it through the
//  same classes -listxml, -listslots and -listmedia use, and
//  start no machine.
//
//============================================================

#include "pinesy.h"

// MAME headers
#include "emu.h"
#include "drivenum.h"
#include "emuopts.h"
#include "main.h"

#include "corestr.h"
#include "romload.h"
#include "softlist_dev.h"

// device headers
#include "imagedev/floppy.h"
#include "machine/ram.h"

// frontend headers
#include "mameopts.h"

// OSD headers
#include "osdmac.h"

#include <algorithm>
#include <cstring>
#include <exception>
#include <memory>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>


namespace {

//============================================================
//  JSON writer
//============================================================

class json_writer
{
public:
	json_writer &begin_object() { value_start(); m_out << '{'; m_first = true; return *this; }
	json_writer &end_object() { m_out << '}'; m_first = false; return *this; }
	json_writer &begin_array() { value_start(); m_out << '['; m_first = true; return *this; }
	json_writer &end_array() { m_out << ']'; m_first = false; return *this; }

	json_writer &key(std::string_view name)
	{
		if (!m_first)
			m_out << ',';
		string(name);
		m_out << ':';
		m_first = true;
		m_after_key = true;
		return *this;
	}

	json_writer &value(std::string_view text) { value_start(); string(text); return *this; }
	json_writer &value(const char *text) { if (text) return value(std::string_view(text)); return null(); }
	json_writer &value(bool flag) { value_start(); m_out << (flag ? "true" : "false"); return *this; }
	json_writer &value(int number) { value_start(); m_out << number; return *this; }
	json_writer &null() { value_start(); m_out << "null"; return *this; }

	// a complete JSON value written by another json_writer
	json_writer &raw(std::string_view text) { value_start(); m_out << text; return *this; }

	// object members ("key":value,...) written by another json_writer
	json_writer &fields(std::string_view text) { if (!m_first) m_out << ','; m_out << text; m_first = false; return *this; }

	std::string str() const { return m_out.str(); }

private:
	void value_start()
	{
		if (!m_after_key && !m_first)
			m_out << ',';
		m_after_key = false;
		m_first = false;
	}

	void string(std::string_view text)
	{
		m_out << '"';
		for (char c : text)
		{
			switch (c)
			{
			case '"': m_out << "\\\""; break;
			case '\\': m_out << "\\\\"; break;
			case '\n': m_out << "\\n"; break;
			case '\r': m_out << "\\r"; break;
			case '\t': m_out << "\\t"; break;
			default:
				if (u8(c) < 0x20)
					m_out << util::string_format("\\u%04x", u8(c));
				else
					m_out << c;
			}
		}
		m_out << '"';
	}

	std::ostringstream m_out;
	bool m_first = true;
	bool m_after_key = false;
};


//============================================================
//  helpers
//============================================================

// the source file as -listxml shows it, relative to src/mame
std::string source_file(std::string_view path)
{
	using namespace std::literals;
	if (auto prefix = path.rfind("src/mame/"sv); prefix != std::string_view::npos)
		path.remove_prefix(prefix + 9);
	else if (auto prefix = path.rfind("/src/"sv); prefix != std::string_view::npos)
		path.remove_prefix(prefix + 5);
	return std::string(path);
}

// a slot's tag as the command line names it: "exp", "exp:intf1:exp"
const char *option_tag(const device_slot_interface &slot)
{
	return slot.device().tag() + 1;
}

// the slot that holds this device, if the device is a slot card
const device_slot_interface *holding_slot(const device_t &device)
{
	const device_t *const owner = device.owner();
	if (!owner)
		return nullptr;
	const device_slot_interface *slot;
	if (!owner->interface(slot))
		return nullptr;
	return (slot->get_card_device() == &device) ? slot : nullptr;
}

// the slot of the nearest card that contains this device; empty for the machine
std::string parent_slot(const device_t &device)
{
	for (const device_t *d = &device; d; d = d->owner())
	{
		const device_slot_interface *const slot = holding_slot(*d);
		if (slot)
			return option_tag(*slot);
	}
	return std::string();
}

// a device that makes sound and holds no media: a sound chip, not a drive
bool is_sound_chip(device_t &device)
{
	bool sound = false;
	for (device_t &d : device_enumerator(device))
	{
		if (dynamic_cast<device_image_interface *>(&d) != nullptr)
			return false;
		if (dynamic_cast<device_sound_interface *>(&d) != nullptr)
			sound = true;
	}
	return sound;
}

// what a card is: memory, sound or port (see slot_kind)
const char *card_kind(device_t &card)
{
	if (dynamic_cast<device_ram_interface *>(&card) != nullptr)
		return "memory";
	if (is_sound_chip(card))
		return "sound";
	return "port";
}

// what a card of this type would be, from a copy made in an empty machine
const char *card_kind(const device_type &type, u32 clock)
{
	try
	{
		emu_options options(emu_options::option_support::GENERAL_ONLY);
		machine_config config(GAME_NAME(___empty), options);
		machine_config::token const tok(config.begin_configuration(config.root_device()));
		device_t *const card = config.device_add("_card", type, clock);
		if (!card->configured())
			card->config_complete();
		return card_kind(*card);
	}
	catch (...)
	{
		// a card that cannot be made on its own is taken for a peripheral
		return "port";
	}
}

// What the slot is to the machine. It depends on the slot, never on the
// card fitted now, so an empty slot keeps its kind:
//   memory  the slot's cards are RAM
//   drive   the slot is a floppy drive connector
//   sound   the slot's cards make sound and hold no media
//   port    anything else: a connector where peripherals plug in
// The slot's default card decides; a slot without one is memory or
// sound only when every card it takes is.
const char *slot_kind(device_slot_interface &slot)
{
	if (dynamic_cast<floppy_connector *>(&slot.device()))
		return "drive";

	if (const device_slot_interface::slot_option *const option = slot.default_option() ? slot.option(slot.default_option()) : nullptr)
		return card_kind(option->devtype(), option->clock());

	const char *kind = nullptr;
	for (auto &option : slot.option_list())
	{
		const char *const this_kind = card_kind(option.second->devtype(), option.second->clock());
		if (!std::strcmp(this_kind, "port") || (kind && std::strcmp(kind, this_kind)))
			return "port";
		kind = this_kind;
	}
	return kind ? kind : "port";
}

void write_biossets(json_writer &json, const device_t &device)
{
	const char *default_name = nullptr;
	for (const tiny_rom_entry *rom = device.rom_region(); rom && !ROMENTRY_ISEND(rom); ++rom)
		if (ROMENTRY_ISDEFAULT_BIOS(rom))
			default_name = rom->name;

	json.begin_array();
	bool first = true;
	for (const romload::system_bios &bios : romload::entries(device.rom_region()).get_system_bioses())
	{
		// without a default named in the ROM definition, MAME uses the first set
		const bool is_default = default_name ? (bios.get_name() == std::string_view(default_name)) : first;
		json.begin_object()
			.key("name").value(bios.get_name())
			.key("description").value(bios.get_description())
			.key("default").value(is_default)
			.end_object();
		first = false;
	}
	json.end_array();
}

const char *driver_status(const game_driver &driver, machine_config &config)
{
	device_t::feature_type unemulated = driver.type.unemulated_features();
	device_t::feature_type imperfect = driver.type.imperfect_features();
	for (device_t &device : device_enumerator(config.root_device()))
	{
		unemulated |= device.type().unemulated_features();
		imperfect |= device.type().imperfect_features();
	}

	// the same rules as the status attribute of -listxml
	const bool not_working = driver.type.emulation_flags() & device_t::flags::NOT_WORKING;
	const bool machine_preliminary = not_working || (driver.flags & machine_flags::MECHANICAL);
	const bool unemulated_preliminary = unemulated & (device_t::feature::PALETTE | device_t::feature::GRAPHICS | device_t::feature::SOUND | device_t::feature::KEYBOARD);
	const bool imperfect_preliminary = (unemulated | imperfect) & device_t::feature::PROTECTION;
	if (machine_preliminary || unemulated_preliminary || imperfect_preliminary)
		return "preliminary";
	if (imperfect)
		return "imperfect";
	return "good";
}

void write_slots(json_writer &json, machine_config &config, emu_options *options)
{
	json.begin_array();
	for (device_slot_interface &slot : slot_interface_enumerator(config.root_device()))
	{
		device_t *const card = slot.get_card_device();
		const char *const tag = option_tag(slot);

		json.begin_object()
			.key("tag").value(tag)
			.key("parent").value(parent_slot(slot.device().owner() ? *slot.device().owner() : slot.device()))
			.key("kind").value(slot_kind(slot))
			.key("type").value(slot.device().type().shortname())
			.key("typedescription").value(slot.device().type().fullname())
			.key("fixed").value(slot.fixed())
			.key("default").value(slot.default_option() ? slot.default_option() : "")
			.key("current").value(card ? card->basetag() : std::string_view())
			.key("currentdescription").value(card ? card->type().fullname() : "");

		std::vector<const device_slot_interface::slot_option *> choices;
		for (auto &option : slot.option_list())
			if (option.second->selectable())
				choices.push_back(option.second.get());
		std::sort(choices.begin(), choices.end(),
				[] (const device_slot_interface::slot_option *a, const device_slot_interface::slot_option *b)
				{ return std::strcmp(a->devtype().fullname(), b->devtype().fullname()) < 0; });

		json.key("options").begin_array();
		for (const device_slot_interface::slot_option *option : choices)
			json.begin_object()
				.key("name").value(option->name())
				.key("description").value(option->devtype().fullname())
				.end_object();
		json.end_array();

		if (card)
		{
			json.key("biossets");
			write_biossets(json, *card);
			const ::slot_option *const value = options ? options->find_slot_option(tag) : nullptr;
			json.key("bios").value(value ? value->bios() : std::string());
		}
		json.end_object();
	}
	json.end_array();
}

// The device a media device is a part of, such as the SD card slot that
// holds a hard disk image; empty when the media device stands alone: a
// cassette, a drive fitted in a connector, or a drive on a fitted card.
std::string part_of(device_t &device)
{
	device_t *const owner = device.owner();
	if (!owner || !owner->owner())
		return std::string();
	device_slot_interface *slot;
	if (owner->interface(slot) || holding_slot(*owner))
		return std::string();
	return owner->type().fullname();
}

void write_media(json_writer &json, machine_config &config, emu_options &options)
{
	json.begin_array();
	for (device_image_interface &image : image_interface_enumerator(config.root_device()))
	{
		if (!image.user_loadable())
			continue;
		// emu_options keeps image options by instance name
		const std::string &instance = image.instance_name();
		json.begin_object()
			.key("name").value(image.brief_instance_name())
			.key("instance").value(image.instance_name())
			.key("brief").value(image.brief_instance_name())
			.key("typename").value(image.image_type_name())
			.key("owner").begin_object()
				.key("tag").value(image.device().tag() + 1)
				.key("description").value(image.device().type().fullname())
				.key("slot").value(parent_slot(image.device()))
				.key("partof").value(part_of(image.device()))
				.end_object()
			.key("extensions").value(image.file_extensions())
			.key("interface").value(image.image_interface() ? image.image_interface() : "")
			.key("mandatory").value(image.must_be_loaded())
			.key("current").value(options.has_image_option(instance) ? options.image_option(instance).value() : std::string())
			.end_object();
	}
	json.end_array();
}

void deliver(const json_writer &json, pinesy_text_fn fn, void *context)
{
	if (fn)
	{
		const std::string text = json.str();
		fn(context, text.c_str());
	}
}

} // anonymous namespace


//============================================================
//  pinesy_list_machines
//============================================================

extern "C" int pinesy_list_machines(pinesy_text_fn fn, void *context)
{
	emu_options options(emu_options::option_support::GENERAL_ONLY);

	json_writer json;
	json.begin_array();
	for (int i = 0; i < driver_list::total(); i++)
	{
		const game_driver &driver = driver_list::driver(i);

		// skip the placeholder driver MAME runs when no machine is given
		if (&driver == &GAME_NAME(___empty))
			continue;

		// each driver is written on its own, so one that fails is left out
		json_writer entry;
		try
		{
			machine_config config(driver, options);

			const int clone_of = driver_list::find(driver.parent);
			const bool has_parent = (clone_of != -1) && !(driver_list::driver(clone_of).flags & machine_flags::IS_BIOS_ROOT);

			entry.begin_object()
				.key("name").value(driver.name)
				.key("description").value(driver.type.fullname())
				.key("manufacturer").value(driver.manufacturer)
				.key("year").value(driver.year)
				.key("cloneof");
			if (has_parent)
				entry.value(driver_list::driver(clone_of).name);
			else
				entry.null();
			entry.key("sourcefile").value(source_file(driver.type.source()))
				.key("unofficial").value(bool(driver.flags & machine_flags::UNOFFICIAL))
				.key("status").value(driver_status(driver, config));

			entry.key("biossets");
			write_biossets(entry, config.root_device());

			// the root device's type is the driver itself, so it is left out
			std::set<std::string> devices;
			for (device_t &device : device_enumerator(config.root_device()))
				if (device.owner())
					devices.insert(device.type().shortname());
			entry.key("devices").begin_array();
			for (const std::string &name : devices)
				entry.value(name);
			entry.end_array();

			entry.key("slots");
			write_slots(entry, config, nullptr);

			entry.end_object();
		}
		catch (...)
		{
			continue;
		}
		json.raw(entry.str());
	}
	json.end_array();

	deliver(json, fn, context);
	return 0;
}


//============================================================
//  pinesy_describe
//============================================================

extern "C" int pinesy_describe(int argc, const char *const argv[], pinesy_text_fn fn, void *context)
{
	std::vector<std::string> args;
	args.emplace_back("pinesy");
	for (int i = 0; i < argc; i++)
		args.emplace_back(argv[i]);

	mac_options options;
	std::string errors;
	std::string warnings;
	int result = EMU_ERR_NONE;
	const game_driver *system = nullptr;

	try
	{
		mame_options::populate_hashpath_from_args_and_inis(options, args);
		try
		{
			options.parse_command_line(args, OPTION_PRIORITY_CMDLINE);
		}
		catch (options_warning_exception &ex)
		{
			warnings += ex.message();
		}

		if (options.read_config())
		{
			std::ostringstream ini_errors;
			mame_options::parse_standard_inis(options, ini_errors);
		}

		system = mame_options::system(options);
		if (!system)
		{
			result = EMU_ERR_NO_SUCH_SYSTEM;
			errors += util::string_format("Unknown system '%s'", options.attempted_system_name().empty() ? options.system_name() : options.attempted_system_name());
		}
	}
	catch (options_exception &ex)
	{
		result = EMU_ERR_INVALID_CONFIG;
		errors += ex.message();
	}
	catch (emu_fatalerror &ex)
	{
		result = ex.exitcode() ? ex.exitcode() : EMU_ERR_INVALID_CONFIG;
		errors += ex.what();
	}
	catch (std::exception &ex)
	{
		result = EMU_ERR_FATALERROR;
		errors += ex.what();
	}
	catch (...)
	{
		result = EMU_ERR_FATALERROR;
		errors += "The emulator could not read this command line.";
	}

	std::unique_ptr<machine_config> config;
	if (system && (result == EMU_ERR_NONE))
	{
		try
		{
			config = std::make_unique<machine_config>(*system, options);
		}
		catch (emu_fatalerror &ex)
		{
			result = ex.exitcode() ? ex.exitcode() : EMU_ERR_INVALID_CONFIG;
			errors += ex.what();
		}
		catch (std::exception &ex)
		{
			result = EMU_ERR_FATALERROR;
			errors += ex.what();
		}
		catch (...)
		{
			result = EMU_ERR_FATALERROR;
			errors += "The emulator could not build this machine.";
		}
	}

	// the machine is written on its own, so a failure leaves only the errors
	json_writer machine;
	if (config)
	{
		try
		{
			machine.key("driver").value(system->name)
				.key("bios").value(options.bios())
				.key("biossets");
			write_biossets(machine, config->root_device());
			machine.key("slots");
			write_slots(machine, *config, &options);
			machine.key("media");
			write_media(machine, *config, options);
		}
		catch (std::exception &ex)
		{
			machine = json_writer();
			result = EMU_ERR_FATALERROR;
			errors += ex.what();
		}
		catch (...)
		{
			machine = json_writer();
			result = EMU_ERR_FATALERROR;
			errors += "The emulator could not describe this machine.";
		}
	}

	json_writer json;
	json.begin_object();
	if (const std::string fields = machine.str(); !fields.empty())
		json.fields(fields);
	json.key("errors").value(strtrimspace(errors))
		.key("warnings").value(strtrimspace(warnings));
	json.end_object();

	deliver(json, fn, context);
	return result;
}
