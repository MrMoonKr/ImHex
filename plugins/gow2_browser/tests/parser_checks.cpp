#include <gow_format.hpp>

#include <algorithm>
#include <cstring>
#include <iostream>
#include <limits>
#include <set>

using namespace hex::plugin::gow2::format;

namespace {
    void check(bool condition, const char *message) {
        if (!condition) throw std::runtime_error(message);
    }
    template<typename F> void rejects(F &&call, const char *message) {
        try { call(); } catch (const FormatError &) { return; }
        throw std::runtime_error(message);
    }
    void put(Bytes &data, std::size_t offset, std::uint32_t value, std::size_t width = 4, bool big = false) {
        for (std::size_t i = 0; i < width; ++i)
            data.at(offset + i) = std::uint8_t(value >> (8 * (big ? width - 1 - i : i)));
    }
    void text(Bytes &data, std::size_t offset, const std::string &value) {
        std::copy(value.begin(), value.end(), data.begin() + offset);
    }
    Read reader(const Bytes &data) {
        return [&data](Offset offset, std::span<std::uint8_t> output) {
            requireRange(offset, output.size(), data.size());
            std::memcpy(output.data(), data.data() + offset, output.size());
        };
    }
    Bytes tag(std::uint16_t id, const std::string &name = "", Bytes payload = {}, std::optional<std::uint32_t> declared = {}) {
        Bytes result(32 + payload.size());
        put(result, 0, id, 2);
        put(result, 4, declared.value_or(static_cast<std::uint32_t>(payload.size())));
        text(result, 8, name);
        std::copy(payload.begin(), payload.end(), result.begin() + 32);
        result.resize((result.size() + 15) & ~std::size_t(15));
        return result;
    }
    void append(Bytes &data, const Bytes &value) { data.insert(data.end(), value.begin(), value.end()); }
    Bytes type(std::uint32_t id) { Bytes result(4); put(result, 0, id); return result; }

    void checkWad() {
        Bytes data;
        append(data, tag(21, "header", Bytes(8)));
        append(data, tag(0, "count", {}, 0xffffffff));
        append(data, tag(2));
        append(data, tag(1, "object", type(0x10001)));
        append(data, tag(2));
        append(data, tag(1, "material", type(8)));
        append(data, tag(1, "texture", type(7)));
        append(data, tag(3));
        append(data, tag(1, "script", type(0x10004)));
        append(data, tag(3));
        append(data, tag(1, "link"));
        append(data, tag(9, "NCV_rail", Bytes(7)));
        auto tags = parseWad(reader(data), data.size());
        check(tags.size() == 12, "WAD tag count");
        check(tags[1].declaredSize == 0xffffffff && tags[1].size == 0, "Entity count is not a payload length");
        check(!tags[3].parent && tags[5].parent == 3 && tags[6].parent == 5 && tags[8].parent == 3, "WAD nested group ownership");
        check(tags[10].kind() == "Links" && tags[11].kind() == "Raw records", "Link and raw classification");
        check(tags[3].kind() == "Objects" && tags[6].kind() == "Textures", "GoW II type IDs");
        auto shape = type(0x10);
        auto terrain = tag(1, "RIB_test", shape);
        check(parseWad(reader(terrain), terrain.size())[0].kind() == "Collision terrain", "Shape-dependent collision type");
        rejects([&] { parseWad(reader(data), data.size() - 1); }, "Missing alignment accepted");
        auto end = tag(3);
        rejects([&] { parseWad(reader(end), end.size()); }, "Unmatched group accepted");
        auto start = tag(2);
        rejects([&] { parseWad(reader(start), start.size()); }, "Unclosed group accepted");
        append(start, tag(2));
        rejects([&] { parseWad(reader(start), start.size()); }, "Pending nested group accepted");
        auto shortResource = tag(1, "bad", Bytes(3));
        rejects([&] { parseWad(reader(shortResource), shortResource.size()); }, "Truncated type accepted");
        auto badRange = tag(1, "bad", {}, 0xffffffff);
        rejects([&] { parseWad(reader(badRange), badRange.size()); }, "Oversized payload accepted");
        const auto gow1 = tag(30, "wrong_game", type(7));
        rejects([&] { parseWad(reader(gow1), gow1.size()); }, "GoW I WAD accepted as GoW II");
        int calls = 0;
        try { parseWad(reader(data), data.size(), [&] { if (++calls == 3) throw std::logic_error("cancel"); }); }
        catch (const std::logic_error &) {}
        check(calls == 3, "Cancellation did not stop parsing");
    }

