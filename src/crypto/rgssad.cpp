/*
** rgssad.cpp
**
** This file is part of mkxp.
**
** Copyright (C) 2014 - 2021 Amaryllis Kulla <ancurio@mapleshrine.eu>
**
** mkxp is free software: you can redistribute it and/or modify
** it under the terms of the GNU General Public License as published by
** the Free Software Foundation, either version 2 of the License, or
** (at your option) any later version.
**
** mkxp is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
** GNU General Public License for more details.
**
** You should have received a copy of the GNU General Public License
** along with mkxp.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "rgssad.h"
#include "boost-hash.h"

#include <stdint.h>
#include <string.h>

#include <string>
#include <vector>
#ifdef __vita__
#include <stdio.h>
#include <sys/stat.h>
#include <zlib.h>
#endif

/* Equivalent Linear Congruential Generator (LCG) constants for iteration 2^n
 * all the way up to 2^32/4 (the largest dword offset possible in
 * RGSS{AD,[23]A}).
 *
 * This table can be easily calculated by taking the original LCG parameter
 * m[0] and a[0] and write them down as LCG_TABLE[0]. Then for the rest of
 * the 29 entries, LGC_TABLE[n] = {m[n], a[n]} where m[n] = pow(m[n-1], 2)
 * and a[n] = a[n-1] * (m[n-1] + 1). */
constexpr static uint32_t LCG_TABLE[30][2] = {
    {0x00000007, 0x00000003}, {0x00000031, 0x00000018},
    {0x00000961, 0x000004b0}, {0x0057f6c1, 0x002bfb60},
    {0xa5057d81, 0xd282bec0}, {0x6e913b01, 0x37489d80},
    {0xc0bb7601, 0x605dbb00}, {0x1bdaec01, 0x8ded7600},
    {0x0145d801, 0x80a2ec00}, {0x28cbb001, 0x1465d800},
    {0xea976001, 0x754bb000}, {0x392ec001, 0x1c976000},
    {0x025d8001, 0x012ec000}, {0x44bb0001, 0x225d8000},
    {0x89760001, 0xc4bb0000}, {0x12ec0001, 0x89760000},
    {0x25d80001, 0x12ec0000}, {0x4bb00001, 0x25d80000},
    {0x97600001, 0x4bb00000}, {0x2ec00001, 0x97600000},
    {0x5d800001, 0x2ec00000}, {0xbb000001, 0x5d800000},
    {0x76000001, 0xbb000000}, {0xec000001, 0x76000000},
    {0xd8000001, 0xec000000}, {0xb0000001, 0xd8000000},
    {0x60000001, 0xb0000000}, {0xc0000001, 0x60000000},
    {0x80000001, 0xc0000000}, {0x00000001, 0x80000000},
};

struct RGSS_entryData
{
	int64_t offset;
	uint64_t size;
	uint32_t startMagic;
};

struct RGSS_entryHandle
{
	const RGSS_entryData data;
	uint32_t currentMagic;
	uint64_t currentOffset;
	PHYSFS_Io *io;

	RGSS_entryHandle(const RGSS_entryData &data, PHYSFS_Io *archIo)
	    : data(data),
	      currentMagic(data.startMagic),
	      currentOffset(0)
	{
		io = archIo->duplicate(archIo);
	}

	~RGSS_entryHandle()
	{
		io->destroy(io);
	}
};

struct RGSS_archiveData
{
	PHYSFS_Io *archiveIo;

	/* Maps: file path
	 * to:   entry data */
	BoostHash<std::string, RGSS_entryData> entryHash;

	/* Maps: directory path,
	 * to:   list of contained entries */
	BoostHash<std::string, BoostSet<std::string> > dirHash;
};

static bool
readUint32(PHYSFS_Io *io, uint32_t &result)
{
	char buff[4];
	PHYSFS_sint64 count = io->read(io, buff, 4);

	result = ((buff[0] << 0x00) & 0x000000FF) |
	         ((buff[1] << 0x08) & 0x0000FF00) |
	         ((buff[2] << 0x10) & 0x00FF0000) |
	         ((buff[3] << 0x18) & 0xFF000000) ;

	return (count == 4);
}

#define RGSS_HEADER "RGSSAD"
#define RGSS_MAGIC 0xDEADCAFE

#define PHYSFS_ALLOC(type) \
	static_cast<type*>(PHYSFS_getAllocator()->Malloc(sizeof(type)))

