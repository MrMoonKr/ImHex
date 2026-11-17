#include <gow_format.hpp>
#include <algorithm>
#include <cctype>
#include <cstring>
#include <map>

namespace hex::plugin::gow3::format {
    namespace {
        constexpr Offset HeaderSize = 64;
        std::uint16_t u16(const std::uint8_t *p) { return std::uint16_t(p[0] | (std::uint16_t(p[1]) << 8)); }
        std::uint32_t u32(const std::uint8_t *p) { return p[0] | (std::uint32_t(p[1]) << 8) | (std::uint32_t(p[2]) << 16) | (std::uint32_t(p[3]) << 24); }
        std::string utf8(const std::filesystem::path &path) {
            const auto value = path.generic_u8string();
            return {reinterpret_cast<const char*>(value.data()), value.size()};
        }
        std::string nameOf(std::span<const std::uint8_t> bytes) {
            std::string result;
            constexpr char digits[] = "0123456789abcdef";
            for (const auto byte : bytes) {
                if (byte == 0) break;
                if (byte >= 32 && byte < 127) result += char(byte);
                else { result += "\\x"; result += digits[byte >> 4]; result += digits[byte & 15]; }
            }
            return result;
        }
        std::string kindOf(const Tag &tag) {
            switch (tag.id) {
                case 7: return "Text & fonts";
                case 8: return "Embedded files";
                case 9: return "Raw data (MSH / NCV)";
                case 28: return "Font atlases";
                default: break;
            }
            if (tag.id != 1) return "Control / auxiliary";
            if (tag.size == 0) return "Links";
            if (!tag.type) return "Unknown resources";
            const auto type = *tag.type;
            if (type & 0x80000000) return "Context chunks";
            static const std::map<std::uint32_t, std::string> kinds {
                {0x10001, "Object data"}, {0x20001, "Object prototypes"}, {0x30001, "Game objects"}, {0x2a0001, "Context names"},
                {3, "Animations"}, {0x10003, "Animation streams"}, {0x10004, "Scripts"},
                {6, "Lights"}, {0x10006, "Spatial lighting"}, {0x10007, "Textures"},
                {8, "Materials"}, {0x10008, "Shaders"}, {0x30008, "Shader debug info"}, {9, "Cameras"},
                {0x1000d, "Mesh geometry"}, {0x2000d, "Models"},
                {0xe, "Collision sheets"}, {0x1000e, "Collision volumes"}, {0x2000e, "Collision debug"}, {0x3000e, "Collision skins"},
                {0xb000f, "FX geometry"}, {0xc000f, "Particle fields"}, {0xd000f, "Particles (PTC)"}, {0xf000f, "Particles (PTS)"},
                {0x12, "Behaviours"}, {0x10012, "SMI"}, {0x30013, "Sound banks"}, {0x40013, "Sound emitters"}, {0x18, "Occluders"}
            };
            if (const auto it = kinds.find(type); it != kinds.end()) return it->second;
            if ((type & 0xffff) == 0xf) return "Particle emitters";
            if ((type & 0xffff) == 0x10) return "Waypoints";
            constexpr char digits[] = "0123456789abcdef";
            std::string label = "Asset type 0x";
            for (int shift = 28; shift >= 0; shift -= 4) label += digits[(type >> shift) & 15];
            return label;
        }
        std::string patternFor(const Tag &tag) {
            if (tag.id != 1 || !tag.type) return "raw.hexpat";
            switch (*tag.type) {
                case 0x10007: return "texture.hexpat";
                case 8: return "material.hexpat";
                case 0x1000d: return "mesh.hexpat";
                case 0x10001: return "object.hexpat";
                case 0x30001: return "game_object.hexpat";
                case 9: return "camera.hexpat";
                default: return "resource.hexpat";
            }
        }
    }

    void requireRange(Offset offset, Offset size, Offset total) {
        if (offset > total || size > total - offset) throw FormatError("Range exceeds the source data");
    }
    FileSource::FileSource(std::filesystem::path path)
        : m_path(std::filesystem::absolute(path).lexically_normal()), m_file(m_path, std::ios::binary) {
        if (!m_file) throw FormatError("Cannot open source file (check path and network access)");
        m_file.seekg(0, std::ios::end);
        const auto end = m_file.tellg();
        if (end < 0) throw FormatError("Cannot determine source size");
        m_size = static_cast<Offset>(end);
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
        std::scoped_lock lock(m_mutex); readFile(offset, output);
    }

    bool Tag::isAsset() const {
        if (id == 1) return type && !(*type & 0x80000000);
        return size != 0 && (id == 7 || id == 8 || id == 9 || id == 28);
    }
    std::string extensionOf(const std::string &name) {
        auto result = std::filesystem::u8path(name).extension().string();
        if (result.empty() || result == ".") return "(No extension)";
        std::ranges::transform(result, result.begin(), [](unsigned char c) { return char(std::toupper(c)); }); return result;
    }
    bool canInspect(const std::string &name) { const auto ext = extensionOf(name); return ext == ".WAD" || ext == ".DDS"; }

