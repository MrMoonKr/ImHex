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
    if (argc != 2) { std::cerr << "Usage: gow_browser_coexistence_checks <plugin directory>\n"; return 1; }
    ImGui::CreateContext();
    hex::TaskManager::init();
    int result = 0;
    {
        hex::Plugin first(std::filesystem::path(argv[1]) / "gow1_browser.hexplug");
        hex::Plugin second(std::filesystem::path(argv[1]) / "gow2_browser.hexplug");
        try {
            if (!first.isLoaded() || !second.isLoaded()) throw std::runtime_error("Both plugins must load");
            first.setImGuiContext(ImGui::GetCurrentContext());
            second.setImGuiContext(ImGui::GetCurrentContext());
            if (!first.initializePlugin() || !second.initializePlugin()) throw std::runtime_error("Both plugins must initialize");
            using namespace hex;
            auto *gow1 = ContentRegistry::Views::getViewByName("hex.gow.browser.name"_unlocalized);
            auto *gow2 = ContentRegistry::Views::getViewByName("hex.gow2.browser.name"_unlocalized);
            if (!gow1 || !gow2 || gow1 == gow2) throw std::runtime_error("Independent browser windows are missing");
            auto provider1 = ContentRegistry::Provider::impl::create("hex.gow.provider.asset"_unlocalized);
            auto provider2 = ContentRegistry::Provider::impl::create("hex.gow2.provider.asset"_unlocalized);
            if (!provider1 || !provider2 || provider1->getTypeName() == provider2->getTypeName())
                throw std::runtime_error("Provider IDs conflict");
            std::size_t menus = 0;
            for (const auto &[priority, item] : ContentRegistry::UserInterface::impl::getMenuItems()) {
                if (item.unlocalizedNames.size() != 2) continue;
                const auto &name = item.unlocalizedNames.back();
                if (name != "hex.gow.browser.name"_unlocalized && name != "hex.gow2.browser.name"_unlocalized) continue;
                if (!item.enabledCallback()) throw std::runtime_error("Browser must be accessible before opening a provider");
                item.callback();
                if (!item.selectedCallback()) throw std::runtime_error("Browser menu did not open its window");
                ++menus;
            }
            if (menus != 2 || !gow1->getWindowOpenState() || !gow2->getWindowOpenState())
                throw std::runtime_error("Both browser menu entries must work independently");
            std::cout << "GoW1 and GoW2 plugin coexistence checks passed\n";
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