#define IO_READ(io, dest, size) (io->read(io, dest, size) == size)

/* Resolve exact archive names first, then ASCII case-insensitive names.
 * Non-ASCII bytes remain unchanged. Folded collisions use lexical order. */
static inline bool
rgssNameEqualFolded(const char *a, const char *b)
{
	while (*a && *b)
	{
		unsigned char ca = static_cast<unsigned char>(*a);
		unsigned char cb = static_cast<unsigned char>(*b);
		if (ca >= 'A' && ca <= 'Z')
			ca += static_cast<unsigned char>('a' - 'A');
		if (cb >= 'A' && cb <= 'Z')
			cb += static_cast<unsigned char>('a' - 'A');
		if (ca != cb)
			return false;
		++a;
		++b;
	}
	return *a == *b;
}

template<typename V>
static const std::string *
rgssFindKeyCaseInsensitive(const BoostHash<std::string, V> &hash,
                           const char *name)
{
	const std::string *match = 0;
	for (typename BoostHash<std::string, V>::const_iterator it = hash.cbegin();
	     it != hash.cend(); ++it)
	{
		if (rgssNameEqualFolded(it->first.c_str(), name) &&
		    (!match || it->first < *match))
			match = &it->first;
	}
	return match;
}

static inline uint32_t
advanceMagic(uint32_t &magic)
{
	uint32_t old = magic;

	magic = magic * 7 + 3;

	return old;
}

static inline uint32_t
advanceMagicN(uint32_t &magic, uint32_t n) {
    uint32_t old = magic;
    int table_index = 0;

    while (n != 0) {
        if (n & 1) {
            magic = magic * LCG_TABLE[table_index][0] + LCG_TABLE[table_index][1];
        }
        n >>= 1;
        table_index++;
    }

    return old;
}

static PHYSFS_sint64
RGSS_ioRead(PHYSFS_Io *self, void *buffer, PHYSFS_uint64 len)
{
	RGSS_entryHandle *entry = static_cast<RGSS_entryHandle*>(self->opaque);

	PHYSFS_Io *io = entry->io;

	uint64_t toRead = std::min<uint64_t>(entry->data.size - entry->currentOffset, len);
	uint64_t offs = entry->currentOffset;

	io->seek(io, entry->data.offset + offs);

	/* We divide up the bytes to be read in 3 categories:
	 *
	 * preAlign: If the current read address is not dword
	 *   aligned, this is the number of bytes to read til
	 *   we reach alignment again (therefore can only be
	 *   3 or less).
	 *
	 * align: The number of aligned dwords we can read
	 *   times 4 (= number of bytes).
	 *
	 * postAlign: The number of bytes to read after the
	 *   last aligned dword. Always 3 or less.
	 *
	 * Treating the pre- and post aligned reads specially,
	 * we can read all aligned dwords in one syscall directly
	 * into the write buffer and then run the xor chain on
	 * it afterwards. */

	uint8_t preAlign = 4 - (offs % 4);

	if (preAlign == 4)
		preAlign = 0;
	else
		preAlign = std::min<uint64_t>(preAlign, len);

	uint8_t postAlign = (len > preAlign) ? (offs + len) % 4 : 0;

	uint64_t align = len - (preAlign + postAlign);

	/* Byte buffer pointer */
	uint8_t *bBufferP = static_cast<uint8_t*>(buffer);

	if (preAlign > 0)
	{
		uint32_t dword;
		io->read(io, &dword, preAlign);

		/* Need to align the bytes with the
		 * magic before xoring */
		dword <<= 8 * (offs % 4);
		dword ^= entry->currentMagic;

		/* Shift them back to normal */
		dword >>= 8 * (offs % 4);
		memcpy(bBufferP, &dword, preAlign);

		bBufferP += preAlign;

		/* Only advance the magic if we actually
		 * reached the next alignment */
		if ((offs+preAlign) % 4 == 0)
			advanceMagic(entry->currentMagic);
	}

	if (align > 0)
	{
		/* Double word buffer pointer */
		uint32_t *dwBufferP = reinterpret_cast<uint32_t*>(bBufferP);

		/* Read aligned dwords in one go */
		io->read(io, bBufferP, align);

		/* Then xor them */
		for (uint64_t i = 0; i < (align / 4); ++i)
			dwBufferP[i] ^= advanceMagic(entry->currentMagic);

		bBufferP += align;
	}

	if (postAlign > 0)
	{
		uint32_t dword;
		io->read(io, &dword, postAlign);

		/* Bytes are already aligned with magic */
		dword ^= entry->currentMagic;
		memcpy(bBufferP, &dword, postAlign);
	}

	entry->currentOffset += toRead;

	return toRead;
}

