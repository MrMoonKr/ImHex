#include "fixtures.hpp"
#include <pl/pattern_language.hpp>
#include <pl/core/errors/error.hpp>
#include <iostream>
#include <iterator>
#include <map>
using namespace fixtures;

namespace {
    bool evaluate(const std::string &name, const Read &read, Offset size, bool expected = true) {
        std::ifstream file(std::filesystem::path(GOWR_PATTERN_DIR) / name, std::ios::binary);
        if (!file) throw std::runtime_error("Missing pattern: " + name);
        const std::string code((std::istreambuf_iterator<char>(file)), {});
        pl::PatternLanguage runtime;
        runtime.setDataSource(0, size, [&](auto offset, auto buffer, auto count) { requireRange(offset, count, size); read(offset, {buffer, count}); });
        bool success = false;
        try { success = runtime.executeString(code) == 0; }
        catch (const FormatError &error) { if (expected) std::cerr << name << ": " << error.what() << '\n'; return !expected; }
        if (success == expected) return true;
        std::cerr << name << " unexpected result\n";
        for (const auto &error : runtime.getCompileErrors()) std::cerr << error.format() << '\n';
        if (runtime.getEvalError()) std::cerr << runtime.getEvalError()->what() << '\n';
        return false;
    }
    bool evaluate(const std::string &name, const Bytes &data, bool expected = true) { return evaluate(name, reader(data), data.size(), expected); }
}
int main(int argc, char **argv) {
    try {
        bool ok = true;
        if (argc == 3 && std::string(argv[1]) == "--audit") {
            std::map<std::string, std::size_t> samples, payloads;
            for (auto &file : discover(std::filesystem::u8path(argv[2]))) {
                if (!file.recognized || samples[extensionOf(file.region.name)] >= 2) continue;
                ++samples[extensionOf(file.region.name)];
                auto source = std::make_shared<FileSource>(file.path); inspect(file, source);
                const Read read = [source](auto offset, auto output) { source->read(offset, output); };
                ok &= evaluate(file.pattern, read, source->size());
                for (const auto &tag : file.tags) {
                    if (payloads[tag.pattern] >= 3 || tag.size == 0) continue;
                    ++payloads[tag.pattern];
                    ok &= evaluate(tag.pattern, [&, tag](auto offset, auto output) { requireRange(offset, output.size(), tag.size); read(tag.offset + offset, output); }, tag.size);
                }
            }
            for (const auto &[ext, count] : samples) std::cout << ext << ": " << count << " container patterns\n";
            for (const auto &[pattern, count] : payloads) std::cout << pattern << ": " << count << " payload patterns\n";
        } else {
            auto wad = wtoc(); ok &= evaluate("wad.hexpat", wad);
            auto textureWad = wtoc(true); ok &= evaluate("wad.hexpat", textureWad);
            ok &= evaluate("entry.hexpat", Bytes(wad.begin() + 64, wad.begin() + 208));
            ok &= evaluate("texture.hexpat", Bytes(textureWad.begin() + 208, textureWad.end()));
            ok &= evaluate("texture.hexpat", Bytes(151), false);
            auto bad = wad; put(bad, 8, 0xffffffff); ok &= evaluate("wad.hexpat", bad, false);
            ok &= evaluate("texpack.hexpat", texpack()); ok &= evaluate("texkey.hexpat", Bytes(24));
            ok &= evaluate("lodpack.hexpat", lodpack()); ok &= evaluate("lodgroup.hexpat", Bytes(24)); ok &= evaluate("lodblock.hexpat", Bytes(24));
            ok &= evaluate("shaderpack.hexpat", shaderpack()); ok &= evaluate("shaderrecord.hexpat", Bytes(104));
            ok &= evaluate("dc_version.hexpat", Bytes(4)); ok &= evaluate("raw.hexpat", Bytes(64));
        }
        if (ok) std::cout << "Ragnarok pattern checks passed\n";
        return ok ? 0 : 1;
    } catch (const std::exception &error) { std::cerr << error.what() << '\n'; return 1; }
}
