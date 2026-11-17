#pragma description "GoW I PS2 TOC: 24-byte records followed by a 12-byte NUL name at EOF"
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

u64 total_size = builtin::std::mem::size();
require(total_size >= 12 && (total_size - 12) % 24 == 0, "Expected 24-byte records followed by a 12-byte terminator");
u64 record_count = (total_size - 12) / 24;
require(record_count <= 100000, "TOC exceeds the 100,000 record limit");
u64 terminator_offset = record_count * 24;
require(builtin::std::mem::read_unsigned(terminator_offset, 12, 0) == 0, "Final 12-byte name must be entirely NUL");
// An empty name with 12 bytes of record data following it is still a record.
TocRecord records[record_count] @ 0;
char terminator[12] @ terminator_offset;