static int
RGSS_ioSeek(PHYSFS_Io *self, PHYSFS_uint64 offset)
{
	RGSS_entryHandle *entry = static_cast<RGSS_entryHandle*>(self->opaque);

	if (offset == entry->currentOffset)
		return 1;

	if (offset > entry->data.size-1)
		return 0;

	/* If rewinding, we need to rewind to begining */
	if (offset < entry->currentOffset)
	{
		entry->currentOffset = 0;
		entry->currentMagic = entry->data.startMagic;
	}

	/* For each overstepped alignment, advance magic */
	uint64_t currentDword = entry->currentOffset / 4;
	uint64_t targetDword  = offset / 4;
	uint64_t dwordsSought = targetDword - currentDword;

	advanceMagicN(entry->currentMagic, (uint32_t) dwordsSought);

	entry->currentOffset = offset;
	entry->io->seek(entry->io, entry->data.offset + entry->currentOffset);

	return 1;
}

static PHYSFS_sint64
RGSS_ioTell(PHYSFS_Io *self)
{
	const RGSS_entryHandle *entry = static_cast<RGSS_entryHandle*>(self->opaque);

	return entry->currentOffset;
}

static PHYSFS_sint64
RGSS_ioLength(PHYSFS_Io *self)
{
	const RGSS_entryHandle *entry = static_cast<RGSS_entryHandle*>(self->opaque);

	return entry->data.size;
}

static PHYSFS_Io*
RGSS_ioDuplicate(PHYSFS_Io *self)
{
	const RGSS_entryHandle *entry = static_cast<RGSS_entryHandle*>(self->opaque);
	RGSS_entryHandle *entryDup = new RGSS_entryHandle(*entry);

	PHYSFS_Io *dup = PHYSFS_ALLOC(PHYSFS_Io);
	*dup = *self;
	dup->opaque = entryDup;

	return dup;
}

static void
RGSS_ioDestroy(PHYSFS_Io *self)
{
	RGSS_entryHandle *entry = static_cast<RGSS_entryHandle*>(self->opaque);

	delete entry;

	PHYSFS_getAllocator()->Free(self);
}

static const PHYSFS_Io RGSS_IoTemplate =
{
    0, /* version */
    0, /* opaque */
    RGSS_ioRead,
    0, /* write */
    RGSS_ioSeek,
    RGSS_ioTell,
    RGSS_ioLength,
    RGSS_ioDuplicate,
    0, /* flush */
    RGSS_ioDestroy
};

/* Reads the entry table while an archive is opened. On the Vita every
 * read of the archive is a system call to the memory card; RGSS1/2 entry
 * headers are three small reads apiece (a thousand-file archive took 0.6 s
 * to open). This reader fetches one block per refill and serves the header
 * reads and seeks from it. */
struct ScanReader
{
	PHYSFS_Io *io;
	const uint32_t refill;
	PHYSFS_uint64 bufStart = 0, pos = 0;
	uint32_t bufLen = 0;
	char buf[16 * 1024];

	ScanReader(PHYSFS_Io *io, uint32_t refill)
	    : io(io), refill(refill < sizeof(buf) ? refill : sizeof(buf)), pos(io->tell(io)) {}

	bool read(void *dest, uint32_t len)
	{
		char *out = static_cast<char *>(dest);
		while (len > 0)
		{
			if (pos < bufStart || pos >= bufStart + bufLen)
			{
				if (!io->seek(io, pos))
					return false;
				PHYSFS_sint64 got = io->read(io, buf, refill);
				if (got <= 0)
					return false;
				bufStart = pos;
				bufLen = static_cast<uint32_t>(got);
			}
			uint32_t offset = static_cast<uint32_t>(pos - bufStart);
			uint32_t chunk = bufLen - offset < len ? bufLen - offset : len;
			memcpy(out, buf + offset, chunk);
			out += chunk;
			pos += chunk;
			len -= chunk;
		}
		return true;
	}

