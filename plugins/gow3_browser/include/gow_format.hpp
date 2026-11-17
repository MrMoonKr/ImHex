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

namespace hex::plugin::gow3::format {
    using Offset = std::uint64_t;
    using Bytes = std::vector<std::uint8_t>;
    using Read = std::function<void(Offset, std::span<std::uint8_t>)>;
    using CheckCancel = std::function<void()>;
    struct FormatError : std::runtime_error { using std::runtime_error::runtime_error; };
    void requireRange(Offset offset, Offset size, Offset total);

    // Bounded, shared file reads; GoW III Remastered WADs are not compressed.
    class FileSource {
    public:
        explicit FileSource(std::filesystem::path path);
        void read(Offset offset, std::span<std::uint8_t> output);
        [[nodiscard]] Offset size() const { return m_size; }
        [[nodiscard]] const std::filesystem::path &path() const { return m_path; }
    private:
        void readFile(Offset offset, std::span<std::uint8_t> output);
        std::filesystem::path m_path;
        std::ifstream m_file;
        Offset m_size = 0;
        std::mutex m_mutex;
        std::array<std::uint8_t, 65536> m_cache{};
        Offset m_cacheOffset = 0;
        std::size_t m_cacheSize = 0;
    };

    struct Region { std::string name; Offset offset = 0, size = 0; };
    struct Tag {
        std::string name;
        std::string category;
        std::uint16_t id = 0, flags = 0;
        std::uint32_t declaredSize = 0;
        std::optional<std::uint32_t> type;
        std::optional<std::size_t> parent;
        Offset headerOffset = 0, headerSize = 0;
        Offset offset = 0, size = 0;
        std::string pattern = "raw.hexpat";
        [[nodiscard]] const std::string &kind() const { return category; }
        [[nodiscard]] bool isAsset() const;
    };
    struct Container {
        Region region;
        std::filesystem::path path;
        std::vector<Tag> tags;
        std::string error;
        std::string pattern = "raw.hexpat";
        bool inspected = false;
        bool recognized = false;
        std::weak_ptr<FileSource> source;
    };

    std::string extensionOf(const std::string &name);
    bool canInspect(const std::string &name);
    std::vector<Container> discover(const std::filesystem::path &path, const CheckCancel &cancel = {});
    std::vector<Tag> parseWad(const Read &read, Offset size, const CheckCancel &cancel = {});
    void inspect(Container &container, const std::shared_ptr<FileSource> &source, const CheckCancel &cancel = {});
}
