#include <view_gow1_browser.hpp>
#include <asset_provider.hpp>

#include <fonts/vscode_icons.hpp>
#include <hex/api/content_registry/views.hpp>
#include <hex/api/content_registry/user_interface.hpp>
#include <hex/api/layout_manager.hpp>
#include <hex/api/events/events_provider.hpp>
#include <hex/api/events/requests_interaction.hpp>
#include <hex/api/imhex_api/provider.hpp>
#include <hex/helpers/fmt.hpp>
#include <hex/helpers/fs.hpp>
#include <romfs/romfs.hpp>
#include <wolv/utils/string.hpp>

#include <algorithm>
#include <cctype>
#include <utility>

namespace hex::plugin::gow1 {

    namespace {
        void showView(const UnlocalizedString &name) {
            if (auto view = ContentRegistry::Views::getViewByName(name))
                view->getWindowOpenState() = true;
        }
    }

    ViewGow1Browser::ViewGow1Browser() : View::Window("hex.gow.browser.name"_unlocalized, ICON_VS_LIBRARY) {
        // Keep the original view ID so existing workspace layouts still locate this window.
        // The automatic View menu disables every entry until a provider exists.
        // This browser opens the first provider, so it needs its own welcome-screen entry.
        ContentRegistry::UserInterface::addMenuItem(
            { "hex.builtin.menu.view"_unlocalized, "hex.gow.browser.name"_unlocalized },
            ICON_VS_LIBRARY, 3500, Shortcut({ ShowOnWelcomeScreen }),
            [this] { this->getWindowOpenState() = !this->getWindowOpenState(); },
            [] { return !LayoutManager::isLayoutLocked(); },
            [this] { return this->getWindowOpenState(); });
    }

    ViewGow1Browser::~ViewGow1Browser() {
        m_scanTask.interrupt();
        m_scanTask.wait();
    }

    void ViewGow1Browser::startScan(const std::filesystem::path &path) {
        if (m_scanTask.isRunning()) return;
        m_path = wolv::util::toUTF8String(path);
        m_containers.clear();
        m_containerKinds.clear();
        m_containerExtensions.clear();
        m_assets.clear();
        m_source.reset();
        m_toc.reset();
        m_selected.reset();
        m_error.clear();
        m_assetCount = 0;
        m_scan = std::make_shared<ScanState>();
        m_scanTask = TaskManager::createBackgroundTask("Indexing GoW I assets", [state = m_scan, path](Task &task) {
            try {
                const auto source = std::make_shared<format::FileSource>(path);
                const format::Read read = [source](auto offset, auto bytes) { source->read(offset, bytes); };
                auto catalog = format::readCatalog(read, source->size(), wolv::util::toUTF8String(path.filename()));
                {
                    std::scoped_lock lock(state->mutex);
                    state->source = source;
                    state->toc = catalog.toc;
                    state->total = catalog.containers.size();
                }
                std::size_t tagCount = 0;
                for (auto &container : catalog.containers) {
                    task.update();
                    if (container.wad && container.error.empty()) {
                        try {
                            container.tags = format::parseWad([&](auto offset, auto bytes) {
                                format::requireRange(offset, bytes.size(), container.region.size);
                                read(container.region.offset + offset, bytes);
                            }, container.region.size, [&task] { task.update(); });
                        } catch (const format::FormatError &error) { container.error = error.what(); }
                    }
                    tagCount += container.tags.size();
                    if (tagCount > 2000000) throw format::FormatError("Index exceeds the 2,000,000 tag budget");
                    std::scoped_lock lock(state->mutex);
                    state->ready.push_back(std::move(container));
                }
            } catch (const std::exception &error) {
                std::scoped_lock lock(state->mutex);
                state->error = task.shouldInterrupt() ? "Indexing cancelled. Partial results are available." : error.what();
            }
            std::scoped_lock lock(state->mutex);
            state->done = true;
        });
    }

