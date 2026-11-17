#pragma description "GoW III Remastered camera transform and rail reference"
#pragma endian little
struct Camera {
    u32 asset_type;
    u8 raw_04[12] [[sealed]];
    float transform[16];
    char rail_name[56];
    if ($ < builtin::std::mem::size()) u8 remaining_data[builtin::std::mem::size() - $] [[sealed]];
};
Camera camera @ 0;
