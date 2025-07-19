#pragma once

#include <cstdint>
#include <vector>

// Reads a 16-bit unsigned integer (u2) from the pointer and advances it by 2 bytes
static _inline uint16_t read_u2(const unsigned char*& ptr) {
    uint16_t val = (ptr[0] << 8) | ptr[1];
    ptr += 2;
    return val;
}

// Reads a 32-bit unsigned integer (u4) from the pointer and advances it by 4 bytes
static _inline uint32_t read_u4(const unsigned char*& ptr) {
    uint32_t val = (ptr[0] << 24) | (ptr[1] << 16) | (ptr[2] << 8) | ptr[3];
    ptr += 4;
    return val;
}

// Writes a 16-bit unsigned integer (u2) to a vector in big-endian format
static _inline void write_u2(std::vector<unsigned char>& vec, uint16_t val) {
    vec.push_back((val >> 8) & 0xFF);
    vec.push_back(val & 0xFF);
}

// Writes a 32-bit unsigned integer (u4) to a vector in big-endian format
static _inline void write_u4(std::vector<unsigned char>& vec, uint32_t val) {
    vec.push_back((val >> 24) & 0xFF);
    vec.push_back((val >> 16) & 0xFF);
    vec.push_back((val >> 8) & 0xFF);
    vec.push_back(val & 0xFF);
}