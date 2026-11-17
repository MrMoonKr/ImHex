#pragma description "GoW III Remastered PS4 64-byte WAD tag header"
#pragma endian little
struct TagHeader {
    u16 tag;
    u16 flags;
    u32 declared_size;
    char name[56];
};
TagHeader header @ 0;
