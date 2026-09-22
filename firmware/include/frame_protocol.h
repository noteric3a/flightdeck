#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace flightdeck {
constexpr size_t kHeaderBytes = 22;
constexpr size_t kPixelBytes = 64 * 32 * 2;
constexpr size_t kPacketBytes = kHeaderBytes + kPixelBytes;
inline uint16_t read16(const uint8_t* p) { return uint16_t(p[0]) | (uint16_t(p[1]) << 8); }
inline uint32_t read32(const uint8_t* p) { return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24); }
inline uint32_t crc32(const uint8_t* data, size_t length) {
  uint32_t crc = 0xffffffff;
  for (size_t i = 0; i < length; ++i) {
    crc ^= data[i];
    for (int j = 0; j < 8; ++j) crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
  }
  return crc ^ 0xffffffff;
}
inline bool validFrame(const uint8_t* bytes, size_t length) {
  return length == kPacketBytes && std::memcmp(bytes, "FLT1", 4) == 0 &&
    read16(bytes + 4) == 64 && read16(bytes + 6) == 32 && bytes[8] > 0 &&
    (bytes[9] & 0xf0) == 0 && read32(bytes + 14) == kPixelBytes &&
    read32(bytes + 18) == crc32(bytes + kHeaderBytes, kPixelBytes);
}
}
