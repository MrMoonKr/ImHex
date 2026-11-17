#include <view_gow3_browser.hpp>
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
#include <limits>
#include <utility>

namespace hex::plugin::gow3 {
    namespace {
        void showView(const UnlocalizedString &name) {
            if (auto view = ContentRegistry::Views::getViewByName(name)) view->getWindowOpenState() = true;
        }
    }
    ViewGow3Browser::ViewGow3Browser() : View::Window("hex.gow3.browser.name"_unlocalized, ICON_VS_LIBRARY) {
        ContentRegistry::UserInterface::addMenuItem(
            {"hex.builtin.menu.view"_unlocalized, "hex.gow3.browser.name"_unlocalized}, ICON_VS_LIBRARY, 3500, Shortcut::None,
            [this] { getWindowOpenState() = !getWindowOpenState(); }, [] { return !LayoutManager::isLayoutLocked(); },
            [this] { return getWindowOpenState(); });
        for (auto &[priority, item] : ContentRegistry::UserInterface::impl::getMenuItemsMutable()) {
            if (item.unlocalizedNames.size() == 2 && item.unlocalizedNames.back() == "hex.gow3.browser.name"_unlocalized)
                item.shortcut = Shortcut({ShowOnWelcomeScreen});
        }
        EventProviderOpened::subscribe(this, [this](prv::Provider *provider) {
            if (auto opening = m_opening.lock(); opening && opening.get() == provider) {
                showView("hex.builtin.view.hex_editor.name"_unlocalized);
                if (m_autoPattern) applyPattern(opening->pattern());
                m_opening.reset();
            }
        });
    }
    ViewGow3Browser::~ViewGow3Browser() {
        EventProviderOpened::unsubscribe(this);
        m_scanTask.interrupt(); m_inspectTask.interrupt();
        m_scanTask.wait(); m_inspectTask.wait();
    }

    void ViewGow3Browser::startScan(const std::filesystem::path &path) {
        if (m_scanTask.isRunning() || m_inspectTask.isRunning()) return;
        m_path = wolv::util::toUTF8String(path);
        m_containers.clear(); m_containerKinds.clear(); m_containerExtensions.clear(); m_assets.clear();
        m_cachedSource.reset(); m_inspection.reset(); m_selected.reset(); m_error.clear();
        m_assetCount = m_inspectedCount = m_tagCount = 0; m_indexAll = false;
        m_scan = std::make_shared<ScanState>();
        m_scanTask = TaskManager::createBackgroundTask("Listing GoW III Remastered containers", [state = m_scan, path](Task &task) {
            try {
                auto containers = format::discover(path, [&task] { task.update(); });
                std::scoped_lock lock(state->mutex);
                state->total = containers.size();
                for (auto &container : containers) state->ready.push_back(std::move(container));
            } catch (const std::exception &error) {
                std::scoped_lock lock(state->mutex);
                state->error = task.shouldInterrupt() ? "File listing cancelled." : error.what();
            }
            std::scoped_lock lock(state->mutex); state->done = true;
        });
    }

    void ViewGow3Browser::inspectContainer(std::size_t index) {
        if (m_inspectTask.isRunning() || m_inspection || index >= m_containers.size()) return;
        const auto &container = m_containers[index];
        if (container.inspected || !container.recognized || !container.error.empty()) return;
        auto state = std::make_shared<InspectionState>(); state->index = index; state->result = container;
        m_inspection = state;
        m_inspectTask = TaskManager::createBackgroundTask("Reading GoW III Remastered container", [state](Task &task) {
            try {
                task.update();
                auto source = std::make_shared<format::FileSource>(state->result.path);
                state->source = source;
                format::inspect(state->result, source, [&task] { task.update(); });
            } catch (const std::exception &error) {
                state->result.tags.clear();
                state->result.error = task.shouldInterrupt() ? "Container inspection cancelled. Reopen the source to retry." : error.what();
                state->result.inspected = true;
            }
            std::scoped_lock lock(state->mutex); state->done = true;
        });
    }