    void ViewGow1Browser::drawAlwaysVisibleContent() {
        if (!m_scan) return;
        std::deque<format::Container> ready;
        {
            std::scoped_lock lock(m_scan->mutex);
            m_source = m_scan->source;
            m_toc = m_scan->toc;
            // Limit UI work per frame while a large ISO is being indexed.
            for (int i = 0; i < 4 && !m_scan->ready.empty(); ++i) {
                ready.push_back(std::move(m_scan->ready.front()));
                m_scan->ready.pop_front();
            }
            if (!m_scan->error.empty()) m_error = std::exchange(m_scan->error, {});
        }
        for (auto &container : ready) {
            const auto index = m_containers.size();
            TagKinds kinds;
            for (std::size_t t = 0; t < container.tags.size(); ++t) {
                const auto &tag = container.tags[t];
                kinds[tag.kind()].push_back(t);
                if (tag.tag != 30 && tag.tag != 112) continue;
                m_assets[tag.kind()][tag.name.empty() ? "<unnamed>" : tag.name].push_back({ index, t });
                ++m_assetCount;
            }
            m_containerKinds.push_back(std::move(kinds));
            m_containerExtensions[containerExtension(container.region.name)].push_back(index);
            m_containers.push_back(std::move(container));
        }
    }

    bool ViewGow1Browser::matchesFilter(const std::string &name) const {
        return m_filter.empty() || std::search(name.begin(), name.end(), m_filter.begin(), m_filter.end(),
            [](unsigned char a, unsigned char b) { return std::tolower(a) == std::tolower(b); }) != name.end();
    }

    void ViewGow1Browser::applyPattern(const std::string &pattern) {
        showView("hex.builtin.view.pattern_editor.name"_unlocalized);
        showView("hex.builtin.view.pattern_data.name"_unlocalized);
        RequestSetPatternLanguageCode::post(std::string(romfs::get("patterns/" + pattern).string()));
        RequestTriggerPatternEvaluation::post();
    }

    void ViewGow1Browser::openRegion(format::Region region, const std::string &pattern) {
        if (!m_source) return;
        // A foreground task prevents provider changes in ImHex. Keep the request explicit.
        if (TaskManager::getRunningTaskCount() != 0) {
            m_error = "Wait for the current ImHex task to finish before opening an asset.";
            return;
        }
        for (auto *provider : ImHexApi::Provider::getProviders()) {
            const auto asset = dynamic_cast<AssetProvider*>(provider);
            if (asset && asset->matches(m_source->path(), region)) {
                ImHexApi::Provider::setCurrentProvider(provider);
                showView("hex.builtin.view.hex_editor.name"_unlocalized);
                return; // preserve any pattern edits in an existing tab
            }
        }
        std::shared_ptr<prv::Provider> provider = std::make_shared<AssetProvider>(m_source, std::move(region), pattern);
        const auto result = provider->open();
        if (result.isFailure()) { m_error = result.getErrorMessage(); return; }
        auto *opened = provider.get();
        ImHexApi::Provider::add(std::move(provider), true);
        EventProviderOpened::post(opened);
        showView("hex.builtin.view.hex_editor.name"_unlocalized);
        if (m_autoPattern) applyPattern(pattern);
    }

    void ViewGow1Browser::openTag(AssetRef ref, bool header) {
        m_selected = ref;
        const auto &container = m_containers.at(ref.container);
        const auto &tag = container.tags.at(ref.tag);
        // A link has no payload; open its real header rather than manufacture data.
        header = header || tag.size == 0;
        openRegion({ fmt::format("{} / {} [#{}:{}]{}", container.region.name,
                         tag.name.empty() ? "<unnamed>" : tag.name, container.recordIndex, ref.tag, header ? " (tag)" : ""),
                     container.region.offset + (header ? tag.headerOffset : tag.payloadOffset()),
                     header ? 32 : tag.size },
                   header ? "tag.hexpat" : tag.tag == 30 ? "resource.hexpat" : "raw.hexpat");
    }

