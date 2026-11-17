#include <hex/api/plugin_manager.hpp>
#include <hex/api/content_registry/provider.hpp>
#include <hex/api/content_registry/user_interface.hpp>
#include <hex/api/content_registry/views.hpp>
#include <hex/api/imhex_api/system.hpp>
#include <hex/api/task_manager.hpp>
#include <hex/api/events/events_lifecycle.hpp>
#include <imgui.h>
#include <iostream>
#include <memory>
#include <set>

int main(int argc, char **argv) {
    if (argc != 2) { std::cerr << "Usage: gow3_coexistence_checks <plugin directory>\n"; return 1; }
    ImGui::CreateContext(); hex::TaskManager::init();
    std::vector<std::unique_ptr<hex::Plugin>> plugins;
    int result = 0;
    try {
        using namespace hex;
        for (const auto *name : {"gow1_browser", "gow2_browser", "gow3_browser", "gowr_browser"}) {
            auto plugin = std::make_unique<Plugin>(std::filesystem::path(argv[1]) / (std::string(name) + ".hexplug"));
            if (!plugin->isLoaded()) throw std::runtime_error("Plugin failed to load");
            plugin->setImGuiContext(ImGui::GetCurrentContext());
            if (!plugin->initializePlugin()) throw std::runtime_error("Plugin failed to initialize");
            plugins.push_back(std::move(plugin));
        }
        const std::set<std::string> ids {"hex.gow", "hex.gow2", "hex.gow3", "hex.gowr"};
        std::set<std::string> providerTypes;
        for (const auto &id : ids) {
            auto *view = ContentRegistry::Views::getViewByName(UnlocalizedString(id + ".browser.name"));
            auto provider = ContentRegistry::Provider::impl::create(UnlocalizedString(id + ".provider.asset"));
            if (!view || !provider || !providerTypes.insert(provider->getTypeName().get()).second) throw std::runtime_error("Browser/provider identities conflict");
        }
        std::size_t menus = 0;
        for (const auto &[priority, item] : ContentRegistry::UserInterface::impl::getMenuItems()) {
            if (item.unlocalizedNames.size() != 2) continue;
            const auto &name = item.unlocalizedNames.back().get();
            for (const auto &id : ids) {
                if (name != id + ".browser.name") continue;
                if (!item.enabledCallback()) throw std::runtime_error("Browser menu unavailable before opening a file");
                item.callback(); if (!item.selectedCallback()) throw std::runtime_error("Browser menu did not open its view"); ++menus;
            }
        }
        if (menus != 4) throw std::runtime_error("All four independent browser menus are required");
        std::cout << "GoW1, GoW2, GoW3 and Ragnarok coexistence checks passed\n";
    } catch (const std::exception &error) { std::cerr << error.what() << '\n'; result = 1; }
    hex::TaskManager::exit(); hex::ImHexApi::System::impl::cleanup(); hex::EventImHexClosing::post(); hex::EventManager::clear();
    plugins.clear(); ImGui::DestroyContext(); return result;
}
