#include <gow_format.hpp>
#include <lz4frame.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <iomanip>
#include <limits>
#include <sstream>

namespace hex::plugin::gowr::format {
    namespace {
        std::uint16_t u16(const std::uint8_t *p) { return std::uint16_t(p[0] | (std::uint16_t(p[1]) << 8)); }
        std::uint32_t u32(const std::uint8_t *p) { return p[0] | (std::uint32_t(p[1]) << 8) | (std::uint32_t(p[2]) << 16) | (std::uint32_t(p[3]) << 24); }
        Offset u64(const std::uint8_t *p) { return u32(p) | (Offset(u32(p + 4)) << 32); }
        Offset add(Offset a, Offset b) {
            if (b > std::numeric_limits<Offset>::max() - a) throw FormatError("Offset arithmetic overflow");
            return a + b;
        }
        std::string utf8(const std::filesystem::path &path) {
            const auto value = path.generic_u8string();
            return {reinterpret_cast<const char*>(value.data()), value.size()};
        }
        std::string nameOf(std::span<const std::uint8_t> bytes) {
            const auto end = std::ranges::find(bytes, 0);
            std::string result(bytes.begin(), end);
            // Names in the format are UTF-8. Escape control bytes in diagnostic labels.
            for (auto &c : result) if (static_cast<unsigned char>(c) < 32) c = '?';
            return result;
        }
        std::string hashName(Offset value) {
            std::ostringstream text; text << "0x" << std::hex << std::setfill('0') << std::setw(16) << value;
            return text.str();
        }
        Bytes bytesAt(const Read &read, Offset offset, Offset count, Offset total) {
            requireRange(offset, count, total);
            if (count > 16 * 1024 * 1024) throw FormatError("Metadata read exceeds 16 MiB");
            Bytes bytes(static_cast<std::size_t>(count)); read(offset, bytes); return bytes;
        }
        std::string kindOf(const Tag &tag) {
            if (tag.id == 25) return "Padding";
            if (tag.id == 29) return tag.server == 25 ? "Texture GPU data" : "GPU data";
            if (tag.id == 60) return "References";
            if (tag.id == 11) return "DC version";
            if (tag.id == 12) return "DC data";
            if (tag.id == 13) return "DC exports";
            if (tag.id == 14) return "DC imports";
            if (tag.id == 15) return "DC pointers";
            if (tag.id != 1) return "Auxiliary chunks";
            if (tag.server == 25 && tag.client == 1) return "Textures";
            if (tag.server == 12) {
                if (tag.name.starts_with("MESH_")) return "Meshes";
                if (tag.name.starts_with("MG_")) return "Model groups";
                if (tag.name.ends_with("_smsh_data")) return "Static meshes";
            }
            if (tag.server == 10 && tag.client == 0) return "Materials";
            if (tag.server == 10 && tag.client == 1) return "Shaders";
            if (tag.server == 2 && tag.name.starts_with("ANM_")) return "Animations";
            if (tag.server == 13) return "Particles";
            if (tag.server == 9 && tag.client == 8 && tag.name.starts_with("SML_")) return "Lighting";
            if (tag.server == 1 && tag.client == 2 && tag.size == 160 && tag.name.starts_with("go")) return "Instances";
            auto lower = tag.name;
            std::ranges::transform(lower, lower.begin(), [](unsigned char c) { return char(std::tolower(c)); });
            if (tag.server == 1 && lower.starts_with("goproto")) return "Skeletons";
            return "Resources";
        }
        struct Entry {
            Tag tag;
            Offset length = 0, memOffset = 0, debugOffset = 0, tempOffset = 0;
            std::uint32_t align = 1;
            std::uint8_t arena = 0, bits = 0;
        };
    }

    void requireRange(Offset offset, Offset size, Offset total) {
        if (offset > total || size > total - offset) throw FormatError("Range exceeds the source data");
    }