	bool readUint32(uint32_t &result)
	{
		char buff[4];
		if (!read(buff, 4))
			return false;
		result = ((buff[0] << 0x00) & 0x000000FF) |
		         ((buff[1] << 0x08) & 0x0000FF00) |
		         ((buff[2] << 0x10) & 0x00FF0000) |
		         ((buff[3] << 0x18) & 0xFF000000) ;
		return true;
	}

	PHYSFS_uint64 tell() const { return pos; }
	void seek(PHYSFS_uint64 offset) { pos = offset; }
};

static void
processDirectories(RGSS_archiveData *data, BoostSet<std::string> &topLevel,
                   char *nameBuf, uint32_t nameLen)
{
	/* Check for top level entries */
	for (uint32_t i = 0; i < nameLen; ++i)
	{
		bool slash = nameBuf[i] == '/';
		if (!slash && i+1 < nameLen)
			continue;

		if (slash)
			nameBuf[i] = '\0';

		topLevel.insert(nameBuf);

		if (slash)
			nameBuf[i] = '/';

		break;
	}

	/* Check for more entries */
	for (uint32_t i = nameLen; i > 0; i--)
		if (nameBuf[i] == '/')
		{
			nameBuf[i] = '\0';

			const char *dir = nameBuf;
			const char *entry = &nameBuf[i+1];

			BoostSet<std::string> &entryList = data->dirHash[dir];
			entryList.insert(entry);
		}
}

static bool
verifyHeader(PHYSFS_Io *io, char version)
{
	char header[8];

	if (!IO_READ(io, header, sizeof(header)))
		return false;

	if (strcmp(header, RGSS_HEADER))
		return false;

	if (header[7] != version)
		return false;

	return true;
}

#ifdef __vita__
/* RGSS1/2 archives have no index: opening one reads a header in front of
 * every file, one card seek each (0.6 s for Ao Oni's thousand files). The
 * entry table is kept in ux0:/data/hardrpg/cache/rgssad-v1/ and used again
 * while the archive keeps its path, size and modification time, its first
 * header still decrypts to the first cached name and its last entry ends at
 * the end of the file. Anything else scans the archive as before. */
struct RGSS1_IndexEntry
{
	std::string name;
	RGSS_entryData data;
};

static const char RGSS1_INDEX_MAGIC[8] = {'H','R','G','S','I','D','X','1'};

static std::string rgss1IndexPath(const char *archive)
{
	char name[96];
	snprintf(name, sizeof(name), "ux0:/data/hardrpg/cache/rgssad-v1/%08lx.idx",
	         (unsigned long)crc32(0, (const Bytef *)archive, strlen(archive)));
	return name;
}

static bool rgss1IndexIdentity(const char *archive, std::string &identity)
{
	struct stat info;
	if (!archive || stat(archive, &info) != 0 || !S_ISREG(info.st_mode))
		return false;
	char numbers[64];
	snprintf(numbers, sizeof(numbers), "%lld:%lld:", (long long)info.st_size, (long long)info.st_mtime);
	identity = std::string(numbers) + archive;
	return true;
}

static bool rgss1LoadIndex(PHYSFS_Io *io, const char *archive, std::vector<RGSS1_IndexEntry> &entries)
{
	std::string identity;
	if (!rgss1IndexIdentity(archive, identity))
		return false;
	FILE *f = fopen(rgss1IndexPath(archive).c_str(), "rb");
	if (!f)
		return false;
	std::vector<char> bytes;
	fseek(f, 0, SEEK_END);
	long size = ftell(f);
	fseek(f, 0, SEEK_SET);
	if (size > 16 && size < 64 * 1024 * 1024)
	{
		bytes.resize(size);
		if (fread(bytes.data(), 1, size, f) != (size_t)size)
			bytes.clear();
	}
	fclose(f);
	if (bytes.size() < 16 || memcmp(bytes.data(), RGSS1_INDEX_MAGIC, 8))
		return false;
	uint32_t stored;
	memcpy(&stored, bytes.data() + bytes.size() - 4, 4);
	if (stored != crc32(0, (const Bytef *)bytes.data(), bytes.size() - 4))
		return false;

	size_t at = 8, end = bytes.size() - 4;
	auto take = [&](void *dest, size_t n) {
		if (end - at < n)
			return false;
		memcpy(dest, bytes.data() + at, n);
		at += n;
		return true;
	};
	uint32_t length, count;
	if (!take(&length, 4) || end - at < length || std::string(bytes.data() + at, length) != identity)
		return false;
	at += length;
	if (!take(&count, 4))
		return false;
	entries.clear();
	for (uint32_t i = 0; i < count; ++i)
	{
		RGSS1_IndexEntry e;
		if (!take(&length, 4) || length >= 512 || end - at < length)
			return false;
		e.name.assign(bytes.data() + at, length);
		at += length;
		if (!take(&e.data.offset, 8) || !take(&e.data.size, 8) || !take(&e.data.startMagic, 4))
			return false;
		entries.push_back(e);
	}
	if (at != end || entries.empty())
		return false;

	/* The archive must still end with the last entry and start with the
	 * first: its header decrypts to the cached name. */
	const RGSS_entryData &last = entries.back().data;
	if ((PHYSFS_sint64)(last.offset + last.size) != io->length(io))
		return false;
	const std::string &first = entries.front().name;
	char header[4 + 511 + 4];
	const uint32_t headerSize = 4 + first.size() + 4;
	if (!io->seek(io, 8) || io->read(io, header, headerSize) != headerSize)
		return false;
	uint32_t magic = RGSS_MAGIC, nameLen;
	memcpy(&nameLen, header, 4);
	if ((nameLen ^ advanceMagic(magic)) != first.size())
		return false;
	for (uint32_t i = 0; i < first.size(); ++i)
	{
		char c = header[4 + i] ^ (advanceMagic(magic) & 0xFF);
		if ((c == '\\' ? '/' : c) != first[i])
			return false;
	}
	return entries.front().data.offset == 8 + (PHYSFS_sint64)headerSize;
}

