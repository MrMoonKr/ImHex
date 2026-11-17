#include <hex/plugin.hpp>
#include <hex/api/content_registry/provider.hpp>
#include <hex/api/content_registry/views.hpp>
#include <hex/api/localization_manager.hpp>

#include <asset_provider.hpp>
#include <view_gow3_browser.hpp>
#include <romfs/romfs.hpp>

using namespace hex;
using namespace hex::plugin::gow3;

IMHEX_PLUGIN_SETUP("GoW3 Asset Browser", "ImHex contributors", "God of War III Remastered PS4 folder / WAD asset browser") {
    LocalizationManager::addLanguages(romfs::get("lang/languages.json").string(), [](const std::filesystem::path &path) {
        return romfs::get(path).string();
    });
    ContentRegistry::Provider::add<AssetProvider>(true);
    ContentRegistry::Views::add<ViewGow3Browser>();
}