    void ViewGow3Browser::drawAlwaysVisibleContent() {
        if (m_scan) {
            std::deque<format::Container> ready;
            {
                std::scoped_lock lock(m_scan->mutex);
                for (int i = 0; i < 100 && !m_scan->ready.empty(); ++i) {
                    ready.push_back(std::move(m_scan->ready.front())); m_scan->ready.pop_front();
                }
                if (!m_scan->error.empty()) m_error = std::exchange(m_scan->error, {});
            }
            for (auto &container : ready) {
                const auto index = m_containers.size();
                m_containerExtensions[containerExtension(container.region.name)].push_back(index);
                m_containerKinds.emplace_back(); m_containers.push_back(std::move(container));
            }
        }
        if (m_inspection) {
            auto state = m_inspection;
            std::scoped_lock lock(state->mutex);
            if (state->done) {
                const auto index = state->index;
                if (m_tagCount + state->result.tags.size() > 4000000) {
                    state->result.tags.clear(); state->result.error = "Index exceeds 4,000,000 tags. Open a smaller folder.";
                    m_indexAll = false;
                }
                m_containers[index] = std::move(state->result);
                m_cachedSource = std::move(state->source);
                const auto &container = m_containers[index];
                m_tagCount += container.tags.size();
                for (std::size_t t = 0; t < container.tags.size(); ++t) {
                    const auto &tag = container.tags[t];
                    m_containerKinds[index][tag.kind()].push_back(t);
                    if (!tag.isAsset()) continue;
                    m_assets[tag.kind()][tag.name.empty() ? "<unnamed>" : tag.name].push_back({index, t}); ++m_assetCount;
                }
                ++m_inspectedCount; m_inspection.reset();
            }
        }
        if (m_indexAll && !m_inspectTask.isRunning() && !m_inspection) {
            const auto next = std::ranges::find_if(m_containers, [](const auto &container) {
                return container.recognized && !container.inspected && container.error.empty();
            });
            if (next == m_containers.end()) {
                if (!m_scan) m_indexAll = false;
                else {
                    std::scoped_lock lock(m_scan->mutex);
                    if (m_scan->done && m_scan->ready.empty()) m_indexAll = false;
                }
            }
            else inspectContainer(static_cast<std::size_t>(next - m_containers.begin()));
        }
    }

    bool ViewGow3Browser::matchesFilter(const std::string &name) const {
        return m_filter.empty() || std::search(name.begin(), name.end(), m_filter.begin(), m_filter.end(),
            [](unsigned char a, unsigned char b) { return std::tolower(a) == std::tolower(b); }) != name.end();
    }
    void ViewGow3Browser::applyPattern(const std::string &pattern) {
        showView("hex.builtin.view.pattern_editor.name"_unlocalized); showView("hex.builtin.view.pattern_data.name"_unlocalized);
        RequestSetPatternLanguageCode::post(std::string(romfs::get("patterns/" + pattern).string()));
        RequestTriggerPatternEvaluation::post();
    }

    void ViewGow3Browser::openRegion(std::size_t index, format::Region region, const std::string &pattern) {
        if (TaskManager::getRunningTaskCount() != 0) { m_error = "Wait for the current ImHex task to finish."; return; }
        const auto &container = m_containers.at(index);
        for (auto *provider : ImHexApi::Provider::getProviders()) {
            if (auto asset = dynamic_cast<AssetProvider*>(provider); asset && asset->matches(container.path, region)) {
                ImHexApi::Provider::setCurrentProvider(provider); showView("hex.builtin.view.hex_editor.name"_unlocalized); return;
            }
        }
        auto source = container.source.lock();
        auto asset = std::make_shared<AssetProvider>(container.path, std::move(region), pattern, std::move(source));
        std::shared_ptr<prv::Provider> provider = asset;
        ImHexApi::Provider::add(std::move(provider), true);
        m_opening = asset;
        std::ignore = ImHexApi::Provider::openProvider(asset);
    }
    void ViewGow3Browser::openTag(AssetRef ref, bool header) {
        m_selected = ref;
        const auto &container = m_containers.at(ref.container); const auto &tag = container.tags.at(ref.tag);
        header = header || tag.size == 0;
        const auto pattern = header ? "tag.hexpat" : tag.pattern;
        openRegion(ref.container, {fmt::format("{} / {} [#{}]{}", container.region.name,
            tag.name.empty() ? "<unnamed>" : tag.name, ref.tag, header ? " (record)" : ""),
            header ? tag.headerOffset : tag.offset, header ? tag.headerSize : tag.size}, pattern);
    }