    FileSource::FileSource(std::filesystem::path path, bool decode, const CheckCancel &cancel)
        : m_path(std::filesystem::absolute(path).lexically_normal()), m_file(m_path, std::ios::binary) {
        if (!m_file) throw FormatError("Cannot open source file (check path and network access)");
        m_file.seekg(0, std::ios::end);
        const auto end = m_file.tellg();
        if (end < 0) throw FormatError("Cannot determine source size");
        m_size = static_cast<Offset>(end);
        std::array<std::uint8_t, 4> magic{};
        if (m_size >= 4) readFile(0, magic);
        m_compressed = u32(magic.data()) == 0x184d2204;
        if (!decode || !m_compressed) return;
        LZ4F_dctx *context = nullptr;
        const auto created = LZ4F_createDecompressionContext(&context, LZ4F_VERSION);
        if (LZ4F_isError(created)) throw FormatError(LZ4F_getErrorName(created));
        const auto owner = std::unique_ptr<LZ4F_dctx, decltype(&LZ4F_freeDecompressionContext)>(context, &LZ4F_freeDecompressionContext);
        Bytes input(1024 * 1024), output(1024 * 1024);
        Offset position = 0;
        std::size_t remaining = 0, consumed = 0, hint = 1;
        while (hint != 0) {
            if (cancel) cancel();
            if (consumed == remaining) {
                remaining = static_cast<std::size_t>(std::min<Offset>(input.size(), m_size - position));
                if (remaining == 0) throw FormatError("Truncated LZ4 frame");
                readFile(position, std::span(input).first(remaining));
                position += remaining; consumed = 0;
            }
            auto srcSize = remaining - consumed;
            auto dstSize = output.size();
            hint = LZ4F_decompress(context, output.data(), &dstSize, input.data() + consumed, &srcSize, nullptr);
            if (LZ4F_isError(hint)) throw FormatError(std::string("Invalid LZ4 frame: ") + LZ4F_getErrorName(hint));
            consumed += srcSize;
            if (dstSize > DecodeLimit - m_data.size()) throw FormatError("Decoded container exceeds 512 MiB limit");
            m_data.insert(m_data.end(), output.begin(), output.begin() + dstSize);
            if (srcSize == 0 && dstSize == 0 && hint != 0) throw FormatError("LZ4 decoder made no progress");
        }
        if (position != m_size || consumed != remaining) throw FormatError("Trailing bytes or concatenated LZ4 frames are unsupported");
        m_decoded = true;
        m_file.close();
        m_cacheSize = 0;
    }

    void FileSource::readFile(Offset offset, std::span<std::uint8_t> output) {
        requireRange(offset, output.size(), m_size);
        if (output.empty()) return;
        if (offset >= m_cacheOffset && offset - m_cacheOffset <= m_cacheSize &&
            output.size() <= m_cacheSize - static_cast<std::size_t>(offset - m_cacheOffset)) {
            std::memcpy(output.data(), m_cache.data() + (offset - m_cacheOffset), output.size()); return;
        }
        m_file.clear(); m_file.seekg(static_cast<std::streamoff>(offset));
        if (output.size() <= m_cache.size()) {
            m_cacheSize = 0;
            const auto count = static_cast<std::size_t>(std::min<Offset>(m_cache.size(), m_size - offset));
            if (!m_file.read(reinterpret_cast<char*>(m_cache.data()), static_cast<std::streamsize>(count))) throw FormatError("Source read failed or source was truncated");
            m_cacheOffset = offset; m_cacheSize = count;
            std::memcpy(output.data(), m_cache.data(), output.size());
        } else if (!m_file.read(reinterpret_cast<char*>(output.data()), static_cast<std::streamsize>(output.size()))) throw FormatError("Source read failed or source was truncated");
    }

    void FileSource::read(Offset offset, std::span<std::uint8_t> output) {
        requireRange(offset, output.size(), size());
        if (output.empty()) return;
        if (m_decoded) { std::memcpy(output.data(), m_data.data() + offset, output.size()); return; }
        std::scoped_lock lock(m_mutex); readFile(offset, output);
    }

