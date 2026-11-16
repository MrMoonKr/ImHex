#pragma description "Ragnarok texpack v6 header and texture key records (not pixel data)"
#pragma endian little
#pragma pattern_limit 2000000
fn require(bool condition, str message) { if (!condition) builtin::std::error(message); };
struct TextureKey { u64 texture_identifier; u64 content_hash; u64 smallest_mip_offset; };
struct Texpack {
    u8 raw_00[32];
    u32 toc_size;
    u32 stream_block_count;
    u32 stream_blocks_offset;
    u32 texture_count;
    u32 version;
    u32 json_size;
    require(version == 6 && toc_size <= builtin::std::mem::size() && toc_size >= 56, "Invalid texpack v6 header");
    require(texture_count <= 1000000 && texture_count <= (toc_size - 56) / 24, "Invalid texture table");
    if (texture_count > 0) TextureKey textures[texture_count];
};
Texpack pack @ 0;
