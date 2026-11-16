#pragma description "Ragnarok lodpack group and block TOC records"
#pragma endian little
#pragma pattern_limit 2000000
fn require(bool condition, str message) { if (!condition) builtin::std::error(message); };
struct Group { u64 file_offset; u64 data_hash; u32 data_size; u32 flags; };
struct Block { u32 group_index; u32 group_offset; u64 data_hash; u32 data_size; u32 flags; };
struct Lodpack {
    u32 group_count;
    u32 block_count;
    u32 prim_parm_count;
    u32 flags;
    require(u64(group_count) + block_count <= (builtin::std::mem::size() - 16) / 24, "Truncated LOD tables");
    if (group_count > 0) Group groups[group_count];
    if (block_count > 0) Block blocks[block_count];
};
Lodpack pack @ 0;