    std::string extensionOf(const std::string &name) {
        auto lower = name;
        std::ranges::transform(lower, lower.begin(), [](unsigned char c) { return char(std::tolower(c)); });
        for (const auto *suffix : {".texpack.toc", ".lodpack.toc"}) if (lower.ends_with(suffix)) {
            auto result = std::string(suffix); std::ranges::transform(result, result.begin(), [](unsigned char c) { return char(std::toupper(c)); }); return result;
        }
        auto result = std::filesystem::u8path(name).extension().string();
        if (result.empty() || result == ".") return "(No extension)";
        std::ranges::transform(result, result.begin(), [](unsigned char c) { return char(std::toupper(c)); }); return result;
    }

    bool canInspect(const std::string &name) {
        const auto ext = extensionOf(name);
        return ext == ".WAD" || ext == ".DCB" || ext == ".WYPDB" || ext == ".TEXPACK" || ext == ".TEXPACK.TOC" ||
               ext == ".LODPACK" || ext == ".LODPACK.TOC" || ext == ".SHADERPACK";
    }

    std::vector<Container> discover(const std::filesystem::path &path, const CheckCancel &cancel) {
        const auto absolute = std::filesystem::absolute(path).lexically_normal();
        std::vector<Container> result;
        const bool directory = std::filesystem::is_directory(absolute);
        const auto addFile = [&](const std::filesystem::path &file) {
            if (cancel) cancel();
            if (result.size() >= 100000) throw FormatError("File list exceeds 100,000 entries");
            Container container;
            container.path = file;
            container.region.name = utf8(directory ? file.lexically_relative(absolute) : file.filename());
            std::error_code error;
            container.region.size = std::filesystem::file_size(file, error);
            if (error) { container.region.size = 0; container.error = error.message(); }
            container.recognized = canInspect(container.region.name);
            result.push_back(std::move(container));
        };
        if (directory) {
            for (const auto &entry : std::filesystem::recursive_directory_iterator(absolute, std::filesystem::directory_options::skip_permission_denied)) {
                if (cancel) cancel();
                if (entry.is_regular_file()) addFile(entry.path());
            }
        } else addFile(absolute);
        std::ranges::sort(result, {}, [](const auto &item) { return item.region.name; });
        return result;
    }

