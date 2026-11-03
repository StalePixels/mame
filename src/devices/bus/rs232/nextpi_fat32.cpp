// license:BSD-3-Clause
// copyright-holders:D. Rimron-Soutter
/***************************************************************************

    Read-only FAT32 reader for the nextpi card

    Reads one file from the root folder of a FAT32 file system, matching
    long or short names without regard to case.  Only what the card needs
    to take the kernel, device tree and command line from the Pi's boot
    partition.

***************************************************************************/

#include "emu.h"
#include "nextpi.h"

#include "multibyte.h"

#include "ioprocs.h"
#include "strformat.h"

#include <cctype>
#include <string>
#include <vector>


namespace {

constexpr u32 FAT_MASK = 0x0fff'ffff;
constexpr u32 FAT_END = 0x0fff'fff8;
constexpr unsigned MAX_CHAIN = 0x40'0000;

bool same_name(std::string_view a, std::string_view b)
{
	if (a.length() != b.length())
		return false;
	for (size_t i = 0; i < a.length(); i++)
	{
		if (std::tolower(u8(a[i])) != std::tolower(u8(b[i])))
			return false;
	}
	return true;
}

std::string short_name(u8 const *entry)
{
	std::string name;
	for (int i = 0; (i < 8) && (entry[i] != ' '); i++)
		name += char(entry[i]);
	if (entry[8] != ' ')
	{
		name += '.';
		for (int i = 8; (i < 11) && (entry[i] != ' '); i++)
			name += char(entry[i]);
	}
	if (!name.empty() && (u8(name[0]) == 0x05))
		name[0] = char(0xe5);
	return name;
}

class fat32_volume
{
public:
	fat32_volume(util::random_read &image, u64 offset) : m_image(image), m_offset(offset) { }

	std::string mount()
	{
		u8 bpb[512];
		if (!read(m_offset, bpb, sizeof(bpb)))
			return "cannot read the boot sector";
		if ((bpb[510] != 0x55) || (bpb[511] != 0xaa))
			return "boot sector has no signature";

		m_sector_size = get_u16le(&bpb[11]);
		u32 const cluster_sectors = bpb[13];
		u32 const reserved = get_u16le(&bpb[14]);
		u32 const fats = bpb[16];
		u32 const fat16_sectors = get_u16le(&bpb[22]);
		u32 const fat_sectors = get_u32le(&bpb[36]);
		m_root_cluster = get_u32le(&bpb[44]);
		if ((m_sector_size < 512) || (m_sector_size & (m_sector_size - 1)) || !cluster_sectors || !fats || fat16_sectors || !fat_sectors || (m_root_cluster < 2))
			return "not a FAT32 file system";

		m_cluster_size = m_sector_size * cluster_sectors;
		m_fat = m_offset + u64(reserved) * m_sector_size;
		m_data = m_fat + u64(fats) * fat_sectors * m_sector_size;
		return std::string();
	}

	bool find(std::string_view name, u32 &cluster, u32 &size)
	{
		std::vector<u8> root;
		if (!read_chain(m_root_cluster, ~u32(0), root))
			return false;

		std::u16string lfn;
		for (size_t pos = 0; (pos + 32) <= root.size(); pos += 32)
		{
			u8 const *const entry = &root[pos];
			if (entry[0] == 0x00)
				break;
			if (entry[0] == 0xe5)
			{
				lfn.clear();
				continue;
			}
			if (entry[11] == 0x0f)
			{
				// long name parts come last part first
				unsigned const seq = entry[0] & 0x1f;
				if (entry[0] & 0x40)
					lfn.assign(seq * 13, u'\0');
				if (seq && (seq * 13 <= lfn.size()))
				{
					static constexpr u8 offsets[13] = { 1, 3, 5, 7, 9, 14, 16, 18, 20, 22, 24, 28, 30 };
					for (int i = 0; i < 13; i++)
						lfn[(seq - 1) * 13 + i] = char16_t(get_u16le(&entry[offsets[i]]));
				}
				continue;
			}
			if (entry[11] & 0x18)
			{
				// folder or volume label
				lfn.clear();
				continue;
			}

			std::string longname;
			for (char16_t c : lfn)
			{
				if (!c || (c == 0xffff))
					break;
				longname += (c < 0x80) ? char(c) : '?';
			}
			lfn.clear();

			if (same_name(longname, name) || same_name(short_name(entry), name))
			{
				cluster = (u32(get_u16le(&entry[20])) << 16) | get_u16le(&entry[26]);
				size = get_u32le(&entry[28]);
				return true;
			}
		}
		return false;
	}

	bool read_chain(u32 cluster, u32 limit, std::vector<u8> &data)
	{
		data.clear();
		for (unsigned count = 0; (cluster >= 2) && (cluster < FAT_END) && (data.size() < limit); count++)
		{
			if (count >= MAX_CHAIN)
				return false;
			size_t const pos = data.size();
			data.resize(pos + m_cluster_size);
			if (!read(m_data + u64(cluster - 2) * m_cluster_size, &data[pos], m_cluster_size))
				return false;

			u8 next[4];
			if (!read(m_fat + u64(cluster) * 4, next, 4))
				return false;
			cluster = get_u32le(next) & FAT_MASK;
		}
		if (limit != ~u32(0))
		{
			if (data.size() < limit)
				return false;
			data.resize(limit);
		}
		return true;
	}

private:
	bool read(u64 pos, void *buffer, size_t length)
	{
		auto const [err, actual] = util::read_at(m_image, pos, buffer, length);
		return !err && (actual == length);
	}

	util::random_read &m_image;
	u64 const m_offset;
	u32 m_sector_size = 0;
	u32 m_cluster_size = 0;
	u32 m_root_cluster = 0;
	u64 m_fat = 0;
	u64 m_data = 0;
};

} // anonymous namespace


std::string nextpi_fat32_read(util::random_read &image, u64 offset, std::string_view name, std::vector<u8> &data)
{
	fat32_volume volume(image, offset);
	std::string err = volume.mount();
	if (!err.empty())
		return err;

	u32 cluster, size;
	if (!volume.find(name, cluster, size))
		return util::string_format("%s not found in the boot partition", name);
	if (!size)
	{
		data.clear();
		return std::string();
	}
	if (!volume.read_chain(cluster, size, data))
		return util::string_format("cannot read %s from the boot partition", name);
	return std::string();
}
