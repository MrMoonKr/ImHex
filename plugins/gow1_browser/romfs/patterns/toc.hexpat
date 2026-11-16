#pragma description "GoW I PS2 TOC: all physical copies, stopping at the first empty record"
#pragma endian little
fn require(bool condition, str message) {
    if (!condition) builtin::std::error(message);
};
#pragma pattern_limit 1000000
#pragma loop_limit 100001



struct TocRecord {
    char name[12];
    u32 pak_index;
    u32 byte_size;
    u32 pak_sector;
};

u64 position = 0;
while (position + 24 <= builtin::std::mem::size() && builtin::std::mem::read_unsigned(position, 1, 1) != 0) {
    TocRecord record @ position;
    position += 24;
}
require(position + 24 <= builtin::std::mem::size(), "Missing GoW I TOC terminator");
TocRecord terminator @ position;
// Bytes following the terminator can contain stale records and are not entries.