    std::vector<Tag> parseWtoc(const Read &read, Offset size, const CheckCancel &cancel) {
        const auto header = bytesAt(read, 0, 64, size);
        if (u32(header.data()) != 0x434f5457) throw FormatError("Expected Ragnarök WTOC magic");
        if (u32(header.data() + 4) != 2) throw FormatError("Only WTOC version 2 is supported");
        const auto count = u32(header.data() + 8);
        if (count > 1000000) throw FormatError("WTOC exceeds 1,000,000 entries");
        requireRange(64, Offset(count) * 144, size);
        std::vector<Entry> entries;
        entries.reserve(count);
        for (std::size_t i = 0; i < count; ++i) {
            if (cancel) cancel();
            const auto bytes = bytesAt(read, 64 + Offset(i) * 144, 144, size);
            const auto *p = bytes.data();
            Entry entry;
            auto &tag = entry.tag;
            tag.id = u16(p); tag.version = u16(p + 2); entry.length = u32(p + 4);
            tag.size = tag.id == 25 ? 0 : entry.length;
            tag.guid = (Offset(u32(p + 16)) << 32) | u32(p + 20);
            tag.name = nameOf(std::span(bytes).subspan(24, 56));
            tag.headerOffset = 64 + i * 144; tag.headerSize = 144;
            tag.debugSize = u32(p + 96); tag.tempSize = u32(p + 100);
            entry.align = std::max(1U, u32(p + 104)); tag.client = p[108]; tag.server = p[109];
            entry.arena = std::min<std::uint8_t>(p[111], 8); entry.bits = p[114];
            entry.memOffset = u64(p + 120); entry.debugOffset = u64(p + 128); entry.tempOffset = u64(p + 136);
            tag.category = kindOf(tag);
            if (tag.category == "Textures" && tag.size >= 0x98) tag.pattern = "texture.hexpat";
            if (tag.id == 11 && tag.size >= 4) tag.pattern = "dc_version.hexpat";
            entries.push_back(std::move(entry));
        }
        std::array<Offset, 9> bs{}, bo{};
        Offset pos = 64 + Offset(count) * 144, autopad = 0;
        std::size_t first = 0, block = 0;
        for (std::size_t i = 0; i < entries.size(); ++i) {
            if (cancel) cancel();
            const auto &entry = entries[i]; const auto t = entry.arena;
            const auto cumulative = add(bo[t], bs[t]);
            const auto pad = (entry.align - cumulative % entry.align) % entry.align;
            bs[t] = add(bs[t], pad);
            if (entry.tag.id == 25) autopad = entry.length;
            else bs[t] = add(bs[t], entry.length);
            bs[5] = add(bs[5], entry.tag.debugSize); bs[8] = add(bs[8], entry.tag.tempSize);
            if (!(entry.bits & 1) && i + 1 != entries.size()) continue;
            std::array<Offset, 9> region{}; auto end = pos;
            for (std::size_t k = 0; k < 9; ++k) { region[k] = end; end = add(end, bs[k]); }
            requireRange(pos, end - pos, size); requireRange(end, autopad, size);
            const auto place = [&](std::size_t arena, Offset offset, Offset length) {
                if (offset < bo[arena]) throw FormatError("WTOC arena offset precedes current block");
                requireRange(offset - bo[arena], length, bs[arena]);
                const auto value = add(region[arena], offset - bo[arena]);
                requireRange(value, length, size); return value;
            };
            for (std::size_t j = first; j <= i; ++j) {
                auto &item = entries[j]; auto &tag = item.tag; tag.block = block;
                tag.offset = tag.id == 25 ? end : place(item.arena, item.memOffset, tag.size);
                if (tag.debugSize) tag.debugOffset = place(5, item.debugOffset, tag.debugSize);
                if (tag.tempSize) tag.tempOffset = place(8, item.tempOffset, tag.tempSize);
            }
            for (std::size_t k = 0; k < 9; ++k) { bo[k] = add(bo[k], bs[k]); bs[k] = 0; }
            pos = end + autopad; autopad = 0; first = i + 1; ++block;
        }
        std::vector<Tag> tags; tags.reserve(entries.size());
        for (auto &entry : entries) tags.push_back(std::move(entry.tag));
        return tags;
    }

