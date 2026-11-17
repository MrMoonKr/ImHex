#pragma description "GoW III Remastered material header and marked texture layers"
#pragma endian little
fn require(bool condition, str message) { if (!condition) builtin::std::error(message); };
struct MaterialLayer {
    u8 semantic;
    u8 sampler;
    u8 flags;
    u8 marker;
    float parameters[11];
    char texture_name[56];
};
struct Material {
    u32 asset_type;
    require(asset_type == 8, "Not a GoW III material");
    u8 raw_04[56] [[sealed]];
    u32 render_category_candidate;
    u8 raw_40[64] [[sealed]];
    char shader_name[56];
    u8 raw_b8[32] [[sealed]];
    MaterialLayer layers[while($ + 104 <= builtin::std::mem::size() && builtin::std::mem::read_unsigned($ + 3, 1, 0) == 0x51)];
    if ($ < builtin::std::mem::size()) u8 remaining_data[builtin::std::mem::size() - $] [[sealed]];
};
Material material @ 0;
