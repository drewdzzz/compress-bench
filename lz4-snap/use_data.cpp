#include <vector>
#include <cstring>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <lz4.h>

#include "config.hpp"

size_t
use_data(const std::vector<std::vector<char>> &compressed_chunks)
{
    std::vector<char> decomp_buf;
    for (auto &chunk : compressed_chunks) {
        size_t cursor = decomp_buf.size();
        decomp_buf.resize(cursor + BLOCK_SIZE_BYTES);
        LZ4_decompress_safe(chunk.data(), decomp_buf.data() + cursor, chunk.size(), BLOCK_SIZE_BYTES);
    }

    volatile size_t checksum = 0;
    for (auto &c : decomp_buf) {
        checksum += c ^ checksum;
    }
    return checksum;
}
