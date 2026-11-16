#include <view_gowr_browser.hpp>
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

namespace hex::plugin::gowr {
    namespace {
        void showView(const UnlocalizedString &name) {
            if (auto view = ContentRegistry::Views::getViewByName(name)) view->getWindowOpenState() = true;
        }
    }
    ViewGowrBrowser::ViewGowrBrowser() : View::Window("hex.gowr.browser.name"_unlocalized, ICON_VS_LIBRARY) {
        ContentRegistry::UserInterface::addMenuItem(
            {"hex.builtin.menu.view"_unlocalized, "hex.gowr.browser.name"_unlocalized}, ICON_VS_LIBRARY, 3500, Shortcut::None,
            [this] { getWindowOpenState() = !getWindowOpenState(); }, [] { return !LayoutManager::isLayoutLocked(); },
            [this] { return getWindowOpenState(); });
        for (auto &[priority, item] : ContentRegistry::UserInterface::impl::getMenuItemsMutable()) {
            if (item.unlocalizedNames.size() == 2 && item.unlocalizedNames.back() == "hex.gowr.browser.name"_unlocalized)
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
    ViewGowrBrowser::~ViewGowrBrowser() {
        EventProviderOpened::unsubscribe(this);
        m_scanTask.interrupt(); m_inspectTask.interrupt();
        m_scanTask.wait(); m_inspectTask.wait();
    }

    void ViewGowrBrowser::startScan(const std::filesystem::path &path) {
        if (m_scanTask.isRunning() || m_inspectTask.isRunning()) return;
        m_path = wolv::util::toUTF8String(path);
        m_containers.clear(); m_containerKinds.clear(); m_containerExtensions.clear(); m_assets.clear();
        m_cachedSource.reset(); m_inspection.reset(); m_selected.reset(); m_error.clear();
        m_assetCount = m_inspectedCount = m_tagCount = 0; m_indexAll = false;
        m_scan = std::make_shared<ScanState>();
        m_scanTask = TaskManager::createBackgroundTask("Listing Ragnarok containers", [state = m_scan, path](Task &task) {
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

    void ViewGowrBrowser::inspectContainer(std::size_t index) {
        if (m_inspectTask.isRunning() || m_inspection || index >= m_containers.size()) return;
        const auto &container = m_containers[index];
        if (container.inspected || !container.recognized || !container.error.empty()) return;
        auto state = std::make_shared<InspectionState>(); state->index = index; state->result = container;
        m_inspection = state;
        m_inspectTask = TaskManager::createBackgroundTask("Reading Ragnarok container", [state](Task &task) {
            try {
                auto source = std::make_shared<format::FileSource>(state->result.path, true, [&task] { task.update(); });
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

    void ViewGowrBrowser::drawAlwaysVisibleContent() {
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
                if (m_tagCount + state->result.tags.size() > 2000000) {
                    state->result.tags.clear(); state->result.error = "Index exceeds 2,000,000 chunks. Open a smaller folder.";
                    m_indexAll = false;
                }
                m_containers[index] = std::move(state->result);
                m_cachedSource = std::move(state->source);
                const auto &container = m_containers[index];
                m_tagCount += container.tags.size();
                for (std::size_t t = 0; t < container.tags.size(); ++t) {
                    const auto &tag = container.tags[t];
                    m_containerKinds[index][tag.kind()].push_back(t);
                    if (tag.id == 25) continue;
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

    bool ViewGowrBrowser::matchesFilter(const std::string &name) const {
        return m_filter.empty() || std::search(name.begin(), name.end(), m_filter.begin(), m_filter.end(),
            [](unsigned char a, unsigned char b) { return std::tolower(a) == std::tolower(b); }) != name.end();
    }
    void ViewGowrBrowser::applyPattern(const std::string &pattern) {
        showView("hex.builtin.view.pattern_editor.name"_unlocalized); showView("hex.builtin.view.pattern_data.name"_unlocalized);
        RequestSetPatternLanguageCode::post(std::string(romfs::get("patterns/" + pattern).string()));
        RequestTriggerPatternEvaluation::post();
    }

    void ViewGowrBrowser::openRegion(std::size_t index, format::Region region, const std::string &pattern, bool decode) {
        if (TaskManager::getRunningTaskCount() != 0) { m_error = "Wait for the current ImHex task to finish."; return; }
        const auto &container = m_containers.at(index);
        for (auto *provider : ImHexApi::Provider::getProviders()) {
            if (auto asset = dynamic_cast<AssetProvider*>(provider); asset && asset->matches(container.path, region, decode)) {
                ImHexApi::Provider::setCurrentProvider(provider); showView("hex.builtin.view.hex_editor.name"_unlocalized); return;
            }
        }
        auto source = decode ? container.source.lock() : nullptr;
        auto asset = std::make_shared<AssetProvider>(container.path, std::move(region), pattern, decode, std::move(source));
        std::shared_ptr<prv::Provider> provider = asset;
        ImHexApi::Provider::add(std::move(provider), true);
        m_opening = asset;
        std::ignore = ImHexApi::Provider::openProvider(asset);
    }
    void ViewGowrBrowser::openTag(AssetRef ref, bool header) {
        m_selected = ref;
        const auto &container = m_containers.at(ref.container); const auto &tag = container.tags.at(ref.tag);
        header = header || tag.size == 0;
        const auto pattern = header ? (tag.headerSize == 144 ? "entry.hexpat" : "raw.hexpat") : tag.pattern;
        openRegion(ref.container, {fmt::format("{} / {} [#{}]{}", container.region.name,
            tag.name.empty() ? "<unnamed>" : tag.name, ref.tag, header ? " (record)" : ""),
            header ? tag.headerOffset : tag.offset, header ? tag.headerSize : tag.size}, pattern);
    }

    void ViewGowrBrowser::drawTag(std::size_t containerIndex, std::size_t tagIndex) {
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
            ImGui::TextUnformatted(fmt::format("{}\n{} | chunk #{} | id {} / server {} / client {}\n{} offset 0x{:X} | {} bytes\nGUID 0x{:016X} | load block {}",
                container.region.name, tag.kind(), tagIndex, tag.id, tag.server, tag.client,
                container.compressed ? "Decoded" : "Stored", tag.offset, tag.size, tag.guid, tag.block).c_str());
            ImGui::EndTooltip();
        }
        if (ImGui::BeginPopupContextItem()) {
            if (ImGui::MenuItem("Open payload / record")) openTag({containerIndex, tagIndex});
            if (ImGui::MenuItem("Open entry record")) openTag({containerIndex, tagIndex}, true);
            if (tag.debugSize && ImGui::MenuItem("Open debug data"))
                openRegion(containerIndex, {container.region.name + " / " + tag.name + " (debug)", tag.debugOffset, tag.debugSize}, "raw.hexpat");
            if (tag.tempSize && ImGui::MenuItem("Open temp data"))
                openRegion(containerIndex, {container.region.name + " / " + tag.name + " (temp)", tag.tempOffset, tag.tempSize}, "raw.hexpat");
            ImGui::EndPopup();
        }
        ImGui::PopID(); ImGui::PopID();
    }
    void ViewGowrBrowser::drawAssets() {
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
    std::string ViewGowrBrowser::containerExtension(const std::string &name) { return format::extensionOf(name); }
    bool ViewGowrBrowser::containerMatchesFilter(std::size_t index) const {
        const auto &container = m_containers[index];
        return matchesFilter(container.region.name) || std::ranges::any_of(container.tags, [this](const auto &tag) { return matchesFilter(tag.name); }) ||
            (m_groupContainerTags && std::ranges::any_of(m_containerKinds[index], [this](const auto &entry) { return matchesFilter(entry.first); }));
    }
    void ViewGowrBrowser::drawContainers() {
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
    void ViewGowrBrowser::drawContainer(std::size_t index, bool showAll) {
        const auto &container = m_containers[index];
        ImGui::PushID(static_cast<int>(index));
        if (ImGui::TreeNodeEx("##container", ImGuiTreeNodeFlags_SpanAvailWidth, "%s", container.region.name.c_str())) {
            if (!container.inspected && container.recognized && container.error.empty()) inspectContainer(index);
            if (!container.error.empty()) ImGui::TextWrapped("%s", container.error.c_str());
            if (ImGui::Button("Open stored file"))
                openRegion(index, {container.region.name + " (stored)", 0, std::numeric_limits<u64>::max()}, "raw.hexpat", false);
            if (container.inspected && container.error.empty()) {
                ImGui::SameLine();
                if (ImGui::Button("Open decoded container")) openRegion(index, {container.region.name + " (decoded)", 0, container.region.size}, container.pattern);
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
    void ViewGowrBrowser::drawContent() {
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
    void ViewGowrBrowser::drawHelpText() {
        ImGui::TextWrapped("Open a Ragnarok game folder or container file. Containers are grouped by extension and read when expanded. "
            "Index assets reads all recognized containers. Double-click a chunk for its read-only hex tab and pattern. "
            "Decoded LZ4 offsets refer to the restored container, not its stored file. Pack TOCs expose records; texture reconstruction and streamed geometry joins are not provided.");
    }
}