    std::vector<Container> discover(const std::filesystem::path &path, const CheckCancel &cancel) {
        const auto absolute = std::filesystem::absolute(path).lexically_normal();
        std::vector<Container> result;
        std::vector<std::pair<std::filesystem::path, std::string>> roots;
        std::optional<std::filesystem::path> installation;
        if (std::filesystem::is_directory(absolute / "game" / "EXEC")) installation = absolute;
        else if (std::filesystem::is_directory(absolute / "EXEC")) {
            auto name = utf8(absolute.filename()); std::ranges::transform(name, name.begin(), [](unsigned char c) { return char(std::tolower(c)); });
            if (name == "game") installation = absolute.parent_path();
        }
        if (installation) {
            roots.emplace_back(*installation / "game", "game/");
            for (const auto &entry : std::filesystem::directory_iterator(*installation)) {
                if (cancel) cancel();
                if (utf8(entry.path().filename()).starts_with("_Update") && std::filesystem::is_directory(entry.path() / "game"))
                    roots.emplace_back(entry.path() / "game", utf8(entry.path().filename()) + "/game/");
            }
        } else roots.emplace_back(absolute, "");
        for (const auto &[root, prefix] : roots) {
            const bool directory = std::filesystem::is_directory(root);
            const auto addFile = [&](const std::filesystem::path &file) {
                if (cancel) cancel();
                if (result.size() >= 200000) throw FormatError("File list exceeds 200,000 entries");
                Container container;
                container.path = file;
                container.region.name = prefix + utf8(directory ? file.lexically_relative(root) : file.filename());
                std::error_code error;
                container.region.size = std::filesystem::file_size(file, error);
                if (error) { container.region.size = 0; container.error = error.message(); }
                container.recognized = canInspect(container.region.name);
                result.push_back(std::move(container));
            };
            if (directory) {
                for (const auto &entry : std::filesystem::recursive_directory_iterator(root, std::filesystem::directory_options::skip_permission_denied)) {
                    if (cancel) cancel();
                    if (entry.is_regular_file()) addFile(entry.path());
                }
            } else addFile(root);
        }
        std::ranges::sort(result, {}, [](const auto &item) { return item.region.name; }); return result;
    }

    std::vector<Tag> parseWad(const Read &read, Offset size, const CheckCancel &cancel) {
        if (size == 0) throw FormatError("Empty GoW III WAD");
        std::vector<Tag> tags;
        Offset position = 0;
        std::optional<std::size_t> parent;
        bool pending = false;
        while (position < size) {
            if (cancel) cancel();
            if (tags.size() >= 500000) throw FormatError("WAD exceeds 500,000 tags");
            requireRange(position, HeaderSize, size);
            std::array<std::uint8_t, HeaderSize> bytes{}; read(position, bytes);
            Tag tag;
            tag.id = u16(bytes.data()); tag.flags = u16(bytes.data() + 2); tag.declaredSize = u32(bytes.data() + 4);
            tag.name = nameOf(std::span(bytes).subspan(8, 56));
            tag.headerOffset = position; tag.headerSize = HeaderSize;
            tag.offset = position + HeaderSize; tag.size = tag.id == 0 ? 0 : tag.declaredSize; tag.parent = parent;
            requireRange(tag.offset, tag.size, size);
            if (tag.id == 1 && tag.size >= 4) {
                std::array<std::uint8_t, 4> type{}; read(tag.offset, type); tag.type = u32(type.data());
            }
            tag.category = kindOf(tag); tag.pattern = patternFor(tag);
            tags.push_back(std::move(tag));
            const auto &current = tags.back();
            if (current.id == 2) {
                if (pending) throw FormatError("Nested group start without owner resource");
                pending = true;
            } else if (current.id == 1 && pending) {
                parent = tags.size() - 1; pending = false;
            } else if (current.id == 3) {
                if (pending) pending = false;
                else if (!parent) throw FormatError("Unmatched WAD group end");
                else parent = tags[*parent].parent;
            }
            const auto end = current.offset + current.size;
            const auto padding = (16 - end % 16) % 16;
            requireRange(end, padding, size); position = end + padding;
        }
        if (pending || parent) throw FormatError("Unclosed WAD resource group");
        return tags;
    }
    void inspect(Container &container, const std::shared_ptr<FileSource> &source, const CheckCancel &cancel) {
        container.inspected = true; container.region.size = source->size(); container.source = source;
        const Read read = [source](auto offset, auto output) { source->read(offset, output); };
        if (extensionOf(container.region.name) == ".WAD") {
            container.tags = parseWad(read, source->size(), cancel); container.pattern = "wad.hexpat";
        } else if (extensionOf(container.region.name) == ".DDS") {
            requireRange(0, 128, source->size());
            std::array<std::uint8_t, 128> header{}; read(0, header);
            if (u32(header.data()) != 0x20534444 || u32(header.data() + 4) != 124 || u32(header.data() + 76) != 32)
                throw FormatError("Invalid DDS header");
            container.pattern = "dds.hexpat";
        }
    }
}
