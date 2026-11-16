#include <hex/api/plugin_manager.hpp>
#include <hex/api/content_registry/provider.hpp>
#include <hex/api/content_registry/user_interface.hpp>
#include <hex/api/content_registry/views.hpp>
#include <hex/api/imhex_api/system.hpp>
#include <hex/api/task_manager.hpp>
#include <hex/api/events/events_lifecycle.hpp>

#include <imgui.h>
#include <iostream>

int main(int argc, char **argv) {
    if (argc != 2) { std::cerr << "Usage: gow_all_browser_checks <plugin directory>\n"; return 1; }
    ImGui::CreateContext();
    hex::TaskManager::init();
    int result = 0;
    {
        hex::Plugin first(std::filesystem::path(argv[1]) / "gow1_browser.hexplug");
        hex::Plugin second(std::filesystem::path(argv[1]) / "gow2_browser.hexplug");
        hex::Plugin third(std::filesystem::path(argv[1]) / "gowr_browser.hexplug");
        try {
            if (!first.isLoaded() || !second.isLoaded() || !third.isLoaded()) throw std::runtime_error("All three plugins must load");
            first.setImGuiContext(ImGui::GetCurrentContext());
            second.setImGuiContext(ImGui::GetCurrentContext());
            third.setImGuiContext(ImGui::GetCurrentContext());
            if (!first.initializePlugin() || !second.initializePlugin() || !third.initializePlugin()) throw std::runtime_error("All plugins must initialize");
            using namespace hex;
            auto *gow1 = ContentRegistry::Views::getViewByName("hex.gow.browser.name"_unlocalized);
            auto *gow2 = ContentRegistry::Views::getViewByName("hex.gow2.browser.name"_unlocalized);
            auto *gowr = ContentRegistry::Views::getViewByName("hex.gowr.browser.name"_unlocalized);
            if (!gow1 || !gow2 || !gowr || gow1 == gow2 || gow1 == gowr || gow2 == gowr) throw std::runtime_error("Independent browser windows are missing");
            auto provider1 = ContentRegistry::Provider::impl::create("hex.gow.provider.asset"_unlocalized);
            auto provider2 = ContentRegistry::Provider::impl::create("hex.gow2.provider.asset"_unlocalized);
            auto providerR = ContentRegistry::Provider::impl::create("hex.gowr.provider.asset"_unlocalized);
            if (!provider1 || !provider2 || !providerR || provider1->getTypeName() == provider2->getTypeName() ||
                provider1->getTypeName() == providerR->getTypeName() || provider2->getTypeName() == providerR->getTypeName())
                throw std::runtime_error("Provider IDs conflict");
            std::size_t menus = 0;
            for (const auto &[priority, item] : ContentRegistry::UserInterface::impl::getMenuItems()) {
                if (item.unlocalizedNames.size() != 2) continue;
                const auto &name = item.unlocalizedNames.back();
                if (name != "hex.gow.browser.name"_unlocalized && name != "hex.gow2.browser.name"_unlocalized && name != "hex.gowr.browser.name"_unlocalized) continue;
                if (!item.enabledCallback()) throw std::runtime_error("Browser must be accessible before opening a provider");
                item.callback();
                if (!item.selectedCallback()) throw std::runtime_error("Browser menu did not open its window");
                ++menus;
            }
            if (menus != 3 || !gow1->getWindowOpenState() || !gow2->getWindowOpenState() || !gowr->getWindowOpenState())
                throw std::runtime_error("All browser menus must work independently");
            std::cout << "GoW1, GoW2 and Ragnarok coexistence checks passed\n";
        } catch (const std::exception &error) { std::cerr << error.what() << '\n'; result = 1; }
        hex::TaskManager::exit();
        // Destroy registered plugin objects while their libraries are still loaded.
        hex::ImHexApi::System::impl::cleanup();
        hex::EventImHexClosing::post();
        hex::EventManager::clear();
    }
    ImGui::DestroyContext();
    return result;
}
