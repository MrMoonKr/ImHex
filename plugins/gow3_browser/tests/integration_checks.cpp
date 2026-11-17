#include "fixtures.hpp"
#include <asset_provider.hpp>
#include <view_gow3_browser.hpp>
#include <hex/api/content_registry/user_interface.hpp>
#include <hex/api/events/events_provider.hpp>
#include <hex/api/events/requests_provider.hpp>
#include <hex/api/events/requests_interaction.hpp>
#include <hex/api/imhex_api/provider.hpp>
#include <nlohmann/json.hpp>
#include <imgui.h>
#include <iostream>
#include <pl/pattern_language.hpp>
#include <pl/patterns/pattern.hpp>

namespace hex::plugin::gow3 {
    struct BrowserChecks {
        static void sourceOpening(const std::filesystem::path &path) {
            using fixtures::check;
            ViewGow3Browser browser;
            browser.startScan(path);
            browser.m_scanTask.wait();
            do { browser.drawAlwaysVisibleContent(); } while (!browser.m_scan->ready.empty());
            check(browser.m_error.empty() && !browser.m_containers.empty(), "Folder/file source listing failed");
            auto file = std::ranges::find_if(browser.m_containers, [](const auto &item) { return item.path.filename() == "BAS100.WAD"; });
            if (file == browser.m_containers.end()) file = std::ranges::find_if(browser.m_containers,
                [](const auto &item) { return format::extensionOf(item.region.name) == ".WAD"; });
            check(file != browser.m_containers.end(), "No WAD in selected source");
            const auto index = static_cast<std::size_t>(file - browser.m_containers.begin());
            browser.inspectContainer(index); browser.m_inspectTask.wait(); browser.drawAlwaysVisibleContent();
            const auto &container = browser.m_containers[index];
            check(container.error.empty() && !container.tags.empty(), "Selected WAD inspection failed");
            std::string code;
            RequestSetPatternLanguageCode::subscribe(&code, [&](const std::string &value) { code = value; });
            RequestOpenProvider::subscribe(&code, [](std::shared_ptr<prv::Provider> provider, TaskHolder*) {
                check(provider->open().isSuccess(), "Whole file provider open failed"); EventProviderOpened::post(provider.get());
            });
            std::vector<prv::Provider*> opened;
            for (const bool withPattern : {false, true}) {
                browser.openRegion(index, {container.region.name + (withPattern ? " (container)" : " (file)"), 0,
                    withPattern ? container.region.size : std::numeric_limits<u64>::max()}, withPattern ? "wad.hexpat" : "raw.hexpat");
                auto *provider = ImHexApi::Provider::get();
                check(provider && provider->isAvailable() && provider->getActualSize() == container.region.size, "Whole WAD provider size");
                opened.push_back(provider);
                pl::PatternLanguage runtime;
                runtime.setDataSource(0, provider->getActualSize(), [provider](auto offset, auto buffer, auto count) { provider->read(offset, buffer, count); });
                check(!code.empty() && runtime.executeString(code) == 0, "Whole file/container pattern evaluation");
                check(runtime.getPatterns().front()->getChildren().size() <= (withPattern ? container.tags.size() * 8 : 1),
                    "Large file highlighting must use payload ranges instead of one interval per byte");
                check(!runtime.getColorsAtAddress(0).empty(), "First-byte highlighting missing");
                // Alignment padding deliberately has no color; it must still be safe to query.
                const auto lastColors = runtime.getColorsAtAddress(provider->getActualSize() - 1);
                if (!withPattern) check(!lastColors.empty(), "Last-byte raw highlighting missing");
                if (withPattern) {
                    const auto *array = runtime.getPatterns().size() == 1 ? dynamic_cast<pl::ptrn::IIterable*>(runtime.getPatterns().front().get()) : nullptr;
                    check(array && array->getEntryCount() == container.tags.size(), "Whole WAD Pattern Data array missing");
                }
            }
            check(opened[0] != opened[1], "File/container pattern tabs must remain separate");
            for (int i = 0; i < 10; ++i) {
                ImHexApi::Provider::setCurrentProvider(opened[i % 2]);
                std::array<u8, 16> actual{}, expected{};
                opened[i % 2]->read(0, actual.data(), actual.size());
                format::FileSource(container.path).read(0, expected);
                check(actual == expected, "Tab switch changed source bytes");
            }
            RequestSetPatternLanguageCode::unsubscribe(&code); RequestOpenProvider::unsubscribe(&code);
            for (auto *provider : opened) ImHexApi::Provider::remove(provider, true);
            TaskManager::runDeferredCalls();
            std::cout << "Source opening passed: " << path.string() << " | tags=" << container.tags.size() << '\n';
        }
        static void run(const std::filesystem::path &path) {
            using fixtures::check;
            ViewGow3Browser browser;
            const ContentRegistry::UserInterface::impl::MenuItem *menu = nullptr;
            for (const auto &[priority, item] : ContentRegistry::UserInterface::impl::getMenuItems()) {
                if (item.unlocalizedNames.size() == 2 && item.unlocalizedNames.back() == "hex.gow3.browser.name"_unlocalized) menu = &item;
            }
            check(menu && menu->enabledCallback() && menu->shortcut == Shortcut({ShowOnWelcomeScreen}), "Welcome screen menu unavailable");
            menu->callback(); check(browser.shouldProcess(), "Menu did not open view");
            browser.startScan(path); browser.m_scanTask.wait(); browser.drawAlwaysVisibleContent();
            check(browser.m_containers.size() == 1 && browser.m_assets.empty(), "Discovery must not eagerly read WADs");
            check(browser.m_containerExtensions.at(".WAD") == std::vector<std::size_t>{0}, "Extension grouping");
            browser.inspectContainer(0); browser.m_inspectTask.wait(); browser.drawAlwaysVisibleContent();
            check(browser.m_containers[0].error.empty() && browser.m_assetCount == 5, "Container inspection and asset visibility");
            check(browser.m_containerKinds[0].at("Textures") == std::vector<std::size_t>{6} && browser.m_containerKinds[0].at("Links") == std::vector<std::size_t>{9}, "Resource/link grouping");
            check(!browser.m_assets.contains("Context chunks") && !browser.m_assets.contains("Links"), "Context/link source records must not be mistaken for asset payloads");
            int codeEvents = 0, runs = 0;
            RequestSetPatternLanguageCode::subscribe(&codeEvents, [&](const std::string &) { ++codeEvents; });
            RequestTriggerPatternEvaluation::subscribe(&codeEvents, [&] { ++runs; });
            RequestOpenProvider::subscribe(&codeEvents, [](std::shared_ptr<prv::Provider> provider, TaskHolder*) {
                check(provider->open().isSuccess(), "Provider failed to open"); EventProviderOpened::post(provider.get());
            });
            browser.openTag({0, 6});
            auto *asset = dynamic_cast<AssetProvider*>(ImHexApi::Provider::get());
            check(asset && asset->isAvailable() && !asset->isWritable() && asset->getActualSize() == 212, "Texture payload provider");
            std::array<u8, 4> magic{}; asset->read(76, magic.data(), magic.size());
            check(magic == std::array<u8, 4>{'D', 'D', 'S', ' '} && codeEvents == 1 && runs == 1, "Payload-relative DDS and pattern integration");
            AssetProvider restored; restored.loadSettings(asset->storeSettings({})); check(restored.open().isSuccess(), "Provider restoration");
            restored.read(76, magic.data(), magic.size()); check(magic[0] == 'D', "Restored payload offset");
            browser.openTag({0, 6}); check(ImHexApi::Provider::getProviders().size() == 1 && codeEvents == 1, "Tab reuse must preserve pattern edits");
            browser.openTag({0, 6}, true); check(ImHexApi::Provider::get()->getActualSize() == 64, "GoW3 tag header must be 64 bytes");
            const auto &texture = browser.m_containers[0].tags[6];
            browser.openRegion(0, {"DDS", texture.offset + 76, texture.size - 76}, "dds.hexpat");
            auto *image = ImHexApi::Provider::get(); image->read(0, magic.data(), magic.size());
            check(image->getActualSize() == 136 && magic[0] == 'D', "Embedded DDS subregion");
            browser.openTag({0, 9}); check(ImHexApi::Provider::get()->getActualSize() == 64, "Zero-size link opens its real header");
            auto &io = ImGui::GetIO(); io.DisplaySize = {1280, 900}; io.DeltaTime = 1.0F / 60; io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
            for (int frame = 0; frame < 2; ++frame) {
                ImGui::NewFrame(); ImGui::SetNextWindowSize({800, 800}); ImGui::Begin("GoW3 browser checks"); browser.drawContent(); ImGui::End(); ImGui::Render();
            }
            check(ImGui::GetDrawData()->CmdListsCount > 0, "Browser generated no UI");
            browser.m_cachedSource.reset(); browser.m_scan.reset(); browser.m_containers.clear();
            asset->read(76, magic.data(), magic.size()); check(magic[0] == 'D', "Source replacement invalidated open asset tab");
            RequestOpenProvider::unsubscribe(&codeEvents); RequestSetPatternLanguageCode::unsubscribe(&codeEvents); RequestTriggerPatternEvaluation::unsubscribe(&codeEvents);
            for (auto *provider : ImHexApi::Provider::getProviders()) ImHexApi::Provider::remove(provider, true);
            TaskManager::runDeferredCalls();
        }
    };
}
int main(int argc, char **argv) {
    fixtures::TempFiles temp;
    ImGui::CreateContext(); ImGui::GetIO().IniFilename = nullptr; hex::TaskManager::init();
    int result = 0;
    try {
        if (argc == 3 && std::string(argv[1]) == "--open") hex::plugin::gow3::BrowserChecks::sourceOpening(std::filesystem::u8path(argv[2]));
        else {
            const auto path = temp.add("sample.WAD", fixtures::wad());
            hex::plugin::gow3::BrowserChecks::run(path);
            hex::plugin::gow3::BrowserChecks::sourceOpening(temp.directory);
            hex::plugin::gow3::BrowserChecks::sourceOpening(path);
        }
        std::cout << "GoW3 integration checks passed\n";
    }
    catch (const std::exception &error) { std::cerr << error.what() << '\n'; result = 1; }
    hex::TaskManager::exit(); ImGui::DestroyContext(); return result;
}
