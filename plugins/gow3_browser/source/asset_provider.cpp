#include <asset_provider.hpp>

#include <fonts/vscode_icons.hpp>
#include <hex/helpers/fmt.hpp>
#include <nlohmann/json.hpp>
#include <wolv/utils/string.hpp>
#include <limits>

namespace hex::plugin::gow3 {

    AssetProvider::AssetProvider(std::filesystem::path path, format::Region region, std::string pattern,
                                 std::shared_ptr<format::FileSource> source)
        : m_source(std::move(source)), m_path(std::move(path)), m_region(std::move(region)), m_pattern(std::move(pattern)) {}

    prv::Provider::OpenResult AssetProvider::open() {
        try {
            if (!m_source) m_source = std::make_shared<format::FileSource>(m_path);
            if (m_region.size == std::numeric_limits<u64>::max()) m_region.size = m_source->size();
            format::requireRange(m_region.offset, m_region.size, m_source->size());
            m_open = true;
            return {};
        } catch (const std::exception &error) { return OpenResult::failure(error.what()); }
    }

    void AssetProvider::readRaw(u64 offset, void *buffer, std::size_t size) {
        if (!m_open || buffer == nullptr) return;
        format::requireRange(offset, size, m_region.size);
        m_source->read(m_region.offset + offset, { static_cast<std::uint8_t*>(buffer), size });
    }

    UnlocalizedString AssetProvider::getTypeName() const { return "hex.gow3.provider.asset"_unlocalized; }
    const char *AssetProvider::getIcon() const { return ICON_VS_FILE_BINARY; }

    std::vector<prv::IProviderDataDescription::Description> AssetProvider::getDataDescription() const {
        return {
            { "Source", wolv::util::toUTF8String(m_path) },
            { "Source offset", fmt::format("0x{:X}", m_region.offset) },
            { "Size", fmt::format("{} bytes", m_region.size) },
            { "Access", "Read only" }
        };
    }

    bool AssetProvider::matches(const std::filesystem::path &path, const format::Region &region) const {
        return m_path == path && m_region.offset == region.offset &&
            (m_region.size == region.size || region.size == std::numeric_limits<u64>::max()) && m_region.name == region.name;
    }

    void AssetProvider::loadSettings(const nlohmann::json &settings) {
        Provider::loadSettings(settings);
        m_path = std::filesystem::u8path(settings.at("path").get<std::string>());
        m_region = { settings.at("name").get<std::string>(), settings.at("offset").get<u64>(), settings.at("size").get<u64>() };
        m_pattern = settings.value("pattern", "raw.hexpat");
    }

    nlohmann::json AssetProvider::storeSettings(nlohmann::json settings) const {
        settings = Provider::storeSettings(std::move(settings));
        settings["path"] = wolv::util::toUTF8String(m_path);
        settings["name"] = m_region.name;
        settings["offset"] = m_region.offset;
        settings["size"] = m_region.size;
        settings["pattern"] = m_pattern;
        return settings;
    }

}
