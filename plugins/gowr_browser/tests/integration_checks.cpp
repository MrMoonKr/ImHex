#include "fixtures.hpp"
#include <asset_provider.hpp>
#include <view_gowr_browser.hpp>
#include <hex/api/content_registry/user_interface.hpp>
#include <hex/api/events/events_provider.hpp>
#include <hex/api/events/requests_provider.hpp>
#include <hex/api/events/requests_interaction.hpp>
#include <hex/api/imhex_api/provider.hpp>
#include <nlohmann/json.hpp>
#include <imgui.h>
#include <iostream>

namespace hex::plugin::gowr {
    struct BrowserChecks {
        static void run(const std::filesystem::path &path) {
            using fixtures::check;
            ViewGowrBrowser browser;
            const ContentRegistry::UserInterface::impl::MenuItem *menu = nullptr;
            for (const auto &[priority, item] : ContentRegistry::UserInterface::impl::getMenuItems()) {
                if (item.unlocalizedNames.size() == 2 && item.unlocalizedNames.back() == "hex.gowr.browser.name"_unlocalized) menu = &item;
            }
            check(menu && menu->enabledCallback() && menu->shortcut == Shortcut({ShowOnWelcomeScreen}), "Welcome screen menu unavailable");
            menu->callback(); check(browser.shouldProcess(), "Browser menu did not open view");
            browser.startScan(path); browser.m_scanTask.wait(); browser.drawAlwaysVisibleContent();
            check(browser.m_containers.size() == 1 && browser.m_assets.empty(), "Discovery should not eagerly decode containers");
            check(browser.m_containerExtensions.at(".WAD") == std::vector<std::size_t>{0}, "Extension grouping");
            browser.inspectContainer(0); browser.m_inspectTask.wait(); browser.drawAlwaysVisibleContent();
            check(browser.m_containers[0].error.empty() && browser.m_containers[0].compressed && browser.m_assetCount == 1, "LZ4 container inspection");
            check(browser.m_containerKinds[0].at("Textures") == std::vector<std::size_t>{0}, "Chunk type grouping");
            int codeEvents = 0, runs = 0;
            RequestSetPatternLanguageCode::subscribe(&codeEvents, [&](const std::string &code) { check(code.find("TextureParm") != std::string::npos || code.find("Entry") != std::string::npos || code.find("data[") != std::string::npos, "Pattern routing"); ++codeEvents; });
            RequestTriggerPatternEvaluation::subscribe(&codeEvents, [&] { ++runs; });
            // The real builtin plugin supplies this asynchronous open handler.
            RequestOpenProvider::subscribe(&codeEvents, [](std::shared_ptr<prv::Provider> provider, TaskHolder*) {
                check(provider->open().isSuccess(), "Provider failed to open"); EventProviderOpened::post(provider.get());
            });
            browser.openTag({0, 0});
            auto *asset = dynamic_cast<AssetProvider*>(ImHexApi::Provider::get());
            check(asset && asset->isAvailable() && !asset->isWritable() && asset->getActualSize() == 168, "Decoded asset provider state");
            std::array<u8, 4> dimensions{}; asset->read(72, dimensions.data(), dimensions.size());
            check(dimensions[0] == 64 && dimensions[2] == 32 && codeEvents == 1 && runs == 1, "Payload base address / pattern integration");
            AssetProvider restored; restored.loadSettings(asset->storeSettings({})); check(restored.open().isSuccess(), "Decoded provider restoration");
            restored.read(72, dimensions.data(), dimensions.size()); check(dimensions[0] == 64, "Restored decoded offset");
            browser.openTag({0, 0}); check(ImHexApi::Provider::getProviders().size() == 1 && codeEvents == 1, "Tab reuse must preserve pattern edits");
            browser.openTag({0, 0}, true);
            check(ImHexApi::Provider::get()->getActualSize() == 144, "WTOC entry record is 144 bytes, not a PS2 tag");
            browser.openRegion(0, {"Stored file", 0, std::numeric_limits<u64>::max()}, "raw.hexpat", false);
            auto *stored = ImHexApi::Provider::get(); check(stored->getActualSize() == std::filesystem::file_size(path), "Stored file size");
            stored->read(0, dimensions.data(), dimensions.size()); check(dimensions[0] == 4 && dimensions[1] == 0x22, "Stored bytes must remain compressed");
            auto &io = ImGui::GetIO(); io.DisplaySize = {1280, 900}; io.DeltaTime = 1.0F / 60; io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
            for (int frame = 0; frame < 2; ++frame) {
                ImGui::NewFrame(); ImGui::SetNextWindowSize({800, 800}); ImGui::Begin("Ragnarok browser checks");
                browser.drawContent(); ImGui::End(); ImGui::Render();
            }
            check(ImGui::GetDrawData()->CmdListsCount > 0, "Browser generated no UI");
            browser.m_cachedSource.reset(); browser.m_scan.reset(); browser.m_containers.clear();
            asset->read(72, dimensions.data(), dimensions.size()); check(dimensions[0] == 64, "Source replacement invalidated open tab");
            RequestOpenProvider::unsubscribe(&codeEvents); RequestSetPatternLanguageCode::unsubscribe(&codeEvents); RequestTriggerPatternEvaluation::unsubscribe(&codeEvents);
            for (auto *provider : ImHexApi::Provider::getProviders()) ImHexApi::Provider::remove(provider, true);
            TaskManager::runDeferredCalls();
        }
    };
}
int main() {
    fixtures::TempFiles temp;
    ImGui::CreateContext(); ImGui::GetIO().IniFilename = nullptr; hex::TaskManager::init();
    int result = 0;
    try {
        const auto path = temp.add("sample.wad", fixtures::frame(fixtures::wtoc(true)));
        hex::plugin::gowr::BrowserChecks::run(path);
        std::cout << "Ragnarok integration checks passed\n";
    } catch (const std::exception &error) { std::cerr << error.what() << '\n'; result = 1; }
    hex::TaskManager::exit(); ImGui::DestroyContext(); return result;
}
