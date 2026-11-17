#pragma description "GoW III Remastered MG mesh geometry header and object offset table"
#pragma endian little
fn require(bool condition, str message) { if (!condition) builtin::std::error(message); };
struct Mesh {
    u32 asset_type;
    require(asset_type == 0x1000d, "Not a GoW III MG mesh");
    u32 vertex_data_start;
    u16 object_count;
    u16 raw_0a;
    u8 raw_0c[12] [[sealed]];
    require(object_count <= 4096 && object_count <= (builtin::std::mem::size() - $) / 4, "Invalid object offset table");
    if (object_count > 0) u32 object_offsets[object_count];
    if ($ < builtin::std::mem::size()) u8 remaining_data[builtin::std::mem::size() - $] [[sealed]];
};
Mesh mesh @ 0;
