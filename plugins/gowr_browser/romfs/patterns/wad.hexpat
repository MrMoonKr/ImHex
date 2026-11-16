#pragma description "Ragnarok decoded WTOC v2 header and entry table"
#pragma endian little
#pragma pattern_limit 2000000
fn require(bool condition, str message) { if (!condition) builtin::std::error(message); };
struct Entry {
    u16 id;
    u16 version;
    u32 length;
    u32 guid[4];
    char name[56];
    u32 dc_version_hash;
    padding[12];
    u32 debug_size;
    u32 temp_size;
    u32 chunk_align;
    u8 client_id;
    u8 server_id;
    u8 ct_flags;
    u8 mem_type;
    u8 group_start_type;
    u8 group_end_count;
    u8 bits;
    padding[5];
    u64 mem_offset;
    u64 debug_offset;
    u64 temp_offset;
};
struct Wtoc {
    u32 magic;
    u32 version;
    require(magic == 0x434f5457 && version == 2, "Expected decoded WTOC version 2");
    u32 entry_count;
    u32 heap_size;
    u32 parm_count;
    u32 total_mem[9];
    u32 bits0;
    u32 bits1;
    require(entry_count <= 1000000 && entry_count <= (builtin::std::mem::size() - 64) / 144, "Invalid WTOC entry count");
    if (entry_count > 0) Entry entries[entry_count];
    if ($ < builtin::std::mem::size()) u8 load_block_data[builtin::std::mem::size() - $];
};
Wtoc container @ 0;