    void ViewGow1Browser::drawTag(std::size_t containerIndex, std::size_t tagIndex) {
        const auto &container = m_containers[containerIndex];
        const auto &tag = container.tags[tagIndex];
        ImGui::PushID(static_cast<int>(containerIndex));
        ImGui::PushID(static_cast<int>(tagIndex));
        const auto label = fmt::format("{}  [#{} / {} bytes]", tag.name.empty() ? "<unnamed>" : tag.name, tagIndex, tag.size);
        const bool selected = m_selected && m_selected->container == containerIndex && m_selected->tag == tagIndex;
        if (ImGui::Selectable(label.c_str(), selected, ImGuiSelectableFlags_AllowDoubleClick)) {
            m_selected = AssetRef{ containerIndex, tagIndex };
            if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) openTag(*m_selected);
        }
        if (ImGui::IsItemHovered()) {
            ImGui::BeginTooltip();
            ImGui::TextUnformatted(fmt::format("{} / TOC #{} / tag #{}\nTag {} / flags 0x{:X}\nWAD payload 0x{:X} / source 0x{:X}\n{}",
                container.region.name, container.recordIndex, tagIndex, tag.tag, tag.flags,
                tag.payloadOffset(), container.region.offset + tag.payloadOffset(), tag.kind()).c_str());
            if (tag.type) ImGui::TextUnformatted(fmt::format("Asset type 0x{:08X}", *tag.type).c_str());
            if (tag.parent) ImGui::TextUnformatted(fmt::format("Parent tag #{}: {}", *tag.parent, container.tags[*tag.parent].name).c_str());
            ImGui::EndTooltip();
        }
        if (ImGui::BeginPopupContextItem()) {
            if (ImGui::MenuItem("Open payload")) openTag({ containerIndex, tagIndex });
            if (ImGui::MenuItem("Open tag header")) openTag({ containerIndex, tagIndex }, true);
            ImGui::EndPopup();
        }
        ImGui::PopID();
        ImGui::PopID();
    }

    void ViewGow1Browser::drawAssets() {
        for (const auto &[kind, names] : m_assets) {
            ImGui::PushID(kind.c_str());
            if (ImGui::TreeNodeEx("##kind", ImGuiTreeNodeFlags_SpanAvailWidth, "%s (%zu)", kind.c_str(), names.size())) {
                for (const auto &[name, refs] : names) {
                    if (!matchesFilter(name)) continue;
                    if (refs.size() == 1) drawTag(refs[0].container, refs[0].tag);
                    else {
                        ImGui::PushID(name.c_str());
                        if (ImGui::TreeNodeEx("##name", ImGuiTreeNodeFlags_SpanAvailWidth, "%s (%zu sources)", name.c_str(), refs.size())) {
                            for (const auto &ref : refs) {
                                ImGui::TextUnformatted(fmt::format("{} / TOC #{}", m_containers[ref.container].region.name,
                                    m_containers[ref.container].recordIndex).c_str());
                                drawTag(ref.container, ref.tag);
                            }
                            ImGui::TreePop();
                        }
                        ImGui::PopID();
                    }
                }
                ImGui::TreePop();
            }
            ImGui::PopID();
        }
    }

    std::string ViewGow1Browser::containerExtension(const std::string &name) {
        auto extension = std::filesystem::path(name).extension().string();
        if (extension.empty() || extension == ".") return "(No extension)";
        std::ranges::transform(extension, extension.begin(), [](unsigned char c) { return char(std::toupper(c)); });
        return extension;
    }

    bool ViewGow1Browser::containerMatchesFilter(std::size_t index) const {
        const auto &container = m_containers[index];
        return matchesFilter(container.region.name) ||
            std::ranges::any_of(container.tags, [this](const auto &tag) { return matchesFilter(tag.name); }) ||
            (m_groupContainerTags && std::ranges::any_of(m_containerKinds[index],
                [this](const auto &entry) { return matchesFilter(entry.first); }));
    }

    void ViewGow1Browser::drawContainers() {
    if (m_toc && ImGui::Button("Open GODOFWAR.TOC")) openRegion(*m_toc, "toc.hexpat");
    for (const auto &[extension, containers] : m_containerExtensions) {
        const bool extensionMatches = matchesFilter(extension);
        std::vector<std::size_t> visible;
        for (const auto index : containers) {
            if (extensionMatches || containerMatchesFilter(index)) visible.push_back(index);
        }
        if (visible.empty()) continue;
        ImGui::PushID(extension.c_str());
        if (ImGui::TreeNodeEx("##extension", ImGuiTreeNodeFlags_SpanAvailWidth, "%s (%zu)", extension.c_str(), visible.size())) {
            for (const auto index : visible) drawContainer(index, extensionMatches);
            ImGui::TreePop();
        }
        ImGui::PopID();
    }
}

