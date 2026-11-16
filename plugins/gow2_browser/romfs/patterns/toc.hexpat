#pragma description "GoW II PS2 TOC: entry table and encoded PAK sector locations"
#pragma endian little
fn require(bool condition, str message) {
    if (!condition) builtin::std::error(message);
};
#pragma pattern_limit 1000000
#pragma loop_limit 100001



struct TocEntry {
    char name[24];
    u32 byte_size;
    u32 location_count;
    u32 location_start;
};

struct TocFile {
    u32 entry_count;
    require(entry_count <= (builtin::std::mem::size() - 4) / 36, "Truncated TOC entry table");
    if (entry_count > 0) TocEntry entries[entry_count];
    require((builtin::std::mem::size() - $) % 4 == 0, "Unaligned TOC location table");
    u64 location_count = (builtin::std::mem::size() - $) / 4;
    if (location_count > 0) u32 encoded_sectors[location_count];
};

// Encoded sector < 10,000,000 is relative to PART1.PAK.
// Otherwise subtract 10,000,000 and use PART2.PAK. Multiply by 2048 for bytes.
TocFile toc @ 0;
u64 resolved_locations = 0;
u64 i = 0;
while (i < toc.entry_count) {
    require(toc.entries[i].location_count > 0, "TOC entry has no locations");
    require(toc.entries[i].location_start <= toc.location_count &&
            toc.entries[i].location_count <= toc.location_count - toc.entries[i].location_start, "TOC location range outside table");
    resolved_locations += toc.entries[i].location_count;
    require(resolved_locations <= 100000, "Too many resolved TOC locations");
    i += 1;
}
