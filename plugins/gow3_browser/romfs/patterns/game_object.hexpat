#pragma description "GoW III Remastered game object transform (type 0x30001)"
#pragma endian little
struct GameObject {
    u32 asset_type;
    u8 raw_04[60] [[sealed]];
    float transform[16];
    if ($ < builtin::std::mem::size()) u8 remaining_data[builtin::std::mem::size() - $] [[sealed]];
};
GameObject object @ 0;
