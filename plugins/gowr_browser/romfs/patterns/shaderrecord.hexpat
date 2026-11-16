#pragma description "Ragnarok shader record (internally compressed bytecode remains in its pack)"
#pragma endian little
struct ShaderRecord {
    u64 bytecode_hash;
    u32 profile;
    u32 binary_size;
    u32 binary_offset;
    u32 compute_group[3];
    u8 vertex_output_count;
    padding[3];
    char debug_name[64];
    padding[4];
};
ShaderRecord shader @ 0;
