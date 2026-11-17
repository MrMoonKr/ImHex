#include <content/views/view_pattern_editor.hpp>
#include <hex/api/content_registry/pattern_language.hpp>
#include <hex/api/events/events_provider.hpp>
#include <hex/api/imhex_api/hex_editor.hpp>
#include <hex/api/imhex_api/provider.hpp>
#include <hex/api/task_manager.hpp>
#include <hex/providers/memory_provider.hpp>
#include <imgui.h>
#include <atomic>
#include <future>
#include <iostream>
#include <thread>

int main() {
    using namespace hex;
    ImGui::CreateContext();
    ImGui::GetIO().IniFilename = nullptr;
    TaskManager::init();
    int result = 0;
    try {
        ContentRegistry::Settings::write("hex.builtin.setting.hex_editor"_unlocalized,
            "hex.builtin.setting.hex_editor.pattern_parent_highlighting"_unlocalized, true);
        plugin::builtin::ViewPatternEditor editor;
        auto provider = std::make_shared<prv::MemoryProvider>(std::vector<u8>(1024));
        ImHexApi::Provider::add(std::shared_ptr<prv::Provider>(provider), true);
        if (!provider->open().isSuccess()) throw std::runtime_error("Memory provider open failed");
        EventProviderOpened::post(provider.get());
        auto &runtime = ContentRegistry::PatternLanguage::getRuntime();
        const std::string code = "u8 bytes[1024] @ 0;";
        {
            std::scoped_lock lock(ContentRegistry::PatternLanguage::getRuntimeLock());
            ContentRegistry::PatternLanguage::configureRuntime(runtime, provider.get());
            if (runtime.executeString(code) != 0) {
                for (const auto &error : runtime.getCompileErrors()) std::cerr << error.format() << '\n';
                if (runtime.getEvalError()) std::cerr << runtime.getEvalError()->what() << '\n';
                throw std::runtime_error("Pattern evaluation failed");
            }
        }
        const auto &callbacks = ImHexApi::HexEditor::impl::getHoveringFunctions();
        if (callbacks.empty()) throw std::runtime_error("Actual pattern hover handler missing");
        for (const auto &[id, hover] : callbacks) {
            if (hover(provider.get(), 0, 1).empty()) throw std::runtime_error("Completed pattern must highlight");
            std::promise<void> locked, release;
            auto released = release.get_future();
            std::jthread evaluator([&] {
                std::scoped_lock lock(ContentRegistry::PatternLanguage::getRuntimeLock());
                locked.set_value();
                released.wait();
            });
            locked.get_future().wait();
            const auto duringEvaluation = hover(provider.get(), 0, 1);
            release.set_value();
            evaluator.join();
            if (!duringEvaluation.empty()) throw std::runtime_error("Hover accessed the runtime while evaluation held its lock");
        }
        std::atomic<bool> done = false, failed = false;
        std::jthread evaluator([&] {
            for (int i = 0; i < 50; ++i) {
                std::scoped_lock lock(ContentRegistry::PatternLanguage::getRuntimeLock());
                if (runtime.executeString(code) != 0) failed = true;
            }
            done = true;
        });
        std::size_t queries = 0;
        do {
            for (const auto &[id, hover] : callbacks) { std::ignore = hover(provider.get(), queries % 1024, 1); ++queries; }
        } while (!done);
        evaluator.join();
        if (failed) throw std::runtime_error("Concurrent re-evaluation failed");
        ImHexApi::Provider::remove(provider.get(), true);
        TaskManager::runDeferredCalls();
        std::cout << "Pattern hover regression passed: 50 evaluations, " << queries << " actual hover queries\n";
    } catch (const std::exception &error) { std::cerr << error.what() << '\n'; result = 1; }
    TaskManager::exit();
    ImGui::DestroyContext();
    return result;
}
