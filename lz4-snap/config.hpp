#pragma once
#include <cstdint>

const size_t TOTAL_NUMBERS = 100'000'000; // 100 млн чисел
const size_t NUMBERS_PER_BLOCK = 100;     // Блок = 100 чисел (800 байт)
const size_t TOTAL_BLOCKS = TOTAL_NUMBERS / NUMBERS_PER_BLOCK;
const size_t BLOCK_SIZE_BYTES = NUMBERS_PER_BLOCK * sizeof(uint64_t);
const size_t LZ4_ACCELERATION = 1;
