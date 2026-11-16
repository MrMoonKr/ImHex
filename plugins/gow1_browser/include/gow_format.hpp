#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace hex::plugin::gow1::format {

    using Offset = std::uint64_t;
    using Bytes = std::vector<std::uint8_t>;
    using Read = std::function<void(Offset, std::span<std::uint8_t>)>;
    using CheckCancel = std::function<void()>;

    struct FormatError : std::runtime_error { using std::runtime_error::runtime_error; };
    void requireRange(Offset offset, Offset size, Offset total);

    // Owns the file independently of the browser and reads only requested ranges.
    // A small cache amortizes header reads on network shares. Access is serialized.
    class FileSource {
    public:
        explicit FileSource(std::filesystem::path path);
        void read(Offset offset, std::span<std::uint8_t> output);
        [[nodiscard]] Offset size() const { return m_size; }
        [[nodiscard]] const std::filesystem::path &path() const { return m_path; }
    private:
        std::filesystem::path m_path;
        std::ifstream m_file;
        Offset m_size = 0;
        std::mutex m_mutex;
        std::array<std::uint8_t, 65536> m_cache{};
        Offset m_cacheOffset = 0;
        std::size_t m_cacheSize = 0;
    };

    struct Region {
        std::string name;
        Offset offset = 0;
        Offset size = 0;
    };

    struct TocRecord {
        std::string name;
        std::uint32_t pak = 0;
        std::uint32_t size = 0;
        std::uint32_t sector = 0;
        std::size_t index = 0;
    };

    struct Tag {
        std::uint16_t tag = 0;
        std::uint16_t flags = 0;
        std::uint32_t declaredSize = 0;
        std::string name;
        Offset headerOffset = 0;
        Offset size = 0;
        std::optional<std::uint32_t> type;
        std::optional<std::size_t> parent;
        std::string shape;
        [[nodiscard]] Offset payloadOffset() const { return headerOffset + 32; }
        [[nodiscard]] std::string kind() const;
    };

    struct Container {
        Region region;
        std::size_t recordIndex = 0;
        bool wad = false;
        std::vector<Tag> tags;
        std::string error;
        bool available = true;
    };

    struct Catalog {
        std::vector<Container> containers;
        std::optional<Region> toc;
    };

    std::vector<Region> readIsoRoot(const Read &read, Offset size);
    std::vector<TocRecord> parseToc(std::span<const std::uint8_t> data);
    std::vector<Tag> parseWad(const Read &read, Offset size, const CheckCancel &checkCancel = {});
    Catalog readCatalog(const Read &read, Offset size, const std::string &filename);

}
