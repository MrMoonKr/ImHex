#pragma once

#include <gow_format.hpp>
#include <hex/providers/provider.hpp>

namespace hex::plugin::gow1 {

    class AssetProvider : public prv::Provider, public prv::IProviderDataDescription {
    public:
        AssetProvider() = default;
        AssetProvider(std::shared_ptr<format::FileSource> source, format::Region region, std::string pattern);

        OpenResult open() override;
        void close() override { m_open = false; m_source.reset(); }
        bool isAvailable() const override { return m_open; }
        bool isReadable() const override { return m_open; }
        bool isWritable() const override { return false; }
        bool isResizable() const override { return false; }
        bool isSavable() const override { return false; }
        void readRaw(u64 offset, void *buffer, std::size_t size) override;
        void writeRaw(u64, const void *, std::size_t) override {}
        u64 getActualSize() const override { return m_region.size; }
        UnlocalizedString getTypeName() const override;
        std::string getName() const override { return m_region.name; }
        const char *getIcon() const override;
        std::vector<Description> getDataDescription() const override;
        void loadSettings(const nlohmann::json &settings) override;
        nlohmann::json storeSettings(nlohmann::json settings) const override;

        const std::string &pattern() const { return m_pattern; }
        bool matches(const std::filesystem::path &path, const format::Region &region) const;
    private:
        std::shared_ptr<format::FileSource> m_source;
        std::filesystem::path m_path;
        format::Region m_region;
        std::string m_pattern;
        bool m_open = false;
    };

}
