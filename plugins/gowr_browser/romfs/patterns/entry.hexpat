#pragma description "Ragnarok WTOC v2 entry record (arena offsets are not file offsets)"
#pragma endian little
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
Entry entry @ 0;
