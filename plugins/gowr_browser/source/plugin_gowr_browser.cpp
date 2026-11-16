#include <hex/plugin.hpp>
#include <hex/api/content_registry/provider.hpp>
#include <hex/api/content_registry/views.hpp>
#include <hex/api/localization_manager.hpp>

#include <asset_provider.hpp>
#include <view_gowr_browser.hpp>
#include <romfs/romfs.hpp>

using namespace hex;
using namespace hex::plugin::gowr;

IMHEX_PLUGIN_SETUP("GoWR Asset Browser", "ImHex contributors", "God of War Ragnarok PC folder / WTOC asset browser") {
    LocalizationManager::addLanguages(romfs::get("lang/languages.json").string(), [](const std::filesystem::path &path) {
        return romfs::get(path).string();
    });
    ContentRegistry::Provider::add<AssetProvider>(true);
    ContentRegistry::Views::add<ViewGowrBrowser>();
}
