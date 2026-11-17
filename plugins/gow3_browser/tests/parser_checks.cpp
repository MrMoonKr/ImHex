#include "fixtures.hpp"
#include <iostream>
#include <map>
using namespace fixtures;

namespace {
    template<typename F> void rejects(F call, const char *message) {
        try { call(); } catch (const FormatError &) { return; }
        throw std::runtime_error(message);
    }
    void synthetic() {
        const auto data = wad(); const auto tags = parseWad(reader(data), data.size());
        check(tags.size() == 14, "WAD tag count");
        check(tags[1].size == 0 && tags[1].declaredSize == 0xffffffff, "Entity count must not consume a payload");
        check(tags[4].headerSize == 64 && tags[4].offset == tags[4].headerOffset + 64 && tags[4].flags == 13, "GoW3 tag header layout");
        check(!tags[3].parent && !tags[4].parent && tags[6].parent == 4 && tags[7].parent == 6 && tags[9].parent == 4 && !tags[11].parent,
              "Owner/resource nesting with auxiliary tags before owner");
        check(tags[6].category == "Textures" && tags[6].pattern == "texture.hexpat" && tags[7].isAsset(), "Texture and embedded file dispatch");
        check(tags[9].category == "Links" && !tags[9].isAsset() && !tags[11].isAsset() && tags[11].category == "Context chunks", "Links/context must stay in source view");
        check(std::ranges::count_if(tags, [](const auto &item) { return item.isAsset(); }) == 5, "Asset count");
        auto fullName = tag(7, std::string(56, 'A'), {42});
        check(parseWad(reader(fullName), fullName.size())[0].name.size() == 56, "56-byte tag names");
        auto tiny = tag(1, "raw_resource", {1, 2, 3});
        check(!parseWad(reader(tiny), tiny.size())[0].type, "Short resource must stay raw without reading beyond its payload");
        auto unknown = tag(1, "unknown", resource(0x12345678));
        check(parseWad(reader(unknown), unknown.size())[0].category == "Asset type 0x12345678", "Unknown type identity");
        auto unclosed = tag(2); rejects([&] { parseWad(reader(unclosed), unclosed.size()); }, "Unclosed group accepted");
        auto unmatched = tag(3); rejects([&] { parseWad(reader(unmatched), unmatched.size()); }, "Unmatched group end accepted");
        append(unclosed, tag(2)); rejects([&] { parseWad(reader(unclosed), unclosed.size()); }, "Pending nested group accepted");
        auto emptyGroup = tag(2); append(emptyGroup, tag(3)); check(parseWad(reader(emptyGroup), emptyGroup.size()).size() == 2, "Empty group");
        auto oversized = tag(1, "bad", {}, 0xffffffff); rejects([&] { parseWad(reader(oversized), oversized.size()); }, "Oversized payload accepted");
        rejects([&] { parseWad(reader(fullName), fullName.size() - 1); }, "Missing alignment accepted");
        rejects([&] { parseWad(reader(data), 32); }, "PS2-sized tag header accepted");
        rejects([&] { parseWad(reader(data), 0); }, "Empty WAD accepted");
        int calls = 0;
        try { parseWad(reader(data), data.size(), [&] { if (++calls == 3) throw std::logic_error("cancel"); }); } catch (const std::logic_error &) {}
        check(calls == 3, "Cancellation did not propagate");
        TempFiles temp;
        temp.add("game/EXEC/base.WAD", data); temp.add("_Update v1.02/game/EXEC/base.WAD", data); temp.add("unrelated.bin", {1});
        const auto files = discover(temp.directory);
        check(files.size() == 2 && files[0].region.name != files[1].region.name, "Base/update roots must remain separate and exclude unrelated installation files");
        check(discover(temp.directory / "game").size() == 2 && discover(temp.directory / "game" / "EXEC").size() == 1, "Game/EXEC source root handling");
        auto source = std::make_shared<FileSource>(files[0].path); Container container = files[0]; inspect(container, source);
        check(container.pattern == "wad.hexpat" && container.tags.size() == 14, "Container inspection");
    }
    int audit(const std::filesystem::path &path) {
        auto files = discover(path); std::size_t wads = 0, tags = 0, resources = 0, assets = 0, errors = 0;
        for (auto &file : files) {
            if (extensionOf(file.region.name) != ".WAD") continue;
            ++wads;
            try {
                auto source = std::make_shared<FileSource>(file.path); inspect(file, source);
                tags += file.tags.size(); resources += std::ranges::count(file.tags, 1, &Tag::id);
                assets += std::ranges::count_if(file.tags, [](const auto &item) { return item.isAsset(); });
            } catch (const std::exception &error) { ++errors; std::cerr << file.region.name << ": " << error.what() << '\n'; }
        }
        std::cout << "Files=" << files.size() << " WADs=" << wads << " tags=" << tags << " resources=" << resources << " assets=" << assets << " errors=" << errors << '\n';
        return errors ? 1 : 0;
    }
}
int main(int argc, char **argv) {
    try {
        if (argc == 3 && std::string(argv[1]) == "--audit") return audit(std::filesystem::u8path(argv[2]));
        synthetic(); std::cout << "GoW3 parser checks passed\n"; return 0;
    } catch (const std::exception &error) { std::cerr << error.what() << '\n'; return 1; }
}