static void rgss1SaveIndex(const char *archive, const std::vector<RGSS1_IndexEntry> &entries)
{
	std::string identity;
	if (entries.empty() || !rgss1IndexIdentity(archive, identity))
		return;
	std::vector<char> bytes(RGSS1_INDEX_MAGIC, RGSS1_INDEX_MAGIC + 8);
	auto put = [&](const void *src, size_t n) {
		bytes.insert(bytes.end(), (const char *)src, (const char *)src + n);
	};
	uint32_t length = identity.size(), count = entries.size();
	put(&length, 4);
	put(identity.data(), length);
	put(&count, 4);
	for (const RGSS1_IndexEntry &e : entries)
	{
		length = e.name.size();
		put(&length, 4);
		put(e.name.data(), length);
		put(&e.data.offset, 8);
		put(&e.data.size, 8);
		put(&e.data.startMagic, 4);
	}
	uint32_t check = crc32(0, (const Bytef *)bytes.data(), bytes.size());
	put(&check, 4);

	mkdir("ux0:/data", 0777);
	mkdir("ux0:/data/hardrpg", 0777);
	mkdir("ux0:/data/hardrpg/cache", 0777);
	mkdir("ux0:/data/hardrpg/cache/rgssad-v1", 0777);
	const std::string path = rgss1IndexPath(archive), temp = path + ".tmp";
	FILE *f = fopen(temp.c_str(), "wb");
	if (!f)
		return;
	bool ok = fwrite(bytes.data(), 1, bytes.size(), f) == bytes.size();
	ok = fclose(f) == 0 && ok;
	if (ok)
	{
		remove(path.c_str());
		ok = rename(temp.c_str(), path.c_str()) == 0;
	}
	if (!ok)
		remove(temp.c_str());
}
#endif

