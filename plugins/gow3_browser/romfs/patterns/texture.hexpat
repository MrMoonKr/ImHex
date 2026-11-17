#pragma description "GoW III Remastered texture resource with embedded DDS at 0x4C"
#pragma endian little
fn require(bool condition, str message) { if (!condition) builtin::std::error(message); };
struct PixelFormat {
    u32 size;
    u32 flags;
    u32 fourcc_or_d3d_format;
    u32 rgb_bits;
    u32 red_mask;
    u32 green_mask;
    u32 blue_mask;
    u32 alpha_mask;
};
struct DdsHeader {
    char magic[4];
    u32 header_size;
    require(magic == "DDS " && header_size == 124, "Invalid DDS header");
    u32 flags;
    u32 height;
    u32 width;
    u32 pitch_or_linear_size;
    u32 depth;
    u32 mip_count;
    u32 reserved[11];
    PixelFormat pixel_format;
    require(pixel_format.size == 32, "Invalid DDS pixel format");
    u32 caps;
    u32 caps2;
    u32 caps3;
    u32 caps4;
    u32 reserved2;
};
struct Texture {
    u32 asset_type;
    require(asset_type == 0x10007, "Not a GoW III texture");
    char name[56];
    u32 raw_3c[4];
    DdsHeader dds;
    if ($ < builtin::std::mem::size()) u8 surface_data[builtin::std::mem::size() - $] [[sealed]];
};
Texture texture @ 0;
