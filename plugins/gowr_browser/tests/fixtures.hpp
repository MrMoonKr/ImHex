#pragma once
#include <gow_format.hpp>
#include <lz4frame.h>
#include <chrono>
#include <cstring>

namespace fixtures {
    using namespace hex::plugin::gowr::format;
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
    inline Bytes frame(const Bytes &data) {
        LZ4F_preferences_t preferences{};
        preferences.frameInfo.contentChecksumFlag = LZ4F_contentChecksumEnabled;
        preferences.frameInfo.blockChecksumFlag = LZ4F_blockChecksumEnabled;
        preferences.frameInfo.contentSize = data.size();
        Bytes output(LZ4F_compressFrameBound(data.size(), &preferences));
        const auto size = LZ4F_compressFrame(output.data(), output.size(), data.data(), data.size(), &preferences);
        if (LZ4F_isError(size)) throw std::runtime_error(LZ4F_getErrorName(size));
        output.resize(size); return output;
    }
    inline Bytes wtoc(bool texture = false) {
        if (texture) {
            Bytes data(64 + 144 + 168);
            put(data, 0, 0x434f5457); put(data, 4, 2); put(data, 8, 1);
            put(data, 64, 1, 2); put(data, 68, 168); text(data, 64 + 24, "TXR_sample");
            data[64 + 108] = 1; data[64 + 109] = 25; data[64 + 114] = 1;
            text(data, 208 + 12, "sample_texture"); put(data, 208 + 72, 64, 2); put(data, 208 + 74, 32, 2);
            put(data, 208 + 104, 0x20464e47); put(data, 208 + 152, 0x52455355); put(data, 208 + 160, 123456, 8);
            return data;
        }
        const std::size_t start = 64 + 5 * 144;
        Bytes data(start + 23);
        put(data, 0, 0x434f5457); put(data, 4, 2); put(data, 8, 5);
        for (std::size_t i = 0; i < 5; ++i) {
            const auto at = 64 + i * 144;
            put(data, at, i == 3 ? 25 : 1, 2); put(data, at + 104, 1);
            text(data, at + 24, std::array<std::string, 5>{"first", "second", "aligned", "pad", "tail"}[i]);
        }
        put(data, 68, 3); put(data, 64 + 96, 2); put(data, 64 + 100, 1);
        put(data, 64 + 16, 0x11223344); put(data, 64 + 20, 0xaabbccdd);
        put(data, 64 + 144 + 4, 4); data[64 + 144 + 111] = 1; put(data, 64 + 144 + 104, 8); data[64 + 144 + 114] = 1;
        put(data, 64 + 288 + 4, 2); put(data, 64 + 288 + 104, 8); put(data, 64 + 288 + 120, 8, 8);
        put(data, 64 + 432 + 4, 5); data[64 + 432 + 114] = 1;
        put(data, 64 + 576 + 4, 1); data[64 + 576 + 111] = 250; put(data, 64 + 576 + 120, 1, 8); put(data, 64 + 576 + 104, 0);
        text(data, start, "ABCDEFGdbT"); text(data, start + 15, "HI"); text(data, start + 22, "Z");
        return data;
    }
    inline Bytes texpack() {
        Bytes data(80); put(data, 32, 80); put(data, 44, 1); put(data, 48, 6);
        put(data, 56, 123, 8); put(data, 64, 456, 8); put(data, 72, ~Offset(0), 8); return data;
    }
    inline Bytes lodpack() {
        Bytes data(64); put(data, 0, 1); put(data, 4, 1);
        put(data, 16, 128, 8); put(data, 24, 123, 8); put(data, 32, 16);
        put(data, 48, 456, 8); put(data, 56, 16); return data;
    }
    inline Bytes shaderpack() {
        Bytes data(236); put(data, 0, 0x4b43415052444853ULL, 8); put(data, 8, 11, 8); put(data, 16, 1, 8);
        put(data, 48, 128, 8); put(data, 80, 232, 8);
        put(data, 112, 4, 8); put(data, 120, 4, 8);
        put(data, 128, 123, 8); put(data, 140, 4); text(data, 164, "sample_shader"); text(data, 232, "DXBC"); return data;
    }
    struct TempFiles {
        std::filesystem::path directory;
        std::vector<std::filesystem::path> files;
        TempFiles() {
            directory = std::filesystem::temp_directory_path() / ("imhex-gowr-checks-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
            std::filesystem::create_directory(directory);
        }
        std::filesystem::path add(const std::string &name, const Bytes &data) {
            auto path = directory / name; save(path, data); files.push_back(path); return path;
        }
        ~TempFiles() {
            std::error_code error;
            for (const auto &file : files) std::filesystem::remove(file, error);
            std::filesystem::remove(directory, error);
        }
    };
}
