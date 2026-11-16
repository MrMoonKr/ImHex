#pragma description "Ragnarok lodpack block TOC record"
#pragma endian little
struct Block { u32 group_index; u32 group_offset; u64 data_hash; u32 data_size; u32 flags; };
Block block @ 0;
