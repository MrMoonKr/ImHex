#pragma description "GoW III Remastered resource type and raw payload"
#pragma endian little
struct Resource {
    u32 asset_type;
    if ($ < builtin::std::mem::size()) u8 data[builtin::std::mem::size() - $] [[sealed]];
};
Resource resource @ 0;