    Bytes tocBytes() {
        Bytes toc(4 + 4 * 36 + 4 * 4);
        put(toc, 0, 4);
        for (std::size_t entry = 0; entry < 4; ++entry) {
            const auto at = 4 + entry * 36;
            text(toc, at, entry == 2 ? "B.WAD" : "A.WAD");
            put(toc, at + 24, entry == 1 ? 64 : entry == 2 ? 32 : 48);
            put(toc, at + 28, entry == 0 ? 2 : 1);
            put(toc, at + 32, entry == 3 ? 1 : static_cast<std::uint32_t>(entry == 0 ? 0 : entry + 1));
        }
        for (std::size_t slot = 0; slot < 4; ++slot)
            put(toc, 4 + 4 * 36 + slot * 4, slot == 3 ? 10000000 : static_cast<std::uint32_t>(slot));
        return toc;
    }

    void directoryRecord(Bytes &image, std::size_t at, std::string name, std::uint32_t sector, std::uint32_t size, bool directory = false) {
        const auto length = 33 + name.size() + (name.size() % 2 == 0 ? 1 : 0);
        image[at] = static_cast<std::uint8_t>(length);
        put(image, at + 2, sector); put(image, at + 6, sector, 4, true);
        put(image, at + 10, size); put(image, at + 14, size, 4, true);
        image[at + 25] = directory ? 2 : 0;
        put(image, at + 28, 1, 2); put(image, at + 30, 1, 2, true);
        image[at + 32] = static_cast<std::uint8_t>(name.size());
        text(image, at + 33, name);
    }

    void checkIsoAndToc() {
        auto toc = tocBytes();
        auto records = parseToc(toc);
        check(records.size() == 5 && records[1].index == 1 && records[1].sector == 1, "TOC copies lost");
        check(records[2].size == 64, "TOC same name different size lost");
        check(records[3].pak == 1 && records[3].sector == 0, "PART2 encoded-sector boundary");
        check(records[4].entryIndex == 3 && records[4].locationIndex == 1, "Overlapping location slots must retain entry identity");
        auto truncated = Bytes(toc.begin(), toc.begin() + 96);
        rejects([&] { parseToc(truncated); }, "Truncated entry table accepted");
        auto badToc = toc;
        put(badToc, 4 + 28, 0);
        rejects([&] { parseToc(badToc); }, "Zero location count accepted");
        badToc = toc; put(badToc, 4 + 32, 0xffffffff);
        rejects([&] { parseToc(badToc); }, "Out-of-range location slot accepted");
        badToc = toc; badToc.push_back(0);
        rejects([&] { parseToc(badToc); }, "Unaligned location table accepted");
        badToc = toc; put(badToc, 0, 0xffffffff);
        rejects([&] { parseToc(badToc); }, "Entry count overflow accepted");
        Bytes gow1(48); text(gow1, 0, "A.WAD");
        rejects([&] { parseToc(gow1); }, "GoW I TOC accepted as GoW II");
        Bytes image(32 * 2048);
        image[16 * 2048] = 1; text(image, 16 * 2048 + 1, "CD001"); image[16 * 2048 + 6] = 1;
        put(image, 16 * 2048 + 128, 2048, 2); put(image, 16 * 2048 + 130, 2048, 2, true);
        directoryRecord(image, 16 * 2048 + 156, std::string(1, '\0'), 20, 2048, true);
        directoryRecord(image, 20 * 2048, "GODOFWAR.TOC;1", 21, static_cast<std::uint32_t>(toc.size()));
        const auto next = 20 * 2048 + image[20 * 2048];
        directoryRecord(image, next, "PART1.PAK;1", 22, 3 * 2048);
        std::copy(toc.begin(), toc.end(), image.begin() + 21 * 2048);
        const auto root = readIsoRoot(reader(image), image.size());
        check(root.size() == 2 && root[0].name == "GODOFWAR.TOC", "ISO root filenames");
        const auto catalog = readCatalog(reader(image), image.size(), "game.iso");
        check(catalog.containers.size() == 5 && catalog.toc.has_value(), "ISO catalog count");
        check(catalog.containers[1].region.offset == 23 * 2048, "PAK sector mapping");
        check(catalog.containers[3].error == "PART2.PAK is missing", "Missing PAK diagnosis");
        const auto part2Record = next + image[next];
        directoryRecord(image, part2Record, "PART2.PAK;1", 27, 2048);
        const auto dualPak = readCatalog(reader(image), image.size(), "game.iso");
        check(dualPak.containers[3].available && dualPak.containers[3].region.offset == 27 * 2048, "PART2 extent mapping");
        put(image, next + 10, 10); put(image, next + 14, 10, 4, true);
        const auto bounded = readCatalog(reader(image), image.size(), "game.iso");
        check(!bounded.containers[0].error.empty(), "Out-of-PAK range accepted");
        image[16 * 2048 + 156 + 6] ^= 1;
        rejects([&] { readIsoRoot(reader(image), image.size()); }, "Both-endian mismatch accepted");
        rejects([] { requireRange(std::numeric_limits<Offset>::max(), 1, 64); }, "Offset overflow accepted");
        rejects([] { requireRange(1, std::numeric_limits<Offset>::max(), 64); }, "Size overflow accepted");
    }

