#include <gow_format.hpp>
#include <pl/pattern_language.hpp>
#include <pl/core/errors/error.hpp>
#include <pl/patterns/pattern.hpp>

#include <cstring>
#include <iostream>
#include <iterator>
#include <map>

using namespace hex::plugin::gow1::format;

namespace {
    void put(Bytes &data, std::size_t at, std::uint32_t value) {
        for (std::size_t i = 0; i < 4; ++i) data.at(at + i) = std::uint8_t(value >> (i * 8));
    }
    bool evaluate(const std::string &name, const Read &read, Offset size, bool expected = true) {
        std::ifstream file(std::filesystem::path(GOW1_PATTERN_DIR) / name, std::ios::binary);
        if (!file) throw std::runtime_error("Pattern file is missing: " + name);
        const std::string code((std::istreambuf_iterator<char>(file)), {});
        pl::PatternLanguage runtime;
        runtime.setDataSource(0, size, [&](auto offset, auto buffer, auto count) {
            requireRange(offset, count, size);
            read(offset, {buffer, count});
        });
        const bool success = runtime.executeString(code) == 0;
        if (success != expected) {
            std::cerr << name << " unexpected result\n";
            for (const auto &error : runtime.getCompileErrors()) std::cerr << error.format() << '\n';
            if (runtime.getEvalError()) std::cerr << runtime.getEvalError()->what() << '\n';
            return false;
        }
        if (success && name == "toc.hexpat") {
            Bytes bytes(static_cast<std::size_t>(size));
            read(0, bytes);
            const auto expectedRecords = parseToc(bytes);
            const auto &patterns = runtime.getPatterns();
            const auto find = [&](const std::string &variable) -> const pl::ptrn::Pattern* {
                for (const auto &pattern : patterns) if (pattern->getVariableName() == variable) return pattern.get();
                return nullptr;
            };
            const auto *records = find("records");
            const auto *array = dynamic_cast<const pl::ptrn::IIterable*>(records);
            const auto *terminator = find("terminator");
            if (!records || !array || array->getEntryCount() != expectedRecords.size() || records->getOffset() != 0 ||
                records->getSize() != expectedRecords.size() * 24 || !terminator || terminator->getOffset() != expectedRecords.size() * 24 || terminator->getSize() != 12) {
                std::cerr << "toc.hexpat must expose all TOC records as one persistent records array, followed by the terminator\n";
                return false;
            }
            for (std::size_t i = 0; i < expectedRecords.size(); ++i) {
                const auto record = array->getEntry(i);
                if (record->getOffset() != i * 24 || record->getSize() != 24) {
                    std::cerr << "TOC array element has an incorrect byte range\n";
                    return false;
                }
            }
            std::cout << "TOC array verified: " << array->getEntryCount() << " records / " << records->getSize() << " bytes\n";
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
            for (const auto &container : catalog.containers) {
                if (!container.wad || !container.error.empty()) continue;
                auto readWad = regionRead(container.region);
                if (!wadChecked) {
                    ok &= evaluate("wad.hexpat", readWad, container.region.size);
                    wadChecked = true;
                }
                for (const auto &tag : parseWad(readWad, container.region.size)) {
                    if (!tag.type || sampled[*tag.type] >= 3) continue;
                    ++sampled[*tag.type];
                    Region payload{tag.name, container.region.offset + tag.payloadOffset(), tag.size};
                    ok &= evaluate("resource.hexpat", regionRead(payload), tag.size);
                }
            }
            std::size_t samples = 0;
            for (const auto &[type, count] : sampled) samples += count;
            std::cout << "Real-data patterns: " << sampled.size() << " asset types / " << samples << " resource samples\n";
        } else {
            ok &= evaluate("raw.hexpat", Bytes{});
            ok &= evaluate("raw.hexpat", Bytes(100));
            ok &= evaluate("tag.hexpat", Bytes(32));
            ok &= evaluate("tag.hexpat", Bytes(31), false);
            Bytes toc(36); std::memcpy(toc.data(), "A.WAD", 5);
            ok &= evaluate("toc.hexpat", toc);
            ok &= evaluate("toc.hexpat", Bytes(toc.begin(), toc.begin() + 24), false);
            Bytes copies(132);
            std::memcpy(copies.data(), "A.WAD", 5); put(copies, 16, 64);
            std::memcpy(copies.data() + 24, "A.WAD", 5); put(copies, 40, 64); put(copies, 44, 1);
            std::memcpy(copies.data() + 48, "B.VPK", 5); put(copies, 64, 128); put(copies, 68, 2);
            put(copies, 88, 256); put(copies, 92, 3); // NUL name, followed by actual record data
            std::memcpy(copies.data() + 96, "C.WAD", 5);
            ok &= evaluate("toc.hexpat", copies);
            ok &= evaluate("toc.hexpat", Bytes(copies.begin(), copies.begin() + 73), false);
            auto badTerminator = copies; badTerminator.back() = 1;
            ok &= evaluate("toc.hexpat", badTerminator, false);
            Bytes wad(80); wad[0] = 24; put(wad, 4, 0xffffffff); wad[32] = 30; put(wad, 36, 4); put(wad, 64, 3);
            ok &= evaluate("wad.hexpat", wad);
            ok &= evaluate("wad.hexpat", Bytes(wad.begin(), wad.end() - 1), false);
            for (const auto &[type, size] : std::map<std::uint32_t, std::size_t>{{3, 4}, {7, 88}, {8, 120}, {9, 588}, {12, 24}, {0x20001, 92}, {0x40001, 16}}) {
                Bytes resource(size); put(resource, 0, type);
                if (type == 8) put(resource, 0x34, 1);
                ok &= evaluate("resource.hexpat", resource);
                if (type == 7 || type == 9 || type == 0x20001) {
                    resource.pop_back();
                    ok &= evaluate("resource.hexpat", resource, false);
                }
            }
            Bytes badMaterial(56); put(badMaterial, 0, 8); put(badMaterial, 0x34, 0xffffffff);
            ok &= evaluate("resource.hexpat", badMaterial, false);
        }
        if (ok) std::cout << "GoW I pattern checks passed\n";
        return ok ? 0 : 1;
    } catch (const std::exception &error) { std::cerr << error.what() << '\n'; return 1; }
}
