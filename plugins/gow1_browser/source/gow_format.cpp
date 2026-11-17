#include <gow_format.hpp>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <limits>
#include <map>

namespace hex::plugin::gow1::format {

    namespace {
        constexpr Offset Sector = 2048;
        constexpr Offset MetadataLimit = 16 * 1024 * 1024;
        constexpr std::size_t TagLimit = 100000;

        std::uint16_t le16(const std::uint8_t *p) {
            return std::uint16_t(p[0] | (std::uint16_t(p[1]) << 8));
        }
        std::uint32_t le32(const std::uint8_t *p) {
            return p[0] | (std::uint32_t(p[1]) << 8) | (std::uint32_t(p[2]) << 16) | (std::uint32_t(p[3]) << 24);
        }
        std::uint32_t be32(const std::uint8_t *p) {
            return p[3] | (std::uint32_t(p[2]) << 8) | (std::uint32_t(p[1]) << 16) | (std::uint32_t(p[0]) << 24);
        }
        std::string nameOf(std::span<const std::uint8_t> data, bool strict = false) {
            std::string result;
            for (auto c : data) {
                if (c == 0) break;
                if (c < 32 || c >= 127) {
                    if (strict) throw FormatError("Invalid ASCII filename");
                    constexpr char digits[] = "0123456789abcdef";
                    result += "\\x";
                    result += digits[c >> 4];
                    result += digits[c & 15];
                } else result += char(c);
            }
            return result;
        }
        bool isWad(std::string name) {
            std::ranges::transform(name, name.begin(), [](unsigned char c) { return char(std::toupper(c)); });
            return name.ends_with(".WAD");
        }
        Bytes metadata(const Read &read, Offset offset, Offset size) {
            if (size > MetadataLimit) throw FormatError("Metadata exceeds the 16 MiB budget");
            Bytes result(static_cast<std::size_t>(size));
            read(offset, result);
            return result;
        }
        Region extent(std::span<const std::uint8_t> record, Offset imageSize) {
            if (record.size() < 34 || record[0] != record.size() || 33U + record[32] > record.size())
                throw FormatError("Malformed ISO directory record");
            const auto lba = le32(record.data() + 2), size = le32(record.data() + 10);
            if (lba != be32(record.data() + 6) || size != be32(record.data() + 14))
                throw FormatError("ISO both-endian extent fields disagree");
            if (record[1] || record[26] || record[27] || (record[25] & 0x80))
                throw FormatError("Extended, interleaved or multi-extent ISO records are unsupported");
            requireRange(Offset(lba) * Sector, size, imageSize);
            return { {}, Offset(lba) * Sector, size };
        }
    }

    void requireRange(Offset offset, Offset size, Offset total) {
        if (offset > total || size > total - offset)
            throw FormatError("Range exceeds the source data");
    }

    FileSource::FileSource(std::filesystem::path path)
        : m_path(std::filesystem::absolute(path).lexically_normal()), m_file(m_path, std::ios::binary) {
        if (!m_file) throw FormatError("Cannot open source file (check the path and network access)");
        m_file.seekg(0, std::ios::end);
        const auto end = m_file.tellg();
        if (end < 0) throw FormatError("Cannot determine source file size");
        m_size = static_cast<Offset>(end);
    }

    void FileSource::read(Offset offset, std::span<std::uint8_t> output) {
        requireRange(offset, output.size(), m_size);
        if (output.empty()) return;
        std::scoped_lock lock(m_mutex);
        if (offset >= m_cacheOffset && offset - m_cacheOffset <= m_cacheSize &&
            output.size() <= m_cacheSize - static_cast<std::size_t>(offset - m_cacheOffset)) {
            std::memcpy(output.data(), m_cache.data() + (offset - m_cacheOffset), output.size());
            return;
        }
        m_file.clear();
        m_file.seekg(static_cast<std::streamoff>(offset));
        if (output.size() <= m_cache.size()) {
            m_cacheSize = 0;
            const auto count = static_cast<std::size_t>(std::min<Offset>(m_cache.size(), m_size - offset));
            if (!m_file.read(reinterpret_cast<char*>(m_cache.data()), static_cast<std::streamsize>(count)))
                throw FormatError("Source read failed or source was truncated");
            m_cacheOffset = offset;
            m_cacheSize = count;
            std::memcpy(output.data(), m_cache.data(), output.size());
        } else {
            if (output.size() > static_cast<std::size_t>(std::numeric_limits<std::streamsize>::max()))
                throw FormatError("Read exceeds stream size limit");
            if (!m_file.read(reinterpret_cast<char*>(output.data()), static_cast<std::streamsize>(output.size())))
                throw FormatError("Source read failed or source was truncated");
        }
    }

