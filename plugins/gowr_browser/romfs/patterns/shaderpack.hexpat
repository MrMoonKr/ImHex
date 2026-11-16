#pragma description "Ragnarok SHDRPACK v11 header"
#pragma endian little
struct Shaderpack {
    u64 magic;
    u64 version;
    u64 shader_count;
    u64 root_signature_count;
    u64 combo_count;
    u64 pso_count;
    u64 shader_records_offset;
    u64 root_signature_records_offset;
    u64 combo_records_offset;
    u64 pso_records_offset;
    u64 shader_binaries_offset;
    u64 root_signature_binaries_offset;
    u64 wad_shader_count;
    u64 wad_shader_bytes;
    u64 unique_shader_bytes;
    u64 unique_shader_compressed_bytes;
};
Shaderpack header @ 0;