    void ViewGow3Browser::drawTag(std::size_t containerIndex, std::size_t tagIndex) {
        const auto &container = m_containers[containerIndex]; const auto &tag = container.tags[tagIndex];
        ImGui::PushID(static_cast<int>(containerIndex)); ImGui::PushID(static_cast<int>(tagIndex));
        const bool selected = m_selected && m_selected->container == containerIndex && m_selected->tag == tagIndex;
        const auto label = fmt::format("{} [#{} / {} bytes]", tag.name.empty() ? "<unnamed>" : tag.name, tagIndex, tag.size);
        if (ImGui::Selectable(label.c_str(), selected, ImGuiSelectableFlags_AllowDoubleClick)) {
            m_selected = AssetRef{containerIndex, tagIndex};
            if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) openTag(*m_selected);
        }
        if (ImGui::IsItemHovered()) {
            ImGui::BeginTooltip();
            ImGui::TextUnformatted(fmt::format("{}\n{} | tag #{} | kind {} / flags 0x{:X}\nWAD offset 0x{:X} | {} bytes",
                container.region.name, tag.kind(), tagIndex, tag.id, tag.flags, tag.offset, tag.size).c_str());
            if (tag.type) ImGui::TextUnformatted(fmt::format("Asset type 0x{:08X} / server 0x{:X} / client 0x{:X}", *tag.type, *tag.type & 0xffff, *tag.type >> 16).c_str());
            if (tag.parent) ImGui::TextUnformatted(fmt::format("Parent tag #{}: {}", *tag.parent, container.tags[*tag.parent].name).c_str());
            ImGui::EndTooltip();
        }
        if (ImGui::BeginPopupContextItem()) {
            if (ImGui::MenuItem("Open payload / record")) openTag({containerIndex, tagIndex});
            if (ImGui::MenuItem("Open 64-byte tag header")) openTag({containerIndex, tagIndex}, true);
            if (tag.type == 0x10007 && tag.size >= 76 + 128 && ImGui::MenuItem("Open embedded DDS"))
                openRegion(containerIndex, {container.region.name + " / " + tag.name + " (DDS)", tag.offset + 76, tag.size - 76}, "dds.hexpat");
            ImGui::EndPopup();
        }
        ImGui::PopID(); ImGui::PopID();
    }
    void ViewGow3Browser::drawAssets() {
        if (m_assets.empty()) ImGui::TextWrapped("Expand a container to read its assets, or use Index assets. Names are searched within indexed containers.");
        for (const auto &[kind, names] : m_assets) {
            ImGui::PushID(kind.c_str());
            if (ImGui::TreeNodeEx("##kind", ImGuiTreeNodeFlags_SpanAvailWidth, "%s (%zu)", kind.c_str(), names.size())) {
                for (const auto &[name, refs] : names) {
                    if (!matchesFilter(name) && !matchesFilter(kind)) continue;
                    if (refs.size() == 1) drawTag(refs[0].container, refs[0].tag);
                    else {
                        ImGui::PushID(name.c_str());
                        if (ImGui::TreeNodeEx("##name", ImGuiTreeNodeFlags_SpanAvailWidth, "%s (%zu sources)", name.c_str(), refs.size())) {
                            for (const auto ref : refs) {
                                ImGui::TextUnformatted(m_containers[ref.container].region.name.c_str()); drawTag(ref.container, ref.tag);
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
    std::string ViewGow3Browser::containerExtension(const std::string &name) { return format::extensionOf(name); }
    bool ViewGow3Browser::containerMatchesFilter(std::size_t index) const {
        const auto &container = m_containers[index];
        return matchesFilter(container.region.name) || std::ranges::any_of(container.tags, [this](const auto &tag) { return matchesFilter(tag.name); }) ||
            (m_groupContainerTags && std::ranges::any_of(m_containerKinds[index], [this](const auto &entry) { return matchesFilter(entry.first); }));
    }
    void ViewGow3Browser::drawContainers() {
        for (const auto &[extension, containers] : m_containerExtensions) {
            const bool extensionMatches = matchesFilter(extension);
            std::vector<std::size_t> visible;
            for (const auto index : containers) if (extensionMatches || containerMatchesFilter(index)) visible.push_back(index);
            if (visible.empty()) continue;
            ImGui::PushID(extension.c_str());
            if (ImGui::TreeNodeEx("##extension", ImGuiTreeNodeFlags_SpanAvailWidth, "%s (%zu)", extension.c_str(), visible.size())) {
                for (const auto index : visible) drawContainer(index, extensionMatches);
                ImGui::TreePop();
            }
            ImGui::PopID();
        }
    }
    void ViewGow3Browser::drawContainer(std::size_t index, bool showAll) {
        const auto &container = m_containers[index];
        ImGui::PushID(static_cast<int>(index));
        if (ImGui::TreeNodeEx("##container", ImGuiTreeNodeFlags_SpanAvailWidth, "%s", container.region.name.c_str())) {
            if (!container.inspected && container.recognized && container.error.empty()) inspectContainer(index);
            if (!container.error.empty()) ImGui::TextWrapped("%s", container.error.c_str());
            if (ImGui::Button("Open file"))
                openRegion(index, {container.region.name + " (file)", 0, std::numeric_limits<u64>::max()}, "raw.hexpat");
            if (container.inspected && container.error.empty()) {
                ImGui::SameLine();
                if (ImGui::Button("Open container with pattern")) openRegion(index, {container.region.name + " (container)", 0, container.region.size}, container.pattern);
            }
            if (container.recognized && !container.inspected) ImGui::TextUnformatted("Waiting / reading contents...");
            const bool all = showAll || matchesFilter(container.region.name);
            if (m_groupContainerTags) {
                for (const auto &[kind, tags] : m_containerKinds[index]) {
                    const bool groupMatches = all || matchesFilter(kind);
                    const auto visible = std::ranges::count_if(tags, [&](auto t) { return groupMatches || matchesFilter(container.tags[t].name); });
                    if (!visible) continue;
                    ImGui::PushID(kind.c_str());
                    if (ImGui::TreeNodeEx("##tag_kind", ImGuiTreeNodeFlags_SpanAvailWidth, "%s (%zu)", kind.c_str(), static_cast<std::size_t>(visible))) {
                        for (const auto t : tags) if (groupMatches || matchesFilter(container.tags[t].name)) drawTag(index, t);
                        ImGui::TreePop();
                    }
                    ImGui::PopID();
                }
            } else {
                for (std::size_t t = 0; t < container.tags.size(); ++t) if (all || matchesFilter(container.tags[t].name)) drawTag(index, t);
            }
            ImGui::TreePop();
        }
        ImGui::PopID();
    }
    void ViewGow3Browser::drawContent() {
        ImGui::BeginDisabled(m_scanTask.isRunning() || m_inspectTask.isRunning() || m_inspection != nullptr);
        ImGui::SetNextItemWidth(-1); ImGui::InputText("##source_path", m_path);
        if (ImGui::Button("Open game folder...")) fs::openFileBrowser(fs::DialogMode::Folder, {}, [this](const auto &path) { startScan(path); });
        ImGui::SameLine();
        if (ImGui::Button("File...")) fs::openFileBrowser(fs::DialogMode::Open, {}, [this](const auto &path) { startScan(path); });
        ImGui::SameLine(); if (ImGui::Button("Open") && !m_path.empty()) startScan(std::filesystem::u8path(m_path));
        ImGui::EndDisabled();
        if (m_scanTask.isRunning() || m_inspectTask.isRunning() || m_indexAll) {
            if (ImGui::Button("Cancel indexing")) { m_scanTask.interrupt(); m_inspectTask.interrupt(); m_indexAll = false; }
        } else {
            if (ImGui::Button("Index assets")) m_indexAll = true;
        }
        ImGui::Text("%zu files | %zu inspected | %zu indexed assets", m_containers.size(), m_inspectedCount, m_assetCount);
        if (!m_error.empty()) ImGui::TextWrapped("%s", m_error.c_str());
        ImGui::Checkbox("Apply pattern on open", &m_autoPattern);
        if (auto asset = dynamic_cast<AssetProvider*>(ImHexApi::Provider::get())) {
            if (ImGui::Button("Apply built-in pattern to current tab")) applyPattern(asset->pattern());
        }
        ImGui::SetNextItemWidth(-1); ImGui::InputTextWithHint("##filter", "Search indexed names / extensions...", m_filter);
        ImGui::BeginDisabled(!m_selected.has_value()); if (ImGui::Button("Open selected") && m_selected) openTag(*m_selected); ImGui::EndDisabled();
        if (ImGui::BeginTabBar("##browse_mode")) {
            if (ImGui::BeginTabItem("Containers (Source)")) {
                ImGui::Checkbox("Group by kind", &m_groupContainerTags);
                ImGui::BeginChild("##container_tree"); drawContainers(); ImGui::EndChild(); ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Assets")) {
                ImGui::BeginChild("##asset_tree"); drawAssets(); ImGui::EndChild(); ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
    }
    void ViewGow3Browser::drawHelpText() {
        ImGui::TextWrapped("Open a GoW III Remastered game folder or container file. Containers are grouped by extension and read when expanded. "
            "Index assets reads all recognized containers. Double-click a chunk for its read-only hex tab and pattern. "
            "This browser supports PS4 GoW III Remastered WADs with 64-byte headers. Textures can open their embedded DDS. PS3, package decryption and 3D previews are not provided.");
    }
}