    std::vector<Region> readIsoRoot(const Read &read, Offset size) {
        std::array<std::uint8_t, Sector> descriptor{};
        bool found = false;
        for (Offset sector = 16; sector < 80; ++sector) {
            requireRange(sector * Sector, Sector, size);
            read(sector * Sector, descriptor);
            if (std::memcmp(descriptor.data() + 1, "CD001\x01", 6) != 0)
                throw FormatError("Expected an ISO9660 image with 2048-byte sectors");
            if (descriptor[0] == 1) { found = true; break; }
            if (descriptor[0] == 255) break;
        }
        if (!found) throw FormatError("ISO9660 primary volume descriptor is missing");
        if (le16(descriptor.data() + 128) != Sector || descriptor[130] != 8 || descriptor[131] != 0)
            throw FormatError("Only 2048-byte ISO logical blocks are supported");
        requireRange(156, descriptor[156], descriptor.size());
        auto root = extent(std::span(descriptor).subspan(156, descriptor[156]), size);
        const auto data = metadata(read, root.offset, root.size);
        std::vector<Region> files;
        for (std::size_t position = 0; position < data.size();) {
            const auto length = data[position];
            if (length == 0) { position = (position / Sector + 1) * Sector; continue; }
            requireRange(position, length, data.size());
            if (position % Sector + length > Sector) throw FormatError("ISO directory record crosses a sector");
            const auto record = std::span(data).subspan(position, length);
            auto region = extent(record, size);
            if ((record[25] & 2) == 0) {
                region.name = nameOf(record.subspan(33, record[32]), true);
                region.name = region.name.substr(0, region.name.find(';'));
                files.push_back(std::move(region));
            }
            position += length;
        }
        return files;
    }

    std::vector<TocRecord> parseToc(std::span<const std::uint8_t> data) {
        if (data.size() > MetadataLimit) throw FormatError("TOC exceeds the 16 MiB budget");
        // GoW II starts with a count; GoW I starts with an ASCII filename.
        if (data.size() == 12 && std::ranges::all_of(data, [](auto byte) { return byte == 0; })) return {};
        if (data.size() < 24 || data[0] < 0x21 || data[0] >= 0x7f || data[2] == 0)
            throw FormatError("Expected a GoW I TOC; GoW II is not supported by this browser");
        std::vector<TocRecord> records;
        for (std::size_t position = 0; position + 12 <= data.size(); position += 24) {
            const auto nameBytes = data.subspan(position, 12);
            if (position + 12 == data.size() && std::ranges::all_of(nameBytes, [](auto byte) { return byte == 0; })) return records;
            requireRange(position, 24, data.size());
            if (records.size() >= 100000) throw FormatError("TOC exceeds the 100,000 location budget");
            const auto *p = data.data() + position;
            auto name = nameOf(nameBytes, true);
            records.push_back({ std::move(name), le32(p + 12), le32(p + 16), le32(p + 20), records.size() });
        }
        throw FormatError("GoW I TOC must end with a 12-byte NUL name without record data");
    }

    Catalog readCatalog(const Read &read, Offset size, const std::string &filename) {
        Catalog result;
        if (isWad(filename)) {
            result.containers.push_back({ { filename, 0, size }, 0, true, {}, {} });
            return result;
        }
        const auto files = readIsoRoot(read, size);
        const auto toc = std::ranges::find(files, "GODOFWAR.TOC", &Region::name);
        if (toc == files.end()) throw FormatError("GODOFWAR.TOC is missing from the ISO root");
        result.toc = *toc;
        for (const auto &record : parseToc(metadata(read, toc->offset, toc->size))) {
            Container container;
            container.region = { record.name.empty() ? "<unnamed>" : record.name, 0, record.size };
            container.recordIndex = record.index;
            container.wad = isWad(record.name);
            const auto pakName = "PART" + std::to_string(Offset(record.pak) + 1) + ".PAK";
            const auto pak = std::ranges::find(files, pakName, &Region::name);
            if (pak == files.end()) {
                container.error = pakName + " is missing";
                container.available = false;
            }
            else {
                const auto relative = Offset(record.sector) * Sector;
                try {
                    requireRange(relative, record.size, pak->size);
                    container.region.offset = pak->offset + relative;
                } catch (const FormatError &error) {
                    container.error = error.what();
                    container.available = false;
                }
            }
            result.containers.push_back(std::move(container));
        }
        return result;
    }

