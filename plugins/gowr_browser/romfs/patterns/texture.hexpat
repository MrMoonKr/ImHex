#pragma description "Ragnarok texture parameter chunk (id 1 / server 25 / client 1)"
#pragma endian little
struct TextureParm {
    u32 client_type;
    u8 raw_04[8];
    char name[56];
    u16 flags;
    u8 mip_count_low;
    u8 mip_count_high;
    u16 width;
    u16 height;
    u8 raw_4c[16];
    u32 uncompressed_size;
    u32 extra_data_offset;
    u32 raw_64;
    u32 gnf_magic;
    u8 raw_6c[8];
    u32 gnf_stream_size;
    u8 tsharp[32];
};
TextureParm texture @ 0;
if (builtin::std::mem::size() >= 168) {
    u32 user_magic @ 152;
    u32 raw_9c @ 156;
    u64 content_hash @ 160;
}