static void*
RGSS_openArchive(PHYSFS_Io *io, const char *archiveName, int forWrite, int *claimed)
{
	if (forWrite)
		return NULL;

	/* Version 1 */
	if (!verifyHeader(io, 1))
		return NULL;
	else
		*claimed = 1;

	RGSS_archiveData *data = new RGSS_archiveData;
	data->archiveIo = io;

	uint32_t magic = RGSS_MAGIC;

	/* Top level entry list */
	BoostSet<std::string> &topLevel = data->dirHash[""];

#ifdef __vita__
	std::vector<RGSS1_IndexEntry> cached;
	if (rgss1LoadIndex(io, archiveName, cached))
	{
		static char nameBuf[512];
		for (const RGSS1_IndexEntry &e : cached)
		{
			memcpy(nameBuf, e.name.c_str(), e.name.size() + 1);
			data->entryHash.insert(nameBuf, e.data);
			processDirectories(data, topLevel, nameBuf, e.name.size());
		}
		return data;
	}
	/* A rejected table may have read the first header: scan from the start. */
	if (!io->seek(io, 8))
	{
		delete data;
		return NULL;
	}
	std::vector<RGSS1_IndexEntry> scanned;
#endif
	/* An entry header is at most 4 + 511 + 4 bytes; small files share it. */
	ScanReader scan(io, 1024);

	while (true)
	{
		/* Read filename length,
         * if nothing was read, no files remain */
		uint32_t nameLen;

		if (!scan.readUint32(nameLen))
			break;

		nameLen ^= advanceMagic(magic);

		static char nameBuf[512];
		if (nameLen >= sizeof(nameBuf) || !scan.read(nameBuf, nameLen))
		{
			delete data;
			return NULL;
		}
		for (uint32_t i = 0; i < nameLen; ++i)
		{
			nameBuf[i] ^= (advanceMagic(magic) & 0xFF);
			if (nameBuf[i] == '\\')
				nameBuf[i] = '/';
		}

		nameBuf[nameLen] = '\0';

		uint32_t entrySize;
		if (!scan.readUint32(entrySize))
			break;
		entrySize ^= advanceMagic(magic);

		RGSS_entryData entry;
		entry.offset = scan.tell();
		entry.size = entrySize;
		entry.startMagic = magic;

		data->entryHash.insert(nameBuf, entry);
#ifdef __vita__
		scanned.push_back({nameBuf, entry});
#endif
		processDirectories(data, topLevel, nameBuf, nameLen);

		scan.seek(entry.offset + entry.size);
	}
#ifdef __vita__
	/* Only a table that reaches the end of the file is complete. */
	if (!scanned.empty() &&
	    (PHYSFS_sint64)(scanned.back().data.offset + scanned.back().data.size) == io->length(io))
		rgss1SaveIndex(archiveName, scanned);
#endif

	return data;
}

static PHYSFS_EnumerateCallbackResult
RGSS_enumerateFiles(void *opaque, const char *dirname,
                    PHYSFS_EnumerateCallback cb,
                    const char *origdir, void *callbackdata)
{
	RGSS_archiveData *data = static_cast<RGSS_archiveData*>(opaque);

	std::string _dirname(dirname);

	if (!data->dirHash.contains(_dirname))
	{
		const std::string *match =
		    rgssFindKeyCaseInsensitive(data->dirHash, dirname);
		if (!match)
			return PHYSFS_ENUM_STOP;
		_dirname = *match;
	}

	const BoostSet<std::string> &entries = data->dirHash[_dirname];

	BoostSet<std::string>::const_iterator iter;
	for (iter = entries.cbegin(); iter != entries.cend(); ++iter)
		cb(callbackdata, origdir, iter->c_str());

	return PHYSFS_ENUM_OK;
}

static PHYSFS_Io*
RGSS_openRead(void *opaque, const char *filename)
{
	RGSS_archiveData *data = static_cast<RGSS_archiveData*>(opaque);

	std::string target(filename);
	if (!data->entryHash.contains(target))
	{
		const std::string *match =
		    rgssFindKeyCaseInsensitive(data->entryHash, filename);
		if (!match)
			return 0;
		target = *match;
	}

	RGSS_entryHandle *entry =
	        new RGSS_entryHandle(data->entryHash[target], data->archiveIo);

	PHYSFS_Io *io = PHYSFS_ALLOC(PHYSFS_Io);

	*io = RGSS_IoTemplate;
	io->opaque = entry;

	return io;
}

static int
RGSS_stat(void *opaque, const char *filename, PHYSFS_Stat *stat)
{
	RGSS_archiveData *data = static_cast<RGSS_archiveData*>(opaque);

	std::string fileKey(filename);
	bool hasFile = data->entryHash.contains(fileKey);
	bool hasDir = data->dirHash.contains(filename);
	if (!hasFile && !hasDir)
	{
		const std::string *match =
		    rgssFindKeyCaseInsensitive(data->entryHash, filename);
		if (match)
		{
			fileKey = *match;
			hasFile = true;
		}
	}
	if (!hasDir && !hasFile)
		hasDir = rgssFindKeyCaseInsensitive(data->dirHash, filename) != 0;

	if (!hasFile && !hasDir)
	{
		PHYSFS_setErrorCode(PHYSFS_ERR_NOT_FOUND);
		return 0;
	}

	stat->modtime    =
	stat->createtime =
	stat->accesstime = 0;
	stat->readonly   = 1;

	if (hasFile)
	{
		const RGSS_entryData &entry = data->entryHash[fileKey];

		stat->filesize = entry.size;
		stat->filetype = PHYSFS_FILETYPE_REGULAR;
	}
	else
	{
		stat->filesize = 0;
		stat->filetype = PHYSFS_FILETYPE_DIRECTORY;
	}

	return 1;
}