    std::vector<Tag> parseWad(const Read &read, Offset size, const CheckCancel &checkCancel) {
        if (size == 0) throw FormatError("Empty WAD");
        std::vector<Tag> tags;
        Offset position = 0;
        std::optional<std::size_t> parent;
        bool pending = false;
        while (position < size) {
            if (checkCancel) checkCancel();
            if (tags.size() >= TagLimit) throw FormatError("WAD exceeds the 100,000 tag budget");
            requireRange(position, 32, size);
            std::array<std::uint8_t, 32> header{};
            read(position, header);
            Tag tag;
            tag.tag = le16(header.data());
            if (position == 0 && (tag.tag == 0 || tag.tag == 1 || tag.tag == 2 || tag.tag == 3 ||
                                  tag.tag == 9 || tag.tag == 19 || tag.tag == 21 || tag.tag == 22))
                throw FormatError("Expected a GoW I WAD; this appears to use GoW II tag codes");
            tag.flags = le16(header.data() + 2);
            tag.declaredSize = le32(header.data() + 4);
            tag.name = nameOf(std::span(header).subspan(8));
            tag.headerOffset = position;
            tag.size = tag.tag == 24 ? 0 : tag.declaredSize;
            tag.parent = parent;
            requireRange(tag.payloadOffset(), tag.size, size);
            if (tag.tag == 30 && tag.size != 0) {
                requireRange(0, 4, tag.size);
                std::array<std::uint8_t, 4> type{};
                read(tag.payloadOffset(), type);
                tag.type = le32(type.data());
                if (*tag.type == 0x11 && tag.size >= 16) {
                    std::array<std::uint8_t, 12> shape{};
                    read(tag.payloadOffset() + 4, shape);
                    if (std::memcmp(shape.data() + 4, "BallHull", 8) == 0) tag.shape = "BallHull";
                    if (std::memcmp(shape.data(), "SheetHdr", 8) == 0) tag.shape = "SheetHdr";
                    if (std::memcmp(shape.data(), "mCDbgHdr", 8) == 0) tag.shape = "mCDbgHdr";
                }
            }
            tags.push_back(std::move(tag));
            const auto &current = tags.back();
            if (current.tag == 40) {
                if (pending) throw FormatError("Nested group start without a resource");
                pending = true;
            } else if (current.tag == 30 && pending) {
                parent = tags.size() - 1;
                pending = false;
            } else if (current.tag == 50) {
                if (pending) pending = false;
                else if (!parent) throw FormatError("Unmatched WAD group end");
                else parent = tags[*parent].parent;
            }
            const auto end = current.payloadOffset() + current.size;
            const auto padding = (16 - end % 16) % 16;
            requireRange(end, padding, size);
            position = end + padding;
        }
        if (pending || parent) throw FormatError("Unclosed WAD resource group");
        return tags;
    }

    std::string Tag::kind() const {
        if (tag == 112) return "Raw records";
        if (tag != 30) return "Control / auxiliary";
        if (size == 0) return "Links";
        if (!type) return "Unknown resources";
        static const std::map<std::uint32_t, std::string> kinds {
            {3, "Animations"}, {0x10003, "Animation streams"}, {6, "Lighting"},
            {7, "Textures"}, {8, "Materials"}, {9, "Cameras"}, {0xc, "Graphics"},
            {0x11, "Collision"}, {0x13, "Particles"}, {0x14, "Waypoints"},
            {0x17, "Behaviours"}, {0x18, "Audio"}, {0x40018, "Audio"},
            {0x1a, "Sound emitters"}, {0x21, "UI"}, {0x27, "Shadows"},
            {0x40001, "Objects"}, {0x20001, "Instances"}, {0x10004, "Scripts"},
            {0x1000f, "Meshes"}, {0x2000f, "Models"}
        };
        if (*type == 0x11 && shape == "SheetHdr") return "Collision terrain";
        if (*type == 0x11 && shape == "mCDbgHdr") return "Collision debug";
        if (const auto it = kinds.find(*type); it != kinds.end()) return it->second;
        if (*type >= 0x80000001 && *type <= 0x80000027) return "Chunks";
        if (*type < 0x80000000 && (*type & 0xffff) == 0x1e) return "Particle emitters";
        return "Unknown resources";
    }

}