    void inspect(Container &container, const std::shared_ptr<FileSource> &source, const CheckCancel &cancel) {
        const auto size = source->size();
        const Read read = [source](auto offset, auto output) { source->read(offset, output); };
        container.inspected = true; container.compressed = source->decoded(); container.region.size = size; container.source = source;
        const auto head = bytesAt(read, 0, std::min<Offset>(size, 128), size);
        if (head.size() >= 4 && u32(head.data()) == 0x434f5457) {
            container.tags = parseWtoc(read, size, cancel); container.pattern = "wad.hexpat"; return;
        }
        const auto ext = extensionOf(container.region.name);
        if (ext == ".WAD" || ext == ".DCB" || ext == ".WYPDB") throw FormatError("Expected WTOC magic after LZ4 decoding");
        if (ext == ".TEXPACK" || ext == ".TEXPACK.TOC") {
            requireRange(0, 56, size);
            const auto tocSize = u32(head.data() + 32), blocks = u32(head.data() + 36), blockOff = u32(head.data() + 40), count = u32(head.data() + 44);
            if (u32(head.data() + 48) != 6) throw FormatError("Only texpack version 6 is supported");
            requireRange(0, tocSize, size); requireRange(56, Offset(count) * 24 + u32(head.data() + 52), tocSize);
            if (blocks) requireRange(blockOff, 32, tocSize);
            if (count > 1000000) throw FormatError("Too many texpack keys");
            for (std::size_t i = 0; i < count; ++i) {
                if (cancel) cancel();
                auto record = bytesAt(read, 56 + i * 24, 24, tocSize);
                Tag tag; tag.name = hashName(u64(record.data() + 8)); tag.category = "Texture keys";
                tag.offset = tag.headerOffset = 56 + i * 24; tag.size = tag.headerSize = 24; tag.pattern = "texkey.hexpat";
                container.tags.push_back(std::move(tag));
            }
            container.pattern = "texpack.hexpat"; return;
        }
        if (ext == ".LODPACK" || ext == ".LODPACK.TOC") {
            requireRange(0, 16, size);
            const auto groups = u32(head.data()), blocks = u32(head.data() + 4);
            if (Offset(groups) + blocks > 1000000) throw FormatError("Too many lodpack records");
            const Offset blockStart = 16 + Offset(groups) * 24;
            requireRange(16, Offset(groups + Offset(blocks)) * 24, size);
            for (std::size_t i = 0; i < Offset(groups) + blocks; ++i) {
                if (cancel) cancel();
                const bool group = i < groups; const auto offset = group ? 16 + i * 24 : blockStart + (i - groups) * 24;
                auto record = bytesAt(read, offset, 24, size);
                if (!group) {
                    const auto index = u32(record.data());
                    if (index >= groups) throw FormatError("LOD block references missing group");
                    const auto parent = bytesAt(read, 16 + Offset(index) * 24, 24, size);
                    requireRange(u32(record.data() + 4), u32(record.data() + 16), u32(parent.data() + 16));
                }
                Tag tag; tag.name = hashName(u64(record.data() + 8)); tag.category = group ? "LOD group records" : "LOD block records";
                tag.offset = tag.headerOffset = offset; tag.size = tag.headerSize = 24;
                tag.pattern = group ? "lodgroup.hexpat" : "lodblock.hexpat"; container.tags.push_back(std::move(tag));
            }
            container.pattern = "lodpack.hexpat"; return;
        }
        if (head.size() >= 8 && u64(head.data()) == 0x4b43415052444853ULL) {
            requireRange(0, 128, size);
            if (u64(head.data() + 8) != 11) throw FormatError("Only shaderpack version 11 is supported");
            const auto count = u64(head.data() + 16), table = u64(head.data() + 48), bins = u64(head.data() + 80);
            const auto rawSize = u64(head.data() + 112), compressedSize = u64(head.data() + 120);
            if (count > 1000000) throw FormatError("Too many shader records");
            requireRange(table, count * 104, size); requireRange(bins, compressedSize, size);
            const bool compressed = rawSize != 0 && rawSize != compressedSize;
            for (std::size_t i = 0; i < count; ++i) {
                if (cancel) cancel();
                const auto at = table + i * 104; auto record = bytesAt(read, at, 104, size);
                const auto codeSize = u32(record.data() + 12), codeOffset = u32(record.data() + 16);
                requireRange(codeOffset, codeSize, compressed ? rawSize : compressedSize);
                Tag tag; tag.name = nameOf(std::span(record).subspan(36, 64)); if (tag.name.empty()) tag.name = hashName(u64(record.data()));
                tag.headerOffset = at; tag.headerSize = 104;
                tag.category = compressed ? "Shader records (compressed bins)" : "Shader bytecode";
                tag.offset = compressed ? at : add(bins, codeOffset); tag.size = compressed ? 104 : codeSize;
                requireRange(tag.offset, tag.size, size); tag.pattern = compressed ? "shaderrecord.hexpat" : "raw.hexpat";
                container.tags.push_back(std::move(tag));
            }
            container.pattern = "shaderpack.hexpat"; return;
        }
        if (ext == ".SHADERPACK") throw FormatError("Expected SHDRPACK magic");
        container.pattern = "raw.hexpat";
    }
}
