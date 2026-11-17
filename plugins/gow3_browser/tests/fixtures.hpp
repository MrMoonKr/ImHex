#pragma once
#include <gow_format.hpp>
#include <algorithm>
#include <chrono>
#include <cstring>

namespace fixtures {
    using namespace hex::plugin::gow3::format;
    inline void check(bool value, const char *message) { if (!value) throw std::runtime_error(message); }
    inline void put(Bytes &data, std::size_t offset, Offset value, std::size_t width = 4) {
        for (std::size_t i = 0; i < width; ++i) data.at(offset + i) = std::uint8_t(value >> (8 * i));
    }
    inline void text(Bytes &data, std::size_t offset, const std::string &name) { std::copy(name.begin(), name.end(), data.begin() + offset); }
    inline Read reader(const Bytes &data) {
        return [&data](Offset offset, std::span<std::uint8_t> out) { requireRange(offset, out.size(), data.size()); std::memcpy(out.data(), data.data() + offset, out.size()); };
    }
    inline void save(const std::filesystem::path &path, const Bytes &bytes) {
        std::ofstream file(path, std::ios::binary); file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        if (!file) throw std::runtime_error("Fixture write failed");
    }
    inline Bytes resource(std::uint32_t type, std::size_t size = 4) { Bytes data(size); put(data, 0, type); return data; }
    inline Bytes dds() {
        Bytes data(136); put(data, 0, 0x20534444); put(data, 4, 124); put(data, 8, 0x1007); put(data, 12, 4); put(data, 16, 4);
        put(data, 28, 1); put(data, 76, 32); put(data, 80, 4); put(data, 84, 0x31545844); put(data, 108, 0x1000); return data;
    }
    inline Bytes texture() {
        auto data = resource(0x10007, 76); text(data, 4, "texture.dds"); const auto image = dds(); data.insert(data.end(), image.begin(), image.end()); return data;
    }
    inline Bytes material() {
        auto data = resource(8, 320); text(data, 128, "shader_name"); data[219] = 0x51; text(data, 264, "texture.dds"); return data;
    }
    inline Bytes mesh() { auto data = resource(0x1000d, 28); put(data, 4, 28); put(data, 8, 1, 2); put(data, 24, 28); return data; }
    inline Bytes object() {
        auto data = resource(0x10001, 148); put(data, 4, 2); put(data, 16, 148); text(data, 36, "root"); text(data, 92, "child"); return data;
    }
    inline Bytes tag(std::uint16_t id, const std::string &name = "", const Bytes &payload = {}, std::optional<std::uint32_t> declared = {}) {
        Bytes data(64 + payload.size()); put(data, 0, id, 2); put(data, 2, 13, 2);
        put(data, 4, declared.value_or(static_cast<std::uint32_t>(payload.size()))); text(data, 8, name);
        std::copy(payload.begin(), payload.end(), data.begin() + 64); data.resize((data.size() + 15) & ~std::size_t(15)); return data;
    }
    inline void append(Bytes &to, const Bytes &from) { to.insert(to.end(), from.begin(), from.end()); }
    inline Bytes wad() {
        Bytes data;
        append(data, tag(18, "WAD_header"));
        append(data, tag(0, "EntityCount", {}, 0xffffffff));
        append(data, tag(2, "GroupStart"));
        append(data, tag(25, "DebugStart"));
        append(data, tag(1, "owner", object()));
        append(data, tag(2, "GroupStart"));
        append(data, tag(1, "texture.dds", texture()));
        append(data, tag(8, "embedded.bin", {1, 2, 3}));
        append(data, tag(3, "GroupEnd"));
        append(data, tag(1, "link"));
        append(data, tag(3, "GroupEnd"));
        append(data, tag(1, "TXRX_context", resource(0x80000007)));
        append(data, tag(7, "MSGS_TXT", {'h', 'i'}));
        append(data, tag(28, "atlas", {1, 2, 3, 4}));
        return data;
    }
    struct TempFiles {
        std::filesystem::path directory;
        std::vector<std::filesystem::path> files, directories;
        TempFiles() {
            directory = std::filesystem::temp_directory_path() / ("imhex-gow3-checks-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
            std::filesystem::create_directory(directory);
        }
        std::filesystem::path add(const std::string &name, const Bytes &data) {
            auto path = directory / name;
            std::vector<std::filesystem::path> missing;
            for (auto parent = path.parent_path(); parent != directory && !std::filesystem::exists(parent); parent = parent.parent_path()) missing.push_back(parent);
            for (auto it = missing.rbegin(); it != missing.rend(); ++it) { std::filesystem::create_directory(*it); directories.push_back(*it); }
            save(path, data); files.push_back(path); return path;
        }
        ~TempFiles() {
            std::error_code error;
            for (const auto &file : files) std::filesystem::remove(file, error);
            for (auto it = directories.rbegin(); it != directories.rend(); ++it) std::filesystem::remove(*it, error);
            std::filesystem::remove(directory, error);
        }
    };
}
