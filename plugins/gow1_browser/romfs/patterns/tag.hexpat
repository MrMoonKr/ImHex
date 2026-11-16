#pragma description "GoW I WAD tag header; tag 24 declared_size is an entity count"
#pragma endian little

struct TagHeader {
    u16 tag;
    u16 flags;
    u32 declared_size;
    char name[24];
};

TagHeader header @ 0;
