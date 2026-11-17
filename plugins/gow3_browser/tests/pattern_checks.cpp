#include "fixtures.hpp"
#include <pl/pattern_language.hpp>
#include <pl/core/errors/error.hpp>
#include <pl/patterns/pattern.hpp>
#include <iostream>
#include <iterator>
#include <map>
using namespace fixtures;

namespace {
    bool evaluate(const std::string &name, const Read &read, Offset size, bool expected = true) {
        std::ifstream file(std::filesystem::path(GOW3_PATTERN_DIR) / name, std::ios::binary);
        if (!file) throw std::runtime_error("Missing pattern: " + name);
        const std::string code((std::istreambuf_iterator<char>(file)), {});
        pl::PatternLanguage runtime;
        runtime.setDataSource(0, size, [&](auto offset, auto buffer, auto count) { requireRange(offset, count, size); read(offset, {buffer, count}); });
        bool success = false;
        try { success = runtime.executeString(code) == 0; }
        catch (const FormatError &error) { if (expected) std::cerr << name << ": " << error.what() << '\n'; return !expected; }
        if (success != expected) {
            std::cerr << name << " unexpected result\n";
            for (const auto &error : runtime.getCompileErrors()) std::cerr << error.format() << '\n';
            if (runtime.getEvalError()) std::cerr << runtime.getEvalError()->what() << '\n';
            return false;
        }
        if (success && name == "wad.hexpat") {
            const auto tags = parseWad(read, size);
            const auto &patterns = runtime.getPatterns();
            const auto *array = patterns.size() == 1 ? dynamic_cast<pl::ptrn::IIterable*>(patterns[0].get()) : nullptr;
            if (!array || array->getEntryCount() != tags.size()) { std::cerr << "WAD tag array missing from Pattern Data\n"; return false; }
            if (patterns[0]->getChildren().size() > tags.size() * 8) { std::cerr << "WAD payload highlighting expanded per byte\n"; return false; }
            for (std::size_t i = 0; i < tags.size(); ++i) {
                if (array->getEntry(i)->getOffset() != tags[i].headerOffset) { std::cerr << "WAD tag array offset mismatch\n"; return false; }
            }
        }
        return true;
    }
    bool evaluate(const std::string &name, const Bytes &data, bool expected = true) { return evaluate(name, reader(data), data.size(), expected); }
}
int main(int argc, char **argv) {
    try {
        bool ok = true;
        if (argc == 3 && std::string(argv[1]) == "--audit") {
            std::map<std::uint32_t, std::size_t> samples;
            std::map<std::string, std::size_t> patterns;
            std::size_t wadChecks = 0, ddsChecks = 0;
            for (auto &file : discover(std::filesystem::u8path(argv[2]))) {
                if (extensionOf(file.region.name) != ".WAD") continue;
                auto source = std::make_shared<FileSource>(file.path); inspect(file, source);
                const Read read = [source](auto offset, auto output) { source->read(offset, output); };
                if (wadChecks < 2) { ok &= evaluate("wad.hexpat", read, source->size()); ++wadChecks; }
                for (const auto &tag : file.tags) {
                    if (!tag.type || samples[*tag.type] >= 3) continue;
                    ++samples[*tag.type]; ++patterns[tag.pattern];
                    ok &= evaluate(tag.pattern, [&, tag](auto offset, auto output) { requireRange(offset, output.size(), tag.size); read(tag.offset + offset, output); }, tag.size);
                    if (tag.type == 0x10007 && ddsChecks < 3) {
                        ++ddsChecks;
                        ok &= evaluate("dds.hexpat", [&, tag](auto offset, auto output) { requireRange(offset, output.size(), tag.size - 76); read(tag.offset + 76 + offset, output); }, tag.size - 76);
                    }
                }
            }
            std::size_t resources = 0; for (const auto &[type, count] : samples) resources += count;
            std::cout << "WAD pattern samples=" << wadChecks << " resource types=" << samples.size() << " resource samples=" << resources << " DDS samples=" << ddsChecks << '\n';
            for (const auto &[name, count] : patterns) std::cout << name << ": " << count << '\n';
        } else {
            const auto data = wad(); ok &= evaluate("wad.hexpat", data);
            ok &= evaluate("tag.hexpat", Bytes(data.begin(), data.begin() + 64));
            ok &= evaluate("tag.hexpat", Bytes(63), false);
            ok &= evaluate("texture.hexpat", texture()); ok &= evaluate("dds.hexpat", dds());
            ok &= evaluate("material.hexpat", material()); ok &= evaluate("mesh.hexpat", mesh());
            ok &= evaluate("object.hexpat", object());
            ok &= evaluate("game_object.hexpat", resource(0x30001, 148));
            ok &= evaluate("camera.hexpat", resource(9, 196));
            ok &= evaluate("resource.hexpat", resource(0x12345678)); ok &= evaluate("raw.hexpat", Bytes(5));
            auto large = tag(1, "large_payload", resource(0x12345678, 1024 * 1024));
            ok &= evaluate("wad.hexpat", large);
            ok &= evaluate("texture.hexpat", Bytes(203), false);
            auto bad = texture(); put(bad, 76 + 4, 0); ok &= evaluate("texture.hexpat", bad, false);
            bad = mesh(); put(bad, 8, 5000, 2); ok &= evaluate("mesh.hexpat", bad, false);
            bad = object(); put(bad, 4, 0xffffffff); ok &= evaluate("object.hexpat", bad, false);
            ok &= evaluate("wad.hexpat", Bytes(data.begin(), data.end() - 1), false);
        }
        if (ok) std::cout << "GoW3 pattern checks passed\n";
        return ok ? 0 : 1;
    } catch (const std::exception &error) { std::cerr << error.what() << '\n'; return 1; }
}
