#pragma description "GoW III Remastered PS4 WAD: 64-byte tags, 16-byte alignment"
#pragma endian little
#pragma pattern_limit 4000000
#pragma loop_limit 500001
fn require(bool condition, str message) { if (!condition) builtin::std::error(message); };
struct WadTag {
    u16 tag;
    u16 flags;
    u32 declared_size;
    char name[56];
    u64 payload_size = tag == 0 ? 0 : declared_size;
    require($ <= builtin::std::mem::size() && payload_size <= builtin::std::mem::size() - $, "Tag payload exceeds WAD");
    if (tag == 1 && payload_size >= 4) {
        u32 asset_type;
        if (payload_size > 4) u8 payload[payload_size - 4] [[sealed]];
    } else if (payload_size > 0) u8 payload[payload_size] [[sealed]];
    u64 alignment_size = (16 - $ % 16) % 16;
    require(alignment_size <= builtin::std::mem::size() - $, "Missing alignment bytes");
    if (alignment_size > 0) padding[alignment_size];
};
require(builtin::std::mem::size() > 0, "Empty WAD");
WadTag tags[while($ < builtin::std::mem::size())] @ 0;