    int audit(const std::filesystem::path &path) {
        auto source = std::make_shared<FileSource>(path);
        Read read = [source](auto offset, auto bytes) { source->read(offset, bytes); };
        const auto catalog = readCatalog(read, source->size(), path.filename().string());
        std::size_t wads = 0, tags = 0, resources = 0, errors = 0;
        std::size_t uniqueTags = 0, uniqueResources = 0;
        std::set<std::pair<std::string, Offset>> uniqueWads;
        std::set<std::pair<std::string, Offset>> uniqueFiles;
        for (const auto &container : catalog.containers) {
            uniqueFiles.emplace(container.region.name, container.region.size);
            if (!container.error.empty()) { ++errors; std::cerr << container.region.name << ": " << container.error << '\n'; continue; }
            if (!container.wad) continue;
            ++wads;
            try {
                const auto parsed = parseWad([&](auto offset, auto bytes) {
                    requireRange(offset, bytes.size(), container.region.size);
                    read(container.region.offset + offset, bytes);
                }, container.region.size);
                tags += parsed.size();
                resources += std::ranges::count(parsed, 1, &Tag::tag);
                if (uniqueWads.emplace(container.region.name, container.region.size).second) {
                    uniqueTags += parsed.size();
                    uniqueResources += std::ranges::count(parsed, 1, &Tag::tag);
                }
            } catch (const std::exception &error) { ++errors; std::cerr << container.region.name << ": " << error.what() << '\n'; }
        }
        std::cout << "TOC locations=" << catalog.containers.size() << " unique files=" << uniqueFiles.size()
                  << " WAD copies=" << wads << " tags=" << tags << " resources=" << resources << " errors=" << errors << '\n';
        std::cout << "Unique WADs=" << uniqueWads.size() << " tags=" << uniqueTags << " resources=" << uniqueResources << '\n';
        return errors == 0 ? 0 : 1;
    }
}

int main(int argc, char **argv) {
    try {
        if (argc == 3 && std::string(argv[1]) == "--iso") return audit(std::filesystem::u8path(argv[2]));
        checkWad();
        checkIsoAndToc();
        std::cout << "GoW II parser checks passed\n";
        return 0;
    } catch (const std::exception &error) { std::cerr << error.what() << '\n'; return 1; }
}
