#include <iostream>
#include <vector>
#include <chrono>
#include <cstring>
#include <fstream>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <lz4.h>

#include "config.hpp"

// Вспомогательный таймер
class Timer {
    std::chrono::high_resolution_clock::time_point start;
public:
    Timer() : start(std::chrono::high_resolution_clock::now()) {}
    double elapsed_ms() {
        auto end = std::chrono::high_resolution_clock::now();
        return std::chrono::duration<double, std::milli>(end - start).count();
    }
};

// Генератор данных
std::vector<uint64_t> generate_raw_data() {
    std::cout << "Generating " << TOTAL_NUMBERS << " random numbers..." << std::endl;
    std::vector<uint64_t> data(TOTAL_NUMBERS);
    /* Случайные числа сжимаются не очень хорошо, но бенчим произвольный случай. */
    for(auto& val : data)
        val = rand();
    return data;
}

size_t
use_data(const std::vector<std::vector<char>> &compressed_chunks);

// ---------------------------------------------------------
// ПОДХОД 1: Store Compressed
// RAM(Comp) -> Disk(Comp) -> RAM(Comp)
// ---------------------------------------------------------
void test_approach_1(const std::vector<std::vector<char>>& compressed_blocks_source) {
    std::cout << "\n=== Approach 1: Store Compressed (Zero-Copy) ===" << std::endl;
    const char* filename = "test_app1.bin";

    /* 1. Запись (Просто сброс буфера) */
    Timer t_write;
    std::ofstream out(filename, std::ios::binary);
    for (const auto& block : compressed_blocks_source) {
        uint32_t sz = block.size();
        out.write(reinterpret_cast<const char*>(&sz), 4);
        out.write(block.data(), sz);
    }
    out.close();
    std::cout << "[Write] RAM(Comp) -> Disk: " << t_write.elapsed_ms() << " ms" << std::endl;

    /* 2. Recovery (mmap + Decompress) */
    Timer t_read;
    int fd = open(filename, O_RDONLY);
    struct stat sb;
    fstat(fd, &sb);
    char* mapped_data = (char*)mmap(nullptr, sb.st_size, PROT_READ, MAP_PRIVATE, fd, 0);

    std::vector<std::vector<char>> compressed_chunks;
    compressed_chunks.reserve(TOTAL_BLOCKS);
    size_t offset = 0;

    while (offset < sb.st_size) {
        uint32_t sz;
        memcpy(&sz, mapped_data + offset, 4);
        offset += 4;

        compressed_chunks.emplace_back(sz);
        memcpy(compressed_chunks.back().data(), mapped_data + offset, sz);
        offset += sz;
    }


    munmap(mapped_data, sb.st_size);
    close(fd);
    std::cout << "[Read]  Disk -> RAM(Comp): " << t_read.elapsed_ms() << " ms" << std::endl;

    /* Use data in order to prevent optimization. */
    std::cout << "Checksum: " << use_data(compressed_chunks) << std::endl;
}

// ---------------------------------------------------------
// ПОДХОД 2: Store Raw (Recompress Cycle)
// RAM(Comp) -> Decompress -> Disk(Raw) -> Read -> Compress -> RAM(Comp)
// ---------------------------------------------------------
void test_approach_2(const std::vector<std::vector<char>>& compressed_blocks_source) {
    std::cout << "\n=== Approach 2: Store Raw (Recompress Cycle) ===" << std::endl;
    const char* filename = "test_app2.bin";

    /* 1. Запись (Decompress + Write) */
    Timer t_write;
    std::ofstream out(filename, std::ios::binary);
    std::vector<char> temp_buf(BLOCK_SIZE_BYTES);

    for (const auto& block : compressed_blocks_source) {
        LZ4_decompress_safe(block.data(), temp_buf.data(), block.size(), BLOCK_SIZE_BYTES);
        out.write(temp_buf.data(), BLOCK_SIZE_BYTES);
    }
    out.close();
    std::cout << "[Write] RAM(Comp) -> Decompress -> Disk(Raw): " << t_write.elapsed_ms() << " ms" << std::endl;

    /* 2. Recovery (Read + Compress) */
    Timer t_read;
    int fd = open(filename, O_RDONLY);
    struct stat sb;
    fstat(fd, &sb);
    char* mapped_data = (char*)mmap(nullptr, sb.st_size, PROT_READ, MAP_PRIVATE, fd, 0);

    std::vector<std::vector<char>> compressed_chunks;
    compressed_chunks.reserve(TOTAL_BLOCKS);
    size_t offset = 0;

    size_t compressBound = LZ4_compressBound(BLOCK_SIZE_BYTES);
    std::vector<char> comp_buf(compressBound);
    while (offset < sb.st_size) {
        int sz = LZ4_compress_fast(
            mapped_data + offset,
            comp_buf.data(),
            BLOCK_SIZE_BYTES,
            comp_buf.size(),
            LZ4_ACCELERATION
        );
        comp_buf.resize(sz);
        compressed_chunks.emplace_back(std::move(comp_buf));
        offset += BLOCK_SIZE_BYTES;
        comp_buf.resize(compressBound);
    }

    munmap(mapped_data, sb.st_size);
    close(fd);
    std::cout << "[Read]  Disk(Raw) -> Compress -> RAM(Comp): " << t_read.elapsed_ms() << " ms" << std::endl;

    /* Use data in order to prevent optimization. */
    std::cout << "Checksum: " << use_data(compressed_chunks) << std::endl;
}

int main() {
    /* 1. Генерация исходных данных */
    auto raw_data = generate_raw_data();

    /* 2. Эмуляция In-Memory хранилища: сжимаем все блоки один раз */
    std::cout << "Compressing blocks for RAM storage..." << std::endl;
    std::vector<std::vector<char>> compressed_blocks_source(TOTAL_BLOCKS);
    size_t total_comp_size = 0;

    for(size_t i = 0; i < TOTAL_BLOCKS; ++i) {
        const char* src = reinterpret_cast<const char*>(&raw_data[i * NUMBERS_PER_BLOCK]);
        compressed_blocks_source[i].resize(LZ4_compressBound(BLOCK_SIZE_BYTES));
        int sz = LZ4_compress_fast(src, compressed_blocks_source[i].data(),
                                   BLOCK_SIZE_BYTES, compressed_blocks_source[i].size(),
                                   LZ4_ACCELERATION);
        compressed_blocks_source[i].resize(sz);
        total_comp_size += sz;
    }
    std::cout << "Source compressed size: " << total_comp_size / (1024.0 * 1024.0) << " MB" << std::endl;

    test_approach_1(compressed_blocks_source);
    test_approach_2(compressed_blocks_source);

    /* Cleanup */
    remove("test_app1.bin");
    remove("test_app2.bin");
    return 0;
}
