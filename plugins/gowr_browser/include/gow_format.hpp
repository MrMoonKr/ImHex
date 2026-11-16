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

namespace hex::plugin::gowr::format {
    using Offset = std::uint64_t;
    using Bytes = std::vector<std::uint8_t>;
    using Read = std::function<void(Offset, std::span<std::uint8_t>)>;
    using CheckCancel = std::function<void()>;
    constexpr Offset DecodeLimit = 512ULL * 1024 * 1024;
    struct FormatError : std::runtime_error { using std::runtime_error::runtime_error; };
    void requireRange(Offset offset, Offset size, Offset total);

    // Bounded file reads; decoded LZ4 buffers are shared only with active users.
    class FileSource {
    public:
        explicit FileSource(std::filesystem::path path, bool decode = true, const CheckCancel &cancel = {});
        void read(Offset offset, std::span<std::uint8_t> output);
        [[nodiscard]] Offset size() const { return m_decoded ? m_data.size() : m_size; }
        [[nodiscard]] Offset storedSize() const { return m_size; }
        [[nodiscard]] bool decoded() const { return m_decoded; }
        [[nodiscard]] bool compressed() const { return m_compressed; }
        [[nodiscard]] const std::filesystem::path &path() const { return m_path; }
    private:
        void readFile(Offset offset, std::span<std::uint8_t> output);
        std::filesystem::path m_path;
        std::ifstream m_file;
        Offset m_size = 0;
        bool m_compressed = false, m_decoded = false;
        Bytes m_data;
        std::mutex m_mutex;
        std::array<std::uint8_t, 65536> m_cache{};
        Offset m_cacheOffset = 0;
        std::size_t m_cacheSize = 0;
    };

    struct Region { std::string name; Offset offset = 0, size = 0; };
    struct Tag {
        std::string name;
        std::string category;
        std::uint16_t id = 0, version = 0;
        std::uint8_t server = 0, client = 0;
        std::uint64_t guid = 0;
        Offset headerOffset = 0, headerSize = 0;
        Offset offset = 0, size = 0;
        Offset debugOffset = 0, debugSize = 0, tempOffset = 0, tempSize = 0;
        std::size_t block = 0;
        std::string pattern = "raw.hexpat";
        [[nodiscard]] const std::string &kind() const { return category; }
    };
    struct Container {
        Region region;
        std::filesystem::path path;
        std::vector<Tag> tags;
        std::string error;
        std::string pattern = "raw.hexpat";
        bool inspected = false;
        bool recognized = false;
        bool compressed = false;
        std::weak_ptr<FileSource> source;
    };

    std::string extensionOf(const std::string &name);
    bool canInspect(const std::string &name);
    std::vector<Container> discover(const std::filesystem::path &path, const CheckCancel &cancel = {});
    std::vector<Tag> parseWtoc(const Read &read, Offset size, const CheckCancel &cancel = {});
    void inspect(Container &container, const std::shared_ptr<FileSource> &source, const CheckCancel &cancel = {});
}
