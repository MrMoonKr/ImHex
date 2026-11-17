#pragma once
#include <gow_format.hpp>
#include <hex/api/task_manager.hpp>
#include <hex/ui/view.hpp>
#include <deque>
#include <map>

namespace hex::plugin::gow3 {
    class AssetProvider;
    class ViewGow3Browser : public View::Window {
    public:
        ViewGow3Browser();
        ~ViewGow3Browser() override;
        void drawContent() override;
        void drawAlwaysVisibleContent() override;
        void drawHelpText() override;
        bool shouldDraw() const override { return true; }
        bool hasViewMenuItemEntry() const override { return false; }
    private:
        friend struct BrowserChecks;
        struct ScanState {
            std::mutex mutex;
            std::deque<format::Container> ready;
            std::size_t total = 0;
            bool done = false;
            std::string error;
        };
        struct InspectionState {
            std::mutex mutex;
            format::Container result;
            std::shared_ptr<format::FileSource> source;
            std::size_t index = 0;
            bool done = false;
        };
        struct AssetRef { std::size_t container; std::size_t tag; };
        using Names = std::map<std::string, std::vector<AssetRef>>;
        using TagKinds = std::map<std::string, std::vector<std::size_t>>;
        void startScan(const std::filesystem::path &path);
        void inspectContainer(std::size_t index);
        void drawAssets();
        void drawContainers();
        void drawContainer(std::size_t index, bool showAll);
        bool containerMatchesFilter(std::size_t index) const;
        static std::string containerExtension(const std::string &name);
        void drawTag(std::size_t container, std::size_t tag);
        void openTag(AssetRef ref, bool header = false);
        void openRegion(std::size_t container, format::Region region, const std::string &pattern);
        void applyPattern(const std::string &pattern);
        bool matchesFilter(const std::string &name) const;
        TaskHolder m_scanTask, m_inspectTask;
        std::shared_ptr<ScanState> m_scan;
        std::shared_ptr<InspectionState> m_inspection;
        std::shared_ptr<format::FileSource> m_cachedSource;
        std::weak_ptr<AssetProvider> m_opening;
        std::vector<format::Container> m_containers;
        std::vector<TagKinds> m_containerKinds;
        std::map<std::string, std::vector<std::size_t>> m_containerExtensions;
        std::map<std::string, Names> m_assets;
        std::optional<AssetRef> m_selected;
        std::string m_path, m_filter, m_error;
        std::size_t m_assetCount = 0, m_inspectedCount = 0, m_tagCount = 0;
        bool m_autoPattern = true, m_groupContainerTags = true, m_indexAll = false;
    };
}
