#pragma description "Ragnarok lodpack group TOC record (payload belongs to paired pack)"
#pragma endian little
struct Group { u64 file_offset; u64 data_hash; u32 data_size; u32 flags; };
Group group @ 0;
