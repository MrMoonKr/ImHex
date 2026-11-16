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
        const auto data = wtoc(); const auto tags = parseWtoc(reader(data), data.size());
        const auto start = 64 + 5 * 144;
        check(tags.size() == 5 && tags[0].offset == start && tags[1].offset == start + 3, "Arena placement");
        check(tags[0].debugOffset == start + 7 && tags[0].tempOffset == start + 9, "Debug/temp arena placement");
        check(tags[2].offset == start + 15 && tags[2].block == 1, "Cumulative alignment across load blocks");
        check(tags[3].size == 0 && tags[3].offset == start + 17 && tags[4].offset == start + 22, "AutoPad and clamped arena placement");
        check(tags[0].guid == 0x11223344aabbccddULL, "GUID word order");
        auto bad = data; put(bad, 64 + 288 + 120, 0, 8);
        rejects([&] { parseWtoc(reader(bad), bad.size()); }, "Offset preceding current arena accepted");
        bad = data; put(bad, 64 + 128, ~Offset(0), 8);
        rejects([&] { parseWtoc(reader(bad), bad.size()); }, "Debug offset overflow accepted");
        bad = data; put(bad, 8, 0xffffffff);
        rejects([&] { parseWtoc(reader(bad), bad.size()); }, "Entry count overflow accepted");
        bad = data; put(bad, 4, 1);
        rejects([&] { parseWtoc(reader(bad), bad.size()); }, "Incorrect WTOC version accepted");
        rejects([&] { parseWtoc(reader(data), data.size() - 1); }, "Truncated arena accepted");
        int calls = 0;
        try { parseWtoc(reader(data), data.size(), [&] { if (++calls == 3) throw std::logic_error("cancel"); }); }
        catch (const std::logic_error &) {}
        check(calls == 3, "Cancellation not propagated");
        TempFiles temp;
        const auto compressed = frame(data); const auto path = temp.add("sample.wad", compressed);
        FileSource decoded(path); check(decoded.decoded() && decoded.size() == data.size(), "LZ4 decode");
        Bytes restored(data.size()); decoded.read(0, restored); check(restored == data, "LZ4 data mismatch");
        FileSource stored(path, false); check(!stored.decoded() && stored.size() == compressed.size(), "Stored and decoded address spaces");
        bad = compressed; bad.pop_back(); const auto truncated = temp.add("truncated.wad", bad);
        rejects([&] { FileSource source(truncated); }, "Truncated LZ4 frame accepted");
        bad = compressed; bad.push_back(0); const auto trailing = temp.add("trailing.wad", bad);
        rejects([&] { FileSource source(trailing); }, "Trailing LZ4 bytes accepted");
        bad = compressed; bad.back() ^= 1; const auto corrupt = temp.add("corrupt.wad", bad);
        rejects([&] { FileSource source(corrupt); }, "Invalid LZ4 checksum accepted");
        for (const auto &[name, bytes] : std::map<std::string, Bytes>{{"sample.texpack.toc", texpack()}, {"sample.lodpack.toc", lodpack()}, {"sample.shaderpack", shaderpack()}}) {
            const auto packPath = temp.add(name, bytes);
            auto source = std::make_shared<FileSource>(packPath); Container container; container.path = packPath; container.region.name = name;
            inspect(container, source); check(!container.tags.empty(), "Pack records missing");
            if (name.ends_with("shaderpack")) check(container.tags[0].offset == 232 && container.tags[0].size == 4, "Shader bytecode placement");
        }
        check(extensionOf("folder/A.TEXPACK.toc") == ".TEXPACK.TOC" && extensionOf("README") == "(No extension)", "Compound extension grouping");
        auto zeroRawSize = shaderpack(); put(zeroRawSize, 112, 0, 8);
        const auto shaderPath = temp.add("zero-size.shaderpack", zeroRawSize);
        auto shaderSource = std::make_shared<FileSource>(shaderPath); Container shader; shader.region.name = "zero-size.shaderpack";
        inspect(shader, shaderSource); check(shader.tags[0].offset == 232, "Zero uncompressed-size field means bins are not compressed");
        const auto files = discover(temp.directory); check(files.size() == 8, "Directory discovery");
    }
    int audit(const std::filesystem::path &path) {
        auto files = discover(path); std::map<std::string, std::size_t> counts;
        std::size_t failures = 0, chunks = 0, checked = 0;
        for (auto &file : files) {
            if (!file.recognized || counts[extensionOf(file.region.name)] >= 2) continue;
            ++counts[extensionOf(file.region.name)]; ++checked;
            try {
                auto source = std::make_shared<FileSource>(file.path); inspect(file, source); chunks += file.tags.size();
                std::cout << extensionOf(file.region.name) << ": " << file.tags.size() << " records / " << (source->decoded() ? "LZ4 decoded" : "stored") << '\n';
            } catch (const std::exception &error) { ++failures; std::cerr << file.region.name << ": " << error.what() << '\n'; }
        }
        std::cout << "Files=" << files.size() << " sampled containers=" << checked << " chunks=" << chunks << " failures=" << failures << '\n';
        return failures ? 1 : 0;
    }
}
int main(int argc, char **argv) {
    try {
        if (argc == 3 && std::string(argv[1]) == "--audit") return audit(std::filesystem::u8path(argv[2]));
        synthetic(); std::cout << "Ragnarok parser checks passed\n"; return 0;
    } catch (const std::exception &error) { std::cerr << error.what() << '\n'; return 1; }
}