void ViewGow1Browser::drawContainer(std::size_t i, bool showAll) {
        const auto &container = m_containers[i];
        const bool containerMatches = showAll || matchesFilter(container.region.name);
        const auto &kinds = m_containerKinds[i];
        ImGui::PushID(static_cast<int>(i));
        if (ImGui::TreeNodeEx("##container", ImGuiTreeNodeFlags_SpanAvailWidth, "%s [TOC #%zu]", container.region.name.c_str(), container.recordIndex)) {
            if (!container.error.empty()) ImGui::TextWrapped("%s", container.error.c_str());
            // Malformed WADs are still inspectable; unresolved PAK ranges are not.
            if (container.available) {
                if (ImGui::Button("Open container")) {
                    auto region = container.region;
                    region.name += fmt::format(" [TOC #{}]", container.recordIndex);
                    openRegion(std::move(region), container.wad ? "wad.hexpat" : "raw.hexpat");
                }
            }
            if (m_groupContainerTags) {
                for (const auto &[kind, tags] : kinds) {
                    const bool groupMatches = containerMatches || matchesFilter(kind);
                    const auto visible = std::ranges::count_if(tags, [&](auto t) {
                        return groupMatches || matchesFilter(container.tags[t].name);
                    });
                    if (visible == 0) continue;
                    ImGui::PushID(kind.c_str());
                    if (ImGui::TreeNodeEx("##tag_kind", ImGuiTreeNodeFlags_SpanAvailWidth, "%s (%zu)", kind.c_str(), static_cast<std::size_t>(visible))) {
                        for (const auto t : tags) {
                            if (groupMatches || matchesFilter(container.tags[t].name)) drawTag(i, t);
                        }
                        ImGui::TreePop();
                    }
                    ImGui::PopID();
                }
            } else {
                for (std::size_t t = 0; t < container.tags.size(); ++t) {
                    if (containerMatches || matchesFilter(container.tags[t].name)) drawTag(i, t);
                }
            }
            ImGui::TreePop();
        }
        ImGui::PopID();
    }

    void ViewGow1Browser::drawContent() {
        ImGui::BeginDisabled(m_scanTask.isRunning());
        ImGui::SetNextItemWidth(-1);
        ImGui::InputText("##source_path", m_path);
        if (ImGui::Button("Browse ISO / WAD...")) {
            fs::openFileBrowser(fs::DialogMode::Open, { { "God of War I (PS2)", "iso,wad" } }, [this](const auto &path) { startScan(path); });
        }
        ImGui::SameLine();
        if (ImGui::Button("Open") && !m_path.empty()) startScan(std::filesystem::u8path(m_path));
        ImGui::EndDisabled();
        if (m_scanTask.isRunning()) {
            ImGui::SameLine();
            if (ImGui::Button("Cancel")) m_scanTask.interrupt();
        }
        if (m_scan) {
            std::scoped_lock lock(m_scan->mutex);
            ImGui::Text("%zu / %zu containers | %zu assets%s", m_containers.size(), m_scan->total, m_assetCount,
                m_scan->done && m_scan->ready.empty() ? "" : " | indexing...");
        }
        if (!m_error.empty()) ImGui::TextWrapped("%s", m_error.c_str());
        ImGui::Checkbox("Apply pattern on open", &m_autoPattern);
        if (auto asset = dynamic_cast<AssetProvider*>(ImHexApi::Provider::get())) {
            if (ImGui::Button("Apply built-in pattern to current tab")) applyPattern(asset->pattern());
        }
        ImGui::SetNextItemWidth(-1);
        ImGui::InputTextWithHint("##filter", "Search names...", m_filter);
        ImGui::BeginDisabled(!m_selected.has_value());
        if (ImGui::Button("Open selected") && m_selected) openTag(*m_selected);
        ImGui::EndDisabled();
        ImGui::Separator();
        if (ImGui::BeginTabBar("##browse_mode")) {
            if (ImGui::BeginTabItem("Assets")) {
                ImGui::BeginChild("##asset_tree");
                drawAssets();
                ImGui::EndChild();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Containers (Source)")) {
                ImGui::Checkbox("Group by kind", &m_groupContainerTags);
                ImGui::BeginChild("##container_tree");
                drawContainers();
                ImGui::EndChild();
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
    }

    void ViewGow1Browser::drawHelpText() {
        ImGui::TextWrapped("Open a God of War I PS2 ISO or WAD. Double-click an asset to open a read-only hex tab. "
            "Assets groups names by type; Containers preserves every TOC copy and WAD tag. "
            "Group by kind organizes each container into type folders; turn it off for original tag order. "
            "Built-in patterns decode supported headers and preserve other bytes as raw data. "
            "Use the Pattern Editor to load or edit your own patterns.");
    }

}
