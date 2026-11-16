#pragma description "Ragnarok texpack texture key record"
#pragma endian little
struct TextureKey { u64 texture_identifier; u64 content_hash; u64 smallest_mip_offset; };
TextureKey key @ 0;
