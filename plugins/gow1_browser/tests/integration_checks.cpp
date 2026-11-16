#include <asset_provider.hpp>
#include <view_gow1_browser.hpp>

#include <hex/api/content_registry/provider.hpp>
#include <hex/api/content_registry/user_interface.hpp>
#include <hex/api/events/requests_interaction.hpp>
#include <hex/api/imhex_api/provider.hpp>
#include <hex/api/task_manager.hpp>
#include <nlohmann/json.hpp>
#include <imgui.h>

#include <chrono>
#include <cstring>
#include <iostream>

namespace hex::plugin::gow1 {
    struct BrowserChecks {
        static void check(bool condition, const char *message) {
            if (!condition) throw std::runtime_error(message);
        }

        static void run(const std::filesystem::path &path) {
            ViewGow1Browser browser;
            check(!browser.shouldProcess(), "Closed browser should not render a window");
            check(!ImHexApi::Provider::isValid(), "Menu regression must start on welcome screen");
            check(!browser.hasViewMenuItemEntry(), "Automatic View menu would disable the browser on welcome screen");
            const ContentRegistry::UserInterface::impl::MenuItem *browserMenu = nullptr;
            for (const auto &[priority, item] : ContentRegistry::UserInterface::impl::getMenuItems()) {
                if (item.unlocalizedNames.size() == 2 && item.unlocalizedNames.back() == "hex.gow.browser.name"_unlocalized)
                    browserMenu = &item;
            }
            check(browserMenu && browserMenu->enabledCallback(), "Browser menu disabled without a provider");
            check(browserMenu->shortcut == Shortcut({ShowOnWelcomeScreen}), "Browser menu hidden on welcome screen");
            check(!browserMenu->selectedCallback(), "Closed browser menu should be unchecked");
            browserMenu->callback();
            check(browserMenu->selectedCallback(), "Menu selection did not open the browser");
            check(browser.shouldDraw() && browser.shouldProcess(), "Browser must work without an open provider");
            browser.startScan(path);
            browser.m_scanTask.wait();
            browser.drawAlwaysVisibleContent();
            check(browser.m_error.empty() && browser.m_assetCount == 2 && browser.m_containers.size() == 1, "Browser indexing failed");
            check(browser.m_assets.at("Textures").contains("TXR_test"), "Asset name tree is missing");
            const auto &kinds = browser.m_containerKinds.at(0);
            check(kinds.at("Textures") == std::vector<std::size_t>{0} && kinds.at("Links") == std::vector<std::size_t>{1} &&
                  kinds.at("Control / auxiliary") == std::vector<std::size_t>{2}, "Container grouping must preserve resource, link and control identities");
            {
                std::scoped_lock lock(browser.m_scan->mutex);
                for (const auto *name : {"sound.vag", "movie.VpK", "archive.backup.wad", "README"}) {
                    format::Container extra;
                    extra.region.name = name;
                    browser.m_scan->ready.push_back(std::move(extra));
                    ++browser.m_scan->total;
                }
            }
            browser.drawAlwaysVisibleContent();
            check(browser.m_containerExtensions.at(".WAD") == std::vector<std::size_t>{0, 3} &&
                  browser.m_containerExtensions.at(".VAG") == std::vector<std::size_t>{1} &&
                  browser.m_containerExtensions.at(".VPK") == std::vector<std::size_t>{2} &&
                  browser.m_containerExtensions.at("(No extension)") == std::vector<std::size_t>{4},
                  "Extension folders must normalize case, use the final suffix and preserve container identity");
            int codeEvents = 0, runEvents = 0;
            std::string code;
            RequestSetPatternLanguageCode::subscribe(&code, [&](const std::string &value) { code = value; ++codeEvents; });
            RequestTriggerPatternEvaluation::subscribe(&code, [&] { ++runEvents; });
            browser.openTag({0, 0});
            auto *asset = dynamic_cast<AssetProvider*>(ImHexApi::Provider::get());
            check(asset && asset->isAvailable() && !asset->isWritable() && !asset->isResizable(), "Asset provider state");
            check(asset->getActualSize() == 88 && asset->pattern() == "resource.hexpat", "Asset payload boundaries");
            check(codeEvents == 1 && runEvents == 1 && code.find("struct Resource") != std::string::npos, "Pattern editor event integration");
            std::array<u8, 4> bytes{};
            asset->read(0, bytes.data(), bytes.size());
            check(bytes[0] == 7 && bytes[1] == 0, "Provider address 0 must be payload, not tag header");
            const auto settings = asset->storeSettings({});
            AssetProvider restored;
            restored.loadSettings(settings);
            check(restored.open().isSuccess(), "Persisted provider did not reopen");
            restored.read(0, bytes.data(), bytes.size());
            check(bytes[0] == 7 && restored.getName() == asset->getName(), "Persisted provider identity/data");
            browser.openTag({0, 0});
            check(ImHexApi::Provider::getProviders().size() == 1 && codeEvents == 1, "Reopening should reuse tab and preserve pattern edits");

            // A GUI frame exercises the controls and both the no-provider/loaded paths.
            auto &io = ImGui::GetIO();
            io.DisplaySize = {1280, 900};
            io.DeltaTime = 1.0F / 60.0F;
            io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
            for (int frame = 0; frame < 2; ++frame) {
                ImGui::NewFrame();
                ImGui::SetNextWindowSize({800, 800});
                ImGui::Begin("GoW browser integration");
                ImGui::SetNextItemOpen(true, ImGuiCond_Always);
                browser.drawContent();
                ImGui::End();
                ImGui::Begin("Container source checks");
                browser.m_groupContainerTags = frame == 0;
                ImGui::SetNextItemOpen(true, ImGuiCond_Always);
                browser.drawContainers();
                ImGui::End();
                ImGui::Render();
            }
            check(ImGui::GetDrawData()->CmdListsCount > 0, "Browser produced no UI");

            browser.openTag({0, 1});
            auto *link = dynamic_cast<AssetProvider*>(ImHexApi::Provider::get());
            check(link && link->getActualSize() == 32 && link->pattern() == "tag.hexpat", "Zero-byte link should open its real header");
            browser.m_source.reset();
            browser.m_scan.reset();
            browser.m_containers.clear();
            asset->read(0, bytes.data(), bytes.size());
            check(bytes[0] == 7, "Closing/replacing browser source must not invalidate open asset tabs");
            RequestSetPatternLanguageCode::unsubscribe(&code);
            RequestTriggerPatternEvaluation::unsubscribe(&code);
            for (auto *provider : ImHexApi::Provider::getProviders()) ImHexApi::Provider::remove(provider, true);
            TaskManager::runDeferredCalls();
        }
    };
}

int main() {
    const auto path = std::filesystem::temp_directory_path() /
        ("imhex-gow1-browser-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".WAD");
    ImGui::CreateContext();
    ImGui::GetIO().IniFilename = nullptr;
    hex::TaskManager::init();
    int result = 0;
    try {
        // TXR, zero-byte link and an auxiliary header must all survive grouping.
        std::array<char, 208> fixture{};
        fixture[0] = 30; fixture[4] = 88;
        std::memcpy(fixture.data() + 8, "TXR_test", 8); fixture[32] = 7;
        fixture[128] = 30; std::memcpy(fixture.data() + 136, "link", 4);
        fixture[160] = char(888 & 0xff); fixture[161] = char(888 >> 8); fixture[164] = 8;
        std::memcpy(fixture.data() + 168, "header", 6);
        std::ofstream file(path, std::ios::binary);
        file.write(fixture.data(), fixture.size()); file.close();
        hex::plugin::gow1::BrowserChecks::run(path);
        std::cout << "GoW browser integration checks passed\n";
    } catch (const std::exception &error) { std::cerr << error.what() << '\n'; result = 1; }
    hex::TaskManager::exit();
    ImGui::DestroyContext();
    std::error_code error;
    std::filesystem::remove(path, error);
    return result;
}
