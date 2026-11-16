#pragma description "GoW II resource: TXR, GFX, MAT, instance and camera headers; other types stay raw"
#pragma endian little
fn require(bool condition, str message) {
    if (!condition) builtin::std::error(message);
};
#pragma pattern_limit 100000



// Apply to the resource payload, with its asset type at offset 0.
// Unknown field meanings are deliberately retained as raw values.
struct MaterialLayer {
    u32 flags[4];
    char texture[24];
    float blend[4];
    float raw_38;
    u32 game_flags;
};

struct CameraLink {
    char target[16];
    float values[5];
    u32 flag;
};

struct Resource {
    u32 asset_type;
    if (asset_type == 7) {
        char graphics[24];
        char palette[24];
        char subtexture[24];
        s32 lod_k;
        float lod_multiplier;
        u32 flags;
    } else if (asset_type == 12) {
        u32 width;
        u32 total_height;
        u32 encoding;
        u32 bits_per_pixel;
        u32 image_count;
    } else if (asset_type == 8) {
        u32 raw_04;
        float color[3];
        float specular_candidate;
        float raw_18[3];
        float scalar_24;
        u32 raw_28;
        float power_candidate;
        u32 raw_30;
        u32 layer_count;
        require(layer_count <= 512, "Too many material layers");
        require($ <= builtin::std::mem::size() && layer_count <= (builtin::std::mem::size() - $) / 64, "Truncated material layers");
        if (layer_count > 0) MaterialLayer layers[layer_count];
    } else if (asset_type == 0x30001 || asset_type == 0x20001) {
        // GoW II placement prefix; 0x20001 is a reference node, not a GoW I instance.
        if (asset_type == 0x20001) char object_name[24];
        else u8 raw_04[24];
        u16 identifier;
        u16 flags;
        float basis[9];
        float position[3];
        float auxiliary_point[3];
        u32 raw_5c;
        u16 raw_60;
        u16 sequence;
        u16 owner_id;
        u16 owner_high;
    } else if (asset_type == 9) {
        u32 raw_04[3];
        float matrix[16];
        char rail_name[24];
        char sway_name[24];
        float raw_80[25];
        u32 link_count;
        require(link_count <= 1024, "Too many camera links");
        require(builtin::std::mem::size() == 232 + u64(link_count) * 40, "Camera size does not match link table");
        if (link_count > 0) CameraLink links[link_count];
    }
    require($ <= builtin::std::mem::size(), "Resource header outside payload");
    if ($ < builtin::std::mem::size()) u8 remaining_data[builtin::std::mem::size() - $];
};

Resource resource @ 0;
