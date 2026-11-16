#include <gow_format.hpp>
#include <pl/pattern_language.hpp>
#include <pl/core/errors/error.hpp>

#include <cstring>
#include <iostream>
#include <iterator>
#include <map>

using namespace hex::plugin::gow2::format;

namespace {
    void put(Bytes &data, std::size_t at, std::uint32_t value) {
        for (std::size_t i = 0; i < 4; ++i) data.at(at + i) = std::uint8_t(value >> (i * 8));
    }
    bool evaluate(const std::string &name, const Read &read, Offset size, bool expected = true) {
        std::ifstream file(std::filesystem::path(GOW2_PATTERN_DIR) / name, std::ios::binary);
        if (!file) throw std::runtime_error("Pattern file is missing: " + name);
        const std::string code((std::istreambuf_iterator<char>(file)), {});
        pl::PatternLanguage runtime;
        runtime.setDataSource(0, size, [&](auto offset, auto buffer, auto count) {
            requireRange(offset, count, size);
            read(offset, {buffer, count});
        });
        bool success = false;
        try { success = runtime.executeString(code) == 0; }
        catch (const FormatError &error) {
            if (expected) std::cerr << name << ": " << error.what() << '\n';
            return !expected;
        }
        if (success != expected) {
            std::cerr << name << " unexpected result\n";
            for (const auto &error : runtime.getCompileErrors()) std::cerr << error.format() << '\n';
            if (runtime.getEvalError()) std::cerr << runtime.getEvalError()->what() << '\n';
            return false;
        }
        return true;
    }
    bool evaluate(const std::string &name, const Bytes &data, bool expected = true) {
        return evaluate(name, [&](auto offset, auto bytes) {
            requireRange(offset, bytes.size(), data.size());
            std::memcpy(bytes.data(), data.data() + offset, bytes.size());
        }, data.size(), expected);
    }
}

int main(int argc, char **argv) {
    try {
        bool ok = true;
        if (argc == 3 && std::string(argv[1]) == "--iso") {
            const auto source = std::make_shared<FileSource>(std::filesystem::u8path(argv[2]));
            Read read = [source](auto offset, auto bytes) { source->read(offset, bytes); };
            const auto catalog = readCatalog(read, source->size(), "game.iso");
            const auto regionRead = [&](const Region &region) -> Read {
                return [&, region](auto offset, auto bytes) {
                    requireRange(offset, bytes.size(), region.size);
                    read(region.offset + offset, bytes);
                };
            };
            if (catalog.toc) ok &= evaluate("toc.hexpat", regionRead(*catalog.toc), catalog.toc->size);
            std::map<std::uint32_t, std::size_t> sampled;
            bool wadChecked = false;
            std::size_t unsupported = 0;
            for (const auto &container : catalog.containers) {
                if (!container.wad || !container.error.empty()) continue;
                auto readWad = regionRead(container.region);
                if (!wadChecked) {
                    ok &= evaluate("wad.hexpat", readWad, container.region.size);
                    wadChecked = true;
                }
                std::vector<Tag> tags;
                try { tags = parseWad(readWad, container.region.size); }
                catch (const FormatError &error) {
                    ++unsupported;
                    std::cerr << "Skipped non-decodable WAD: " << container.region.name << ": " << error.what() << '\n';
                    continue;
                }
                for (const auto &tag : tags) {
                    if (!tag.type || sampled[*tag.type] >= 3) continue;
                    ++sampled[*tag.type];
                    Region payload{tag.name, container.region.offset + tag.payloadOffset(), tag.size};
                    ok &= evaluate("resource.hexpat", regionRead(payload), tag.size);
                }
            }
            std::size_t samples = 0;
            for (const auto &[type, count] : sampled) samples += count;
            std::cout << "Real-data patterns: " << sampled.size() << " asset types / " << samples << " resource samples\n";
            std::cout << "Non-decodable WAD copies skipped=" << unsupported << '\n';
        } else {
            ok &= evaluate("raw.hexpat", Bytes{});
            ok &= evaluate("raw.hexpat", Bytes(100));
            ok &= evaluate("tag.hexpat", Bytes(32));
            ok &= evaluate("tag.hexpat", Bytes(31), false);
            Bytes toc(48); put(toc, 0, 1); std::memcpy(toc.data() + 4, "A.WAD", 5);
            put(toc, 28, 48); put(toc, 32, 2); put(toc, 44, 10000001);
            ok &= evaluate("toc.hexpat", toc);
            ok &= evaluate("toc.hexpat", Bytes(toc.begin(), toc.begin() + 39), false);
            auto badToc = toc; put(badToc, 32, 4);
            ok &= evaluate("toc.hexpat", badToc, false);
            badToc = toc; put(badToc, 32, 0);
            ok &= evaluate("toc.hexpat", badToc, false);
            Bytes wad(80); put(wad, 4, 0xffffffff); wad[32] = 1; put(wad, 36, 4); put(wad, 64, 3);
            ok &= evaluate("wad.hexpat", wad);
            ok &= evaluate("wad.hexpat", Bytes(wad.begin(), wad.end() - 1), false);
            for (const auto &[type, size] : std::map<std::uint32_t, std::size_t>{{3, 4}, {7, 88}, {8, 120}, {9, 232}, {12, 24}, {0x20001, 104}, {0x30001, 104}, {0x10001, 16}}) {
                Bytes resource(size); put(resource, 0, type);
                if (type == 8) put(resource, 0x34, 1);
                ok &= evaluate("resource.hexpat", resource);
                if (type == 7 || type == 9 || type == 0x20001 || type == 0x30001) {
                    resource.pop_back();
                    ok &= evaluate("resource.hexpat", resource, false);
                }
            }
            Bytes badMaterial(56); put(badMaterial, 0, 8); put(badMaterial, 0x34, 0xffffffff);
            ok &= evaluate("resource.hexpat", badMaterial, false);
            Bytes camera(272); put(camera, 0, 9); put(camera, 228, 1);
            ok &= evaluate("resource.hexpat", camera);
            put(camera, 228, 1025);
            ok &= evaluate("resource.hexpat", camera, false);
        }
        if (ok) std::cout << "GoW II pattern checks passed\n";
        return ok ? 0 : 1;
    } catch (const std::exception &error) { std::cerr << error.what() << '\n'; return 1; }
}
