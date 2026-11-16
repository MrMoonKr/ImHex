#pragma description "GoW II PS2 WAD tag stream (1 resource, 0 entity count, 2/3 groups)"
#pragma endian little
fn require(bool condition, str message) {
    if (!condition) builtin::std::error(message);
};
#pragma pattern_limit 2000000
#pragma loop_limit 100001



struct WadTag {
    u16 tag;
    u16 flags;
    u32 declared_size;
    char name[24];
    u64 payload_size = tag == 0 ? 0 : declared_size;
    require($ <= builtin::std::mem::size() && payload_size <= builtin::std::mem::size() - $, "WAD payload outside file");
    if (tag == 1 && payload_size > 0) {
        require(payload_size >= 4, "Truncated asset type");
        u32 asset_type;
        if (payload_size > 4) u8 payload[payload_size - 4];
    } else if (payload_size > 0) {
        u8 payload[payload_size];
    }
    u64 alignment_size = (16 - $ % 16) % 16;
    require(alignment_size <= builtin::std::mem::size() - $, "Missing WAD alignment");
    if (alignment_size > 0) padding[alignment_size];
};

require(builtin::std::mem::size() > 0, "Empty WAD");
WadTag tags[while($ < builtin::std::mem::size())] @ 0;
