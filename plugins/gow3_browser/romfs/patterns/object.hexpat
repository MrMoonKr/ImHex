#pragma description "GoW III Remastered object joint hierarchy and names (matrix data remains raw)"
#pragma endian little
fn require(bool condition, str message) { if (!condition) builtin::std::error(message); };
struct Joint { u16 flags; u16 child; u16 sibling; u16 parent_index; };
struct JointName { char name[56]; };
struct ObjectData {
    u32 asset_type;
    require(asset_type == 0x10001, "Not a GoW III object data resource");
    u32 joint_count;
    u8 raw_08[8] [[sealed]];
    u32 matrix_block_offset;
    require(joint_count <= 16384 && joint_count <= (builtin::std::mem::size() - $) / 64, "Invalid joint table");
    if (joint_count > 0) {
        Joint joints[joint_count];
        JointName names[joint_count];
    }
    if ($ < builtin::std::mem::size()) u8 remaining_data[builtin::std::mem::size() - $] [[sealed]];
};
ObjectData object @ 0;