static void
RGSS_closeArchive(void *opaque)
{
	RGSS_archiveData *data = static_cast<RGSS_archiveData*>(opaque);

	delete data;
}

static PHYSFS_Io*
RGSS_noop1(void*, const char*)
{
	return 0;
}

static int
RGSS_noop2(void*, const char*)
{
	return 0;
}

const PHYSFS_Archiver RGSS1_Archiver =
{
	0,
	{
		"RGSSAD",
		"RGSS encrypted archive format",
		"", /* Author */
		"", /* Website */
		0 /* symlinks not supported */
	},
	RGSS_openArchive,
	RGSS_enumerateFiles,
	RGSS_openRead,
	RGSS_noop1, /* openWrite */
	RGSS_noop1, /* openAppend */
	RGSS_noop2, /* remove */
	RGSS_noop2, /* mkdir */
	RGSS_stat,
	RGSS_closeArchive
};

const PHYSFS_Archiver RGSS2_Archiver =
{
	0,
	{
		"RGSS2A",
		"RGSS2 encrypted archive format",
		"", /* Author */
		"", /* Website */
		0 /* symlinks not supported */
	},
	RGSS_openArchive,
	RGSS_enumerateFiles,
	RGSS_openRead,
	RGSS_noop1, /* openWrite */
	RGSS_noop1, /* openAppend */
	RGSS_noop2, /* remove */
	RGSS_noop2, /* mkdir */
	RGSS_stat,
	RGSS_closeArchive
};

static bool
readUint32AndXor(ScanReader &io, uint32_t &result, uint32_t key)
{
	if (!io.readUint32(result))
		return false;

	result ^= key;

	return true;
}

static void*
RGSS3_openArchive(PHYSFS_Io *io, const char *, int forWrite, int *claimed)
{
	if (forWrite)
		return NULL;

	/* Version 3 */
	if (!verifyHeader(io, 3))
		return NULL;
	else
		*claimed = 1;

	uint32_t baseMagic;

	if (!readUint32(io, baseMagic))
		return NULL;

	baseMagic = (baseMagic * 9) + 3;

	RGSS_archiveData *data = new RGSS_archiveData;
	data->archiveIo = io;

	/* Top level entry list */
	BoostSet<std::string> &topLevel = data->dirHash[""];
	/* The entry table is one contiguous block at the start. */
	ScanReader scan(io, 16 * 1024);

	while (true)
	{
		uint32_t offset, size, magic, nameLen;

		if (!readUint32AndXor(scan, offset, baseMagic))
			goto error;

		/* Zero offset means entry list has ended */
		if (offset == 0)
			break;

		if (!readUint32AndXor(scan, size, baseMagic))
			goto error;

		if (!readUint32AndXor(scan, magic, baseMagic))
			goto error;

		if (!readUint32AndXor(scan, nameLen, baseMagic))
			goto error;

		char nameBuf[512];

		if (nameLen >= sizeof(nameBuf) || !scan.read(nameBuf, nameLen))
			goto error;

		for (uint32_t i = 0; i < nameLen; ++i)
		{
			nameBuf[i] ^= ((baseMagic >> 8*(i%4)) & 0xFF);

			if (nameBuf[i] == '\\')
				nameBuf[i] = '/';
		}

		nameBuf[nameLen] = '\0';

		RGSS_entryData entry;
		entry.offset = offset;
		entry.size = size;
		entry.startMagic = magic;

		data->entryHash.insert(nameBuf, entry);
		processDirectories(data, topLevel, nameBuf, nameLen);

		continue;

	error:
		delete data;
		return NULL;
	}

	return data;
}

const PHYSFS_Archiver RGSS3_Archiver =
{
	0,
	{
		"RGSS3A",
		"RGSS3 encrypted archive format",
		"", /* Author */
		"", /* Website */
		0 /* symlinks not supported */
	},
	RGSS3_openArchive,
	RGSS_enumerateFiles,
	RGSS_openRead,
	RGSS_noop1, /* openWrite */
	RGSS_noop1, /* openAppend */
	RGSS_noop2, /* remove */
	RGSS_noop2, /* mkdir */
	RGSS_stat,
	RGSS_closeArchive
};
