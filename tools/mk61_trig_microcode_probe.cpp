#include <array>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

static constexpr std::array<uint32_t, 68> ik1302_microcommand_rom = {
    0x0, 0x800001, 0xa00820, 0x40020, 0xa03120, 0x203081, 0xa00181, 0x803800,
    0x818001, 0x800400, 0xa00089, 0xa03c20, 0x800820, 0x80020, 0x800120, 0x1400020,
    0x800081, 0x210801, 0x40000, 0x58001, 0x808001, 0xa03081, 0xa01081, 0xa01181,
    0x40090, 0x800401, 0xa00081, 0x40001, 0x800801, 0x1000000, 0x800100, 0x1200801,
    0x13c01, 0x800008, 0xa00088, 0x10200, 0x800040, 0x800280, 0x1801200, 0x1000208,
    0x80001, 0xa00082, 0xa01008, 0x1000001, 0xa00808, 0x900001, 0x8010004, 0x80820,
    0x800002, 0x140002, 0x8000, 0xa00090, 0xa00220, 0x801001, 0x1203200, 0x4800001,
    0x11801, 0x1008001, 0xa04020, 0x4800801, 0x840801, 0x840020, 0x13081, 0x10801,
    0x818180, 0x800180, 0xa00081, 0x800001
};

static constexpr std::array<uint8_t, 1152> ik1302_sync_rom = {
    0, 0, 0, 16, 3, 29, 0, 7, 30, 16, 3, 28, 11, 7, 12, 30, 0, 0,
    21, 24, 9, 22, 24, 9, 22, 24, 36, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 3, 14, 30, 51, 0, 0, 0, 0, 35, 0, 0, 0, 47, 0, 44, 0, 1,
    17, 50, 0, 0, 0, 3, 0, 14, 26, 15, 14, 13, 25, 3, 47, 14, 13, 8,
    28, 12, 13, 1, 0, 0, 3, 36, 15, 28, 12, 47, 9, 30, 52, 14, 30, 12,
    6, 10, 13, 0, 0, 0, 0, 9, 15, 56, 0, 0, 0, 0, 0, 10, 38, 6,
    53, 52, 13, 36, 30, 26, 9, 12, 15, 61, 0, 0, 28, 3, 14, 10, 15, 6,
    61, 0, 14, 63, 3, 1, 0, 0, 14, 63, 51, 13, 1, 8, 0, 1, 8, 4,
    6, 3, 14, 43, 58, 9, 18, 30, 51, 53, 3, 7, 12, 30, 26, 0, 0, 0,
    53, 12, 47, 14, 3, 1, 0, 0, 21, 36, 30, 26, 35, 29, 0, 0, 0, 0,
    9, 12, 47, 9, 3, 0, 36, 12, 15, 61, 9, 30, 63, 3, 7, 11, 34, 3,
    7, 11, 13, 12, 3, 14, 30, 58, 43, 60, 3, 0, 9, 52, 14, 30, 12, 30,
    46, 1, 49, 46, 1, 49, 0, 0, 0, 46, 48, 3, 46, 48, 3, 0, 0, 0,
    46, 45, 0, 46, 45, 0, 0, 0, 0, 59, 4, 47, 55, 18, 0, 0, 0, 0,
    20, 0, 0, 8, 0, 0, 0, 0, 0, 1, 19, 0, 1, 19, 0, 1, 19, 4,
    46, 0, 0, 46, 0, 0, 46, 0, 0, 61, 7, 16, 63, 3, 0, 44, 7, 30,
    0, 0, 0, 0, 0, 0, 0, 0, 4, 0, 15, 16, 3, 0, 28, 3, 15, 29,
    3, 50, 0, 43, 20, 0, 0, 8, 0, 4, 20, 0, 0, 50, 0, 0, 50, 12,
    10, 50, 0, 0, 50, 0, 0, 50, 0, 33, 21, 24, 33, 22, 24, 0, 23, 24,
    25, 26, 24, 25, 22, 24, 9, 22, 24, 43, 21, 0, 0, 23, 0, 0, 23, 0,
    18, 27, 14, 15, 27, 14, 35, 43, 10, 44, 24, 0, 42, 24, 7, 11, 3, 4,
    50, 20, 0, 50, 50, 17, 0, 8, 0, 9, 12, 21, 3, 0, 0, 6, 60, 0,
    0, 44, 0, 0, 42, 0, 9, 22, 0, 0, 0, 17, 0, 9, 22, 24, 9, 30,
    0, 0, 7, 10, 41, 62, 51, 41, 0, 15, 11, 15, 16, 3, 8, 36, 3, 35,
    50, 1, 29, 50, 8, 0, 50, 8, 50, 50, 8, 35, 50, 8, 15, 35, 35, 4,
    9, 30, 15, 0, 0, 20, 0, 0, 8, 55, 0, 0, 55, 0, 0, 55, 0, 0,
    1, 49, 0, 1, 49, 0, 1, 49, 54, 26, 48, 13, 0, 48, 13, 0, 48, 13,
    48, 3, 0, 48, 3, 0, 48, 3, 43, 45, 0, 0, 45, 0, 0, 45, 0, 0,
    10, 48, 3, 0, 48, 3, 0, 48, 3, 0, 1, 49, 0, 1, 49, 0, 1, 49,
    0, 45, 0, 0, 45, 0, 0, 45, 0, 44, 0, 0, 42, 0, 0, 9, 24, 0,
    7, 30, 15, 1, 0, 8, 28, 10, 8, 20, 0, 0, 50, 0, 0, 50, 43, 0,
    50, 0, 0, 50, 39, 54, 8, 9, 12, 30, 2, 29, 15, 12, 15, 38, 7, 34,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 29, 35, 35, 9, 35, 12, 3, 35, 35,
    2, 53, 3, 15, 0, 0, 0, 4, 12, 1, 18, 0, 8, 0, 50, 10, 0, 0,
    6, 24, 0, 23, 24, 0, 23, 24, 0, 0, 1, 19, 0, 1, 19, 4, 1, 19,
    0, 0, 0, 9, 21, 24, 0, 53, 3, 14, 3, 9, 12, 27, 30, 15, 27, 8,
    0, 0, 28, 3, 30, 21, 2, 12, 0, 7, 30, 16, 15, 9, 50, 30, 15, 8,
    9, 30, 26, 24, 29, 23, 3, 15, 61, 7, 11, 26, 29, 40, 0, 14, 40, 8,
    0, 0, 0, 0, 0, 6, 3, 0, 9, 0, 4, 43, 35, 4, 8, 8, 0, 8,
    14, 3, 0, 43, 47, 13, 18, 3, 4, 1, 8, 0, 1, 8, 0, 1, 8, 4,
    15, 29, 47, 14, 3, 35, 7, 30, 13, 15, 18, 0, 35, 36, 30, 35, 15, 4,
    38, 18, 21, 3, 18, 4, 36, 47, 15, 18, 4, 1, 15, 7, 30, 15, 0, 1,
    14, 15, 32, 5, 0, 7, 18, 14, 8, 30, 0, 16, 3, 15, 4, 0, 0, 0,
    50, 0, 0, 50, 0, 0, 0, 3, 0, 0, 0, 1, 0, 0, 5, 0, 23, 13,
    0, 0, 0, 10, 26, 24, 0, 23, 3, 50, 9, 15, 50, 7, 12, 12, 26, 15,
    20, 0, 0, 50, 0, 0, 50, 0, 0, 14, 30, 21, 0, 0, 2, 0, 0, 2,
    0, 14, 8, 14, 29, 35, 30, 58, 58, 29, 4, 21, 0, 0, 58, 0, 0, 58,
    0, 0, 58, 0, 13, 14, 3, 15, 0, 59, 60, 47, 55, 60, 1, 0, 0, 0,
    0, 0, 0, 0, 48, 0, 2, 36, 30, 0, 0, 0, 0, 7, 11, 34, 3, 4,
    0, 0, 57, 4, 37, 8, 3, 7, 15, 18, 44, 0, 43, 42, 38, 13, 7, 15,
    4, 11, 8, 1, 16, 13, 9, 0, 0, 0, 1, 8, 4, 1, 8, 35, 1, 8,
    0, 0, 27, 0, 0, 27, 31, 14, 27, 0, 0, 0, 0, 0, 44, 0, 27, 0,
    0, 0, 1, 15, 13, 1, 9, 30, 43, 0, 35, 26, 7, 30, 12, 15, 0, 0,
    30, 18, 0, 0, 18, 0, 0, 18, 26, 30, 0, 16, 15, 36, 30, 52, 29, 0,
    2, 0, 0, 0, 0, 0, 9, 47, 1, 0, 0, 0, 0, 0, 0, 18, 9, 21,
    0, 0, 0, 0, 0, 40, 0, 0, 40, 0, 0, 43, 0, 0, 0, 9, 12, 35,
    36, 12, 30, 15, 0, 7, 3, 15, 0, 0, 0, 1, 15, 7, 11, 15, 37, 15,
    15, 4, 0, 0, 0, 18, 9, 12, 18, 0, 0, 0, 18, 0, 0, 0, 9, 12,
    3, 0, 0, 0, 4, 50, 36, 15, 35, 14, 13, 0, 0, 0, 0, 9, 30, 26,
    7, 11, 15, 7, 12, 30, 26, 15, 0, 14, 13, 0, 0, 0, 0, 0, 4, 8,
    18, 0, 1, 11, 0, 0, 0, 0, 9, 0, 0, 0, 0, 0, 0, 0, 6, 11,
    0, 0, 18, 0, 0, 18, 4, 12, 18, 50, 0, 0, 50, 0, 0, 8, 54, 0,
    2, 13, 0, 1, 15, 13, 0, 14, 30, 30, 0, 16, 15, 7, 11, 52, 15, 29,
    29, 4, 8, 54, 0, 8, 18, 0, 0, 3, 30, 15, 38, 10, 2, 38, 62, 8
};

static constexpr std::array<uint32_t, 256> ik1302_command_rom = {
    0x8274e, 0x479e0, 0x46440, 0x410140, 0x5a040, 0x6d940, 0x1a340, 0x56e013,
    0x3658b0, 0x840, 0x695014, 0x0, 0x0, 0x0, 0x0, 0x305040,
    0x3429ed, 0x147bc0, 0x2d18b0, 0x2c9123, 0x3069c0, 0x3f5040, 0xb4e9d, 0x2203b,
    0x250c0, 0x26061, 0x290c0, 0x210c0, 0x37876, 0x43600d, 0x2a03b, 0x176c0,
    0x40159, 0x42c9c0, 0x91044, 0x42fc40, 0x1a002, 0x7fd008, 0x22b5a, 0x49a03f,
    0x5b200d, 0x305077, 0x147bc0, 0x147bc0, 0x21940, 0x43200d, 0x7fd00a, 0x42dae8,
    0x47dda, 0x8506d, 0x489034, 0x406e5b, 0x741a54, 0x2f5d, 0x43a00d, 0x438044,
    0x3d2740, 0x26906d, 0x3d275a, 0x0, 0x0, 0x0, 0x0, 0x304e9d,
    0x199b3, 0x42e03b, 0x4d2043, 0x2756a, 0x2a243, 0x1e50a0, 0x74d047, 0x20f1e,
    0x6bc8c0, 0x2ace9d, 0xe594c, 0x2d94c, 0x2196d, 0x3604c, 0x2166c0, 0x4e040,
    0x2604c, 0x1b7bf0, 0x1b7940, 0x400b40, 0x29667e, 0x3d2740, 0x54f480, 0x2ac0e,
    0xf2962, 0x178ed, 0x4f2013, 0x42070, 0x177ed, 0x299070, 0x437c0, 0x4177c0,
    0x7df3d, 0xa2cda, 0x79123, 0x1a5c0, 0x14a540, 0x1a50a0, 0x6e4c0, 0x25940,
    0x47c0c0, 0x56440, 0x19223, 0x19223, 0x4179c0, 0x57a00d, 0x26c28, 0x25123,
    0x2657c0, 0x2617c0, 0x189040, 0x176c20, 0x1d07c0, 0x2e0e20, 0x3429f3, 0x16d05c,
    0x1b5062, 0x4cd00a, 0x42fee2, 0x2950a0, 0x416072, 0x526013, 0x4102c0, 0x879e2,
    0x4f7540, 0x1bc40, 0x6af66d, 0x4cd014, 0x3be6c, 0x145bf, 0x3069c0, 0x254453,
    0x3820fa, 0x382140, 0x14d0a0, 0xdaae2, 0x23cf20, 0x4f4bc0, 0x1310a0, 0x5c206a,
    0x3029c0, 0x18e9d, 0x2062c5, 0x199b3, 0x18f1e, 0x24f1e, 0x2403a0, 0x546078,
    0x20e9d, 0x589106, 0x7a2c5, 0x46060, 0x2d740, 0x3d6c0, 0x438fc0, 0x37b77,
    0x34de40, 0x74d032, 0x47760, 0x475a40, 0x3ffa40, 0x1d940, 0x6e18a, 0x74d047,
    0x45a34, 0x7b740, 0x34de40, 0x24fac0, 0x340f20, 0x11977, 0x32a020, 0x43fd4,
    0x85068, 0x2fc40, 0x5f200c, 0x177ed, 0x51c640, 0x5c75e2, 0x1ad0a0, 0xcd0a0,
    0x1b504c, 0x39b9e2, 0x4344c0, 0x19223, 0x69d3a, 0x63e06f, 0x5459cc, 0x434b96,
    0x74e9c0, 0x34a061, 0x32b2d4, 0x42064, 0x145123, 0x1e10a0, 0x998b0, 0x79a34,
    0x65c38, 0x61cb9, 0x37354, 0x437d40, 0x438bc0, 0x179c0, 0x2e30c0, 0x12963,
    0x493f3, 0x23e9f3, 0xba8d0, 0x54604c, 0x79bb7, 0x226740, 0x79b36, 0x1b6b3d,
    0x383d41, 0x3854f3, 0x21340, 0x192c0, 0x363ce7, 0x16b5a, 0x1546c, 0x1d9041,
    0x255040, 0x23ce9d, 0x74e9c0, 0x23d040, 0x3327cf, 0x46060, 0x16040, 0x79a34,
    0x4139c0, 0x479a34, 0x31962b, 0x3158b0, 0xf8e9d, 0x410bc0, 0x79ab5, 0x34e9d3,
    0x37a077, 0x38a057, 0x1a768, 0x795c12, 0x362067, 0x1a069, 0x2a02a, 0x3bd02a,
    0x406e5b, 0x247c0, 0x2f5d, 0x42db13, 0x523c0, 0x45f406, 0x2ad5a, 0x74d00d
};

static constexpr std::array<uint32_t, 68> ik1303_microcommand_rom = {
    0x0, 0x800001, 0x40020, 0x1440090, 0xa00081, 0x1000000, 0x1400020, 0x800008,
    0xa03180, 0x1002200, 0x800400, 0x1418001, 0x80020, 0x841020, 0x203100, 0x203088,
    0xa00820, 0x800120, 0x8001c0, 0x810081, 0xa00089, 0x800401, 0xa010a0, 0xa01081,
    0x818001, 0x1a00220, 0x201100, 0x203420, 0x8000, 0x801020, 0x201420, 0x801190,
    0x40000, 0x80820, 0x800002, 0x140002, 0x800100, 0xa03c20, 0xa00808, 0xa01008,
    0x200540, 0x601209, 0x83100, 0xa03081, 0x8800004, 0x58001, 0x1001280, 0x1008001,
    0x1200209, 0x4018001, 0x40002, 0x1000001, 0x10200, 0x800840, 0xa01181, 0x4018801,
    0xa10181, 0x800801, 0x40001, 0x11190, 0x858001, 0x40020, 0x3200209, 0x8000c0,
    0x4000020, 0x600081, 0x1000000, 0x1000180
};

static constexpr std::array<uint8_t, 1152> ik1303_sync_rom = {
    44, 35, 0, 44, 35, 0, 44, 35, 48, 49, 50, 0, 49, 50, 18, 49, 50, 48,
    0, 0, 0, 17, 35, 0, 31, 6, 0, 49, 0, 28, 49, 0, 0, 49, 8, 29,
    44, 2, 14, 44, 2, 1, 44, 2, 8, 8, 58, 0, 0, 58, 1, 5, 58, 17,
    24, 10, 43, 0, 1, 51, 2, 36, 37, 55, 58, 24, 49, 58, 31, 49, 58, 61,
    55, 2, 6, 49, 2, 18, 49, 16, 25, 57, 2, 38, 51, 9, 8, 25, 25, 8,
    1, 20, 12, 0, 0, 0, 27, 6, 1, 38, 0, 33, 18, 20, 36, 6, 18, 0,
    57, 0, 33, 8, 34, 0, 16, 20, 0, 32, 0, 0, 57, 2, 0, 6, 37, 37,
    25, 2, 22, 9, 17, 25, 22, 17, 19, 24, 8, 16, 24, 0, 1, 31, 6, 18,
    26, 18, 46, 25, 2, 0, 51, 56, 0, 13, 6, 59, 19, 10, 2, 0, 39, 0,
    0, 0, 51, 19, 60, 0, 17, 20, 4, 17, 29, 52, 19, 1, 0, 20, 39, 0,
    44, 16, 33, 44, 2, 51, 0, 0, 0, 55, 18, 42, 49, 2, 0, 18, 6, 9,
    55, 18, 42, 49, 20, 12, 0, 0, 0, 57, 13, 18, 16, 15, 0, 0, 39, 3,
    55, 18, 12, 49, 5, 0, 49, 0, 0, 55, 32, 10, 49, 0, 0, 0, 0, 0,
    17, 19, 14, 1, 13, 17, 5, 37, 36, 10, 36, 12, 8, 13, 33, 0, 0, 0,
    55, 6, 58, 49, 5, 2, 10, 29, 22, 56, 20, 12, 0, 8, 6, 32, 27, 52,
    14, 2, 6, 0, 2, 31, 25, 32, 8, 55, 16, 33, 49, 18, 12, 0, 0, 0,
    1, 45, 48, 1, 45, 0, 1, 45, 48, 51, 52, 6, 1, 24, 0, 1, 24, 8,
    49, 32, 52, 49, 32, 5, 49, 32, 8, 31, 58, 32, 20, 58, 32, 12, 0, 32,
    10, 32, 6, 48, 31, 12, 0, 32, 0, 53, 32, 5, 52, 20, 9, 48, 32, 17,
    8, 24, 24, 8, 24, 24, 8, 51, 32, 4, 22, 6, 54, 6, 12, 1, 3, 0,
    47, 8, 24, 28, 0, 24, 0, 32, 24, 0, 24, 20, 53, 29, 6, 20, 0, 59,
    6, 32, 5, 52, 20, 9, 25, 0, 33, 5, 58, 58, 6, 58, 58, 5, 58, 58,
    1, 35, 0, 1, 35, 0, 1, 35, 8, 1, 50, 2, 1, 50, 2, 1, 50, 2,
    21, 4, 3, 21, 23, 3, 21, 23, 3, 7, 43, 3, 7, 23, 3, 7, 23, 3,
    4, 30, 6, 30, 63, 14, 9, 17, 19, 15, 41, 5, 9, 40, 9, 9, 9, 1,
    8, 11, 11, 27, 11, 11, 30, 11, 0, 8, 11, 11, 14, 11, 11, 26, 11, 0,
    17, 29, 6, 8, 16, 4, 2, 6, 47, 31, 28, 47, 0, 28, 28, 9, 24, 17,
    11, 12, 12, 11, 2, 48, 0, 0, 0, 37, 28, 4, 1, 28, 29, 29, 6, 8,
    1, 48, 33, 63, 46, 17, 25, 37, 1, 22, 0, 0, 3, 12, 10, 25, 10, 25,
    14, 22, 27, 17, 29, 16, 60, 58, 5, 32, 8, 16, 6, 34, 25, 2, 34, 24,
    6, 12, 1, 16, 0, 0, 0, 17, 19, 10, 43, 3, 10, 23, 3, 10, 23, 3,
    18, 20, 6, 18, 2, 0, 10, 2, 0, 10, 36, 12, 0, 10, 33, 6, 32, 24,
    10, 33, 33, 53, 2, 8, 16, 2, 5, 0, 18, 15, 17, 36, 33, 53, 2, 5,
    6, 37, 12, 6, 2, 18, 20, 2, 24, 18, 32, 20, 0, 0, 33, 24, 18, 11,
    10, 36, 6, 0, 32, 8, 37, 2, 0, 36, 2, 53, 24, 18, 20, 52, 0, 24,
    18, 20, 12, 0, 10, 33, 53, 2, 0, 0, 38, 3, 6, 39, 3, 6, 39, 3,
    38, 3, 0, 39, 3, 0, 39, 3, 0, 17, 4, 3, 0, 54, 3, 0, 54, 3,
    6, 4, 3, 7, 23, 3, 7, 23, 3, 10, 32, 36, 37, 3, 6, 8, 2, 11,
    18, 4, 22, 10, 23, 3, 10, 23, 3, 7, 43, 0, 7, 23, 0, 7, 23, 37,
    0, 7, 43, 3, 7, 23, 3, 7, 23, 3, 54, 3, 17, 36, 29, 36, 3, 6,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 21, 4, 3, 21, 23, 3, 21, 23,
    3, 18, 29, 29, 20, 6, 18, 6, 0, 28, 28, 0, 28, 47, 0, 6, 32, 32,
    0, 11, 2, 0, 11, 2, 0, 54, 0, 1, 24, 24, 1, 24, 24, 1, 24, 24,
    0, 0, 0, 51, 0, 8, 24, 4, 40, 31, 12, 8, 37, 6, 14, 6, 0, 24,
    6, 14, 0, 22, 22, 0, 29, 32, 32, 24, 7, 6, 53, 16, 52, 5, 9, 36,
    5, 9, 9, 9, 9, 1, 13, 16, 9, 8, 37, 51, 46, 6, 27, 6, 0, 19,
    28, 0, 0, 28, 0, 0, 28, 0, 56, 29, 61, 5, 29, 61, 5, 29, 61, 5,
    29, 32, 29, 0, 24, 0, 51, 52, 6, 57, 60, 33, 1, 60, 1, 6, 31, 25,
    0, 0, 0, 0, 0, 0, 0, 0, 8, 10, 23, 3, 17, 19, 20, 0, 5, 53,
    0, 5, 52, 0, 0, 52, 0, 0, 52, 56, 4, 2, 51, 0, 17, 4, 0, 0,
    12, 0, 38, 51, 9, 9, 32, 8, 24, 15, 54, 0, 0, 0, 0, 0, 39, 5,
    48, 9, 32, 32, 6, 32, 33, 0, 0, 10, 58, 16, 43, 24, 56, 56, 14, 2,
    22, 12, 53, 5, 0, 0, 25, 48, 0, 8, 28, 24, 0, 28, 0, 0, 5, 58,
    0, 0, 0, 0, 0, 0, 0, 56, 32, 31, 12, 8, 37, 6, 8, 0, 0, 52,
    6, 28, 5, 37, 28, 37, 31, 24, 52, 51, 32, 38, 11, 2, 0, 52, 9, 9,
    0, 6, 54, 0, 0, 17, 36, 11, 52, 32, 32, 0, 0, 57, 2, 8, 29, 0,
    0, 8, 0, 62, 0, 0, 55, 8, 29, 0, 0, 32, 0, 53, 32, 5, 52, 52,
    18, 20, 36, 52, 46, 48, 31, 6, 8, 1, 5, 48, 4, 48, 46, 6, 14, 0,
    54, 0, 0, 0, 0, 0, 52, 52, 0, 10, 6, 27, 31, 0, 0, 37, 0, 59,
    37, 16, 6, 0, 0, 10, 16, 7, 3, 10, 16, 1, 0, 0, 0, 22, 25, 53,
    6, 18, 16, 25, 16, 0, 0, 0, 58, 17, 6, 9, 53, 22, 16, 62, 19, 13,
    36, 61, 16, 14, 18, 51, 3, 6, 48, 0, 38, 0, 0, 39, 0, 0, 59, 8,
    6, 12, 12, 32, 10, 6, 17, 20, 0, 24, 36, 6, 10, 16, 24, 17, 36, 24,
    16, 37, 5, 6, 60, 5, 6, 0, 0, 6, 12, 12, 0, 0, 18, 36, 29, 29
};

static constexpr std::array<uint32_t, 256> ik1303_command_rom = {
    0xe3050, 0x16dfbe, 0x3ecf0, 0x55270, 0x31ed0, 0x458af, 0x16e2c4, 0x5a850,
    0x31a04, 0x16dfbe, 0x350250, 0x59224, 0x324e2, 0x7ee950, 0x364a4, 0x6ed122,
    0x56850, 0x43e947, 0x612a5, 0x20285, 0x3821e, 0x49a33, 0x1ff225, 0x1fc6a5,
    0x5944d0, 0x45f2d3, 0x1f9232, 0x238d0, 0x1f9255, 0x4e876, 0x22cf7, 0x16e2c4,
    0x31726, 0xc46ae, 0x40735, 0xc75c7, 0x4e0950, 0x4582e, 0x4e2fd0, 0x14150,
    0x470081, 0x66850, 0xe162c, 0x5bd122, 0x4e9122, 0xbf5d6, 0x25eec, 0x3c26d0,
    0x30bd0, 0x1e550, 0x6d2847, 0xf1020, 0x6a95ea, 0x49a32, 0x764b3, 0x44e80c,
    0x152d6, 0x23e50, 0x44c000, 0x515ab, 0x128ed0, 0x1babd6, 0x127050, 0x397ed8,
    0x47aea2, 0x7cefd0, 0x3a8285, 0x73d50, 0x4205d0, 0x15124b, 0x32050, 0xa90a1,
    0x4ee5c, 0x2a350, 0x55284, 0x2755e0, 0x1a1d0, 0x65020, 0xa562c, 0x48ee50,
    0x1b5e3c, 0xc40fd, 0x256ad, 0x1396ad, 0x56757e, 0x38dcee, 0x1b9b54, 0x5bb747,
    0x14e4d0, 0x3b9062, 0x5916e, 0x1982a5, 0x4ee5c, 0x2a141, 0xe1dbb, 0x33977,
    0xd8204, 0x11020, 0x4052e, 0x56850, 0x14d204, 0x1322b, 0x611e47, 0x68e847,
    0x613950, 0x5708af, 0x20234, 0x557a3, 0x20285, 0x243047, 0x44ca8c, 0x1b5124,
    0x1d3950, 0x331ab, 0x2b73ab, 0x2b0ae, 0x6e7a63, 0x1079f4, 0x2f4358, 0x3a9250,
    0x238e6, 0x6f5cd0, 0x69750, 0x2f7047, 0x5e879, 0x17b035, 0x29c47, 0x41bfc7,
    0x232951, 0x4f0ae, 0x21f02e, 0x16dfbe, 0x3710a1, 0x5f9f4, 0x612a5, 0xa3050,
    0x1a7ce, 0x32951, 0x1b94a6, 0x23f02f, 0x23282a, 0x5d4a8, 0x2067ce, 0xfe9cb,
    0x3c3db, 0x212a5, 0x7a2847, 0x1e4285, 0x557a3, 0x5e86a, 0x26847, 0x212a5,
    0x399d62, 0x3685c7, 0x45e0d0, 0x612a5, 0x24250, 0x45edd0, 0x26450, 0x6d7d0,
    0x203047, 0x29ba0, 0x41c2f, 0xb0285, 0x26e821, 0x58285, 0x7b5d50, 0x10285,
    0x212a5, 0x4207d0, 0x6ce847, 0x35ecc, 0x60204, 0x701d50, 0x388221, 0xa3dd0,
    0x27fa6, 0x4f0ae, 0x6d88af, 0xc9225, 0x6e0c47, 0x2eb8cb, 0x61250, 0x20285,
    0x612a5, 0x13cea4, 0x1cf7dc, 0x29bae9, 0x2b53a6, 0x6fa822, 0x2af04, 0x5dd62,
    0x32faae, 0x2c4f25, 0x32c4d3, 0x22868, 0xad020, 0x6620d0, 0x31fe04, 0x3684d0,
    0x58204, 0x3d7040, 0x378250, 0x72c8e0, 0x33e4d0, 0x2a3c7, 0x7d08b, 0x52850,
    0x45c850, 0x15075, 0x75eb7, 0xdaad5, 0x4c081, 0x757a24, 0x35b047, 0x713c50,
    0x13562c, 0x45e0d0, 0x5e447, 0x3241d0, 0x293e0, 0x6682e, 0x35d62c, 0x45e450,
    0x1b114b, 0x2a4db, 0x40735, 0xc5084, 0x700450, 0x46d2f, 0x3a8285, 0x202f4,
    0x557a3, 0x173050, 0x7260a2, 0x691122, 0x37d447, 0x32502e, 0x29f047, 0x45e82f,
    0xb9020, 0x482848, 0x3e306d, 0xb704c, 0x111d62, 0x35eae, 0x571cd0, 0x58a822,
    0x1b89ee, 0xc702e, 0x422e9a, 0x43f7d0, 0x5e86a, 0x3ee820, 0x29e47, 0x5e6d0
};

static constexpr std::array<uint32_t, 68> ik1306_microcommand_rom = {
    0x0, 0x800008, 0x40020, 0x800001, 0x800021, 0x80020, 0xa00028, 0x40100,
    0x4000100, 0x10100, 0xa00101, 0x201089, 0x213201, 0x800004, 0x800800, 0x800820,
    0x200088, 0x4810002, 0xa00820, 0x800400, 0x801000, 0x100000, 0x8800004, 0x8000,
    0x1400020, 0x800005, 0x4000020, 0xa00180, 0x100000, 0x4000001, 0x8241004, 0x400000,
    0x80001, 0x40001, 0x212801, 0x200808, 0x800000, 0x10020, 0xa00808, 0x40090,
    0xa01008, 0x800401, 0xa00081, 0xa01081, 0x803400, 0xa01001, 0xa11801, 0x11001,
    0xa10801, 0x213801, 0x98001, 0x818001, 0x800420, 0x880090, 0x203c08, 0x200809,
    0xa00089, 0x203090, 0x840090, 0x810002, 0x210801, 0x210081, 0x10000, 0x200090,
    0x210081, 0x212801, 0xa01020, 0xa01020
};

static constexpr std::array<uint8_t, 1152> ik1306_sync_rom = {
    0, 0, 0, 0, 0, 0, 0, 0, 0, 44, 42, 39, 19, 43, 39, 19, 43, 39,
    52, 42, 39, 19, 43, 39, 19, 43, 39, 41, 42, 53, 41, 43, 53, 41, 43, 53,
    41, 18, 53, 41, 63, 53, 41, 63, 53, 46, 0, 0, 45, 2, 0, 0, 0, 0,
    42, 2, 0, 45, 2, 0, 0, 0, 0, 3, 18, 5, 45, 2, 0, 0, 0, 0,
    63, 63, 63, 63, 63, 63, 63, 63, 63, 14, 2, 0, 36, 2, 0, 36, 2, 0,
    48, 29, 5, 47, 29, 0, 0, 29, 0, 34, 0, 0, 45, 2, 0, 0, 0, 0,
    12, 0, 0, 45, 2, 0, 0, 0, 0, 63, 63, 63, 63, 63, 63, 63, 63, 63,
    0, 0, 0, 0, 0, 0, 36, 37, 0, 49, 0, 0, 45, 2, 0, 0, 0, 0,
    14, 15, 15, 0, 0, 0, 15, 15, 15, 14, 52, 5, 0, 0, 0, 0, 0, 0,
    24, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 3, 24, 0, 0, 0,
    24, 37, 0, 3, 24, 0, 0, 0, 0, 27, 3, 57, 0, 0, 0, 20, 24, 0,
    54, 0, 0, 3, 11, 0, 0, 0, 0, 0, 0, 0, 3, 24, 0, 0, 0, 0,
    55, 30, 0, 0, 30, 0, 0, 0, 0, 1, 6, 7, 1, 6, 7, 1, 6, 7,
    52, 18, 0, 0, 0, 0, 0, 0, 0, 60, 0, 0, 45, 2, 0, 0, 0, 0,
    62, 0, 0, 45, 2, 0, 0, 0, 0, 63, 63, 63, 63, 63, 63, 63, 63, 63,
    63, 63, 63, 63, 63, 63, 63, 63, 63, 63, 63, 63, 63, 63, 63, 63, 63, 63,
    1, 37, 0, 1, 37, 0, 36, 2, 0, 3, 4, 2, 3, 4, 2, 36, 2, 0,
    1, 6, 7, 1, 6, 7, 36, 2, 0, 1, 4, 8, 1, 4, 8, 36, 0, 26,
    3, 6, 9, 3, 6, 9, 36, 0, 2, 3, 37, 0, 3, 37, 0, 36, 37, 0,
    3, 0, 56, 3, 0, 11, 3, 37, 0, 36, 37, 0, 36, 37, 14, 5, 0, 0,
    3, 37, 0, 3, 37, 0, 3, 37, 0, 0, 0, 25, 5, 0, 25, 5, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 41, 18, 0, 33, 0, 0, 33, 36, 37, 3, 37,
    0, 13, 2, 0, 13, 2, 0, 13, 2, 0, 23, 0, 0, 23, 36, 5, 0, 0,
    36, 0, 5, 36, 0, 5, 36, 0, 5, 36, 37, 0, 36, 37, 0, 36, 37, 0,
    19, 10, 0, 0, 3, 11, 0, 40, 0, 0, 3, 5, 0, 3, 5, 0, 3, 5,
    27, 3, 0, 11, 3, 11, 0, 0, 0, 44, 2, 0, 36, 2, 0, 36, 2, 0,
    14, 15, 15, 0, 0, 0, 2, 0, 0, 14, 15, 15, 0, 0, 0, 15, 15, 0,
    0, 22, 0, 0, 22, 0, 0, 22, 0, 0, 23, 0, 0, 23, 0, 0, 0, 33,
    0, 0, 23, 0, 0, 23, 36, 2, 0, 0, 0, 0, 0, 0, 0, 41, 18, 0,
    20, 15, 15, 0, 0, 0, 15, 0, 0, 36, 15, 15, 0, 0, 0, 15, 0, 0,
    19, 10, 0, 0, 3, 11, 0, 0, 0, 27, 24, 11, 0, 0, 0, 0, 0, 0,
    19, 15, 15, 0, 0, 0, 0, 42, 0, 27, 3, 61, 0, 0, 0, 0, 0, 0,
    0, 59, 0, 0, 59, 0, 18, 20, 0, 14, 0, 0, 15, 0, 0, 5, 36, 2,
    3, 0, 37, 3, 0, 37, 3, 0, 37, 0, 0, 32, 0, 0, 32, 0, 0, 32,
    0, 17, 5, 0, 17, 5, 0, 17, 5, 0, 17, 37, 0, 17, 37, 0, 17, 37,
    14, 15, 15, 0, 0, 0, 16, 0, 0, 3, 0, 0, 51, 0, 0, 0, 0, 0,
    14, 15, 15, 42, 15, 15, 18, 0, 0, 0, 0, 0, 27, 28, 0, 0, 0, 0,
    3, 21, 0, 3, 21, 0, 3, 21, 0, 27, 2, 0, 0, 0, 0, 27, 2, 0,
    0, 0, 0, 3, 0, 18, 0, 0, 0, 0, 0, 0, 3, 18, 18, 18, 18, 0,
    0, 0, 0, 0, 0, 0, 36, 35, 2, 0, 0, 0, 0, 0, 36, 35, 2, 0,
    38, 39, 0, 40, 39, 0, 40, 39, 0, 0, 38, 39, 0, 40, 39, 0, 40, 39,
    41, 42, 39, 41, 43, 39, 41, 43, 58, 14, 18, 18, 18, 0, 0, 16, 0, 0,
    14, 18, 0, 0, 0, 0, 16, 0, 0, 14, 15, 15, 0, 0, 15, 15, 15, 0,
    0, 0, 0, 37, 0, 14, 15, 15, 15, 14, 15, 15, 0, 0, 15, 15, 15, 15,
    0, 0, 0, 0, 0, 0, 14, 24, 0, 36, 24, 0, 0, 0, 0, 0, 0, 0,
    0, 29, 0, 0, 29, 0, 0, 29, 0, 31, 26, 0, 0, 0, 0, 0, 0, 0,
    0, 22, 24, 0, 0, 0, 0, 0, 0, 0, 22, 5, 0, 22, 5, 0, 22, 5,
    0, 22, 2, 0, 22, 2, 0, 22, 2, 3, 33, 2, 3, 33, 2, 3, 33, 2,
    24, 15, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 27, 3, 11, 0, 0, 0,
    3, 18, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 27, 3, 61, 0, 0, 0,
    36, 24, 3, 24, 5, 3, 24, 5, 0, 3, 0, 50, 3, 0, 50, 3, 0, 50,
    36, 51, 0, 0, 51, 0, 0, 0, 0, 0, 0, 33, 0, 0, 33, 0, 0, 0,
    0, 44, 42, 39, 19, 43, 39, 0, 0, 3, 37, 0, 3, 37, 0, 19, 9, 0,
    0, 59, 5, 0, 59, 5, 0, 59, 5, 0, 13, 5, 0, 13, 5, 0, 13, 5,
    19, 0, 0, 0, 0, 0, 0, 0, 7, 27, 24, 11, 0, 0, 0, 0, 0, 0,
    0, 23, 0, 0, 23, 14, 5, 13, 2, 24, 0, 37, 0, 0, 0, 0, 0, 0,
    0, 0, 3, 0, 0, 0, 24, 0, 0, 19, 9, 0, 0, 9, 0, 0, 9, 0,
    14, 15, 2, 36, 37, 0, 36, 37, 0, 0, 0, 0, 41, 15, 15, 15, 18, 0,
    0, 41, 18, 0, 41, 63, 0, 19, 15, 0, 61, 0, 0, 61, 0, 0, 61, 0,
    27, 3, 0, 11, 3, 11, 19, 57, 36, 14, 2, 0, 36, 2, 0, 19, 7, 0,
    63, 63, 63, 63, 63, 63, 63, 63, 63, 63, 63, 63, 63, 63, 63, 63, 63, 63,
    63, 63, 63, 63, 63, 63, 63, 63, 63, 63, 63, 63, 63, 63, 63, 63, 63, 63,
    63, 63, 63, 63, 63, 63, 63, 63, 63, 63, 63, 63, 63, 63, 63, 63, 63, 63,
    63, 63, 63, 63, 63, 63, 63, 63, 63, 63, 63, 63, 63, 63, 63, 63, 63, 63
};

static constexpr std::array<uint32_t, 256> ik1306_command_rom = {
    0x1c000, 0x18040, 0x1f52f, 0x2e600, 0x24000, 0x2e680, 0x24000, 0x16980,
    0x24000, 0x2ea00, 0x24000, 0x16300, 0x18800, 0x2e400, 0x1ab57, 0x17980,
    0x24000, 0x1eb55, 0x1cb80, 0x18040, 0x1c000, 0x1c000, 0x1e244, 0x322c5,
    0xa0058, 0x1a1425, 0x228000, 0xa0059, 0x200058, 0x200059, 0x136fdf, 0x3ed7a2,
    0x3ed7a1, 0x3e0000, 0x3ed7a0, 0x250000, 0x2e0059, 0x2e0058, 0x20c000, 0xf61c3,
    0x1ef5e, 0x1ed80, 0x1a6c80, 0x1c02b, 0x1c028, 0x1c003, 0x1c028, 0x1c052,
    0x1c015, 0x30037, 0x3ee00, 0x1ee01, 0x1eedd, 0x1d45f, 0x372c5b, 0x3005c,
    0x1a0000, 0x1c50a, 0x1edd9, 0x1c154, 0xaafdf, 0x1efdf, 0x2c076, 0x1fb80,
    0x2c039, 0x19d2a, 0x6ddaa, 0x1a1428, 0x1a0000, 0x3c2c00, 0x2c03d, 0x128000,
    0x15900, 0x141400, 0x16700, 0x158000, 0x14c000, 0x2c076, 0x1fb80, 0xfafdf,
    0x370058, 0x14032, 0x1a1428, 0x1402a, 0x1a162c, 0x1a1428, 0x14039, 0x1a1428,
    0x1a162c, 0x328025, 0x1c013, 0x1c066, 0x1c014, 0x1c066, 0x1c014, 0x3d805f,
    0x2df00, 0x1a980, 0x2e700, 0x1a980, 0x19dd8, 0x15500, 0x1c058, 0x621c3,
    0x3f3af6, 0x29428, 0x15500, 0x1a980, 0x30000, 0x60000, 0x1a17af, 0x15e00,
    0x1a980, 0x30000, 0x617af, 0x1a0000, 0x1c22e, 0x14b00, 0x1e8000, 0x1c247,
    0x2cb4b, 0x1dc000, 0x31899, 0x60000, 0x1c05d, 0x372c5f, 0x20c000, 0x1a0000,
    0x1a6f5e, 0x20c000, 0x1a0000, 0x28009, 0x2c016, 0x2c061, 0x62d5a, 0x1ec66,
    0x3c0480, 0x210004, 0x157a6, 0x1a002f, 0x1a0027, 0x17680, 0x60000, 0x248000,
    0x3c04d9, 0x60000, 0x28000, 0x2d4015, 0x1c011, 0x1c052, 0x1c066, 0x1c001,
    0x1c001, 0x1c066, 0x1c001, 0x1c066, 0x1c001, 0x1c001, 0x1c066, 0x1c001,
    0x1c066, 0x1c002, 0x1c066, 0x1c001, 0x1eedd, 0x1c052, 0x1eedd, 0x1eedd,
    0x164003, 0x2ad00, 0x2d500, 0x73a00, 0x2df80, 0x62f00, 0x2fa58, 0x2c805f,
    0x3c04c7, 0x2b8000, 0x2ef63, 0x24000, 0x635ac, 0x3006e, 0x60000, 0x60001,
    0x1d428, 0x2d800, 0x1a0000, 0x31b36, 0x304000, 0x3c3959, 0x2a4000, 0x312f80,
    0x1d8b1, 0x2a6d5a, 0x312d5a, 0x1a0000, 0x28000, 0x1a4059, 0x329600, 0x372cb1,
    0x372ceb, 0x234000, 0x2ad5a, 0x1c00e, 0x1d72e, 0x1e142, 0x1d9b4, 0x2f165,
    0x36ef5e, 0x1c064, 0x1c3df, 0x1efd1, 0x2cd03, 0x3c051, 0x350068, 0x1efdf,
    0x1c052, 0x1c065, 0x33c038, 0x60067, 0x2a142, 0x1404e, 0x1c051, 0x1b000,
    0x1a980, 0x1404f, 0x1a980, 0x1a350, 0x1404f, 0x1c050, 0x1c059, 0x1c052,
    0x6dabe, 0x1402a, 0x1c058, 0x1c00e, 0x19dd1, 0x1404e, 0x1ec00, 0x621c3,
    0x2a142, 0x1b000, 0x19d80, 0x1c000, 0x1e800, 0x182959, 0x20f8a5, 0x1a0000,
    0x1c023, 0x1c024, 0x1d7a9, 0x1c041, 0x418040, 0x1e480, 0x1efdf, 0x2654a
};


struct Rom {
  const uint32_t* microcommand;
  size_t microcommand_size;
  const uint8_t* sync;
  size_t sync_size;
  const uint32_t* command;
  size_t command_size;
};

static constexpr Rom kIk1302Rom{
    ik1302_microcommand_rom.data(), ik1302_microcommand_rom.size(),
    ik1302_sync_rom.data(), ik1302_sync_rom.size(),
    ik1302_command_rom.data(), ik1302_command_rom.size()};
static constexpr Rom kIk1303Rom{
    ik1303_microcommand_rom.data(), ik1303_microcommand_rom.size(),
    ik1303_sync_rom.data(), ik1303_sync_rom.size(),
    ik1303_command_rom.data(), ik1303_command_rom.size()};
static constexpr Rom kIk1306Rom{
    ik1306_microcommand_rom.data(), ik1306_microcommand_rom.size(),
    ik1306_sync_rom.data(), ik1306_sync_rom.size(),
    ik1306_command_rom.data(), ik1306_command_rom.size()};

struct ParsedNumber {
  bool mantissa_negative = false;
  bool exponent_negative = false;
  int exponent = 0;
  std::array<uint8_t, 8> mantissa{};
};

struct Accumulator {
  uint8_t alpha = 0;
  uint8_t beta = 0;
  uint8_t gamma = 0;
  uint8_t sigma = 0;
};

int digit_value(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  throw std::runtime_error("only decimal literals are supported in this probe");
}

ParsedNumber parse_mk_literal(std::string text) {
  ParsedNumber out;
  std::string s;
  for (char c : text) {
    if (!std::isspace(static_cast<unsigned char>(c))) s.push_back(c == ',' ? '.' : c);
  }
  if (s.empty()) throw std::runtime_error("empty number");

  size_t pos = 0;
  if (s[pos] == '+' || s[pos] == '-') {
    out.mantissa_negative = s[pos] == '-';
    ++pos;
  }

  size_t exp_pos = s.find_first_of("eE", pos);
  std::string mant = exp_pos == std::string::npos ? s.substr(pos) : s.substr(pos, exp_pos - pos);
  int exp_adjust = 0;
  if (exp_pos != std::string::npos) exp_adjust = std::stoi(s.substr(exp_pos + 1));

  size_t dot = mant.find('.');
  int digits_before_dot = dot == std::string::npos ? static_cast<int>(mant.size()) : static_cast<int>(dot);
  std::string digits;
  for (char c : mant) {
    if (c == '.') continue;
    if (!std::isdigit(static_cast<unsigned char>(c))) throw std::runtime_error("bad number literal");
    digits.push_back(c);
  }
  if (digits.empty()) digits = "0";

  int exponent = digits_before_dot - 1 + exp_adjust;
  for (size_t i = 0; i < out.mantissa.size(); ++i) {
    out.mantissa[i] = i < digits.size() ? static_cast<uint8_t>(digit_value(digits[i])) : 0;
  }

  if (exponent < 0) {
    out.exponent_negative = true;
    out.exponent = 100 + exponent;
  } else {
    out.exponent_negative = false;
    out.exponent = exponent;
  }
  if (out.exponent < 0 || out.exponent > 99) throw std::runtime_error("exponent out of MK range");
  return out;
}

struct IK13 {
  explicit IK13(const Rom& rom_ref) : rom(&rom_ref) { reset(); }

  const Rom* rom;
  std::array<uint8_t, 42> memory{};
  std::array<uint8_t, 42> r{};
  std::array<uint8_t, 42> stack{};
  uint8_t s = 0;
  uint8_t s1 = 0;
  uint8_t l = 0;
  uint8_t t = 0;
  uint8_t carry = 0;
  std::array<uint8_t, 42> j{};
  std::array<bool, 256> used_command_addresses{};
  std::array<std::array<bool, 256>, 256> used_command_edges{};
  std::array<bool, 128> used_sync_addresses{};
  std::array<bool, 68> used_microcommands{};
  int last_command_address = -1;
  int tick_index = 0;
  uint32_t command = 0;
  int sync_address = 0;
  uint8_t input = 0;
  uint8_t output = 0;
  uint8_t key_x = 0;
  uint8_t key_y = 0;
  std::array<bool, 14> commas{};

  void reset() {
    memory.fill(0);
    r.fill(0);
    stack.fill(0);
    s = s1 = l = t = carry = 0;
    for (int i = 0; i < 42; ++i) j[i] = static_cast<uint8_t>(i < 6 ? i : i < 21 ? i % 3 + 3 : i % 9);
    used_command_addresses.fill(false);
    for (auto& row : used_command_edges) row.fill(false);
    used_sync_addresses.fill(false);
    used_microcommands.fill(false);
    last_command_address = -1;
    tick_index = 0;
    command = 0;
    sync_address = 0;
    input = output = key_x = key_y = 0;
    commas.fill(false);
  }

  void clear_coverage() {
    used_command_addresses.fill(false);
    for (auto& row : used_command_edges) row.fill(false);
    used_sync_addresses.fill(false);
    used_microcommands.fill(false);
    last_command_address = -1;
  }

  void execute_micro_order(int number, Accumulator& acc) {
    switch (number) {
      case 0: acc.alpha |= r[tick_index]; break;
      case 1: acc.alpha |= memory[tick_index]; break;
      case 2: acc.alpha |= stack[tick_index]; break;
      case 3: acc.alpha |= static_cast<uint8_t>((~r[tick_index]) & 0xF); break;
      case 4: if (l == 0) acc.alpha |= 0xA; break;
      case 5: acc.alpha |= s; break;
      case 6: acc.alpha |= 4; break;
      case 7: acc.beta |= s; break;
      case 8: acc.beta |= static_cast<uint8_t>((~s) & 0xF); break;
      case 9: acc.beta |= s1; break;
      case 10: acc.beta |= 6; break;
      case 11: acc.beta |= 1; break;
      case 12: acc.gamma |= l & 1; break;
      case 13: acc.gamma |= static_cast<uint8_t>((~l) & 1); break;
      case 14: acc.gamma |= static_cast<uint8_t>((~t) & 1); break;
      case 15: r[tick_index] = r[(tick_index + 3) % 42]; break;
      case 16: r[tick_index] = acc.sigma; break;
      case 17: r[tick_index] = s; break;
      case 18: r[tick_index] = r[tick_index] | s | acc.sigma; break;
      case 19: r[tick_index] = s | acc.sigma; break;
      case 20: r[tick_index] = r[tick_index] | s; break;
      case 21: r[tick_index] = r[tick_index] | acc.sigma; break;
      case 22: r[(tick_index + 41) % 42] = acc.sigma; break;
      case 23: r[(tick_index + 40) % 42] = acc.sigma; break;
      case 24: memory[tick_index] = s; break;
      case 25: l = carry; break;
      case 26: s = s1; break;
      case 27: s = acc.sigma; break;
      case 28: s = s1 | acc.sigma; break;
      case 29: s1 = acc.sigma; break;
      case 30: break;
      case 31: s1 = s1 | acc.sigma; break;
      case 32:
        stack[(tick_index + 2) % 42] = stack[(tick_index + 1) % 42];
        stack[(tick_index + 1) % 42] = stack[tick_index];
        stack[tick_index] = acc.sigma;
        break;
      case 33: {
        uint8_t x = stack[tick_index];
        stack[tick_index] = stack[(tick_index + 1) % 42];
        stack[(tick_index + 1) % 42] = stack[(tick_index + 2) % 42];
        stack[(tick_index + 2) % 42] = x;
        break;
      }
      case 34: {
        uint8_t x = stack[tick_index];
        uint8_t y = stack[(tick_index + 1) % 42];
        uint8_t z = stack[(tick_index + 2) % 42];
        stack[tick_index] = acc.sigma | y;
        stack[(tick_index + 1) % 42] = x | z;
        stack[(tick_index + 2) % 42] = y | x;
        break;
      }
    }
  }

  int command_address_for_trace() const {
    return r[36] + 16 * r[39];
  }

  uint32_t command_for_trace() const {
    return tick_index == 0 ? rom->command[command_address_for_trace()] : command;
  }

  int sync_address_for_trace(uint32_t command_for_tick) const {
    int sync = sync_address;
    int nine = tick_index / 9;
    int tick_in_nine = tick_index - nine * 9;
    if (tick_in_nine == 0 && !(nine > 0 && nine < 3)) {
      if (nine < 3) {
        sync = command_for_tick & 0x7F;
      } else if (nine == 3) {
        sync = (command_for_tick >> 7) & 0x7F;
      } else if (nine == 4) {
        sync = (command_for_tick >> 14) & 0xFF;
        if (sync > 31) sync = 95;
      }
    }
    return sync;
  }

  int sync_microcommand_for_trace(int sync) const {
    int sync_microcommand = rom->sync[sync * 9 + j[tick_index]] & 0x3F;
    if (sync_microcommand > 59) {
      sync_microcommand = (sync_microcommand - 60) * 2;
      if (l == 0) ++sync_microcommand;
      sync_microcommand += 60;
    }
    return sync_microcommand;
  }

  uint32_t microcommand_word_for_trace(int sync_microcommand) const {
    return rom->microcommand[sync_microcommand];
  }

  static std::string micro_order_list(uint32_t microcommand_word) {
    std::ostringstream out;
    bool first = true;
    for (int i = 0; i < 28; ++i) {
      if ((microcommand_word & 1) != 0) {
        if (!first) out << ',';
        first = false;
        out << i;
      }
      microcommand_word >>= 1;
    }
    return out.str();
  }

  static std::string micro_execute_order_list(uint32_t microcommand_word,
                                              uint32_t command_for_tick,
                                              int tick_index_for_trace) {
    std::array<uint8_t, 28> micro_orders{};
    uint32_t word = microcommand_word;
    for (int i = 0; i < 28; ++i) {
      micro_orders[i] = word & 1;
      word >>= 1;
    }

    std::ostringstream out;
    bool first = true;
    auto append = [&](int order) {
      if (!first) out << ',';
      first = false;
      out << order;
    };

    for (int i = 0; i < 12; ++i) {
      if (micro_orders[i]) append(i);
    }
    for (int i = 12; i < 15; ++i) {
      if (micro_orders[i]) append(i);
    }

    const int nine = tick_index_for_trace / 9;
    if (((command_for_tick >> 22) & 1) == 0 || nine == 4) {
      int field = (micro_orders[17] << 2) | (micro_orders[16] << 1) | micro_orders[15];
      if (field > 0) append(field + 14);
      for (int i = 18; i < 20; ++i) {
        if (micro_orders[i]) append(i + 4);
      }
    }

    for (int i = 20; i < 22; ++i) {
      if (micro_orders[i]) append(i + 4);
    }

    for (int i = 0; i < 3; ++i) {
      int field = (micro_orders[23 + i * 2] << 1) | micro_orders[22 + i * 2];
      if (field > 0) append(field + 25 + i * 3);
    }
    return out.str();
  }

  void execute_order_list(std::initializer_list<int> orders, Accumulator& acc) {
    for (int order : orders) execute_micro_order(order, acc);
  }

  void execute_049a32_pre_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 0:
      case 30:
        execute_order_list({7, 8}, acc);
        break;
      case 1:
      case 2:
      case 4:
      case 5:
      case 7:
      case 8:
      case 10:
      case 11:
      case 13:
      case 14:
      case 16:
      case 17:
      case 19:
      case 20:
      case 22:
      case 23:
      case 25:
      case 35:
      case 38:
        execute_order_list({0}, acc);
        break;
      case 3:
      case 6:
      case 9:
      case 12:
      case 15:
      case 18:
      case 21:
      case 24:
        execute_order_list({5, 10}, acc);
        break;
      case 27:
        execute_order_list({5, 8}, acc);
        break;
      case 28:
      case 29:
      case 33:
      case 34:
        execute_order_list({5}, acc);
        break;
      case 31:
        execute_order_list({5, 11}, acc);
        break;
      case 32:
      case 39:
        execute_order_list({0, 7}, acc);
        break;
      case 40:
        execute_order_list(branch_l ? std::initializer_list<int>{0}
                                    : std::initializer_list<int>{5},
                           acc);
        break;
      default:
        break;
    }
  }

  void execute_049a32_gamma_orders(int tick, Accumulator& acc) {
    switch (tick) {
      case 0:
      case 3:
      case 6:
      case 9:
      case 12:
      case 15:
      case 18:
      case 21:
      case 30:
        execute_order_list({12, 13}, acc);
        break;
      case 24:
      case 28:
        execute_order_list({12}, acc);
        break;
      default:
        break;
    }
  }

  void execute_049a32_post_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 0:
      case 30:
      case 31:
      case 32:
        execute_order_list({25, 27}, acc);
        break;
      case 1:
      case 2:
      case 4:
      case 5:
      case 7:
      case 8:
      case 10:
      case 11:
      case 13:
      case 14:
      case 16:
      case 17:
      case 19:
      case 20:
      case 22:
      case 23:
      case 25:
        execute_order_list({17, 26, 29}, acc);
        break;
      case 3:
      case 6:
      case 9:
      case 12:
      case 15:
      case 18:
      case 21:
      case 24:
        execute_order_list({25}, acc);
        break;
      case 27:
      case 28:
        execute_order_list({27}, acc);
        break;
      case 29:
      case 34:
        execute_order_list({26, 29}, acc);
        break;
      case 33:
        execute_order_list({22}, acc);
        break;
      case 35:
        execute_order_list({15, 29}, acc);
        break;
      case 38:
        execute_order_list({29}, acc);
        break;
      case 39:
        execute_order_list({16, 27}, acc);
        break;
      case 40:
        execute_order_list(branch_l ? std::initializer_list<int>{17, 22, 27}
                                    : std::initializer_list<int>{22},
                           acc);
        break;
      default:
        break;
    }
  }

  void tick_ik1303_049a32_decoded() {
    static constexpr uint32_t kCommand = 0x049a32;
    if (tick_index == 0) command = kCommand;

    const int current_tick = tick_index;
    const bool branch_l = l != 0;
    if (current_tick < 27) {
      sync_address = 50;
    } else if (current_tick < 36) {
      sync_address = 52;
    } else {
      sync_address = 18;
    }

    Accumulator acc;
    execute_049a32_pre_add_orders(current_tick, branch_l, acc);

    if (key_y == 0) t = 0;

    execute_049a32_gamma_orders(current_tick, acc);

    int sum = acc.alpha + acc.beta + acc.gamma;
    acc.sigma = sum & 0xF;
    carry = (sum >> 4) & 1;

    execute_049a32_post_add_orders(current_tick, branch_l, acc);

    output = memory[current_tick];
    memory[current_tick] = input;
    ++tick_index;
    if (tick_index == 42) tick_index = 0;
  }

  void execute_04582e_pre_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 0:
      case 3:
      case 6:
      case 9:
      case 12:
      case 15:
      case 18:
      case 21:
      case 24:
        execute_order_list({0, 10}, acc);
        break;
      case 1:
      case 4:
      case 7:
      case 10:
      case 13:
      case 16:
      case 19:
      case 22:
      case 25:
      case 27:
      case 35:
      case 39:
        execute_order_list({0, 7}, acc);
        break;
      case 2:
      case 5:
      case 8:
      case 11:
      case 14:
      case 17:
      case 20:
      case 23:
      case 26:
        execute_order_list({4, 7}, acc);
        break;
      case 38:
        execute_order_list({4, 7, 8}, acc);
        break;
      case 28:
      case 30:
        execute_order_list({5, 10}, acc);
        break;
      case 29:
      case 36:
      case 37:
      case 41:
        execute_order_list({5}, acc);
        break;
      case 31:
        if (!branch_l) execute_order_list({7, 8}, acc);
        break;
      case 32:
        execute_order_list({8}, acc);
        break;
      case 33:
        execute_order_list({9}, acc);
        break;
      case 34:
        execute_order_list({5, 8}, acc);
        break;
      case 40:
        execute_order_list({10}, acc);
        break;
      default:
        break;
    }
  }

  void execute_04582e_gamma_orders(int tick, Accumulator& acc) {
    switch (tick) {
      case 4:
      case 7:
      case 10:
      case 13:
      case 16:
      case 19:
      case 22:
      case 25:
      case 28:
      case 30:
      case 36:
      case 38:
        execute_order_list({12}, acc);
        break;
      case 32:
        execute_order_list({12, 13}, acc);
        break;
      case 33:
        execute_order_list({13}, acc);
        break;
      default:
        break;
    }
  }

  void execute_04582e_post_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 0:
      case 3:
      case 6:
      case 9:
      case 12:
      case 15:
      case 18:
      case 21:
      case 24:
      case 34:
      case 40:
        execute_order_list({27}, acc);
        break;
      case 1:
      case 4:
      case 7:
      case 10:
      case 13:
      case 16:
      case 19:
      case 22:
      case 25:
      case 27:
        execute_order_list({25, 27}, acc);
        break;
      case 2:
      case 5:
      case 8:
      case 11:
      case 14:
      case 17:
      case 20:
      case 23:
      case 26:
        execute_order_list({22, 26, 29}, acc);
        break;
      case 28:
      case 30:
      case 32:
        execute_order_list({25}, acc);
        break;
      case 29:
      case 37:
        execute_order_list({26, 29}, acc);
        break;
      case 31:
        execute_order_list({29}, acc);
        break;
      case 33:
        execute_order_list({29}, acc);
        break;
      case 35:
      case 39:
        execute_order_list({16, 27}, acc);
        break;
      case 36:
        execute_order_list({22, 27}, acc);
        break;
      case 38:
        execute_order_list({16}, acc);
        break;
      case 41:
        execute_order_list({22}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void tick_ik1303_04582e_decoded() {
    static constexpr uint32_t kCommand = 0x04582e;
    if (tick_index == 0) command = kCommand;

    const int current_tick = tick_index;
    const bool branch_l = l != 0;
    if (current_tick < 27) {
      sync_address = 46;
    } else if (current_tick < 36) {
      sync_address = 48;
    } else {
      sync_address = 17;
    }

    Accumulator acc;
    execute_04582e_pre_add_orders(current_tick, branch_l, acc);

    if (key_y == 0) t = 0;

    execute_04582e_gamma_orders(current_tick, acc);

    int sum = acc.alpha + acc.beta + acc.gamma;
    acc.sigma = sum & 0xF;
    carry = (sum >> 4) & 1;

    execute_04582e_post_add_orders(current_tick, branch_l, acc);

    output = memory[current_tick];
    memory[current_tick] = input;
    ++tick_index;
    if (tick_index == 42) tick_index = 0;
  }

  void execute_049a33_pre_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 0:
      case 30:
        execute_order_list({7, 8}, acc);
        break;
      case 1:
      case 2:
      case 4:
      case 5:
      case 7:
      case 8:
      case 10:
      case 11:
      case 13:
      case 14:
      case 16:
      case 17:
      case 19:
      case 20:
      case 22:
      case 23:
      case 25:
      case 35:
      case 38:
        execute_order_list({0}, acc);
        break;
      case 3:
      case 6:
      case 9:
      case 12:
      case 15:
      case 18:
      case 21:
      case 24:
        execute_order_list({8}, acc);
        break;
      case 27:
        execute_order_list({5, 8}, acc);
        break;
      case 28:
      case 29:
      case 33:
      case 34:
        execute_order_list({5}, acc);
        break;
      case 31:
        execute_order_list({5, 11}, acc);
        break;
      case 32:
      case 39:
        execute_order_list({0, 7}, acc);
        break;
      case 40:
        execute_order_list(branch_l ? std::initializer_list<int>{0}
                                    : std::initializer_list<int>{5},
                           acc);
        break;
      default:
        break;
    }
  }

  void tick_ik1303_049a33_decoded() {
    static constexpr uint32_t kCommand = 0x049a33;
    if (tick_index == 0) command = kCommand;

    const int current_tick = tick_index;
    const bool branch_l = l != 0;
    if (current_tick < 27) {
      sync_address = 51;
    } else if (current_tick < 36) {
      sync_address = 52;
    } else {
      sync_address = 18;
    }

    Accumulator acc;
    execute_049a33_pre_add_orders(current_tick, branch_l, acc);

    if (key_y == 0) t = 0;

    execute_049a32_gamma_orders(current_tick, acc);

    int sum = acc.alpha + acc.beta + acc.gamma;
    acc.sigma = sum & 0xF;
    carry = (sum >> 4) & 1;

    execute_049a32_post_add_orders(current_tick, branch_l, acc);

    output = memory[current_tick];
    memory[current_tick] = input;
    ++tick_index;
    if (tick_index == 42) tick_index = 0;
  }

  void execute_44ca8c_pre_add_orders(int tick, Accumulator& acc) {
    switch (tick) {
      case 0:
      case 27:
        execute_order_list({0, 11}, acc);
        break;
      case 2:
      case 24:
        execute_order_list({5, 11}, acc);
        break;
      case 3:
      case 6:
      case 9:
      case 12:
      case 15:
      case 18:
      case 21:
        execute_order_list({7, 8}, acc);
        break;
      case 4:
      case 7:
      case 10:
      case 13:
      case 16:
      case 19:
      case 22:
        execute_order_list({1}, acc);
        break;
      case 25:
        execute_order_list({0, 3, 7}, acc);
        break;
      case 28:
      case 33:
        execute_order_list({6, 7, 8}, acc);
        break;
      case 29:
        execute_order_list({8}, acc);
        break;
      case 30:
        execute_order_list({0}, acc);
        break;
      case 39:
        execute_order_list({0, 7}, acc);
        break;
      case 31:
      case 34:
      case 37:
        execute_order_list({5}, acc);
        break;
      case 36:
        execute_order_list({5, 8}, acc);
        break;
      case 35:
      case 38:
        execute_order_list({9}, acc);
        break;
      case 40:
        execute_order_list({0}, acc);
        break;
      default:
        break;
    }
  }

  void execute_44ca8c_gamma_orders(int tick, Accumulator& acc) {
    switch (tick) {
      case 3:
      case 6:
      case 9:
      case 12:
      case 15:
      case 18:
      case 21:
      case 29:
        execute_order_list({12, 13}, acc);
        break;
      case 35:
        execute_order_list({13}, acc);
        break;
      case 37:
        execute_order_list({12}, acc);
        break;
      default:
        break;
    }
  }

  void execute_44ca8c_post_add_orders(int tick, Accumulator& acc) {
    switch (tick) {
      case 0:
      case 4:
      case 7:
      case 10:
      case 13:
      case 16:
      case 19:
      case 22:
      case 28:
      case 33:
      case 36:
      case 37:
      case 40:
        execute_order_list({27}, acc);
        break;
      case 3:
      case 6:
      case 9:
      case 12:
      case 15:
      case 18:
      case 21:
      case 24:
      case 25:
        execute_order_list({25, 27}, acc);
        break;
      case 27:
      case 30:
        execute_order_list({32}, acc);
        break;
      case 34:
        execute_order_list({26, 29}, acc);
        break;
      case 35:
        execute_order_list({29}, acc);
        break;
      case 38:
        execute_order_list({16}, acc);
        break;
      case 39:
        execute_order_list({16, 27}, acc);
        break;
      default:
        break;
    }
  }

  void tick_ik1303_44ca8c_decoded() {
    static constexpr uint32_t kCommand = 0x44ca8c;
    if (tick_index == 0) command = kCommand;

    const int current_tick = tick_index;
    if (current_tick < 27) {
      sync_address = 12;
    } else if (current_tick < 36) {
      sync_address = 21;
    } else {
      sync_address = 19;
    }

    Accumulator acc;
    execute_44ca8c_pre_add_orders(current_tick, acc);

    if (key_y == 0) t = 0;

    execute_44ca8c_gamma_orders(current_tick, acc);

    int sum = acc.alpha + acc.beta + acc.gamma;
    acc.sigma = sum & 0xF;
    carry = (sum >> 4) & 1;

    execute_44ca8c_post_add_orders(current_tick, acc);

    output = memory[current_tick];
    memory[current_tick] = input;
    ++tick_index;
    if (tick_index == 42) tick_index = 0;
  }

  void execute_023e50_pre_add_orders(int tick, Accumulator& acc) {
    switch (tick) {
      case 27:
      case 28:
      case 29:
      case 32:
      case 37:
      case 38:
      case 40:
        execute_order_list({5}, acc);
        break;
      case 31:
        execute_order_list({10}, acc);
        break;
      case 33:
        execute_order_list({5, 8}, acc);
        break;
      case 34:
        execute_order_list({0, 3, 7}, acc);
        break;
      case 36:
        execute_order_list({0, 11}, acc);
        break;
      case 39:
        execute_order_list({0}, acc);
        break;
      case 41:
        execute_order_list({6, 7, 8}, acc);
        break;
      default:
        break;
    }
  }

  void execute_023e50_post_add_orders(int tick, Accumulator& acc) {
    switch (tick) {
      case 27:
      case 32:
      case 38:
        execute_order_list({26, 29}, acc);
        break;
      case 28:
      case 29:
        execute_order_list({23}, acc);
        break;
      case 30:
      case 37:
      case 40:
        execute_order_list({22}, acc);
        break;
      case 31:
      case 33:
      case 34:
      case 41:
        execute_order_list({27}, acc);
        break;
      case 36:
      case 39:
        execute_order_list({17, 32}, acc);
        break;
      default:
        break;
    }
  }

  void tick_ik1303_023e50_decoded() {
    static constexpr uint32_t kCommand = 0x023e50;
    if (tick_index == 0) command = kCommand;

    const int current_tick = tick_index;
    if (current_tick < 27) {
      sync_address = 80;
    } else if (current_tick < 36) {
      sync_address = 124;
    } else {
      sync_address = 8;
    }

    Accumulator acc;
    execute_023e50_pre_add_orders(current_tick, acc);

    if (key_y == 0) t = 0;

    int sum = acc.alpha + acc.beta + acc.gamma;
    acc.sigma = sum & 0xF;
    carry = (sum >> 4) & 1;

    execute_023e50_post_add_orders(current_tick, acc);

    output = memory[current_tick];
    memory[current_tick] = input;
    ++tick_index;
    if (tick_index == 42) tick_index = 0;
  }

  void execute_0e1dbb_pre_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 1:
      case 28:
        execute_order_list({7, 8}, acc);
        break;
      case 2:
      case 29:
      case 38:
        execute_order_list({5, 11}, acc);
        break;
      case 3:
      case 6:
      case 9:
      case 12:
      case 15:
      case 18:
      case 21:
      case 24:
      case 30:
      case 33:
        execute_order_list({5}, acc);
        break;
      case 4:
      case 7:
      case 10:
      case 13:
      case 16:
      case 19:
      case 22:
      case 31:
      case 34:
        execute_order_list({1}, acc);
        break;
      case 5:
      case 8:
      case 11:
      case 14:
      case 17:
      case 20:
      case 23:
      case 32:
        execute_order_list({5, 9}, acc);
        break;
      case 26:
      case 35:
      case 39:
      case 41:
        execute_order_list({0}, acc);
        break;
      case 37:
      case 40:
        execute_order_list(branch_l ? std::initializer_list<int>{0}
                                    : std::initializer_list<int>{5},
                           acc);
        break;
      case 36:
        execute_order_list({0, 11}, acc);
        break;
      default:
        break;
    }
  }

  void execute_0e1dbb_gamma_orders(int tick, Accumulator& acc) {
    switch (tick) {
      case 1:
      case 28:
        execute_order_list({12, 13}, acc);
        break;
      default:
        break;
    }
  }

  void execute_0e1dbb_post_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 0:
      case 24:
      case 27:
        execute_order_list({22}, acc);
        break;
      case 38:
        execute_order_list({23}, acc);
        break;
      case 1:
      case 2:
      case 28:
      case 29:
        execute_order_list({25, 27}, acc);
        break;
      case 5:
      case 8:
      case 11:
      case 14:
      case 17:
      case 20:
      case 23:
      case 32:
        execute_order_list({25, 27, 29}, acc);
        break;
      case 36:
      case 39:
      case 41:
        execute_order_list({27}, acc);
        break;
      case 3:
      case 6:
      case 9:
      case 12:
      case 15:
      case 18:
      case 21:
      case 30:
        execute_order_list({26, 29}, acc);
        break;
      case 4:
      case 7:
      case 10:
      case 13:
      case 16:
      case 19:
      case 22:
      case 31:
      case 34:
        execute_order_list({27}, acc);
        break;
      case 25:
      case 35:
        execute_order_list({17, 27}, acc);
        break;
      case 26:
        execute_order_list({17, 27}, acc);
        break;
      case 33:
        execute_order_list({22}, acc);
        break;
      case 40:
      case 37:
        execute_order_list(branch_l ? std::initializer_list<int>{17, 22, 27}
                                    : std::initializer_list<int>{22},
                           acc);
        break;
      default:
        break;
    }
  }

  void tick_ik1303_0e1dbb_decoded() {
    static constexpr uint32_t kCommand = 0x0e1dbb;
    if (tick_index == 0) command = kCommand;

    const int current_tick = tick_index;
    const bool branch_l = l != 0;
    if (current_tick < 36) {
      sync_address = 59;
    } else {
      if (current_tick == 36) {
        const int raw_sync = (kCommand >> 14) & 0xFF;
        r[37] = raw_sync & 0xF;
        r[40] = raw_sync >> 4;
      }
      sync_address = 95;
    }

    Accumulator acc;
    execute_0e1dbb_pre_add_orders(current_tick, branch_l, acc);

    if (key_y == 0) t = 0;

    execute_0e1dbb_gamma_orders(current_tick, acc);

    int sum = acc.alpha + acc.beta + acc.gamma;
    acc.sigma = sum & 0xF;
    carry = (sum >> 4) & 1;

    execute_0e1dbb_post_add_orders(current_tick, branch_l, acc);

    output = memory[current_tick];
    memory[current_tick] = input;
    ++tick_index;
    if (tick_index == 42) tick_index = 0;
  }

  void execute_04f0ae_pre_add_orders(int tick, Accumulator& acc) {
    switch (tick) {
      case 0:
      case 3:
      case 6:
      case 9:
      case 12:
      case 15:
      case 18:
      case 21:
      case 24:
        execute_order_list({0, 10}, acc);
        break;
      case 1:
      case 4:
      case 7:
      case 10:
      case 13:
      case 16:
      case 19:
      case 22:
      case 25:
      case 28:
      case 31:
      case 39:
        execute_order_list({0, 7}, acc);
        break;
      case 40:
        execute_order_list({0}, acc);
        break;
      case 32:
        execute_order_list({0, 3, 7}, acc);
        break;
      case 2:
      case 5:
      case 8:
      case 11:
      case 14:
      case 17:
      case 20:
      case 23:
      case 26:
      case 29:
        execute_order_list({4, 7}, acc);
        break;
      case 27:
        execute_order_list({10}, acc);
        break;
      case 30:
      case 36:
        execute_order_list({5, 8}, acc);
        break;
      case 35:
        execute_order_list({6, 11}, acc);
        break;
      case 37:
        execute_order_list({5}, acc);
        break;
      case 38:
        execute_order_list({9}, acc);
        break;
      default:
        break;
    }
  }

  void execute_04f0ae_gamma_orders(int tick, Accumulator& acc) {
    switch (tick) {
      case 4:
      case 7:
      case 10:
      case 13:
      case 16:
      case 19:
      case 22:
      case 25:
      case 28:
      case 37:
        execute_order_list({12}, acc);
        break;
      default:
        break;
    }
  }

  void execute_04f0ae_post_add_orders(int tick, Accumulator& acc) {
    switch (tick) {
      case 0:
      case 3:
      case 6:
      case 9:
      case 12:
      case 15:
      case 18:
      case 21:
      case 24:
      case 27:
      case 30:
      case 35:
      case 36:
      case 37:
      case 40:
        execute_order_list({27}, acc);
        break;
      case 1:
      case 4:
      case 7:
      case 10:
      case 13:
      case 16:
      case 19:
      case 22:
      case 25:
      case 28:
      case 32:
        execute_order_list({25, 27}, acc);
        break;
      case 2:
      case 5:
      case 8:
      case 11:
      case 14:
      case 17:
      case 20:
      case 23:
      case 26:
      case 29:
        execute_order_list({22, 26, 29}, acc);
        break;
      case 31:
      case 39:
        execute_order_list({16, 27}, acc);
        break;
      case 34:
        execute_order_list({29}, acc);
        break;
      case 38:
        execute_order_list({16}, acc);
        break;
      default:
        break;
    }
  }

  void tick_ik1303_04f0ae_decoded() {
    static constexpr uint32_t kCommand = 0x04f0ae;
    if (tick_index == 0) command = kCommand;

    const int current_tick = tick_index;
    if (current_tick < 27) {
      sync_address = 46;
    } else if (current_tick < 36) {
      sync_address = 97;
    } else {
      sync_address = 19;
    }

    Accumulator acc;
    execute_04f0ae_pre_add_orders(current_tick, acc);

    if (key_y == 0) t = 0;

    execute_04f0ae_gamma_orders(current_tick, acc);

    int sum = acc.alpha + acc.beta + acc.gamma;
    acc.sigma = sum & 0xF;
    carry = (sum >> 4) & 1;

    execute_04f0ae_post_add_orders(current_tick, acc);

    output = memory[current_tick];
    memory[current_tick] = input;
    ++tick_index;
    if (tick_index == 42) tick_index = 0;
  }

  void execute_0458af_pre_add_orders(int tick, Accumulator& acc) {
    switch (tick) {
      case 0:
      case 3:
      case 6:
      case 9:
      case 12:
      case 15:
      case 18:
      case 21:
      case 24:
        execute_order_list({3}, acc);
        break;
      case 27:
        execute_order_list({3, 7}, acc);
        break;
      case 1:
      case 4:
      case 7:
      case 10:
      case 13:
      case 16:
      case 19:
      case 22:
      case 25:
      case 39:
        execute_order_list({0, 7}, acc);
        break;
      case 35:
        execute_order_list({0}, acc);
        break;
      case 2:
      case 5:
      case 8:
      case 11:
      case 14:
      case 17:
      case 20:
      case 23:
      case 26:
        execute_order_list({4, 7}, acc);
        break;
      case 38:
        execute_order_list({4, 7, 8}, acc);
        break;
      case 28:
        execute_order_list({0, 3, 9}, acc);
        break;
      case 30:
      case 32:
      case 33:
      case 34:
        execute_order_list({9}, acc);
        break;
      case 31:
        execute_order_list({6, 8, 10}, acc);
        break;
      case 36:
      case 37:
      case 41:
        execute_order_list({5}, acc);
        break;
      case 40:
        execute_order_list({10}, acc);
        break;
      default:
        break;
    }
  }

  void execute_0458af_gamma_orders(int tick, Accumulator& acc) {
    switch (tick) {
      case 1:
      case 27:
        execute_order_list({12, 13}, acc);
        break;
      case 4:
      case 7:
      case 10:
      case 13:
      case 16:
      case 19:
      case 22:
      case 25:
      case 28:
      case 36:
      case 38:
        execute_order_list({12}, acc);
        break;
      case 30:
      case 32:
      case 33:
      case 34:
        execute_order_list({13}, acc);
        break;
      default:
        break;
    }
  }

  void execute_0458af_post_add_orders(int tick, Accumulator& acc) {
    switch (tick) {
      case 0:
      case 3:
      case 6:
      case 9:
      case 12:
      case 15:
      case 18:
      case 21:
      case 24:
      case 35:
      case 40:
        execute_order_list({27}, acc);
        break;
      case 36:
        execute_order_list({22, 27}, acc);
        break;
      case 1:
      case 4:
      case 7:
      case 10:
      case 13:
      case 16:
      case 19:
      case 22:
      case 25:
        execute_order_list({25, 27}, acc);
        break;
      case 2:
      case 5:
      case 8:
      case 11:
      case 14:
      case 17:
      case 20:
      case 23:
      case 26:
        execute_order_list({22, 26, 29}, acc);
        break;
      case 27:
      case 31:
        execute_order_list({25}, acc);
        break;
      case 28:
        execute_order_list({25, 26}, acc);
        break;
      case 29:
      case 30:
      case 32:
      case 33:
      case 34:
        execute_order_list({29}, acc);
        break;
      case 37:
        execute_order_list({26, 29}, acc);
        break;
      case 38:
        execute_order_list({16}, acc);
        break;
      case 39:
        execute_order_list({16, 27}, acc);
        break;
      case 41:
        execute_order_list({22}, acc);
        break;
      default:
        break;
    }
  }

  void tick_ik1303_0458af_decoded() {
    static constexpr uint32_t kCommand = 0x0458af;
    if (tick_index == 0) command = kCommand;

    const int current_tick = tick_index;
    if (current_tick < 27) {
      sync_address = 47;
    } else if (current_tick < 36) {
      sync_address = 49;
    } else {
      sync_address = 17;
    }

    Accumulator acc;
    execute_0458af_pre_add_orders(current_tick, acc);

    if (key_y == 0) t = 0;

    execute_0458af_gamma_orders(current_tick, acc);

    int sum = acc.alpha + acc.beta + acc.gamma;
    acc.sigma = sum & 0xF;
    carry = (sum >> 4) & 1;

    execute_0458af_post_add_orders(current_tick, acc);

    output = memory[current_tick];
    memory[current_tick] = input;
    ++tick_index;
    if (tick_index == 42) tick_index = 0;
  }

  void execute_21f02e_pre_add_orders(int tick, Accumulator& acc) {
    switch (tick) {
      case 0:
      case 3:
      case 6:
      case 9:
      case 12:
      case 15:
      case 18:
      case 21:
      case 24:
        execute_order_list({0, 10}, acc);
        break;
      case 1:
      case 4:
      case 7:
      case 10:
      case 13:
      case 16:
      case 19:
      case 22:
      case 25:
        execute_order_list({0, 7}, acc);
        break;
      case 37:
      case 39:
      case 40:
      case 41:
        execute_order_list({0}, acc);
        break;
      case 2:
      case 5:
      case 8:
      case 11:
      case 14:
      case 17:
      case 20:
      case 23:
      case 26:
        execute_order_list({4, 7}, acc);
        break;
      case 35:
        execute_order_list({7, 8}, acc);
        break;
      case 36:
        execute_order_list({0, 11}, acc);
        break;
      case 38:
        execute_order_list({5, 11}, acc);
        break;
      default:
        break;
    }
  }

  void execute_21f02e_gamma_orders(int tick, Accumulator& acc) {
    switch (tick) {
      case 4:
      case 7:
      case 10:
      case 13:
      case 16:
      case 19:
      case 22:
      case 25:
        execute_order_list({12}, acc);
        break;
      case 35:
        execute_order_list({12, 13}, acc);
        break;
      default:
        break;
    }
  }

  void execute_21f02e_post_add_orders(int tick, Accumulator& acc) {
    switch (tick) {
      case 0:
      case 3:
      case 6:
      case 9:
      case 12:
      case 15:
      case 18:
      case 21:
      case 24:
      case 36:
      case 39:
      case 41:
        execute_order_list({27}, acc);
        break;
      case 37:
      case 40:
        execute_order_list({17, 22, 27}, acc);
        break;
      case 1:
      case 4:
      case 7:
      case 10:
      case 13:
      case 16:
      case 19:
      case 22:
      case 25:
      case 35:
        execute_order_list({25, 27}, acc);
        break;
      case 2:
      case 5:
      case 8:
      case 11:
      case 14:
      case 17:
      case 20:
      case 23:
      case 26:
        execute_order_list({22, 26, 29}, acc);
        break;
      case 38:
        execute_order_list({23}, acc);
        break;
      default:
        break;
    }
  }

  void tick_ik1303_21f02e_decoded() {
    static constexpr uint32_t kCommand = 0x21f02e;
    if (tick_index == 0) command = kCommand;

    const int current_tick = tick_index;
    if (current_tick < 27) {
      sync_address = 46;
    } else if (current_tick < 36) {
      sync_address = 96;
    } else {
      if (current_tick == 36) {
        const int raw_sync = (kCommand >> 14) & 0xFF;
        r[37] = raw_sync & 0xF;
        r[40] = raw_sync >> 4;
      }
      sync_address = 95;
    }

    Accumulator acc;
    execute_21f02e_pre_add_orders(current_tick, acc);

    if (key_y == 0) t = 0;

    execute_21f02e_gamma_orders(current_tick, acc);

    int sum = acc.alpha + acc.beta + acc.gamma;
    acc.sigma = sum & 0xF;
    carry = (sum >> 4) & 1;

    execute_21f02e_post_add_orders(current_tick, acc);

    output = memory[current_tick];
    memory[current_tick] = input;
    ++tick_index;
    if (tick_index == 42) tick_index = 0;
  }

  void execute_2067ce_pre_add_orders(int tick, Accumulator& acc) {
    switch (tick) {
      case 1:
      case 4:
      case 7:
      case 10:
      case 13:
      case 16:
      case 19:
      case 22:
      case 25:
        execute_order_list({3}, acc);
        break;
      case 2:
      case 5:
      case 8:
      case 11:
      case 14:
      case 17:
      case 20:
      case 23:
      case 26:
        execute_order_list({0, 7}, acc);
        break;
      case 3:
      case 6:
      case 9:
      case 12:
      case 15:
      case 18:
      case 21:
      case 24:
      case 27:
      case 29:
      case 34:
        execute_order_list({4, 7}, acc);
        break;
      case 28:
        execute_order_list({0, 7, 8}, acc);
        break;
      case 30:
        execute_order_list({5, 8}, acc);
        break;
      case 31:
      case 33:
        execute_order_list({8}, acc);
        break;
      case 32:
      case 35:
        execute_order_list({5}, acc);
        break;
      case 36:
        execute_order_list({0, 11}, acc);
        break;
      case 37:
      case 40:
        if (l == 0)
          execute_order_list({5}, acc);
        else
          execute_order_list({0}, acc);
        break;
      case 38:
        execute_order_list({5, 11}, acc);
        break;
      case 39:
      case 41:
        execute_order_list({0}, acc);
        break;
      default:
        break;
    }
  }

  void execute_2067ce_gamma_orders(int tick, Accumulator& acc) {
    switch (tick) {
      case 2:
        execute_order_list({12, 13}, acc);
        break;
      case 5:
      case 8:
      case 11:
      case 14:
      case 17:
      case 20:
      case 23:
      case 26:
      case 28:
      case 32:
        execute_order_list({12}, acc);
        break;
      default:
        break;
    }
  }

  void execute_2067ce_post_add_orders(int tick, Accumulator& acc) {
    switch (tick) {
      case 1:
      case 4:
      case 7:
      case 10:
      case 13:
      case 16:
      case 19:
      case 22:
      case 25:
      case 28:
      case 30:
      case 31:
      case 32:
      case 33:
      case 36:
      case 39:
      case 41:
        execute_order_list({27}, acc);
        break;
      case 2:
      case 5:
      case 8:
      case 11:
      case 14:
      case 17:
      case 20:
      case 23:
      case 26:
        execute_order_list({25, 27}, acc);
        break;
      case 3:
      case 6:
      case 9:
      case 12:
      case 15:
      case 18:
      case 21:
      case 24:
      case 27:
      case 29:
      case 34:
        execute_order_list({22, 26, 29}, acc);
        break;
      case 35:
        execute_order_list({26, 29}, acc);
        break;
      case 37:
      case 40:
        if (l == 0)
          execute_order_list({22}, acc);
        else
          execute_order_list({17, 22, 27}, acc);
        break;
      case 38:
        execute_order_list({23}, acc);
        break;
      default:
        break;
    }
  }

  void tick_ik1303_2067ce_decoded() {
    static constexpr uint32_t kCommand = 0x2067ce;
    if (tick_index == 0) command = kCommand;

    const int current_tick = tick_index;
    if (current_tick < 27) {
      sync_address = 78;
    } else if (current_tick < 36) {
      sync_address = 79;
    } else {
      if (current_tick == 36) {
        const int raw_sync = (kCommand >> 14) & 0xFF;
        r[37] = raw_sync & 0xF;
        r[40] = raw_sync >> 4;
      }
      sync_address = 95;
    }

    Accumulator acc;
    execute_2067ce_pre_add_orders(current_tick, acc);

    if (key_y == 0) t = 0;

    execute_2067ce_gamma_orders(current_tick, acc);

    int sum = acc.alpha + acc.beta + acc.gamma;
    acc.sigma = sum & 0xF;
    carry = (sum >> 4) & 1;

    execute_2067ce_post_add_orders(current_tick, acc);

    output = memory[current_tick];
    memory[current_tick] = input;
    ++tick_index;
    if (tick_index == 42) tick_index = 0;
  }

  void execute_01a7ce_pre_add_orders(int tick, Accumulator& acc) {
    if (tick < 36) {
      execute_2067ce_pre_add_orders(tick, acc);
      return;
    }

    switch (tick) {
      case 36:
      case 40:
      case 41:
        execute_order_list({0}, acc);
        break;
      case 37:
        execute_order_list({10}, acc);
        break;
      case 38:
        execute_order_list({0, 7}, acc);
        break;
      default:
        break;
    }
  }

  void execute_01a7ce_gamma_orders(int tick, Accumulator& acc) {
    if (tick < 36) {
      execute_2067ce_gamma_orders(tick, acc);
      return;
    }

    switch (tick) {
      case 38:
        execute_order_list({12, 13}, acc);
        break;
      default:
        break;
    }
  }

  void execute_01a7ce_post_add_orders(int tick, Accumulator& acc) {
    if (tick < 36) {
      execute_2067ce_post_add_orders(tick, acc);
      return;
    }

    switch (tick) {
      case 36:
        execute_order_list({17, 27}, acc);
        break;
      case 37:
      case 40:
        execute_order_list({27}, acc);
        break;
      case 38:
        execute_order_list({25, 27}, acc);
        break;
      case 41:
        execute_order_list({29}, acc);
        break;
      default:
        break;
    }
  }

  void tick_ik1303_01a7ce_decoded() {
    static constexpr uint32_t kCommand = 0x01a7ce;
    if (tick_index == 0) command = kCommand;

    const int current_tick = tick_index;
    if (current_tick < 27) {
      sync_address = 78;
    } else if (current_tick < 36) {
      sync_address = 79;
    } else {
      sync_address = 6;
    }

    Accumulator acc;
    execute_01a7ce_pre_add_orders(current_tick, acc);

    if (key_y == 0) t = 0;

    execute_01a7ce_gamma_orders(current_tick, acc);

    int sum = acc.alpha + acc.beta + acc.gamma;
    acc.sigma = sum & 0xF;
    carry = (sum >> 4) & 1;

    execute_01a7ce_post_add_orders(current_tick, acc);

    output = memory[current_tick];
    memory[current_tick] = input;
    ++tick_index;
    if (tick_index == 42) tick_index = 0;
  }

  void execute_6e7a63_pre_add_orders(int tick, Accumulator& acc) {
    switch (tick) {
      case 0:
      case 27:
        execute_order_list({0, 7, 8}, acc);
        break;
      case 1:
      case 24:
        execute_order_list({0, 7}, acc);
        break;
      case 2:
        execute_order_list({5}, acc);
        break;
      case 3:
      case 6:
      case 9:
      case 12:
      case 15:
      case 18:
      case 21:
        execute_order_list({0}, acc);
        break;
      case 5:
      case 8:
      case 11:
      case 14:
      case 17:
      case 20:
      case 23:
        execute_order_list({5, 8}, acc);
        break;
      case 33:
      case 34:
        execute_order_list({9}, acc);
        break;
      case 36:
        execute_order_list({0, 11}, acc);
        break;
      case 37:
      case 40:
        if (l == 0)
          execute_order_list({5}, acc);
        else
          execute_order_list({0}, acc);
        break;
      case 38:
        execute_order_list({5, 11}, acc);
        break;
      case 39:
      case 41:
        execute_order_list({0}, acc);
        break;
      default:
        break;
    }
  }

  void execute_6e7a63_gamma_orders(int tick, Accumulator& acc) {
    switch (tick) {
      case 27:
        execute_order_list({12}, acc);
        break;
      default:
        break;
    }
  }

  void execute_6e7a63_post_add_orders(int tick, Accumulator& acc) {
    switch (tick) {
      case 0:
      case 1:
      case 24:
      case 27:
        execute_order_list({25, 27}, acc);
        break;
      case 3:
      case 6:
      case 9:
      case 12:
      case 15:
      case 18:
      case 21:
        execute_order_list({29}, acc);
        break;
      case 5:
      case 8:
      case 11:
      case 14:
      case 17:
      case 20:
      case 23:
      case 36:
      case 39:
      case 41:
        execute_order_list({27}, acc);
        break;
      case 37:
      case 40:
        if (l == 0)
          execute_order_list({22}, acc);
        else
          execute_order_list({17, 22, 27}, acc);
        break;
      case 38:
        execute_order_list({23}, acc);
        break;
      default:
        break;
    }
  }

  void tick_ik1303_6e7a63_decoded() {
    static constexpr uint32_t kCommand = 0x6e7a63;
    if (tick_index == 0) command = kCommand;

    const int current_tick = tick_index;
    if (current_tick < 27) {
      sync_address = 99;
    } else if (current_tick < 36) {
      sync_address = 116;
    } else {
      if (current_tick == 36) {
        const int raw_sync = (kCommand >> 14) & 0xFF;
        r[37] = raw_sync & 0xF;
        r[40] = raw_sync >> 4;
      }
      sync_address = 95;
    }

    Accumulator acc;
    execute_6e7a63_pre_add_orders(current_tick, acc);

    if (key_y == 0) t = 0;

    execute_6e7a63_gamma_orders(current_tick, acc);

    int sum = acc.alpha + acc.beta + acc.gamma;
    acc.sigma = sum & 0xF;
    carry = (sum >> 4) & 1;

    execute_6e7a63_post_add_orders(current_tick, acc);

    output = memory[current_tick];
    memory[current_tick] = input;
    ++tick_index;
    if (tick_index == 42) tick_index = 0;
  }

  void execute_04052e_pre_add_orders(int tick, Accumulator& acc) {
    switch (tick) {
      case 0:
      case 3:
      case 6:
      case 9:
      case 12:
      case 15:
      case 18:
      case 21:
      case 24:
        execute_order_list({0, 10}, acc);
        break;
      case 1:
      case 4:
      case 7:
      case 10:
      case 13:
      case 16:
      case 19:
      case 22:
      case 25:
        execute_order_list({0, 7}, acc);
        break;
      case 2:
      case 5:
      case 8:
      case 11:
      case 14:
      case 17:
      case 20:
      case 23:
      case 26:
        execute_order_list({4, 7}, acc);
        break;
      case 27:
      case 35:
        execute_order_list({0}, acc);
        break;
      case 28:
        execute_order_list({0, 3, 7}, acc);
        break;
      case 29:
      case 34:
      case 40:
        execute_order_list({5}, acc);
        break;
      case 33:
        execute_order_list({5, 10}, acc);
        break;
      case 36:
        execute_order_list({8}, acc);
        break;
      case 37:
        execute_order_list({6, 7, 8}, acc);
        break;
      case 38:
        execute_order_list({7, 9}, acc);
        break;
      case 39:
        execute_order_list({5, 9}, acc);
        break;
      default:
        break;
    }
  }

  void execute_04052e_gamma_orders(int tick, Accumulator& acc) {
    switch (tick) {
      case 4:
      case 7:
      case 10:
      case 13:
      case 16:
      case 19:
      case 22:
      case 25:
      case 36:
      case 38:
        execute_order_list({12}, acc);
        break;
      case 33:
        execute_order_list({12, 13}, acc);
        break;
      default:
        break;
    }
  }

  void execute_04052e_post_add_orders(int tick, Accumulator& acc) {
    switch (tick) {
      case 0:
      case 3:
      case 6:
      case 9:
      case 12:
      case 15:
      case 18:
      case 21:
      case 24:
      case 27:
      case 35:
      case 37:
        execute_order_list({27}, acc);
        break;
      case 1:
      case 4:
      case 7:
      case 10:
      case 13:
      case 16:
      case 19:
      case 22:
      case 25:
      case 28:
        execute_order_list({25, 27}, acc);
        break;
      case 2:
      case 5:
      case 8:
      case 11:
      case 14:
      case 17:
      case 20:
      case 23:
      case 26:
        execute_order_list({22, 26, 29}, acc);
        break;
      case 29:
        execute_order_list({23}, acc);
        break;
      case 33:
      case 36:
        execute_order_list({25}, acc);
        break;
      case 34:
        execute_order_list({26, 29}, acc);
        break;
      case 38:
        execute_order_list({29}, acc);
        break;
      case 39:
        execute_order_list({25, 27, 29}, acc);
        break;
      case 40:
        execute_order_list({22}, acc);
        break;
      default:
        break;
    }
  }

  void tick_ik1303_04052e_decoded() {
    static constexpr uint32_t kCommand = 0x04052e;
    if (tick_index == 0) command = kCommand;

    const int current_tick = tick_index;
    if (current_tick < 27) {
      sync_address = 46;
    } else if (current_tick < 36) {
      sync_address = 10;
    } else {
      sync_address = 16;
    }

    Accumulator acc;
    execute_04052e_pre_add_orders(current_tick, acc);

    if (key_y == 0) t = 0;

    execute_04052e_gamma_orders(current_tick, acc);

    int sum = acc.alpha + acc.beta + acc.gamma;
    acc.sigma = sum & 0xF;
    carry = (sum >> 4) & 1;

    execute_04052e_post_add_orders(current_tick, acc);

    output = memory[current_tick];
    memory[current_tick] = input;
    ++tick_index;
    if (tick_index == 42) tick_index = 0;
  }

  void execute_6a95ea_pre_add_orders(int tick, Accumulator& acc) {
    switch (tick) {
      case 25:
        execute_order_list({0, 7, 8}, acc);
        break;
      case 28:
      case 29:
      case 31:
      case 32:
      case 34:
      case 35:
      case 39:
      case 41:
        execute_order_list({0}, acc);
        break;
      case 30:
        execute_order_list({5}, acc);
        break;
      case 36:
        execute_order_list({0, 11}, acc);
        break;
      case 37:
      case 40:
        if (l == 0)
          execute_order_list({5}, acc);
        else
          execute_order_list({0}, acc);
        break;
      case 38:
        execute_order_list({5, 11}, acc);
        break;
      default:
        break;
    }
  }

  void execute_6a95ea_post_add_orders(int tick, Accumulator& acc) {
    switch (tick) {
      case 25:
        execute_order_list({25, 27}, acc);
        break;
      case 27:
      case 33:
        execute_order_list({29}, acc);
        break;
      case 30:
        execute_order_list({26, 29}, acc);
        break;
      case 36:
      case 39:
      case 41:
        execute_order_list({27}, acc);
        break;
      case 37:
      case 40:
        if (l == 0)
          execute_order_list({22}, acc);
        else
          execute_order_list({17, 22, 27}, acc);
        break;
      case 38:
        execute_order_list({23}, acc);
        break;
      default:
        break;
    }
  }

  void tick_ik1303_6a95ea_decoded() {
    static constexpr uint32_t kCommand = 0x6a95ea;
    if (tick_index == 0) command = kCommand;

    const int current_tick = tick_index;
    if (current_tick < 27) {
      sync_address = 106;
    } else if (current_tick < 36) {
      sync_address = 43;
    } else {
      if (current_tick == 36) {
        const int raw_sync = (kCommand >> 14) & 0xFF;
        r[37] = raw_sync & 0xF;
        r[40] = raw_sync >> 4;
      }
      sync_address = 95;
    }

    Accumulator acc;
    execute_6a95ea_pre_add_orders(current_tick, acc);

    if (key_y == 0) t = 0;

    int sum = acc.alpha + acc.beta + acc.gamma;
    acc.sigma = sum & 0xF;
    carry = (sum >> 4) & 1;

    execute_6a95ea_post_add_orders(current_tick, acc);

    output = memory[current_tick];
    memory[current_tick] = input;
    ++tick_index;
    if (tick_index == 42) tick_index = 0;
  }

  void execute_01322b_pre_add_orders(int tick, Accumulator& acc) {
    switch (tick) {
      case 1:
      case 2:
      case 4:
      case 5:
      case 7:
      case 8:
      case 10:
      case 11:
      case 13:
      case 14:
      case 16:
      case 17:
      case 19:
      case 20:
      case 22:
      case 23:
      case 25:
      case 26:
      case 30:
      case 35:
      case 41:
        execute_order_list({0}, acc);
        break;
      case 3:
      case 6:
      case 9:
      case 12:
      case 15:
      case 18:
      case 21:
      case 27:
      case 37:
      case 40:
        execute_order_list({5}, acc);
        break;
      case 29:
        execute_order_list({3, 11}, acc);
        break;
      case 31:
      case 32:
        execute_order_list({9}, acc);
        break;
      case 34:
        execute_order_list({7, 8}, acc);
        break;
      case 36:
      case 39:
        execute_order_list({2}, acc);
        break;
      case 38:
        execute_order_list({8}, acc);
        break;
      default:
        break;
    }
  }

  void execute_01322b_gamma_orders(int tick, Accumulator& acc) {
    switch (tick) {
      case 31:
      case 32:
        execute_order_list({13}, acc);
        break;
      case 34:
      case 38:
        execute_order_list({12, 13}, acc);
        break;
      default:
        break;
    }
  }

  void execute_01322b_post_add_orders(int tick, Accumulator& acc) {
    switch (tick) {
      case 0:
      case 24:
      case 30:
      case 31:
      case 32:
        execute_order_list({29}, acc);
        break;
      case 1:
      case 2:
      case 4:
      case 5:
      case 7:
      case 8:
      case 10:
      case 11:
      case 13:
      case 14:
      case 16:
      case 17:
      case 19:
      case 20:
      case 22:
      case 23:
      case 25:
      case 26:
      case 33:
      case 37:
      case 40:
        execute_order_list({22}, acc);
        break;
      case 3:
      case 6:
      case 9:
      case 12:
      case 15:
      case 18:
      case 21:
        execute_order_list({26, 29}, acc);
        break;
      case 29:
      case 34:
        execute_order_list({25, 27}, acc);
        break;
      case 27:
        execute_order_list({23}, acc);
        break;
      case 35:
        execute_order_list({17, 27}, acc);
        break;
      case 41:
        execute_order_list({27}, acc);
        break;
      case 36:
      case 39:
        execute_order_list({27, 33}, acc);
        break;
      case 38:
        execute_order_list({25}, acc);
        break;
      default:
        break;
    }
  }

  void tick_ik1303_01322b_decoded() {
    static constexpr uint32_t kCommand = 0x01322b;
    if (tick_index == 0) command = kCommand;

    const int current_tick = tick_index;
    if (current_tick < 27) {
      sync_address = 43;
    } else if (current_tick < 36) {
      sync_address = 100;
    } else {
      sync_address = 4;
    }

    Accumulator acc;
    execute_01322b_pre_add_orders(current_tick, acc);

    if (key_y == 0) t = 0;

    execute_01322b_gamma_orders(current_tick, acc);

    int sum = acc.alpha + acc.beta + acc.gamma;
    acc.sigma = sum & 0xF;
    carry = (sum >> 4) & 1;

    execute_01322b_post_add_orders(current_tick, acc);

    output = memory[current_tick];
    memory[current_tick] = input;
    ++tick_index;
    if (tick_index == 42) tick_index = 0;
  }

  void execute_6fa822_pre_add_orders(int tick, Accumulator& acc) {
    switch (tick) {
      case 0:
      case 3:
      case 6:
      case 9:
      case 12:
      case 15:
      case 18:
      case 21:
      case 24:
      case 39:
      case 41:
        execute_order_list({0}, acc);
        break;
      case 2:
        execute_order_list({9}, acc);
        break;
      case 26:
        execute_order_list({7, 8}, acc);
        break;
      case 36:
        execute_order_list({0, 11}, acc);
        break;
      case 37:
      case 40:
        if (l == 0)
          execute_order_list({5}, acc);
        else
          execute_order_list({0}, acc);
        break;
      case 38:
        execute_order_list({5, 11}, acc);
        break;
      default:
        break;
    }
  }

  void execute_6fa822_gamma_orders(int tick, Accumulator& acc) {
    switch (tick) {
      case 26:
        execute_order_list({12, 13}, acc);
        break;
      default:
        break;
    }
  }

  void execute_6fa822_post_add_orders(int tick, Accumulator& acc) {
    switch (tick) {
      case 0:
      case 3:
      case 6:
      case 9:
      case 12:
      case 15:
      case 18:
      case 21:
      case 24:
        execute_order_list({32}, acc);
        break;
      case 5:
      case 8:
      case 11:
      case 14:
      case 17:
      case 20:
      case 23:
        execute_order_list({29}, acc);
        break;
      case 26:
        execute_order_list({25, 27}, acc);
        break;
      case 36:
      case 39:
      case 41:
        execute_order_list({27}, acc);
        break;
      case 37:
      case 40:
        if (l == 0)
          execute_order_list({22}, acc);
        else
          execute_order_list({17, 22, 27}, acc);
        break;
      case 38:
        execute_order_list({23}, acc);
        break;
      default:
        break;
    }
  }

  void tick_ik1303_6fa822_decoded() {
    static constexpr uint32_t kCommand = 0x6fa822;
    if (tick_index == 0) command = kCommand;

    const int current_tick = tick_index;
    if (current_tick < 27) {
      sync_address = 34;
    } else if (current_tick < 36) {
      sync_address = 80;
    } else {
      if (current_tick == 36) {
        const int raw_sync = (kCommand >> 14) & 0xFF;
        r[37] = raw_sync & 0xF;
        r[40] = raw_sync >> 4;
      }
      sync_address = 95;
    }

    Accumulator acc;
    execute_6fa822_pre_add_orders(current_tick, acc);

    if (key_y == 0) t = 0;

    execute_6fa822_gamma_orders(current_tick, acc);

    int sum = acc.alpha + acc.beta + acc.gamma;
    acc.sigma = sum & 0xF;
    carry = (sum >> 4) & 1;

    execute_6fa822_post_add_orders(current_tick, acc);

    output = memory[current_tick];
    memory[current_tick] = input;
    ++tick_index;
    if (tick_index == 42) tick_index = 0;
  }

  void execute_44e80c_pre_add_orders(int tick, Accumulator& acc) {
    switch (tick) {
      case 0:
        execute_order_list({0, 11}, acc);
        break;
      case 2:
      case 24:
        execute_order_list({5, 11}, acc);
        break;
      case 3:
      case 6:
      case 9:
      case 12:
      case 15:
      case 18:
      case 21:
        execute_order_list({7, 8}, acc);
        break;
      case 4:
      case 7:
      case 10:
      case 13:
      case 16:
      case 19:
      case 22:
        execute_order_list({1}, acc);
        break;
      case 25:
        execute_order_list({0, 3, 7}, acc);
        break;
      case 36:
        execute_order_list({5, 8}, acc);
        break;
      case 37:
        execute_order_list({5}, acc);
        break;
      case 38:
        execute_order_list({9}, acc);
        break;
      case 39:
        execute_order_list({0, 7}, acc);
        break;
      case 40:
        execute_order_list({0}, acc);
        break;
      default:
        break;
    }
  }

  void execute_44e80c_gamma_orders(int tick, Accumulator& acc) {
    switch (tick) {
      case 3:
      case 6:
      case 9:
      case 12:
      case 15:
      case 18:
      case 21:
        execute_order_list({12, 13}, acc);
        break;
      case 37:
        execute_order_list({12}, acc);
        break;
      default:
        break;
    }
  }

  void execute_44e80c_post_add_orders(int tick, Accumulator& acc) {
    switch (tick) {
      case 0:
      case 4:
      case 7:
      case 10:
      case 13:
      case 16:
      case 19:
      case 22:
      case 36:
      case 37:
      case 40:
        execute_order_list({27}, acc);
        break;
      case 3:
      case 6:
      case 9:
      case 12:
      case 15:
      case 18:
      case 21:
      case 24:
      case 25:
        execute_order_list({25, 27}, acc);
        break;
      case 38:
        execute_order_list({16}, acc);
        break;
      case 39:
        execute_order_list({16, 27}, acc);
        break;
      default:
        break;
    }
  }

  void tick_ik1303_44e80c_decoded() {
    static constexpr uint32_t kCommand = 0x44e80c;
    if (tick_index == 0) command = kCommand;

    const int current_tick = tick_index;
    if (current_tick < 27) {
      sync_address = 12;
    } else if (current_tick < 36) {
      sync_address = 80;
    } else {
      sync_address = 19;
    }

    Accumulator acc;
    execute_44e80c_pre_add_orders(current_tick, acc);

    if (key_y == 0) t = 0;

    execute_44e80c_gamma_orders(current_tick, acc);

    int sum = acc.alpha + acc.beta + acc.gamma;
    acc.sigma = sum & 0xF;
    carry = (sum >> 4) & 1;

    execute_44e80c_post_add_orders(current_tick, acc);

    output = memory[current_tick];
    memory[current_tick] = input;
    ++tick_index;
    if (tick_index == 42) tick_index = 0;
  }

  void execute_2b73ab_pre_add_orders(int tick, Accumulator& acc) {
    switch (tick) {
      case 1:
      case 2:
      case 4:
      case 5:
      case 7:
      case 8:
      case 10:
      case 11:
      case 13:
      case 14:
      case 16:
      case 17:
      case 19:
      case 20:
      case 22:
      case 23:
      case 25:
      case 26:
      case 28:
      case 31:
      case 39:
      case 41:
        execute_order_list({0}, acc);
        break;
      case 3:
      case 6:
      case 9:
      case 12:
      case 15:
      case 18:
      case 21:
      case 35:
        execute_order_list({5}, acc);
        break;
      case 27:
        execute_order_list({10}, acc);
        break;
      case 29:
      case 38:
        execute_order_list({5, 11}, acc);
        break;
      case 30:
        execute_order_list({0, 7}, acc);
        break;
      case 32:
      case 33:
        execute_order_list({0, 7, 8}, acc);
        break;
      case 34:
        execute_order_list({8}, acc);
        break;
      case 36:
        execute_order_list({0, 11}, acc);
        break;
      case 37:
      case 40:
        if (l == 0)
          execute_order_list({5}, acc);
        else
          execute_order_list({0}, acc);
        break;
      default:
        break;
    }
  }

  void execute_2b73ab_gamma_orders(int tick, Accumulator& acc) {
    switch (tick) {
      case 30:
      case 34:
        execute_order_list({12, 13}, acc);
        break;
      default:
        break;
    }
  }

  void execute_2b73ab_post_add_orders(int tick, Accumulator& acc) {
    switch (tick) {
      case 0:
      case 24:
        execute_order_list({29}, acc);
        break;
      case 1:
      case 2:
      case 4:
      case 5:
      case 7:
      case 8:
      case 10:
      case 11:
      case 13:
      case 14:
      case 16:
      case 17:
      case 19:
      case 20:
      case 22:
      case 23:
      case 25:
      case 26:
      case 28:
      case 35:
        execute_order_list({22}, acc);
        break;
      case 37:
      case 40:
        if (l == 0)
          execute_order_list({22}, acc);
        else
          execute_order_list({17, 22, 27}, acc);
        break;
      case 3:
      case 6:
      case 9:
      case 12:
      case 15:
      case 18:
      case 21:
        execute_order_list({26, 29}, acc);
        break;
      case 27:
      case 36:
      case 39:
      case 41:
        execute_order_list({27}, acc);
        break;
      case 29:
      case 30:
        execute_order_list({25, 27}, acc);
        break;
      case 31:
        execute_order_list({17, 27}, acc);
        break;
      case 32:
      case 33:
        execute_order_list({16, 25, 27}, acc);
        break;
      case 34:
        execute_order_list({25}, acc);
        break;
      case 38:
        execute_order_list({23}, acc);
        break;
      default:
        break;
    }
  }

  void tick_ik1303_2b73ab_decoded() {
    static constexpr uint32_t kCommand = 0x2b73ab;
    if (tick_index == 0) command = kCommand;

    const int current_tick = tick_index;
    if (current_tick < 27) {
      sync_address = 43;
    } else if (current_tick < 36) {
      sync_address = 103;
    } else {
      if (current_tick == 36) {
        const int raw_sync = (kCommand >> 14) & 0xFF;
        r[37] = raw_sync & 0xF;
        r[40] = raw_sync >> 4;
      }
      sync_address = 95;
    }

    Accumulator acc;
    execute_2b73ab_pre_add_orders(current_tick, acc);

    if (key_y == 0) t = 0;

    execute_2b73ab_gamma_orders(current_tick, acc);

    int sum = acc.alpha + acc.beta + acc.gamma;
    acc.sigma = sum & 0xF;
    carry = (sum >> 4) & 1;

    execute_2b73ab_post_add_orders(current_tick, acc);

    output = memory[current_tick];
    memory[current_tick] = input;
    ++tick_index;
    if (tick_index == 42) tick_index = 0;
  }

  void execute_203047_pre_add_orders(int tick, Accumulator& acc) {
    switch (tick) {
      case 1:
        execute_order_list({3, 11}, acc);
        break;
      case 2:
      case 5:
      case 8:
      case 11:
      case 14:
      case 17:
      case 20:
      case 23:
      case 26:
        execute_order_list({4, 7}, acc);
        break;
      case 3:
      case 6:
      case 9:
      case 12:
      case 15:
      case 18:
      case 21:
      case 24:
        execute_order_list({5}, acc);
        break;
      case 4:
      case 7:
      case 10:
      case 13:
      case 16:
      case 19:
      case 22:
      case 25:
        execute_order_list({3}, acc);
        break;
      case 35:
        execute_order_list({7, 8}, acc);
        break;
      case 36:
        execute_order_list({0, 11}, acc);
        break;
      case 37:
      case 40:
        if (l == 0)
          execute_order_list({5}, acc);
        else
          execute_order_list({0}, acc);
        break;
      case 38:
        execute_order_list({5, 11}, acc);
        break;
      case 39:
      case 41:
        execute_order_list({0}, acc);
        break;
      default:
        break;
    }
  }

  void execute_203047_gamma_orders(int tick, Accumulator& acc) {
    switch (tick) {
      case 4:
      case 7:
      case 10:
      case 13:
      case 16:
      case 19:
      case 22:
      case 25:
        execute_order_list({12}, acc);
        break;
      case 35:
        execute_order_list({12, 13}, acc);
        break;
      default:
        break;
    }
  }

  void execute_203047_post_add_orders(int tick, Accumulator& acc) {
    switch (tick) {
      case 1:
      case 4:
      case 7:
      case 10:
      case 13:
      case 16:
      case 19:
      case 22:
      case 25:
      case 35:
        execute_order_list({25, 27}, acc);
        break;
      case 2:
      case 5:
      case 8:
      case 11:
      case 14:
      case 17:
      case 20:
      case 23:
      case 26:
        execute_order_list({22, 26, 29}, acc);
        break;
      case 3:
      case 6:
      case 9:
      case 12:
      case 15:
      case 18:
      case 21:
      case 24:
        execute_order_list({26, 29}, acc);
        break;
      case 36:
      case 39:
      case 41:
        execute_order_list({27}, acc);
        break;
      case 37:
      case 40:
        if (l == 0)
          execute_order_list({22}, acc);
        else
          execute_order_list({17, 22, 27}, acc);
        break;
      case 38:
        execute_order_list({23}, acc);
        break;
      default:
        break;
    }
  }

  void tick_ik1303_203047_decoded() {
    static constexpr uint32_t kCommand = 0x203047;
    if (tick_index == 0) command = kCommand;

    const int current_tick = tick_index;
    if (current_tick < 27) {
      sync_address = 71;
    } else if (current_tick < 36) {
      sync_address = 96;
    } else {
      if (current_tick == 36) {
        const int raw_sync = (kCommand >> 14) & 0xFF;
        r[37] = raw_sync & 0xF;
        r[40] = raw_sync >> 4;
      }
      sync_address = 95;
    }

    Accumulator acc;
    execute_203047_pre_add_orders(current_tick, acc);

    if (key_y == 0) t = 0;

    execute_203047_gamma_orders(current_tick, acc);

    int sum = acc.alpha + acc.beta + acc.gamma;
    acc.sigma = sum & 0xF;
    carry = (sum >> 4) & 1;

    execute_203047_post_add_orders(current_tick, acc);

    output = memory[current_tick];
    memory[current_tick] = input;
    ++tick_index;
    if (tick_index == 42) tick_index = 0;
  }

  void execute_05dd62_pre_add_orders(int tick, Accumulator& acc) {
    switch (tick) {
      case 2:
      case 5:
      case 8:
      case 11:
      case 14:
      case 17:
      case 20:
      case 23:
      case 26:
        execute_order_list({9}, acc);
        break;
      case 27:
        execute_order_list({8}, acc);
        break;
      case 28:
        execute_order_list({5, 7}, acc);
        break;
      case 29:
        execute_order_list({5, 10}, acc);
        break;
      case 30:
        execute_order_list({5, 8}, acc);
        break;
      case 31:
      case 33:
        execute_order_list({5}, acc);
        break;
      case 32:
      case 39:
        execute_order_list({5, 11}, acc);
        break;
      case 36:
        execute_order_list({0, 11}, acc);
        break;
      case 34:
        execute_order_list({0}, acc);
        break;
      case 37:
        execute_order_list({5}, acc);
        break;
      case 38:
        execute_order_list({6, 7, 8}, acc);
        break;
      case 40:
        execute_order_list({3, 7}, acc);
        break;
      default:
        break;
    }
  }

  void execute_05dd62_gamma_orders(int tick, Accumulator& acc) {
    switch (tick) {
      case 27:
        execute_order_list({12, 13}, acc);
        break;
      case 28:
      case 31:
      case 37:
        execute_order_list({12}, acc);
        break;
      case 29:
      case 40:
        execute_order_list({12, 13}, acc);
        break;
      default:
        break;
    }
  }

  void execute_05dd62_post_add_orders(int tick, Accumulator& acc) {
    switch (tick) {
      case 1:
      case 35:
        execute_order_list({29}, acc);
        break;
      case 2:
      case 5:
      case 8:
      case 11:
      case 14:
      case 17:
      case 20:
      case 23:
      case 26:
        execute_order_list({16}, acc);
        break;
      case 27:
      case 29:
      case 40:
        execute_order_list({25}, acc);
        break;
      case 30:
      case 31:
      case 36:
      case 38:
        execute_order_list({27}, acc);
        break;
      case 28:
      case 32:
      case 39:
        execute_order_list({25, 27}, acc);
        break;
      case 33:
      case 34:
        execute_order_list({22}, acc);
        break;
      case 37:
        execute_order_list({22, 27}, acc);
        break;
      default:
        break;
    }
  }

  void tick_ik1303_05dd62_decoded() {
    static constexpr uint32_t kCommand = 0x05dd62;
    if (tick_index == 0) command = kCommand;

    const int current_tick = tick_index;
    if (current_tick < 27) {
      sync_address = 98;
    } else if (current_tick < 36) {
      sync_address = 58;
    } else {
      sync_address = 23;
    }

    Accumulator acc;
    execute_05dd62_pre_add_orders(current_tick, acc);

    if (key_y == 0) t = 0;

    execute_05dd62_gamma_orders(current_tick, acc);

    int sum = acc.alpha + acc.beta + acc.gamma;
    acc.sigma = sum & 0xF;
    carry = (sum >> 4) & 1;

    execute_05dd62_post_add_orders(current_tick, acc);

    output = memory[current_tick];
    memory[current_tick] = input;
    ++tick_index;
    if (tick_index == 42) tick_index = 0;
  }

  void execute_0331ab_pre_add_orders(int tick, Accumulator& acc) {
    switch (tick) {
      case 1:
      case 2:
      case 4:
      case 5:
      case 7:
      case 8:
      case 10:
      case 11:
      case 13:
      case 14:
      case 16:
      case 17:
      case 19:
      case 20:
      case 22:
      case 23:
      case 25:
      case 26:
      case 30:
        execute_order_list({0}, acc);
        break;
      case 3:
      case 6:
      case 9:
      case 12:
      case 15:
      case 18:
      case 21:
      case 29:
        execute_order_list({5}, acc);
        break;
      case 27:
        execute_order_list({0, 7, 8}, acc);
        break;
      case 28:
      case 33:
        execute_order_list({0, 7}, acc);
        break;
      case 32:
        execute_order_list({5, 8}, acc);
        break;
      case 36:
        execute_order_list({0, 11}, acc);
        break;
      case 38:
        execute_order_list({5, 11}, acc);
        break;
      case 39:
        execute_order_list({7, 8}, acc);
        break;
      case 40:
        execute_order_list({1}, acc);
        break;
      default:
        break;
    }
  }

  void execute_0331ab_gamma_orders(int tick, Accumulator& acc) {
    switch (tick) {
      case 39:
        execute_order_list({12, 13}, acc);
        break;
      default:
        break;
    }
  }

  void execute_0331ab_post_add_orders(int tick, Accumulator& acc) {
    switch (tick) {
      case 0:
      case 24:
      case 30:
        execute_order_list({29}, acc);
        break;
      case 1:
      case 2:
      case 4:
      case 5:
      case 7:
      case 8:
      case 10:
      case 11:
      case 13:
      case 14:
      case 16:
      case 17:
      case 19:
      case 20:
      case 22:
      case 23:
      case 25:
      case 26:
      case 29:
        execute_order_list({22}, acc);
        break;
      case 3:
      case 6:
      case 9:
      case 12:
      case 15:
      case 18:
      case 21:
        execute_order_list({26, 29}, acc);
        break;
      case 27:
        execute_order_list({16, 25, 27}, acc);
        break;
      case 28:
      case 33:
      case 39:
        execute_order_list({25, 27}, acc);
        break;
      case 32:
      case 36:
      case 40:
        execute_order_list({27}, acc);
        break;
      case 38:
        execute_order_list({23}, acc);
        break;
      default:
        break;
    }
  }

  void tick_ik1303_0331ab_decoded() {
    static constexpr uint32_t kCommand = 0x0331ab;
    if (tick_index == 0) command = kCommand;

    const int current_tick = tick_index;
    if (current_tick < 27) {
      sync_address = 43;
    } else if (current_tick < 36) {
      sync_address = 99;
    } else {
      sync_address = 12;
    }

    Accumulator acc;
    execute_0331ab_pre_add_orders(current_tick, acc);

    if (key_y == 0) t = 0;

    execute_0331ab_gamma_orders(current_tick, acc);

    int sum = acc.alpha + acc.beta + acc.gamma;
    acc.sigma = sum & 0xF;
    carry = (sum >> 4) & 1;

    execute_0331ab_post_add_orders(current_tick, acc);

    output = memory[current_tick];
    memory[current_tick] = input;
    ++tick_index;
    if (tick_index == 42) tick_index = 0;
  }

  void execute_02b0ae_pre_add_orders(int tick, Accumulator& acc) {
    switch (tick) {
      case 0:
      case 3:
      case 6:
      case 9:
      case 12:
      case 15:
      case 18:
      case 21:
      case 24:
        execute_order_list({0, 10}, acc);
        break;
      case 1:
      case 4:
      case 7:
      case 10:
      case 13:
      case 16:
      case 19:
      case 22:
      case 25:
      case 28:
        execute_order_list({0, 7}, acc);
        break;
      case 2:
      case 5:
      case 8:
      case 11:
      case 14:
      case 17:
      case 20:
      case 23:
      case 26:
      case 29:
        execute_order_list({4, 7}, acc);
        break;
      case 27:
        execute_order_list({10}, acc);
        break;
      case 30:
        execute_order_list({5, 8}, acc);
        break;
      case 31:
        execute_order_list({0, 7}, acc);
        break;
      case 32:
      case 37:
        execute_order_list({0, 3, 7}, acc);
        break;
      case 35:
        execute_order_list({6, 11}, acc);
        break;
      case 36:
        execute_order_list({0}, acc);
        break;
      case 38:
        execute_order_list({5}, acc);
        break;
      default:
        break;
    }
  }

  void execute_02b0ae_gamma_orders(int tick, Accumulator& acc) {
    switch (tick) {
      case 4:
      case 7:
      case 10:
      case 13:
      case 16:
      case 19:
      case 22:
      case 25:
      case 28:
        execute_order_list({12}, acc);
        break;
      default:
        break;
    }
  }

  void execute_02b0ae_post_add_orders(int tick, Accumulator& acc) {
    switch (tick) {
      case 0:
      case 3:
      case 6:
      case 9:
      case 12:
      case 15:
      case 18:
      case 21:
      case 24:
      case 27:
      case 30:
      case 35:
      case 36:
        execute_order_list({27}, acc);
        break;
      case 31:
        execute_order_list({16, 27}, acc);
        break;
      case 1:
      case 4:
      case 7:
      case 10:
      case 13:
      case 16:
      case 19:
      case 22:
      case 25:
      case 28:
      case 32:
      case 37:
        execute_order_list({25, 27}, acc);
        break;
      case 2:
      case 5:
      case 8:
      case 11:
      case 14:
      case 17:
      case 20:
      case 23:
      case 26:
      case 29:
        execute_order_list({22, 26, 29}, acc);
        break;
      case 34:
        execute_order_list({29}, acc);
        break;
      case 38:
        execute_order_list({23}, acc);
        break;
      default:
        break;
    }
  }

  void tick_ik1303_02b0ae_decoded() {
    static constexpr uint32_t kCommand = 0x02b0ae;
    if (tick_index == 0) command = kCommand;

    const int current_tick = tick_index;
    if (current_tick < 27) {
      sync_address = 46;
    } else if (current_tick < 36) {
      sync_address = 97;
    } else {
      sync_address = 10;
    }

    Accumulator acc;
    execute_02b0ae_pre_add_orders(current_tick, acc);

    if (key_y == 0) t = 0;

    execute_02b0ae_gamma_orders(current_tick, acc);

    int sum = acc.alpha + acc.beta + acc.gamma;
    acc.sigma = sum & 0xF;
    carry = (sum >> 4) & 1;

    execute_02b0ae_post_add_orders(current_tick, acc);

    output = memory[current_tick];
    memory[current_tick] = input;
    ++tick_index;
    if (tick_index == 42) tick_index = 0;
  }

  void execute_02af04_pre_add_orders(int tick, Accumulator& acc) {
    switch (tick) {
      case 0:
      case 3:
      case 6:
      case 9:
      case 12:
      case 15:
      case 18:
      case 21:
      case 24:
        execute_order_list({2}, acc);
        break;
      case 1:
      case 4:
      case 7:
      case 10:
      case 13:
      case 16:
      case 19:
      case 22:
      case 25:
      case 27:
      case 29:
      case 35:
      case 38:
        execute_order_list({5}, acc);
        break;
      case 2:
        execute_order_list({8}, acc);
        break;
      case 5:
      case 8:
      case 11:
      case 14:
      case 17:
      case 20:
      case 23:
      case 31:
      case 33:
      case 36:
        execute_order_list({0}, acc);
        break;
      case 37:
        execute_order_list({0, 3, 7}, acc);
        break;
      case 26:
        execute_order_list({7, 8}, acc);
        break;
      case 34:
        execute_order_list({9}, acc);
        break;
      default:
        break;
    }
  }

  void execute_02af04_gamma_orders(int tick, Accumulator& acc) {
    switch (tick) {
      case 2:
      case 26:
        execute_order_list({12, 13}, acc);
        break;
      case 27:
      case 29:
        execute_order_list({12}, acc);
        break;
      default:
        break;
    }
  }

  void execute_02af04_post_add_orders(int tick, Accumulator& acc) {
    switch (tick) {
      case 0:
      case 3:
      case 6:
      case 9:
      case 12:
      case 15:
      case 18:
      case 21:
      case 24:
        execute_order_list({27, 33}, acc);
        break;
      case 1:
      case 4:
      case 7:
      case 10:
      case 13:
      case 16:
      case 19:
      case 22:
      case 25:
      case 28:
        execute_order_list({22}, acc);
        break;
      case 2:
        execute_order_list({25}, acc);
        break;
      case 5:
      case 8:
      case 11:
      case 14:
      case 17:
      case 20:
      case 23:
      case 27:
      case 29:
      case 36:
        execute_order_list({27}, acc);
        break;
      case 31:
        execute_order_list({17, 27}, acc);
        break;
      case 26:
      case 37:
        execute_order_list({25, 27}, acc);
        break;
      case 33:
        execute_order_list({29}, acc);
        break;
      case 34:
        execute_order_list({16}, acc);
        break;
      case 35:
        execute_order_list({26, 29}, acc);
        break;
      case 38:
        execute_order_list({23}, acc);
        break;
      default:
        break;
    }
  }

  void tick_ik1303_02af04_decoded() {
    static constexpr uint32_t kCommand = 0x02af04;
    if (tick_index == 0) command = kCommand;

    const int current_tick = tick_index;
    if (current_tick < 27) {
      sync_address = 4;
    } else if (current_tick < 36) {
      sync_address = 94;
    } else {
      sync_address = 10;
    }

    Accumulator acc;
    execute_02af04_pre_add_orders(current_tick, acc);

    if (key_y == 0) t = 0;

    execute_02af04_gamma_orders(current_tick, acc);

    int sum = acc.alpha + acc.beta + acc.gamma;
    acc.sigma = sum & 0xF;
    carry = (sum >> 4) & 1;

    execute_02af04_post_add_orders(current_tick, acc);

    output = memory[current_tick];
    memory[current_tick] = input;
    ++tick_index;
    if (tick_index == 42) tick_index = 0;
  }

  void execute_029ba0_pre_add_orders(int tick, Accumulator& acc) {
    switch (tick) {
      case 0:
      case 1:
      case 3:
      case 4:
      case 6:
      case 7:
      case 9:
      case 10:
      case 12:
      case 13:
      case 15:
      case 16:
      case 18:
      case 19:
      case 21:
      case 22:
      case 24:
      case 25:
      case 30:
      case 36:
        execute_order_list({0}, acc);
        break;
      case 29:
        execute_order_list({0, 7}, acc);
        break;
      case 2:
      case 26:
        execute_order_list({0, 3, 9}, acc);
        break;
      case 27:
        execute_order_list({5, 10, 11}, acc);
        break;
      case 32:
      case 33:
      case 34:
      case 38:
        execute_order_list({5}, acc);
        break;
      case 35:
        execute_order_list({7, 8}, acc);
        break;
      case 37:
        execute_order_list({0, 3, 7}, acc);
        break;
      default:
        break;
    }
  }

  void execute_029ba0_gamma_orders(int tick, Accumulator& acc) {
    switch (tick) {
      case 27:
      case 35:
        execute_order_list({12, 13}, acc);
        break;
      case 32:
      case 33:
        execute_order_list({12}, acc);
        break;
      default:
        break;
    }
  }

  void execute_029ba0_post_add_orders(int tick, Accumulator& acc) {
    switch (tick) {
      case 0:
      case 3:
      case 6:
      case 9:
      case 12:
      case 15:
      case 18:
      case 21:
      case 24:
      case 30:
      case 36:
        execute_order_list({27}, acc);
        break;
      case 1:
      case 4:
      case 7:
      case 10:
      case 13:
      case 16:
      case 19:
      case 22:
      case 25:
        execute_order_list({17, 22}, acc);
        break;
      case 2:
      case 26:
        execute_order_list({25, 29}, acc);
        break;
      case 27:
      case 29:
      case 35:
      case 37:
        execute_order_list({25, 27}, acc);
        break;
      case 28:
      case 31:
        execute_order_list({15}, acc);
        break;
      case 32:
      case 33:
        execute_order_list({27}, acc);
        break;
      case 34:
        execute_order_list({26, 29}, acc);
        break;
      case 38:
        execute_order_list({23}, acc);
        break;
      default:
        break;
    }
  }

  void tick_ik1303_029ba0_decoded() {
    static constexpr uint32_t kCommand = 0x029ba0;
    if (tick_index == 0) command = kCommand;

    const int current_tick = tick_index;
    if (current_tick < 27) {
      sync_address = 32;
    } else if (current_tick < 36) {
      sync_address = 55;
    } else {
      sync_address = 10;
    }

    Accumulator acc;
    execute_029ba0_pre_add_orders(current_tick, acc);

    if (key_y == 0) t = 0;

    execute_029ba0_gamma_orders(current_tick, acc);

    int sum = acc.alpha + acc.beta + acc.gamma;
    acc.sigma = sum & 0xF;
    carry = (sum >> 4) & 1;

    execute_029ba0_post_add_orders(current_tick, acc);

    output = memory[current_tick];
    memory[current_tick] = input;
    ++tick_index;
    if (tick_index == 42) tick_index = 0;
  }

  void execute_027fa6_pre_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 0:
        execute_order_list({7, 8}, acc);
        break;
      case 1:
        execute_order_list({0}, acc);
        break;
      case 2:
        execute_order_list({0}, acc);
        break;
      case 3:
        execute_order_list({7, 8}, acc);
        break;
      case 4:
        execute_order_list({0}, acc);
        break;
      case 5:
        execute_order_list({0}, acc);
        break;
      case 6:
        execute_order_list({7, 8}, acc);
        break;
      case 7:
        execute_order_list({0}, acc);
        break;
      case 8:
        execute_order_list({0}, acc);
        break;
      case 9:
        execute_order_list({7, 8}, acc);
        break;
      case 10:
        execute_order_list({0}, acc);
        break;
      case 11:
        execute_order_list({0}, acc);
        break;
      case 12:
        execute_order_list({7, 8}, acc);
        break;
      case 13:
        execute_order_list({0}, acc);
        break;
      case 14:
        execute_order_list({0}, acc);
        break;
      case 15:
        execute_order_list({7, 8}, acc);
        break;
      case 16:
        execute_order_list({0}, acc);
        break;
      case 17:
        execute_order_list({0}, acc);
        break;
      case 18:
        execute_order_list({7, 8}, acc);
        break;
      case 19:
        execute_order_list({0}, acc);
        break;
      case 20:
        execute_order_list({0}, acc);
        break;
      case 21:
        execute_order_list({7, 8}, acc);
        break;
      case 22:
        execute_order_list({0}, acc);
        break;
      case 23:
        execute_order_list({0}, acc);
        break;
      case 24:
        execute_order_list({7, 8}, acc);
        break;
      case 25:
        execute_order_list({0}, acc);
        break;
      case 27:
        execute_order_list({5}, acc);
        break;
      case 28:
        execute_order_list({5}, acc);
        break;
      case 29:
        execute_order_list({5}, acc);
        break;
      case 32:
        execute_order_list({6, 7, 8}, acc);
        break;
      case 33:
        execute_order_list({8}, acc);
        break;
      case 34:
        execute_order_list({5}, acc);
        break;
      case 35:
        execute_order_list({5}, acc);
        break;
      case 36:
        execute_order_list({0, 11}, acc);
        break;
      case 37:
        execute_order_list({5}, acc);
        break;
      case 38:
        execute_order_list({3, 11}, acc);
        break;
      case 39:
        execute_order_list({0}, acc);
        break;
      case 40:
        execute_order_list({9}, acc);
        break;
      case 41:
        execute_order_list({7, 8}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void execute_027fa6_gamma_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 0:
        execute_order_list({12, 13}, acc);
        break;
      case 3:
        execute_order_list({12, 13}, acc);
        break;
      case 6:
        execute_order_list({12, 13}, acc);
        break;
      case 9:
        execute_order_list({12, 13}, acc);
        break;
      case 12:
        execute_order_list({12, 13}, acc);
        break;
      case 15:
        execute_order_list({12, 13}, acc);
        break;
      case 18:
        execute_order_list({12, 13}, acc);
        break;
      case 21:
        execute_order_list({12, 13}, acc);
        break;
      case 24:
        execute_order_list({12, 13}, acc);
        break;
      case 34:
        execute_order_list({12}, acc);
        break;
      case 35:
        execute_order_list({12}, acc);
        break;
      case 40:
        execute_order_list({13}, acc);
        break;
      case 41:
        execute_order_list({12, 13}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void execute_027fa6_post_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 0:
        execute_order_list({25, 27}, acc);
        break;
      case 1:
        execute_order_list({17, 27}, acc);
        break;
      case 2:
        execute_order_list({17, 27}, acc);
        break;
      case 3:
        execute_order_list({25, 27}, acc);
        break;
      case 4:
        execute_order_list({17, 27}, acc);
        break;
      case 5:
        execute_order_list({17, 27}, acc);
        break;
      case 6:
        execute_order_list({25, 27}, acc);
        break;
      case 7:
        execute_order_list({17, 27}, acc);
        break;
      case 8:
        execute_order_list({17, 27}, acc);
        break;
      case 9:
        execute_order_list({25, 27}, acc);
        break;
      case 10:
        execute_order_list({17, 27}, acc);
        break;
      case 11:
        execute_order_list({17, 27}, acc);
        break;
      case 12:
        execute_order_list({25, 27}, acc);
        break;
      case 13:
        execute_order_list({17, 27}, acc);
        break;
      case 14:
        execute_order_list({17, 27}, acc);
        break;
      case 15:
        execute_order_list({25, 27}, acc);
        break;
      case 16:
        execute_order_list({17, 27}, acc);
        break;
      case 17:
        execute_order_list({17, 27}, acc);
        break;
      case 18:
        execute_order_list({25, 27}, acc);
        break;
      case 19:
        execute_order_list({17, 27}, acc);
        break;
      case 20:
        execute_order_list({17, 27}, acc);
        break;
      case 21:
        execute_order_list({25, 27}, acc);
        break;
      case 22:
        execute_order_list({17, 27}, acc);
        break;
      case 23:
        execute_order_list({17, 27}, acc);
        break;
      case 24:
        execute_order_list({25, 27}, acc);
        break;
      case 25:
        execute_order_list({29}, acc);
        break;
      case 26:
        execute_order_list({22}, acc);
        break;
      case 27:
        execute_order_list({26, 29}, acc);
        break;
      case 28:
        execute_order_list({23}, acc);
        break;
      case 29:
        execute_order_list({23}, acc);
        break;
      case 32:
        execute_order_list({27}, acc);
        break;
      case 33:
        execute_order_list({27}, acc);
        break;
      case 34:
        execute_order_list({27}, acc);
        break;
      case 35:
        execute_order_list({27}, acc);
        break;
      case 36:
        execute_order_list({27}, acc);
        break;
      case 37:
        execute_order_list({22}, acc);
        break;
      case 38:
        execute_order_list({25, 27}, acc);
        break;
      case 39:
        execute_order_list({29}, acc);
        break;
      case 40:
        execute_order_list({29}, acc);
        break;
      case 41:
        execute_order_list({25, 27}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void tick_ik1303_027fa6_decoded() {
    static constexpr uint32_t kCommand = 0x027fa6;
    if (tick_index == 0) command = kCommand;

    const int current_tick = tick_index;
    const bool branch_l = l != 0;
    if (current_tick < 27) {
      sync_address = 38;
    } else if (current_tick < 36) {
      sync_address = 127;
    } else {
      sync_address = 9;
    }

    Accumulator acc;
    execute_027fa6_pre_add_orders(current_tick, branch_l, acc);

    if (key_y == 0) t = 0;

    execute_027fa6_gamma_orders(current_tick, branch_l, acc);

    int sum = acc.alpha + acc.beta + acc.gamma;
    acc.sigma = sum & 0xF;
    carry = (sum >> 4) & 1;

    execute_027fa6_post_add_orders(current_tick, branch_l, acc);

    output = memory[current_tick];
    memory[current_tick] = input;
    ++tick_index;
    if (tick_index == 42) tick_index = 0;
  }


  void execute_16dfbe_pre_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 0:
        execute_order_list({6, 7, 8}, acc);
        break;
      case 1:
        execute_order_list({0, 3, 7}, acc);
        break;
      case 2:
        execute_order_list({5}, acc);
        break;
      case 3:
        execute_order_list({6, 7, 8}, acc);
        break;
      case 4:
        execute_order_list({5}, acc);
        break;
      case 6:
        execute_order_list({6, 7, 8}, acc);
        break;
      case 7:
        execute_order_list({5}, acc);
        break;
      case 9:
        execute_order_list({6, 7, 8}, acc);
        break;
      case 10:
        execute_order_list({5}, acc);
        break;
      case 12:
        execute_order_list({6, 7, 8}, acc);
        break;
      case 13:
        execute_order_list({5}, acc);
        break;
      case 15:
        execute_order_list({6, 7, 8}, acc);
        break;
      case 16:
        execute_order_list({5}, acc);
        break;
      case 18:
        execute_order_list({6, 7, 8}, acc);
        break;
      case 19:
        execute_order_list({5}, acc);
        break;
      case 21:
        execute_order_list({6, 7, 8}, acc);
        break;
      case 22:
        execute_order_list({5}, acc);
        break;
      case 24:
        execute_order_list({10}, acc);
        break;
      case 25:
        execute_order_list({5}, acc);
        break;
      case 27:
        execute_order_list({10}, acc);
        break;
      case 28:
        execute_order_list({8}, acc);
        break;
      case 29:
        execute_order_list({5}, acc);
        break;
      case 31:
        execute_order_list({10}, acc);
        break;
      case 32:
        execute_order_list({5, 11}, acc);
        break;
      case 33:
        execute_order_list({5}, acc);
        break;
      case 35:
        execute_order_list({0}, acc);
        break;
      case 36:
        execute_order_list({0, 11}, acc);
        break;
      case 37:
        if (branch_l) {
          execute_order_list({0}, acc);
        } else {
          execute_order_list({5}, acc);
        }
        break;
      case 38:
        execute_order_list({5, 11}, acc);
        break;
      case 39:
        execute_order_list({0}, acc);
        break;
      case 40:
        if (branch_l) {
          execute_order_list({0}, acc);
        } else {
          execute_order_list({5}, acc);
        }
        break;
      case 41:
        execute_order_list({0}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void execute_16dfbe_gamma_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      default:
        break;
    }
    (void)branch_l;
  }

  void execute_16dfbe_post_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 0:
        execute_order_list({27}, acc);
        break;
      case 1:
        execute_order_list({25, 27}, acc);
        break;
      case 2:
        execute_order_list({26, 29}, acc);
        break;
      case 3:
        execute_order_list({27}, acc);
        break;
      case 4:
        execute_order_list({22}, acc);
        break;
      case 6:
        execute_order_list({27}, acc);
        break;
      case 7:
        execute_order_list({22}, acc);
        break;
      case 9:
        execute_order_list({27}, acc);
        break;
      case 10:
        execute_order_list({22}, acc);
        break;
      case 12:
        execute_order_list({27}, acc);
        break;
      case 13:
        execute_order_list({22}, acc);
        break;
      case 15:
        execute_order_list({27}, acc);
        break;
      case 16:
        execute_order_list({22}, acc);
        break;
      case 18:
        execute_order_list({27}, acc);
        break;
      case 19:
        execute_order_list({22}, acc);
        break;
      case 21:
        execute_order_list({27}, acc);
        break;
      case 22:
        execute_order_list({22}, acc);
        break;
      case 24:
        execute_order_list({27}, acc);
        break;
      case 25:
        execute_order_list({22}, acc);
        break;
      case 27:
        execute_order_list({27}, acc);
        break;
      case 28:
        execute_order_list({27}, acc);
        break;
      case 29:
        execute_order_list({23}, acc);
        break;
      case 31:
        execute_order_list({27}, acc);
        break;
      case 32:
        execute_order_list({23}, acc);
        break;
      case 33:
        execute_order_list({26, 29}, acc);
        break;
      case 34:
        execute_order_list({22}, acc);
        break;
      case 35:
        execute_order_list({17, 27}, acc);
        break;
      case 36:
        execute_order_list({27}, acc);
        break;
      case 37:
        if (branch_l) {
          execute_order_list({17, 22, 27}, acc);
        } else {
          execute_order_list({22}, acc);
        }
        break;
      case 38:
        execute_order_list({23}, acc);
        break;
      case 39:
        execute_order_list({27}, acc);
        break;
      case 40:
        if (branch_l) {
          execute_order_list({17, 22, 27}, acc);
        } else {
          execute_order_list({22}, acc);
        }
        break;
      case 41:
        execute_order_list({27}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void tick_ik1303_16dfbe_decoded() {
    static constexpr uint32_t kCommand = 0x16dfbe;
    if (tick_index == 0) command = kCommand;

    const int current_tick = tick_index;
    const bool branch_l = l != 0;
    if (current_tick < 27) {
      sync_address = 62;
    } else if (current_tick < 36) {
      sync_address = 63;
    } else {
      if (current_tick == 36) {
        r[37] = 91 & 0xF;
        r[40] = 91 >> 4;
      }
      sync_address = 95;
    }

    Accumulator acc;
    execute_16dfbe_pre_add_orders(current_tick, branch_l, acc);

    if (key_y == 0) t = 0;

    execute_16dfbe_gamma_orders(current_tick, branch_l, acc);

    int sum = acc.alpha + acc.beta + acc.gamma;
    acc.sigma = sum & 0xF;
    carry = (sum >> 4) & 1;

    execute_16dfbe_post_add_orders(current_tick, branch_l, acc);

    output = memory[current_tick];
    memory[current_tick] = input;
    ++tick_index;
    if (tick_index == 42) tick_index = 0;
  }


  void execute_03ecf0_pre_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 1:
        execute_order_list({7, 8}, acc);
        break;
      case 3:
        if (branch_l) {
          execute_order_list({5}, acc);
        } else {
          execute_order_list({0, 7}, acc);
        }
        break;
      case 6:
        if (branch_l) {
          execute_order_list({5}, acc);
        } else {
          execute_order_list({0, 7}, acc);
        }
        break;
      case 9:
        if (branch_l) {
          execute_order_list({5}, acc);
        } else {
          execute_order_list({0, 7}, acc);
        }
        break;
      case 12:
        if (branch_l) {
          execute_order_list({5}, acc);
        } else {
          execute_order_list({0, 7}, acc);
        }
        break;
      case 15:
        if (branch_l) {
          execute_order_list({5}, acc);
        } else {
          execute_order_list({0, 7}, acc);
        }
        break;
      case 18:
        if (branch_l) {
          execute_order_list({5}, acc);
        } else {
          execute_order_list({0, 7}, acc);
        }
        break;
      case 21:
        if (branch_l) {
          execute_order_list({5}, acc);
        } else {
          execute_order_list({0, 7}, acc);
        }
        break;
      case 24:
        execute_order_list({0, 11}, acc);
        break;
      case 25:
        execute_order_list({7, 8}, acc);
        break;
      case 26:
        execute_order_list({5}, acc);
        break;
      case 27:
        execute_order_list({0}, acc);
        break;
      case 28:
        execute_order_list({3}, acc);
        break;
      case 29:
        execute_order_list({5}, acc);
        break;
      case 30:
        execute_order_list({6, 11}, acc);
        break;
      case 31:
        execute_order_list({5, 11}, acc);
        break;
      case 32:
        execute_order_list({9}, acc);
        break;
      case 34:
        execute_order_list({9}, acc);
        break;
      case 35:
        execute_order_list({8}, acc);
        break;
      case 36:
        execute_order_list({0}, acc);
        break;
      case 37:
        execute_order_list({7, 8}, acc);
        break;
      case 38:
        execute_order_list({5, 11}, acc);
        break;
      case 39:
        execute_order_list({0}, acc);
        break;
      case 41:
        execute_order_list({0}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void execute_03ecf0_gamma_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 1:
        execute_order_list({12, 13}, acc);
        break;
      case 25:
        execute_order_list({12, 13}, acc);
        break;
      case 26:
        execute_order_list({12}, acc);
        break;
      case 34:
        execute_order_list({13}, acc);
        break;
      case 37:
        execute_order_list({12, 13}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void execute_03ecf0_post_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 1:
        execute_order_list({25, 27}, acc);
        break;
      case 3:
        if (branch_l) {
          execute_order_list({32}, acc);
        } else {
          execute_order_list({25, 26}, acc);
        }
        break;
      case 6:
        if (branch_l) {
          execute_order_list({32}, acc);
        } else {
          execute_order_list({25, 26}, acc);
        }
        break;
      case 9:
        if (branch_l) {
          execute_order_list({32}, acc);
        } else {
          execute_order_list({25, 26}, acc);
        }
        break;
      case 12:
        if (branch_l) {
          execute_order_list({32}, acc);
        } else {
          execute_order_list({25, 26}, acc);
        }
        break;
      case 15:
        if (branch_l) {
          execute_order_list({32}, acc);
        } else {
          execute_order_list({25, 26}, acc);
        }
        break;
      case 18:
        if (branch_l) {
          execute_order_list({32}, acc);
        } else {
          execute_order_list({25, 26}, acc);
        }
        break;
      case 21:
        if (branch_l) {
          execute_order_list({32}, acc);
        } else {
          execute_order_list({25, 26}, acc);
        }
        break;
      case 24:
        execute_order_list({17, 32}, acc);
        break;
      case 25:
        execute_order_list({25, 27}, acc);
        break;
      case 26:
        execute_order_list({27}, acc);
        break;
      case 27:
        execute_order_list({17, 27}, acc);
        break;
      case 28:
        execute_order_list({27}, acc);
        break;
      case 29:
        execute_order_list({26, 29}, acc);
        break;
      case 30:
        execute_order_list({27}, acc);
        break;
      case 31:
        execute_order_list({25, 27}, acc);
        break;
      case 32:
        execute_order_list({16}, acc);
        break;
      case 33:
        execute_order_list({29}, acc);
        break;
      case 34:
        execute_order_list({29}, acc);
        break;
      case 35:
        execute_order_list({27}, acc);
        break;
      case 36:
        execute_order_list({17, 27}, acc);
        break;
      case 37:
        execute_order_list({25, 27}, acc);
        break;
      case 38:
        execute_order_list({25, 27}, acc);
        break;
      case 39:
        execute_order_list({17, 27}, acc);
        break;
      case 41:
        execute_order_list({27}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void tick_ik1303_03ecf0_decoded() {
    static constexpr uint32_t kCommand = 0x03ecf0;
    if (tick_index == 0) command = kCommand;

    const int current_tick = tick_index;
    const bool branch_l = l != 0;
    if (current_tick < 27) {
      sync_address = 112;
    } else if (current_tick < 36) {
      sync_address = 89;
    } else {
      sync_address = 15;
    }

    Accumulator acc;
    execute_03ecf0_pre_add_orders(current_tick, branch_l, acc);

    if (key_y == 0) t = 0;

    execute_03ecf0_gamma_orders(current_tick, branch_l, acc);

    int sum = acc.alpha + acc.beta + acc.gamma;
    acc.sigma = sum & 0xF;
    carry = (sum >> 4) & 1;

    execute_03ecf0_post_add_orders(current_tick, branch_l, acc);

    output = memory[current_tick];
    memory[current_tick] = input;
    ++tick_index;
    if (tick_index == 42) tick_index = 0;
  }


  void execute_6ed122_pre_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 0:
        execute_order_list({0}, acc);
        break;
      case 2:
        execute_order_list({9}, acc);
        break;
      case 3:
        execute_order_list({0}, acc);
        break;
      case 6:
        execute_order_list({0}, acc);
        break;
      case 9:
        execute_order_list({0}, acc);
        break;
      case 12:
        execute_order_list({0}, acc);
        break;
      case 15:
        execute_order_list({0}, acc);
        break;
      case 18:
        execute_order_list({0}, acc);
        break;
      case 21:
        execute_order_list({0}, acc);
        break;
      case 24:
        execute_order_list({0}, acc);
        break;
      case 26:
        execute_order_list({7, 8}, acc);
        break;
      case 27:
        execute_order_list({0}, acc);
        break;
      case 29:
        execute_order_list({9}, acc);
        break;
      case 30:
        execute_order_list({0}, acc);
        break;
      case 33:
        execute_order_list({0}, acc);
        break;
      case 35:
        execute_order_list({7, 8}, acc);
        break;
      case 36:
        execute_order_list({0, 11}, acc);
        break;
      case 37:
        if (branch_l) {
          execute_order_list({0}, acc);
        } else {
          execute_order_list({5}, acc);
        }
        break;
      case 38:
        execute_order_list({5, 11}, acc);
        break;
      case 39:
        execute_order_list({0}, acc);
        break;
      case 40:
        if (branch_l) {
          execute_order_list({0}, acc);
        } else {
          execute_order_list({5}, acc);
        }
        break;
      case 41:
        execute_order_list({0}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void execute_6ed122_gamma_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 26:
        execute_order_list({12, 13}, acc);
        break;
      case 35:
        execute_order_list({12, 13}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void execute_6ed122_post_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 0:
        execute_order_list({32}, acc);
        break;
      case 3:
        execute_order_list({32}, acc);
        break;
      case 5:
        execute_order_list({29}, acc);
        break;
      case 6:
        execute_order_list({32}, acc);
        break;
      case 8:
        execute_order_list({29}, acc);
        break;
      case 9:
        execute_order_list({32}, acc);
        break;
      case 11:
        execute_order_list({29}, acc);
        break;
      case 12:
        execute_order_list({32}, acc);
        break;
      case 14:
        execute_order_list({29}, acc);
        break;
      case 15:
        execute_order_list({32}, acc);
        break;
      case 17:
        execute_order_list({29}, acc);
        break;
      case 18:
        execute_order_list({32}, acc);
        break;
      case 20:
        execute_order_list({29}, acc);
        break;
      case 21:
        execute_order_list({32}, acc);
        break;
      case 23:
        execute_order_list({29}, acc);
        break;
      case 24:
        execute_order_list({32}, acc);
        break;
      case 26:
        execute_order_list({25, 27}, acc);
        break;
      case 27:
        execute_order_list({32}, acc);
        break;
      case 30:
        execute_order_list({32}, acc);
        break;
      case 32:
        execute_order_list({29}, acc);
        break;
      case 33:
        execute_order_list({32}, acc);
        break;
      case 35:
        execute_order_list({25, 27}, acc);
        break;
      case 36:
        execute_order_list({27}, acc);
        break;
      case 37:
        if (branch_l) {
          execute_order_list({17, 22, 27}, acc);
        } else {
          execute_order_list({22}, acc);
        }
        break;
      case 38:
        execute_order_list({23}, acc);
        break;
      case 39:
        execute_order_list({27}, acc);
        break;
      case 40:
        if (branch_l) {
          execute_order_list({17, 22, 27}, acc);
        } else {
          execute_order_list({22}, acc);
        }
        break;
      case 41:
        execute_order_list({27}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void tick_ik1303_6ed122_decoded() {
    static constexpr uint32_t kCommand = 0x6ed122;
    if (tick_index == 0) command = kCommand;

    const int current_tick = tick_index;
    const bool branch_l = l != 0;
    if (current_tick < 27) {
      sync_address = 34;
    } else if (current_tick < 36) {
      sync_address = 34;
    } else {
      if (current_tick == 36) {
        r[37] = 11 & 0xF;
        r[40] = 187 >> 4;
      }
      sync_address = 95;
    }

    Accumulator acc;
    execute_6ed122_pre_add_orders(current_tick, branch_l, acc);

    if (key_y == 0) t = 0;

    execute_6ed122_gamma_orders(current_tick, branch_l, acc);

    int sum = acc.alpha + acc.beta + acc.gamma;
    acc.sigma = sum & 0xF;
    carry = (sum >> 4) & 1;

    execute_6ed122_post_add_orders(current_tick, branch_l, acc);

    output = memory[current_tick];
    memory[current_tick] = input;
    ++tick_index;
    if (tick_index == 42) tick_index = 0;
  }


  void execute_43e947_pre_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 1:
        execute_order_list({3, 11}, acc);
        break;
      case 2:
        execute_order_list({4, 7}, acc);
        break;
      case 3:
        execute_order_list({5}, acc);
        break;
      case 4:
        execute_order_list({3}, acc);
        break;
      case 5:
        execute_order_list({4, 7}, acc);
        break;
      case 6:
        execute_order_list({5}, acc);
        break;
      case 7:
        execute_order_list({3}, acc);
        break;
      case 8:
        execute_order_list({4, 7}, acc);
        break;
      case 9:
        execute_order_list({5}, acc);
        break;
      case 10:
        execute_order_list({3}, acc);
        break;
      case 11:
        execute_order_list({4, 7}, acc);
        break;
      case 12:
        execute_order_list({5}, acc);
        break;
      case 13:
        execute_order_list({3}, acc);
        break;
      case 14:
        execute_order_list({4, 7}, acc);
        break;
      case 15:
        execute_order_list({5}, acc);
        break;
      case 16:
        execute_order_list({3}, acc);
        break;
      case 17:
        execute_order_list({4, 7}, acc);
        break;
      case 18:
        execute_order_list({5}, acc);
        break;
      case 19:
        execute_order_list({3}, acc);
        break;
      case 20:
        execute_order_list({4, 7}, acc);
        break;
      case 21:
        execute_order_list({5}, acc);
        break;
      case 22:
        execute_order_list({3}, acc);
        break;
      case 23:
        execute_order_list({4, 7}, acc);
        break;
      case 24:
        execute_order_list({5}, acc);
        break;
      case 25:
        execute_order_list({3}, acc);
        break;
      case 26:
        execute_order_list({4, 7}, acc);
        break;
      case 27:
        execute_order_list({4, 7}, acc);
        break;
      case 28:
        execute_order_list({6, 7, 8}, acc);
        break;
      case 29:
        execute_order_list({5}, acc);
        break;
      case 30:
        execute_order_list({5}, acc);
        break;
      case 31:
        execute_order_list({0, 3, 7}, acc);
        break;
      case 32:
        execute_order_list({5}, acc);
        break;
      case 33:
        execute_order_list({6, 7, 8}, acc);
        break;
      case 34:
        execute_order_list({5}, acc);
        break;
      case 36:
        execute_order_list({0}, acc);
        break;
      case 37:
        execute_order_list({7, 8}, acc);
        break;
      case 38:
        execute_order_list({5, 11}, acc);
        break;
      case 39:
        execute_order_list({0}, acc);
        break;
      case 41:
        execute_order_list({0}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void execute_43e947_gamma_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 4:
        execute_order_list({12}, acc);
        break;
      case 7:
        execute_order_list({12}, acc);
        break;
      case 10:
        execute_order_list({12}, acc);
        break;
      case 13:
        execute_order_list({12}, acc);
        break;
      case 16:
        execute_order_list({12}, acc);
        break;
      case 19:
        execute_order_list({12}, acc);
        break;
      case 22:
        execute_order_list({12}, acc);
        break;
      case 25:
        execute_order_list({12}, acc);
        break;
      case 29:
        execute_order_list({12}, acc);
        break;
      case 30:
        execute_order_list({12}, acc);
        break;
      case 37:
        execute_order_list({12, 13}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void execute_43e947_post_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 1:
        execute_order_list({25, 27}, acc);
        break;
      case 2:
        execute_order_list({26, 29}, acc);
        break;
      case 3:
        execute_order_list({26, 29}, acc);
        break;
      case 4:
        execute_order_list({25, 27}, acc);
        break;
      case 5:
        execute_order_list({26, 29}, acc);
        break;
      case 6:
        execute_order_list({26, 29}, acc);
        break;
      case 7:
        execute_order_list({25, 27}, acc);
        break;
      case 8:
        execute_order_list({26, 29}, acc);
        break;
      case 9:
        execute_order_list({26, 29}, acc);
        break;
      case 10:
        execute_order_list({25, 27}, acc);
        break;
      case 11:
        execute_order_list({26, 29}, acc);
        break;
      case 12:
        execute_order_list({26, 29}, acc);
        break;
      case 13:
        execute_order_list({25, 27}, acc);
        break;
      case 14:
        execute_order_list({26, 29}, acc);
        break;
      case 15:
        execute_order_list({26, 29}, acc);
        break;
      case 16:
        execute_order_list({25, 27}, acc);
        break;
      case 17:
        execute_order_list({26, 29}, acc);
        break;
      case 18:
        execute_order_list({26, 29}, acc);
        break;
      case 19:
        execute_order_list({25, 27}, acc);
        break;
      case 20:
        execute_order_list({26, 29}, acc);
        break;
      case 21:
        execute_order_list({26, 29}, acc);
        break;
      case 22:
        execute_order_list({25, 27}, acc);
        break;
      case 23:
        execute_order_list({26, 29}, acc);
        break;
      case 24:
        execute_order_list({26, 29}, acc);
        break;
      case 25:
        execute_order_list({25, 27}, acc);
        break;
      case 26:
        execute_order_list({26, 29}, acc);
        break;
      case 27:
        execute_order_list({26, 29}, acc);
        break;
      case 28:
        execute_order_list({27}, acc);
        break;
      case 29:
        execute_order_list({27}, acc);
        break;
      case 30:
        execute_order_list({27}, acc);
        break;
      case 31:
        execute_order_list({25, 27}, acc);
        break;
      case 32:
        execute_order_list({26, 29}, acc);
        break;
      case 33:
        execute_order_list({27}, acc);
        break;
      case 34:
        execute_order_list({26, 29}, acc);
        break;
      case 36:
        execute_order_list({17, 27}, acc);
        break;
      case 37:
        execute_order_list({25, 27}, acc);
        break;
      case 38:
        execute_order_list({25, 27}, acc);
        break;
      case 39:
        execute_order_list({17, 27}, acc);
        break;
      case 41:
        execute_order_list({27}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void tick_ik1303_43e947_decoded() {
    static constexpr uint32_t kCommand = 0x43e947;
    if (tick_index == 0) command = kCommand;

    const int current_tick = tick_index;
    const bool branch_l = l != 0;
    if (current_tick < 27) {
      sync_address = 71;
    } else if (current_tick < 36) {
      sync_address = 82;
    } else {
      sync_address = 15;
    }

    Accumulator acc;
    execute_43e947_pre_add_orders(current_tick, branch_l, acc);

    if (key_y == 0) t = 0;

    execute_43e947_gamma_orders(current_tick, branch_l, acc);

    int sum = acc.alpha + acc.beta + acc.gamma;
    acc.sigma = sum & 0xF;
    carry = (sum >> 4) & 1;

    execute_43e947_post_add_orders(current_tick, branch_l, acc);

    output = memory[current_tick];
    memory[current_tick] = input;
    ++tick_index;
    if (tick_index == 42) tick_index = 0;
  }


  void execute_0612a5_pre_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 0:
        execute_order_list({6, 11}, acc);
        break;
      case 3:
        execute_order_list({9}, acc);
        break;
      case 4:
        execute_order_list({0, 3, 7}, acc);
        break;
      case 5:
        execute_order_list({9}, acc);
        break;
      case 6:
        execute_order_list({9}, acc);
        break;
      case 7:
        execute_order_list({0, 3, 7}, acc);
        break;
      case 8:
        execute_order_list({9}, acc);
        break;
      case 9:
        execute_order_list({9}, acc);
        break;
      case 10:
        execute_order_list({0, 3, 7}, acc);
        break;
      case 11:
        execute_order_list({9}, acc);
        break;
      case 12:
        execute_order_list({9}, acc);
        break;
      case 13:
        execute_order_list({0, 3, 7}, acc);
        break;
      case 14:
        execute_order_list({9}, acc);
        break;
      case 15:
        execute_order_list({9}, acc);
        break;
      case 16:
        execute_order_list({0, 3, 7}, acc);
        break;
      case 17:
        execute_order_list({9}, acc);
        break;
      case 18:
        execute_order_list({9}, acc);
        break;
      case 19:
        execute_order_list({0, 3, 7}, acc);
        break;
      case 20:
        execute_order_list({9}, acc);
        break;
      case 21:
        execute_order_list({9}, acc);
        break;
      case 22:
        execute_order_list({0, 3, 7}, acc);
        break;
      case 23:
        execute_order_list({9}, acc);
        break;
      case 24:
        execute_order_list({0, 3, 9}, acc);
        break;
      case 26:
        execute_order_list({5, 8}, acc);
        break;
      case 27:
        execute_order_list({6, 11}, acc);
        break;
      case 30:
        execute_order_list({9}, acc);
        break;
      case 31:
        execute_order_list({0, 3, 7}, acc);
        break;
      case 32:
        execute_order_list({9}, acc);
        break;
      case 33:
        execute_order_list({0, 3, 9}, acc);
        break;
      case 35:
        execute_order_list({5, 8}, acc);
        break;
      case 36:
        execute_order_list({0, 11}, acc);
        break;
      case 37:
        execute_order_list({6, 7, 8}, acc);
        break;
      case 38:
        execute_order_list({5}, acc);
        break;
      case 39:
        execute_order_list({0}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void execute_0612a5_gamma_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 5:
        execute_order_list({13}, acc);
        break;
      case 8:
        execute_order_list({13}, acc);
        break;
      case 11:
        execute_order_list({13}, acc);
        break;
      case 14:
        execute_order_list({13}, acc);
        break;
      case 17:
        execute_order_list({13}, acc);
        break;
      case 20:
        execute_order_list({13}, acc);
        break;
      case 23:
        execute_order_list({13}, acc);
        break;
      case 32:
        execute_order_list({13}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void execute_0612a5_post_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 0:
        execute_order_list({27}, acc);
        break;
      case 1:
        execute_order_list({22}, acc);
        break;
      case 2:
        execute_order_list({29}, acc);
        break;
      case 3:
        execute_order_list({16}, acc);
        break;
      case 4:
        execute_order_list({25, 27}, acc);
        break;
      case 5:
        execute_order_list({29}, acc);
        break;
      case 6:
        execute_order_list({16}, acc);
        break;
      case 7:
        execute_order_list({25, 27}, acc);
        break;
      case 8:
        execute_order_list({29}, acc);
        break;
      case 9:
        execute_order_list({16}, acc);
        break;
      case 10:
        execute_order_list({25, 27}, acc);
        break;
      case 11:
        execute_order_list({29}, acc);
        break;
      case 12:
        execute_order_list({16}, acc);
        break;
      case 13:
        execute_order_list({25, 27}, acc);
        break;
      case 14:
        execute_order_list({29}, acc);
        break;
      case 15:
        execute_order_list({16}, acc);
        break;
      case 16:
        execute_order_list({25, 27}, acc);
        break;
      case 17:
        execute_order_list({29}, acc);
        break;
      case 18:
        execute_order_list({16}, acc);
        break;
      case 19:
        execute_order_list({25, 27}, acc);
        break;
      case 20:
        execute_order_list({29}, acc);
        break;
      case 21:
        execute_order_list({16}, acc);
        break;
      case 22:
        execute_order_list({25, 27}, acc);
        break;
      case 23:
        execute_order_list({29}, acc);
        break;
      case 24:
        execute_order_list({25, 29}, acc);
        break;
      case 25:
        execute_order_list({22}, acc);
        break;
      case 26:
        execute_order_list({27}, acc);
        break;
      case 27:
        execute_order_list({27}, acc);
        break;
      case 28:
        execute_order_list({22}, acc);
        break;
      case 29:
        execute_order_list({29}, acc);
        break;
      case 30:
        execute_order_list({16}, acc);
        break;
      case 31:
        execute_order_list({25, 27}, acc);
        break;
      case 32:
        execute_order_list({29}, acc);
        break;
      case 33:
        execute_order_list({25, 29}, acc);
        break;
      case 34:
        execute_order_list({22}, acc);
        break;
      case 35:
        execute_order_list({27}, acc);
        break;
      case 36:
        execute_order_list({17, 32}, acc);
        break;
      case 37:
        execute_order_list({27}, acc);
        break;
      case 38:
        execute_order_list({23}, acc);
        break;
      case 39:
        execute_order_list({17, 32}, acc);
        break;
      case 40:
        execute_order_list({29}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void tick_ik1303_0612a5_decoded() {
    static constexpr uint32_t kCommand = 0x0612a5;
    if (tick_index == 0) command = kCommand;

    const int current_tick = tick_index;
    const bool branch_l = l != 0;
    if (current_tick < 27) {
      sync_address = 37;
    } else if (current_tick < 36) {
      sync_address = 37;
    } else {
      sync_address = 24;
    }

    Accumulator acc;
    execute_0612a5_pre_add_orders(current_tick, branch_l, acc);

    if (key_y == 0) t = 0;

    execute_0612a5_gamma_orders(current_tick, branch_l, acc);

    int sum = acc.alpha + acc.beta + acc.gamma;
    acc.sigma = sum & 0xF;
    carry = (sum >> 4) & 1;

    execute_0612a5_post_add_orders(current_tick, branch_l, acc);

    output = memory[current_tick];
    memory[current_tick] = input;
    ++tick_index;
    if (tick_index == 42) tick_index = 0;
  }


  void execute_020285_pre_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 0:
        execute_order_list({7, 8}, acc);
        break;
      case 1:
        execute_order_list({0}, acc);
        break;
      case 4:
        execute_order_list({0}, acc);
        break;
      case 5:
        execute_order_list({0}, acc);
        break;
      case 7:
        execute_order_list({0}, acc);
        break;
      case 8:
        execute_order_list({0}, acc);
        break;
      case 10:
        execute_order_list({0}, acc);
        break;
      case 11:
        execute_order_list({0}, acc);
        break;
      case 13:
        execute_order_list({0}, acc);
        break;
      case 14:
        execute_order_list({0}, acc);
        break;
      case 16:
        execute_order_list({0}, acc);
        break;
      case 17:
        execute_order_list({0}, acc);
        break;
      case 19:
        execute_order_list({0}, acc);
        break;
      case 20:
        execute_order_list({0}, acc);
        break;
      case 22:
        execute_order_list({0}, acc);
        break;
      case 23:
        execute_order_list({0}, acc);
        break;
      case 25:
        execute_order_list({0}, acc);
        break;
      case 26:
        execute_order_list({5, 8}, acc);
        break;
      case 27:
        execute_order_list({7, 8}, acc);
        break;
      case 28:
        execute_order_list({0}, acc);
        break;
      case 31:
        execute_order_list({0}, acc);
        break;
      case 32:
        execute_order_list({0}, acc);
        break;
      case 34:
        execute_order_list({0}, acc);
        break;
      case 35:
        execute_order_list({5, 8}, acc);
        break;
      case 36:
        execute_order_list({0, 11}, acc);
        break;
      case 37:
        execute_order_list({5}, acc);
        break;
      case 38:
        execute_order_list({5}, acc);
        break;
      case 39:
        execute_order_list({0}, acc);
        break;
      case 40:
        execute_order_list({5}, acc);
        break;
      case 41:
        execute_order_list({6, 7, 8}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void execute_020285_gamma_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 0:
        execute_order_list({12, 13}, acc);
        break;
      case 27:
        execute_order_list({12, 13}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void execute_020285_post_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 0:
        execute_order_list({25, 27}, acc);
        break;
      case 1:
        execute_order_list({22}, acc);
        break;
      case 4:
        execute_order_list({22}, acc);
        break;
      case 5:
        execute_order_list({27}, acc);
        break;
      case 7:
        execute_order_list({22}, acc);
        break;
      case 8:
        execute_order_list({27}, acc);
        break;
      case 10:
        execute_order_list({22}, acc);
        break;
      case 11:
        execute_order_list({27}, acc);
        break;
      case 13:
        execute_order_list({22}, acc);
        break;
      case 14:
        execute_order_list({27}, acc);
        break;
      case 16:
        execute_order_list({22}, acc);
        break;
      case 17:
        execute_order_list({27}, acc);
        break;
      case 19:
        execute_order_list({22}, acc);
        break;
      case 20:
        execute_order_list({27}, acc);
        break;
      case 22:
        execute_order_list({22}, acc);
        break;
      case 23:
        execute_order_list({27}, acc);
        break;
      case 24:
        execute_order_list({29}, acc);
        break;
      case 25:
        execute_order_list({22}, acc);
        break;
      case 26:
        execute_order_list({27}, acc);
        break;
      case 27:
        execute_order_list({25, 27}, acc);
        break;
      case 28:
        execute_order_list({22}, acc);
        break;
      case 31:
        execute_order_list({22}, acc);
        break;
      case 32:
        execute_order_list({27}, acc);
        break;
      case 33:
        execute_order_list({29}, acc);
        break;
      case 34:
        execute_order_list({22}, acc);
        break;
      case 35:
        execute_order_list({27}, acc);
        break;
      case 36:
        execute_order_list({17, 32}, acc);
        break;
      case 37:
        execute_order_list({22}, acc);
        break;
      case 38:
        execute_order_list({26, 29}, acc);
        break;
      case 39:
        execute_order_list({17, 32}, acc);
        break;
      case 40:
        execute_order_list({22}, acc);
        break;
      case 41:
        execute_order_list({27}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void tick_ik1303_020285_decoded() {
    static constexpr uint32_t kCommand = 0x020285;
    if (tick_index == 0) command = kCommand;

    const int current_tick = tick_index;
    const bool branch_l = l != 0;
    if (current_tick < 27) {
      sync_address = 5;
    } else if (current_tick < 36) {
      sync_address = 5;
    } else {
      sync_address = 8;
    }

    Accumulator acc;
    execute_020285_pre_add_orders(current_tick, branch_l, acc);

    if (key_y == 0) t = 0;

    execute_020285_gamma_orders(current_tick, branch_l, acc);

    int sum = acc.alpha + acc.beta + acc.gamma;
    acc.sigma = sum & 0xF;
    carry = (sum >> 4) & 1;

    execute_020285_post_add_orders(current_tick, branch_l, acc);

    output = memory[current_tick];
    memory[current_tick] = input;
    ++tick_index;
    if (tick_index == 42) tick_index = 0;
  }


  void execute_03821e_pre_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 0:
        execute_order_list({8}, acc);
        break;
      case 1:
        execute_order_list({5}, acc);
        break;
      case 2:
        execute_order_list({5}, acc);
        break;
      case 4:
        execute_order_list({5}, acc);
        break;
      case 5:
        execute_order_list({4, 7, 8}, acc);
        break;
      case 7:
        execute_order_list({5}, acc);
        break;
      case 8:
        execute_order_list({4, 7, 8}, acc);
        break;
      case 10:
        execute_order_list({5}, acc);
        break;
      case 11:
        execute_order_list({4, 7, 8}, acc);
        break;
      case 13:
        execute_order_list({5}, acc);
        break;
      case 14:
        execute_order_list({4, 7, 8}, acc);
        break;
      case 16:
        execute_order_list({5}, acc);
        break;
      case 17:
        execute_order_list({4, 7, 8}, acc);
        break;
      case 19:
        execute_order_list({5}, acc);
        break;
      case 20:
        execute_order_list({4, 7, 8}, acc);
        break;
      case 22:
        execute_order_list({5}, acc);
        break;
      case 23:
        execute_order_list({4, 7, 8}, acc);
        break;
      case 24:
        execute_order_list({5, 9}, acc);
        break;
      case 26:
        execute_order_list({7, 8}, acc);
        break;
      case 27:
        execute_order_list({2}, acc);
        break;
      case 28:
        execute_order_list({5}, acc);
        break;
      case 29:
        execute_order_list({8}, acc);
        break;
      case 30:
        execute_order_list({2}, acc);
        break;
      case 31:
        execute_order_list({5}, acc);
        break;
      case 32:
        execute_order_list({0}, acc);
        break;
      case 33:
        execute_order_list({2}, acc);
        break;
      case 34:
        execute_order_list({5}, acc);
        break;
      case 35:
        execute_order_list({7, 8}, acc);
        break;
      case 36:
        execute_order_list({5, 9}, acc);
        break;
      case 37:
        execute_order_list({5}, acc);
        break;
      case 38:
        execute_order_list({5, 7}, acc);
        break;
      case 39:
        execute_order_list({9}, acc);
        break;
      case 40:
        execute_order_list({5, 8}, acc);
        break;
      case 41:
        execute_order_list({5, 9}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void execute_03821e_gamma_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 0:
        execute_order_list({12, 13}, acc);
        break;
      case 5:
        execute_order_list({12}, acc);
        break;
      case 8:
        execute_order_list({12}, acc);
        break;
      case 11:
        execute_order_list({12}, acc);
        break;
      case 14:
        execute_order_list({12}, acc);
        break;
      case 17:
        execute_order_list({12}, acc);
        break;
      case 20:
        execute_order_list({12}, acc);
        break;
      case 23:
        execute_order_list({12}, acc);
        break;
      case 26:
        execute_order_list({12, 13}, acc);
        break;
      case 29:
        execute_order_list({12, 13}, acc);
        break;
      case 35:
        execute_order_list({12, 13}, acc);
        break;
      case 38:
        execute_order_list({12}, acc);
        break;
      case 39:
        execute_order_list({13}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void execute_03821e_post_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 0:
        execute_order_list({25}, acc);
        break;
      case 1:
        execute_order_list({22}, acc);
        break;
      case 2:
        execute_order_list({26, 29}, acc);
        break;
      case 4:
        execute_order_list({22}, acc);
        break;
      case 5:
        execute_order_list({27}, acc);
        break;
      case 7:
        execute_order_list({22}, acc);
        break;
      case 8:
        execute_order_list({27}, acc);
        break;
      case 10:
        execute_order_list({22}, acc);
        break;
      case 11:
        execute_order_list({27}, acc);
        break;
      case 13:
        execute_order_list({22}, acc);
        break;
      case 14:
        execute_order_list({27}, acc);
        break;
      case 16:
        execute_order_list({22}, acc);
        break;
      case 17:
        execute_order_list({27}, acc);
        break;
      case 19:
        execute_order_list({22}, acc);
        break;
      case 20:
        execute_order_list({27}, acc);
        break;
      case 22:
        execute_order_list({22}, acc);
        break;
      case 23:
        execute_order_list({27}, acc);
        break;
      case 24:
        execute_order_list({25, 27, 29}, acc);
        break;
      case 25:
        execute_order_list({22}, acc);
        break;
      case 26:
        execute_order_list({25, 27}, acc);
        break;
      case 27:
        execute_order_list({27, 33}, acc);
        break;
      case 28:
        execute_order_list({22}, acc);
        break;
      case 29:
        execute_order_list({25}, acc);
        break;
      case 30:
        execute_order_list({27, 33}, acc);
        break;
      case 31:
        execute_order_list({22}, acc);
        break;
      case 32:
        execute_order_list({27}, acc);
        break;
      case 33:
        execute_order_list({27, 33}, acc);
        break;
      case 34:
        execute_order_list({22}, acc);
        break;
      case 35:
        execute_order_list({25, 27}, acc);
        break;
      case 36:
        execute_order_list({25, 27, 29}, acc);
        break;
      case 37:
        execute_order_list({22}, acc);
        break;
      case 38:
        execute_order_list({25, 27}, acc);
        break;
      case 39:
        execute_order_list({29}, acc);
        break;
      case 40:
        execute_order_list({27}, acc);
        break;
      case 41:
        execute_order_list({25, 27, 29}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void tick_ik1303_03821e_decoded() {
    static constexpr uint32_t kCommand = 0x03821e;
    if (tick_index == 0) command = kCommand;

    const int current_tick = tick_index;
    const bool branch_l = l != 0;
    if (current_tick < 27) {
      sync_address = 30;
    } else if (current_tick < 36) {
      sync_address = 4;
    } else {
      sync_address = 14;
    }

    Accumulator acc;
    execute_03821e_pre_add_orders(current_tick, branch_l, acc);

    if (key_y == 0) t = 0;

    execute_03821e_gamma_orders(current_tick, branch_l, acc);

    int sum = acc.alpha + acc.beta + acc.gamma;
    acc.sigma = sum & 0xF;
    carry = (sum >> 4) & 1;

    execute_03821e_post_add_orders(current_tick, branch_l, acc);

    output = memory[current_tick];
    memory[current_tick] = input;
    ++tick_index;
    if (tick_index == 42) tick_index = 0;
  }


  void execute_1ff225_pre_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 0:
        execute_order_list({6, 11}, acc);
        break;
      case 3:
        execute_order_list({9}, acc);
        break;
      case 4:
        execute_order_list({0, 3, 7}, acc);
        break;
      case 5:
        execute_order_list({9}, acc);
        break;
      case 6:
        execute_order_list({9}, acc);
        break;
      case 7:
        execute_order_list({0, 3, 7}, acc);
        break;
      case 8:
        execute_order_list({9}, acc);
        break;
      case 9:
        execute_order_list({9}, acc);
        break;
      case 10:
        execute_order_list({0, 3, 7}, acc);
        break;
      case 11:
        execute_order_list({9}, acc);
        break;
      case 12:
        execute_order_list({9}, acc);
        break;
      case 13:
        execute_order_list({0, 3, 7}, acc);
        break;
      case 14:
        execute_order_list({9}, acc);
        break;
      case 15:
        execute_order_list({9}, acc);
        break;
      case 16:
        execute_order_list({0, 3, 7}, acc);
        break;
      case 17:
        execute_order_list({9}, acc);
        break;
      case 18:
        execute_order_list({9}, acc);
        break;
      case 19:
        execute_order_list({0, 3, 7}, acc);
        break;
      case 20:
        execute_order_list({9}, acc);
        break;
      case 21:
        execute_order_list({9}, acc);
        break;
      case 22:
        execute_order_list({0, 3, 7}, acc);
        break;
      case 23:
        execute_order_list({9}, acc);
        break;
      case 24:
        execute_order_list({0, 3, 9}, acc);
        break;
      case 26:
        execute_order_list({5, 8}, acc);
        break;
      case 27:
        execute_order_list({5}, acc);
        break;
      case 29:
        execute_order_list({3, 11}, acc);
        break;
      case 30:
        execute_order_list({0}, acc);
        break;
      case 31:
        execute_order_list({9}, acc);
        break;
      case 32:
        execute_order_list({9}, acc);
        break;
      case 34:
        execute_order_list({7, 8}, acc);
        break;
      case 35:
        execute_order_list({0}, acc);
        break;
      case 36:
        execute_order_list({0, 11}, acc);
        break;
      case 37:
        if (branch_l) {
          execute_order_list({0}, acc);
        } else {
          execute_order_list({5}, acc);
        }
        break;
      case 38:
        execute_order_list({5, 11}, acc);
        break;
      case 39:
        execute_order_list({0}, acc);
        break;
      case 40:
        if (branch_l) {
          execute_order_list({0}, acc);
        } else {
          execute_order_list({5}, acc);
        }
        break;
      case 41:
        execute_order_list({0}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void execute_1ff225_gamma_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 5:
        execute_order_list({13}, acc);
        break;
      case 8:
        execute_order_list({13}, acc);
        break;
      case 11:
        execute_order_list({13}, acc);
        break;
      case 14:
        execute_order_list({13}, acc);
        break;
      case 17:
        execute_order_list({13}, acc);
        break;
      case 20:
        execute_order_list({13}, acc);
        break;
      case 23:
        execute_order_list({13}, acc);
        break;
      case 31:
        execute_order_list({13}, acc);
        break;
      case 32:
        execute_order_list({13}, acc);
        break;
      case 34:
        execute_order_list({12, 13}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void execute_1ff225_post_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 0:
        execute_order_list({27}, acc);
        break;
      case 1:
        execute_order_list({22}, acc);
        break;
      case 2:
        execute_order_list({29}, acc);
        break;
      case 3:
        execute_order_list({16}, acc);
        break;
      case 4:
        execute_order_list({25, 27}, acc);
        break;
      case 5:
        execute_order_list({29}, acc);
        break;
      case 6:
        execute_order_list({16}, acc);
        break;
      case 7:
        execute_order_list({25, 27}, acc);
        break;
      case 8:
        execute_order_list({29}, acc);
        break;
      case 9:
        execute_order_list({16}, acc);
        break;
      case 10:
        execute_order_list({25, 27}, acc);
        break;
      case 11:
        execute_order_list({29}, acc);
        break;
      case 12:
        execute_order_list({16}, acc);
        break;
      case 13:
        execute_order_list({25, 27}, acc);
        break;
      case 14:
        execute_order_list({29}, acc);
        break;
      case 15:
        execute_order_list({16}, acc);
        break;
      case 16:
        execute_order_list({25, 27}, acc);
        break;
      case 17:
        execute_order_list({29}, acc);
        break;
      case 18:
        execute_order_list({16}, acc);
        break;
      case 19:
        execute_order_list({25, 27}, acc);
        break;
      case 20:
        execute_order_list({29}, acc);
        break;
      case 21:
        execute_order_list({16}, acc);
        break;
      case 22:
        execute_order_list({25, 27}, acc);
        break;
      case 23:
        execute_order_list({29}, acc);
        break;
      case 24:
        execute_order_list({25, 29}, acc);
        break;
      case 25:
        execute_order_list({22}, acc);
        break;
      case 26:
        execute_order_list({27}, acc);
        break;
      case 27:
        execute_order_list({23}, acc);
        break;
      case 29:
        execute_order_list({25, 27}, acc);
        break;
      case 30:
        execute_order_list({29}, acc);
        break;
      case 31:
        execute_order_list({29}, acc);
        break;
      case 32:
        execute_order_list({29}, acc);
        break;
      case 33:
        execute_order_list({22}, acc);
        break;
      case 34:
        execute_order_list({25, 27}, acc);
        break;
      case 35:
        execute_order_list({17, 27}, acc);
        break;
      case 36:
        execute_order_list({27}, acc);
        break;
      case 37:
        if (branch_l) {
          execute_order_list({17, 22, 27}, acc);
        } else {
          execute_order_list({22}, acc);
        }
        break;
      case 38:
        execute_order_list({23}, acc);
        break;
      case 39:
        execute_order_list({27}, acc);
        break;
      case 40:
        if (branch_l) {
          execute_order_list({17, 22, 27}, acc);
        } else {
          execute_order_list({22}, acc);
        }
        break;
      case 41:
        execute_order_list({27}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void tick_ik1303_1ff225_decoded() {
    static constexpr uint32_t kCommand = 0x1ff225;
    if (tick_index == 0) command = kCommand;

    const int current_tick = tick_index;
    const bool branch_l = l != 0;
    if (current_tick < 27) {
      sync_address = 37;
    } else if (current_tick < 36) {
      sync_address = 100;
    } else {
      if (current_tick == 36) {
        r[37] = 15 & 0xF;
        r[40] = 127 >> 4;
      }
      sync_address = 95;
    }

    Accumulator acc;
    execute_1ff225_pre_add_orders(current_tick, branch_l, acc);

    if (key_y == 0) t = 0;

    execute_1ff225_gamma_orders(current_tick, branch_l, acc);

    int sum = acc.alpha + acc.beta + acc.gamma;
    acc.sigma = sum & 0xF;
    carry = (sum >> 4) & 1;

    execute_1ff225_post_add_orders(current_tick, branch_l, acc);

    output = memory[current_tick];
    memory[current_tick] = input;
    ++tick_index;
    if (tick_index == 42) tick_index = 0;
  }


  void execute_45f2d3_pre_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 4:
        execute_order_list({0}, acc);
        break;
      case 7:
        execute_order_list({0}, acc);
        break;
      case 10:
        execute_order_list({0}, acc);
        break;
      case 13:
        execute_order_list({0}, acc);
        break;
      case 16:
        execute_order_list({0}, acc);
        break;
      case 19:
        execute_order_list({0}, acc);
        break;
      case 22:
        execute_order_list({0}, acc);
        break;
      case 24:
        execute_order_list({5}, acc);
        break;
      case 27:
        execute_order_list({3, 7}, acc);
        break;
      case 28:
        execute_order_list({0, 7, 8}, acc);
        break;
      case 34:
        execute_order_list({3}, acc);
        break;
      case 36:
        execute_order_list({0, 11}, acc);
        break;
      case 37:
        execute_order_list({5}, acc);
        break;
      case 38:
        execute_order_list({6, 7, 8}, acc);
        break;
      case 39:
        execute_order_list({5, 11}, acc);
        break;
      case 40:
        execute_order_list({3, 7}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void execute_45f2d3_gamma_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 27:
        execute_order_list({12, 13}, acc);
        break;
      case 28:
        execute_order_list({12}, acc);
        break;
      case 34:
        execute_order_list({12}, acc);
        break;
      case 37:
        execute_order_list({12}, acc);
        break;
      case 40:
        execute_order_list({12, 13}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void execute_45f2d3_post_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 4:
        execute_order_list({29}, acc);
        break;
      case 7:
        execute_order_list({29}, acc);
        break;
      case 10:
        execute_order_list({29}, acc);
        break;
      case 13:
        execute_order_list({29}, acc);
        break;
      case 16:
        execute_order_list({29}, acc);
        break;
      case 19:
        execute_order_list({29}, acc);
        break;
      case 22:
        execute_order_list({29}, acc);
        break;
      case 24:
        execute_order_list({26, 29}, acc);
        break;
      case 27:
        execute_order_list({25}, acc);
        break;
      case 28:
        execute_order_list({25, 27}, acc);
        break;
      case 34:
        execute_order_list({25, 27}, acc);
        break;
      case 35:
        execute_order_list({29}, acc);
        break;
      case 36:
        execute_order_list({27}, acc);
        break;
      case 37:
        execute_order_list({22, 27}, acc);
        break;
      case 38:
        execute_order_list({27}, acc);
        break;
      case 39:
        execute_order_list({25, 27}, acc);
        break;
      case 40:
        execute_order_list({25}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void tick_ik1303_45f2d3_decoded() {
    static constexpr uint32_t kCommand = 0x45f2d3;
    if (tick_index == 0) command = kCommand;

    const int current_tick = tick_index;
    const bool branch_l = l != 0;
    if (current_tick < 27) {
      sync_address = 83;
    } else if (current_tick < 36) {
      sync_address = 101;
    } else {
      sync_address = 23;
    }

    Accumulator acc;
    execute_45f2d3_pre_add_orders(current_tick, branch_l, acc);

    if (key_y == 0) t = 0;

    execute_45f2d3_gamma_orders(current_tick, branch_l, acc);

    int sum = acc.alpha + acc.beta + acc.gamma;
    acc.sigma = sum & 0xF;
    carry = (sum >> 4) & 1;

    execute_45f2d3_post_add_orders(current_tick, branch_l, acc);

    output = memory[current_tick];
    memory[current_tick] = input;
    ++tick_index;
    if (tick_index == 42) tick_index = 0;
  }


  void execute_1f9232_pre_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 0:
        execute_order_list({7, 8}, acc);
        break;
      case 1:
        execute_order_list({0}, acc);
        break;
      case 2:
        execute_order_list({0}, acc);
        break;
      case 3:
        execute_order_list({5, 10}, acc);
        break;
      case 4:
        execute_order_list({0}, acc);
        break;
      case 5:
        execute_order_list({0}, acc);
        break;
      case 6:
        execute_order_list({5, 10}, acc);
        break;
      case 7:
        execute_order_list({0}, acc);
        break;
      case 8:
        execute_order_list({0}, acc);
        break;
      case 9:
        execute_order_list({5, 10}, acc);
        break;
      case 10:
        execute_order_list({0}, acc);
        break;
      case 11:
        execute_order_list({0}, acc);
        break;
      case 12:
        execute_order_list({5, 10}, acc);
        break;
      case 13:
        execute_order_list({0}, acc);
        break;
      case 14:
        execute_order_list({0}, acc);
        break;
      case 15:
        execute_order_list({5, 10}, acc);
        break;
      case 16:
        execute_order_list({0}, acc);
        break;
      case 17:
        execute_order_list({0}, acc);
        break;
      case 18:
        execute_order_list({5, 10}, acc);
        break;
      case 19:
        execute_order_list({0}, acc);
        break;
      case 20:
        execute_order_list({0}, acc);
        break;
      case 21:
        execute_order_list({5, 10}, acc);
        break;
      case 22:
        execute_order_list({0}, acc);
        break;
      case 23:
        execute_order_list({0}, acc);
        break;
      case 24:
        execute_order_list({5, 10}, acc);
        break;
      case 25:
        execute_order_list({0}, acc);
        break;
      case 27:
        execute_order_list({10}, acc);
        break;
      case 29:
        execute_order_list({5}, acc);
        break;
      case 30:
        execute_order_list({0, 3, 9}, acc);
        break;
      case 31:
        execute_order_list({4, 7, 8}, acc);
        break;
      case 32:
        execute_order_list({5}, acc);
        break;
      case 36:
        execute_order_list({0, 11}, acc);
        break;
      case 37:
        if (branch_l) {
          execute_order_list({0}, acc);
        } else {
          execute_order_list({5}, acc);
        }
        break;
      case 38:
        execute_order_list({5, 11}, acc);
        break;
      case 39:
        execute_order_list({0}, acc);
        break;
      case 40:
        if (branch_l) {
          execute_order_list({0}, acc);
        } else {
          execute_order_list({5}, acc);
        }
        break;
      case 41:
        execute_order_list({0}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void execute_1f9232_gamma_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 0:
        execute_order_list({12, 13}, acc);
        break;
      case 3:
        execute_order_list({12, 13}, acc);
        break;
      case 6:
        execute_order_list({12, 13}, acc);
        break;
      case 9:
        execute_order_list({12, 13}, acc);
        break;
      case 12:
        execute_order_list({12, 13}, acc);
        break;
      case 15:
        execute_order_list({12, 13}, acc);
        break;
      case 18:
        execute_order_list({12, 13}, acc);
        break;
      case 21:
        execute_order_list({12, 13}, acc);
        break;
      case 24:
        execute_order_list({12}, acc);
        break;
      case 31:
        execute_order_list({12}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void execute_1f9232_post_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 0:
        execute_order_list({25, 27}, acc);
        break;
      case 1:
        execute_order_list({17, 26, 29}, acc);
        break;
      case 2:
        execute_order_list({17, 26, 29}, acc);
        break;
      case 3:
        execute_order_list({25}, acc);
        break;
      case 4:
        execute_order_list({17, 26, 29}, acc);
        break;
      case 5:
        execute_order_list({17, 26, 29}, acc);
        break;
      case 6:
        execute_order_list({25}, acc);
        break;
      case 7:
        execute_order_list({17, 26, 29}, acc);
        break;
      case 8:
        execute_order_list({17, 26, 29}, acc);
        break;
      case 9:
        execute_order_list({25}, acc);
        break;
      case 10:
        execute_order_list({17, 26, 29}, acc);
        break;
      case 11:
        execute_order_list({17, 26, 29}, acc);
        break;
      case 12:
        execute_order_list({25}, acc);
        break;
      case 13:
        execute_order_list({17, 26, 29}, acc);
        break;
      case 14:
        execute_order_list({17, 26, 29}, acc);
        break;
      case 15:
        execute_order_list({25}, acc);
        break;
      case 16:
        execute_order_list({17, 26, 29}, acc);
        break;
      case 17:
        execute_order_list({17, 26, 29}, acc);
        break;
      case 18:
        execute_order_list({25}, acc);
        break;
      case 19:
        execute_order_list({17, 26, 29}, acc);
        break;
      case 20:
        execute_order_list({17, 26, 29}, acc);
        break;
      case 21:
        execute_order_list({25}, acc);
        break;
      case 22:
        execute_order_list({17, 26, 29}, acc);
        break;
      case 23:
        execute_order_list({17, 26, 29}, acc);
        break;
      case 24:
        execute_order_list({25}, acc);
        break;
      case 25:
        execute_order_list({17, 26, 29}, acc);
        break;
      case 27:
        execute_order_list({27}, acc);
        break;
      case 28:
        execute_order_list({22}, acc);
        break;
      case 29:
        execute_order_list({26, 29}, acc);
        break;
      case 30:
        execute_order_list({25, 29}, acc);
        break;
      case 31:
        execute_order_list({27}, acc);
        break;
      case 32:
        execute_order_list({23}, acc);
        break;
      case 34:
        execute_order_list({22}, acc);
        break;
      case 36:
        execute_order_list({27}, acc);
        break;
      case 37:
        if (branch_l) {
          execute_order_list({17, 22, 27}, acc);
        } else {
          execute_order_list({22}, acc);
        }
        break;
      case 38:
        execute_order_list({23}, acc);
        break;
      case 39:
        execute_order_list({27}, acc);
        break;
      case 40:
        if (branch_l) {
          execute_order_list({17, 22, 27}, acc);
        } else {
          execute_order_list({22}, acc);
        }
        break;
      case 41:
        execute_order_list({27}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void tick_ik1303_1f9232_decoded() {
    static constexpr uint32_t kCommand = 0x1f9232;
    if (tick_index == 0) command = kCommand;

    const int current_tick = tick_index;
    const bool branch_l = l != 0;
    if (current_tick < 27) {
      sync_address = 50;
    } else if (current_tick < 36) {
      sync_address = 36;
    } else {
      if (current_tick == 36) {
        r[37] = 14 & 0xF;
        r[40] = 126 >> 4;
      }
      sync_address = 95;
    }

    Accumulator acc;
    execute_1f9232_pre_add_orders(current_tick, branch_l, acc);

    if (key_y == 0) t = 0;

    execute_1f9232_gamma_orders(current_tick, branch_l, acc);

    int sum = acc.alpha + acc.beta + acc.gamma;
    acc.sigma = sum & 0xF;
    carry = (sum >> 4) & 1;

    execute_1f9232_post_add_orders(current_tick, branch_l, acc);

    output = memory[current_tick];
    memory[current_tick] = input;
    ++tick_index;
    if (tick_index == 42) tick_index = 0;
  }


  void execute_031726_pre_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 0:
        execute_order_list({7, 8}, acc);
        break;
      case 1:
        execute_order_list({0}, acc);
        break;
      case 2:
        execute_order_list({0}, acc);
        break;
      case 3:
        execute_order_list({7, 8}, acc);
        break;
      case 4:
        execute_order_list({0}, acc);
        break;
      case 5:
        execute_order_list({0}, acc);
        break;
      case 6:
        execute_order_list({7, 8}, acc);
        break;
      case 7:
        execute_order_list({0}, acc);
        break;
      case 8:
        execute_order_list({0}, acc);
        break;
      case 9:
        execute_order_list({7, 8}, acc);
        break;
      case 10:
        execute_order_list({0}, acc);
        break;
      case 11:
        execute_order_list({0}, acc);
        break;
      case 12:
        execute_order_list({7, 8}, acc);
        break;
      case 13:
        execute_order_list({0}, acc);
        break;
      case 14:
        execute_order_list({0}, acc);
        break;
      case 15:
        execute_order_list({7, 8}, acc);
        break;
      case 16:
        execute_order_list({0}, acc);
        break;
      case 17:
        execute_order_list({0}, acc);
        break;
      case 18:
        execute_order_list({7, 8}, acc);
        break;
      case 19:
        execute_order_list({0}, acc);
        break;
      case 20:
        execute_order_list({0}, acc);
        break;
      case 21:
        execute_order_list({7, 8}, acc);
        break;
      case 22:
        execute_order_list({0}, acc);
        break;
      case 23:
        execute_order_list({0}, acc);
        break;
      case 24:
        execute_order_list({7, 8}, acc);
        break;
      case 25:
        execute_order_list({0}, acc);
        break;
      case 27:
        execute_order_list({0, 10}, acc);
        break;
      case 28:
        execute_order_list({0, 7}, acc);
        break;
      case 29:
        execute_order_list({4, 7}, acc);
        break;
      case 30:
        execute_order_list({0, 10}, acc);
        break;
      case 31:
        execute_order_list({0, 7}, acc);
        break;
      case 32:
        execute_order_list({4, 7}, acc);
        break;
      case 33:
        execute_order_list({0, 10}, acc);
        break;
      case 34:
        execute_order_list({0, 7}, acc);
        break;
      case 35:
        execute_order_list({4, 7}, acc);
        break;
      case 36:
        execute_order_list({0, 11}, acc);
        break;
      case 38:
        execute_order_list({5, 11}, acc);
        break;
      case 39:
        execute_order_list({7, 8}, acc);
        break;
      case 40:
        execute_order_list({1}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void execute_031726_gamma_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 0:
        execute_order_list({12, 13}, acc);
        break;
      case 3:
        execute_order_list({12, 13}, acc);
        break;
      case 6:
        execute_order_list({12, 13}, acc);
        break;
      case 9:
        execute_order_list({12, 13}, acc);
        break;
      case 12:
        execute_order_list({12, 13}, acc);
        break;
      case 15:
        execute_order_list({12, 13}, acc);
        break;
      case 18:
        execute_order_list({12, 13}, acc);
        break;
      case 21:
        execute_order_list({12, 13}, acc);
        break;
      case 24:
        execute_order_list({12, 13}, acc);
        break;
      case 31:
        execute_order_list({12}, acc);
        break;
      case 34:
        execute_order_list({12}, acc);
        break;
      case 39:
        execute_order_list({12, 13}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void execute_031726_post_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 0:
        execute_order_list({25, 27}, acc);
        break;
      case 1:
        execute_order_list({17, 27}, acc);
        break;
      case 2:
        execute_order_list({17, 27}, acc);
        break;
      case 3:
        execute_order_list({25, 27}, acc);
        break;
      case 4:
        execute_order_list({17, 27}, acc);
        break;
      case 5:
        execute_order_list({17, 27}, acc);
        break;
      case 6:
        execute_order_list({25, 27}, acc);
        break;
      case 7:
        execute_order_list({17, 27}, acc);
        break;
      case 8:
        execute_order_list({17, 27}, acc);
        break;
      case 9:
        execute_order_list({25, 27}, acc);
        break;
      case 10:
        execute_order_list({17, 27}, acc);
        break;
      case 11:
        execute_order_list({17, 27}, acc);
        break;
      case 12:
        execute_order_list({25, 27}, acc);
        break;
      case 13:
        execute_order_list({17, 27}, acc);
        break;
      case 14:
        execute_order_list({17, 27}, acc);
        break;
      case 15:
        execute_order_list({25, 27}, acc);
        break;
      case 16:
        execute_order_list({17, 27}, acc);
        break;
      case 17:
        execute_order_list({17, 27}, acc);
        break;
      case 18:
        execute_order_list({25, 27}, acc);
        break;
      case 19:
        execute_order_list({17, 27}, acc);
        break;
      case 20:
        execute_order_list({17, 27}, acc);
        break;
      case 21:
        execute_order_list({25, 27}, acc);
        break;
      case 22:
        execute_order_list({17, 27}, acc);
        break;
      case 23:
        execute_order_list({17, 27}, acc);
        break;
      case 24:
        execute_order_list({25, 27}, acc);
        break;
      case 25:
        execute_order_list({29}, acc);
        break;
      case 26:
        execute_order_list({22}, acc);
        break;
      case 27:
        execute_order_list({27}, acc);
        break;
      case 28:
        execute_order_list({25, 27}, acc);
        break;
      case 29:
        execute_order_list({22, 26, 29}, acc);
        break;
      case 30:
        execute_order_list({27}, acc);
        break;
      case 31:
        execute_order_list({25, 27}, acc);
        break;
      case 32:
        execute_order_list({22, 26, 29}, acc);
        break;
      case 33:
        execute_order_list({27}, acc);
        break;
      case 34:
        execute_order_list({25, 27}, acc);
        break;
      case 35:
        execute_order_list({22, 26, 29}, acc);
        break;
      case 36:
        execute_order_list({27}, acc);
        break;
      case 38:
        execute_order_list({23}, acc);
        break;
      case 39:
        execute_order_list({25, 27}, acc);
        break;
      case 40:
        execute_order_list({27}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void tick_ik1303_031726_decoded() {
    static constexpr uint32_t kCommand = 0x031726;
    if (tick_index == 0) command = kCommand;

    const int current_tick = tick_index;
    const bool branch_l = l != 0;
    if (current_tick < 27) {
      sync_address = 38;
    } else if (current_tick < 36) {
      sync_address = 46;
    } else {
      sync_address = 12;
    }

    Accumulator acc;
    execute_031726_pre_add_orders(current_tick, branch_l, acc);

    if (key_y == 0) t = 0;

    execute_031726_gamma_orders(current_tick, branch_l, acc);

    int sum = acc.alpha + acc.beta + acc.gamma;
    acc.sigma = sum & 0xF;
    carry = (sum >> 4) & 1;

    execute_031726_post_add_orders(current_tick, branch_l, acc);

    output = memory[current_tick];
    memory[current_tick] = input;
    ++tick_index;
    if (tick_index == 42) tick_index = 0;
  }


  void execute_0c46ae_pre_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 0:
        execute_order_list({0, 10}, acc);
        break;
      case 1:
        execute_order_list({0, 7}, acc);
        break;
      case 2:
        execute_order_list({4, 7}, acc);
        break;
      case 3:
        execute_order_list({0, 10}, acc);
        break;
      case 4:
        execute_order_list({0, 7}, acc);
        break;
      case 5:
        execute_order_list({4, 7}, acc);
        break;
      case 6:
        execute_order_list({0, 10}, acc);
        break;
      case 7:
        execute_order_list({0, 7}, acc);
        break;
      case 8:
        execute_order_list({4, 7}, acc);
        break;
      case 9:
        execute_order_list({0, 10}, acc);
        break;
      case 10:
        execute_order_list({0, 7}, acc);
        break;
      case 11:
        execute_order_list({4, 7}, acc);
        break;
      case 12:
        execute_order_list({0, 10}, acc);
        break;
      case 13:
        execute_order_list({0, 7}, acc);
        break;
      case 14:
        execute_order_list({4, 7}, acc);
        break;
      case 15:
        execute_order_list({0, 10}, acc);
        break;
      case 16:
        execute_order_list({0, 7}, acc);
        break;
      case 17:
        execute_order_list({4, 7}, acc);
        break;
      case 18:
        execute_order_list({0, 10}, acc);
        break;
      case 19:
        execute_order_list({0, 7}, acc);
        break;
      case 20:
        execute_order_list({4, 7}, acc);
        break;
      case 21:
        execute_order_list({0, 10}, acc);
        break;
      case 22:
        execute_order_list({0, 7}, acc);
        break;
      case 23:
        execute_order_list({4, 7}, acc);
        break;
      case 24:
        execute_order_list({0, 10}, acc);
        break;
      case 25:
        execute_order_list({0, 7}, acc);
        break;
      case 26:
        execute_order_list({4, 7}, acc);
        break;
      case 30:
        execute_order_list({0, 11}, acc);
        break;
      case 31:
        execute_order_list({5}, acc);
        break;
      case 33:
        execute_order_list({5}, acc);
        break;
      case 34:
        execute_order_list({5, 10, 11}, acc);
        break;
      case 35:
        execute_order_list({5, 10, 11}, acc);
        break;
      case 36:
        execute_order_list({0, 11}, acc);
        break;
      case 37:
        if (branch_l) {
          execute_order_list({0}, acc);
        } else {
          execute_order_list({5}, acc);
        }
        break;
      case 38:
        execute_order_list({5, 11}, acc);
        break;
      case 39:
        execute_order_list({0}, acc);
        break;
      case 40:
        if (branch_l) {
          execute_order_list({0}, acc);
        } else {
          execute_order_list({5}, acc);
        }
        break;
      case 41:
        execute_order_list({0}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void execute_0c46ae_gamma_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 4:
        execute_order_list({12}, acc);
        break;
      case 7:
        execute_order_list({12}, acc);
        break;
      case 10:
        execute_order_list({12}, acc);
        break;
      case 13:
        execute_order_list({12}, acc);
        break;
      case 16:
        execute_order_list({12}, acc);
        break;
      case 19:
        execute_order_list({12}, acc);
        break;
      case 22:
        execute_order_list({12}, acc);
        break;
      case 25:
        execute_order_list({12}, acc);
        break;
      case 34:
        execute_order_list({12, 13}, acc);
        break;
      case 35:
        execute_order_list({12, 13}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void execute_0c46ae_post_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 0:
        execute_order_list({27}, acc);
        break;
      case 1:
        execute_order_list({25, 27}, acc);
        break;
      case 2:
        execute_order_list({22, 26, 29}, acc);
        break;
      case 3:
        execute_order_list({27}, acc);
        break;
      case 4:
        execute_order_list({25, 27}, acc);
        break;
      case 5:
        execute_order_list({22, 26, 29}, acc);
        break;
      case 6:
        execute_order_list({27}, acc);
        break;
      case 7:
        execute_order_list({25, 27}, acc);
        break;
      case 8:
        execute_order_list({22, 26, 29}, acc);
        break;
      case 9:
        execute_order_list({27}, acc);
        break;
      case 10:
        execute_order_list({25, 27}, acc);
        break;
      case 11:
        execute_order_list({22, 26, 29}, acc);
        break;
      case 12:
        execute_order_list({27}, acc);
        break;
      case 13:
        execute_order_list({25, 27}, acc);
        break;
      case 14:
        execute_order_list({22, 26, 29}, acc);
        break;
      case 15:
        execute_order_list({27}, acc);
        break;
      case 16:
        execute_order_list({25, 27}, acc);
        break;
      case 17:
        execute_order_list({22, 26, 29}, acc);
        break;
      case 18:
        execute_order_list({27}, acc);
        break;
      case 19:
        execute_order_list({25, 27}, acc);
        break;
      case 20:
        execute_order_list({22, 26, 29}, acc);
        break;
      case 21:
        execute_order_list({27}, acc);
        break;
      case 22:
        execute_order_list({25, 27}, acc);
        break;
      case 23:
        execute_order_list({22, 26, 29}, acc);
        break;
      case 24:
        execute_order_list({27}, acc);
        break;
      case 25:
        execute_order_list({25, 27}, acc);
        break;
      case 26:
        execute_order_list({22, 26, 29}, acc);
        break;
      case 27:
        execute_order_list({22}, acc);
        break;
      case 30:
        execute_order_list({27}, acc);
        break;
      case 31:
        execute_order_list({22}, acc);
        break;
      case 33:
        execute_order_list({26, 29}, acc);
        break;
      case 34:
        execute_order_list({25, 27}, acc);
        break;
      case 35:
        execute_order_list({25, 27}, acc);
        break;
      case 36:
        execute_order_list({27}, acc);
        break;
      case 37:
        if (branch_l) {
          execute_order_list({17, 22, 27}, acc);
        } else {
          execute_order_list({22}, acc);
        }
        break;
      case 38:
        execute_order_list({23}, acc);
        break;
      case 39:
        execute_order_list({27}, acc);
        break;
      case 40:
        if (branch_l) {
          execute_order_list({17, 22, 27}, acc);
        } else {
          execute_order_list({22}, acc);
        }
        break;
      case 41:
        execute_order_list({27}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void tick_ik1303_0c46ae_decoded() {
    static constexpr uint32_t kCommand = 0x0c46ae;
    if (tick_index == 0) command = kCommand;

    const int current_tick = tick_index;
    const bool branch_l = l != 0;
    if (current_tick < 27) {
      sync_address = 46;
    } else if (current_tick < 36) {
      sync_address = 13;
    } else {
      if (current_tick == 36) {
        r[37] = 1 & 0xF;
        r[40] = 49 >> 4;
      }
      sync_address = 95;
    }

    Accumulator acc;
    execute_0c46ae_pre_add_orders(current_tick, branch_l, acc);

    if (key_y == 0) t = 0;

    execute_0c46ae_gamma_orders(current_tick, branch_l, acc);

    int sum = acc.alpha + acc.beta + acc.gamma;
    acc.sigma = sum & 0xF;
    carry = (sum >> 4) & 1;

    execute_0c46ae_post_add_orders(current_tick, branch_l, acc);

    output = memory[current_tick];
    memory[current_tick] = input;
    ++tick_index;
    if (tick_index == 42) tick_index = 0;
  }


  void execute_040735_pre_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 0:
        execute_order_list({4, 7, 8}, acc);
        break;
      case 2:
        execute_order_list({0}, acc);
        break;
      case 24:
        execute_order_list({9}, acc);
        break;
      case 25:
        execute_order_list({0}, acc);
        break;
      case 26:
        execute_order_list({5, 8}, acc);
        break;
      case 27:
        execute_order_list({5, 9}, acc);
        break;
      case 28:
        execute_order_list({5}, acc);
        break;
      case 29:
        execute_order_list({5, 7}, acc);
        break;
      case 30:
        execute_order_list({9}, acc);
        break;
      case 31:
        execute_order_list({5, 8}, acc);
        break;
      case 32:
        execute_order_list({5, 9}, acc);
        break;
      case 33:
        execute_order_list({5, 7}, acc);
        break;
      case 34:
        execute_order_list({5, 8}, acc);
        break;
      case 35:
        execute_order_list({0, 7}, acc);
        break;
      case 36:
        execute_order_list({8}, acc);
        break;
      case 37:
        execute_order_list({6, 7, 8}, acc);
        break;
      case 38:
        execute_order_list({7, 9}, acc);
        break;
      case 39:
        execute_order_list({5, 9}, acc);
        break;
      case 40:
        execute_order_list({5}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void execute_040735_gamma_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 0:
        execute_order_list({12}, acc);
        break;
      case 24:
        execute_order_list({13}, acc);
        break;
      case 29:
        execute_order_list({12}, acc);
        break;
      case 30:
        execute_order_list({13}, acc);
        break;
      case 33:
        execute_order_list({12}, acc);
        break;
      case 36:
        execute_order_list({12}, acc);
        break;
      case 38:
        execute_order_list({12}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void execute_040735_post_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 0:
        execute_order_list({27}, acc);
        break;
      case 1:
        execute_order_list({15}, acc);
        break;
      case 2:
        execute_order_list({15, 29}, acc);
        break;
      case 4:
        execute_order_list({15}, acc);
        break;
      case 5:
        execute_order_list({15}, acc);
        break;
      case 7:
        execute_order_list({15}, acc);
        break;
      case 8:
        execute_order_list({15}, acc);
        break;
      case 10:
        execute_order_list({15}, acc);
        break;
      case 11:
        execute_order_list({15}, acc);
        break;
      case 13:
        execute_order_list({15}, acc);
        break;
      case 14:
        execute_order_list({15}, acc);
        break;
      case 16:
        execute_order_list({15}, acc);
        break;
      case 17:
        execute_order_list({15}, acc);
        break;
      case 19:
        execute_order_list({15}, acc);
        break;
      case 20:
        execute_order_list({15}, acc);
        break;
      case 22:
        execute_order_list({15}, acc);
        break;
      case 23:
        execute_order_list({15}, acc);
        break;
      case 24:
        execute_order_list({29}, acc);
        break;
      case 25:
        execute_order_list({17, 27}, acc);
        break;
      case 26:
        execute_order_list({27}, acc);
        break;
      case 27:
        execute_order_list({25, 27, 29}, acc);
        break;
      case 28:
        execute_order_list({22}, acc);
        break;
      case 29:
        execute_order_list({25, 27}, acc);
        break;
      case 30:
        execute_order_list({29}, acc);
        break;
      case 31:
        execute_order_list({27}, acc);
        break;
      case 32:
        execute_order_list({25, 27, 29}, acc);
        break;
      case 33:
        execute_order_list({25, 27}, acc);
        break;
      case 34:
        execute_order_list({27}, acc);
        break;
      case 35:
        execute_order_list({16, 27}, acc);
        break;
      case 36:
        execute_order_list({25}, acc);
        break;
      case 37:
        execute_order_list({27}, acc);
        break;
      case 38:
        execute_order_list({29}, acc);
        break;
      case 39:
        execute_order_list({25, 27, 29}, acc);
        break;
      case 40:
        execute_order_list({22}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void tick_ik1303_040735_decoded() {
    static constexpr uint32_t kCommand = 0x040735;
    if (tick_index == 0) command = kCommand;

    const int current_tick = tick_index;
    const bool branch_l = l != 0;
    if (current_tick < 27) {
      sync_address = 53;
    } else if (current_tick < 36) {
      sync_address = 14;
    } else {
      sync_address = 16;
    }

    Accumulator acc;
    execute_040735_pre_add_orders(current_tick, branch_l, acc);

    if (key_y == 0) t = 0;

    execute_040735_gamma_orders(current_tick, branch_l, acc);

    int sum = acc.alpha + acc.beta + acc.gamma;
    acc.sigma = sum & 0xF;
    carry = (sum >> 4) & 1;

    execute_040735_post_add_orders(current_tick, branch_l, acc);

    output = memory[current_tick];
    memory[current_tick] = input;
    ++tick_index;
    if (tick_index == 42) tick_index = 0;
  }


  void execute_014150_pre_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 30:
        execute_order_list({5, 8}, acc);
        break;
      case 31:
        execute_order_list({1}, acc);
        break;
      case 33:
        execute_order_list({4, 7, 8}, acc);
        break;
      case 34:
        execute_order_list({5}, acc);
        break;
      case 36:
        execute_order_list({7, 8}, acc);
        break;
      case 37:
        execute_order_list({0}, acc);
        break;
      case 40:
        execute_order_list({0}, acc);
        break;
      case 41:
        execute_order_list({0}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void execute_014150_gamma_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 33:
        execute_order_list({12}, acc);
        break;
      case 36:
        execute_order_list({12, 13}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void execute_014150_post_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 30:
        execute_order_list({27}, acc);
        break;
      case 31:
        execute_order_list({22, 24}, acc);
        break;
      case 33:
        execute_order_list({27}, acc);
        break;
      case 34:
        execute_order_list({26, 29}, acc);
        break;
      case 36:
        execute_order_list({25, 27}, acc);
        break;
      case 37:
        execute_order_list({22}, acc);
        break;
      case 40:
        execute_order_list({22}, acc);
        break;
      case 41:
        execute_order_list({27}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void tick_ik1303_014150_decoded() {
    static constexpr uint32_t kCommand = 0x014150;
    if (tick_index == 0) command = kCommand;

    const int current_tick = tick_index;
    const bool branch_l = l != 0;
    if (current_tick < 27) {
      sync_address = 80;
    } else if (current_tick < 36) {
      sync_address = 2;
    } else {
      sync_address = 5;
    }

    Accumulator acc;
    execute_014150_pre_add_orders(current_tick, branch_l, acc);

    if (key_y == 0) t = 0;

    execute_014150_gamma_orders(current_tick, branch_l, acc);

    int sum = acc.alpha + acc.beta + acc.gamma;
    acc.sigma = sum & 0xF;
    carry = (sum >> 4) & 1;

    execute_014150_post_add_orders(current_tick, branch_l, acc);

    output = memory[current_tick];
    memory[current_tick] = input;
    ++tick_index;
    if (tick_index == 42) tick_index = 0;
  }


  void execute_066850_pre_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 36:
        execute_order_list({0, 11}, acc);
        break;
      case 38:
        execute_order_list({10}, acc);
        break;
      case 39:
        execute_order_list({0}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void execute_066850_gamma_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      default:
        break;
    }
    (void)branch_l;
  }

  void execute_066850_post_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 36:
        execute_order_list({17, 32}, acc);
        break;
      case 37:
        execute_order_list({22}, acc);
        break;
      case 38:
        execute_order_list({27}, acc);
        break;
      case 39:
        execute_order_list({17, 32}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void tick_ik1303_066850_decoded() {
    static constexpr uint32_t kCommand = 0x066850;
    if (tick_index == 0) command = kCommand;

    const int current_tick = tick_index;
    const bool branch_l = l != 0;
    if (current_tick < 27) {
      sync_address = 80;
    } else if (current_tick < 36) {
      sync_address = 80;
    } else {
      sync_address = 25;
    }

    Accumulator acc;
    execute_066850_pre_add_orders(current_tick, branch_l, acc);

    if (key_y == 0) t = 0;

    execute_066850_gamma_orders(current_tick, branch_l, acc);

    int sum = acc.alpha + acc.beta + acc.gamma;
    acc.sigma = sum & 0xF;
    carry = (sum >> 4) & 1;

    execute_066850_post_add_orders(current_tick, branch_l, acc);

    output = memory[current_tick];
    memory[current_tick] = input;
    ++tick_index;
    if (tick_index == 42) tick_index = 0;
  }


  void execute_0e162c_pre_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 0:
        execute_order_list({0}, acc);
        break;
      case 1:
        execute_order_list({1}, acc);
        break;
      case 3:
        execute_order_list({0}, acc);
        break;
      case 4:
        execute_order_list({1}, acc);
        break;
      case 6:
        execute_order_list({0}, acc);
        break;
      case 7:
        execute_order_list({1}, acc);
        break;
      case 9:
        execute_order_list({0}, acc);
        break;
      case 10:
        execute_order_list({1}, acc);
        break;
      case 12:
        execute_order_list({0}, acc);
        break;
      case 13:
        execute_order_list({1}, acc);
        break;
      case 15:
        execute_order_list({0}, acc);
        break;
      case 16:
        execute_order_list({1}, acc);
        break;
      case 18:
        execute_order_list({0}, acc);
        break;
      case 19:
        execute_order_list({1}, acc);
        break;
      case 21:
        execute_order_list({0}, acc);
        break;
      case 22:
        execute_order_list({1}, acc);
        break;
      case 24:
        execute_order_list({0}, acc);
        break;
      case 25:
        execute_order_list({1}, acc);
        break;
      case 26:
        execute_order_list({7, 8}, acc);
        break;
      case 27:
        execute_order_list({0}, acc);
        break;
      case 28:
        execute_order_list({1}, acc);
        break;
      case 30:
        execute_order_list({0}, acc);
        break;
      case 31:
        execute_order_list({1}, acc);
        break;
      case 33:
        execute_order_list({0}, acc);
        break;
      case 34:
        execute_order_list({1}, acc);
        break;
      case 35:
        execute_order_list({7, 8}, acc);
        break;
      case 36:
        execute_order_list({0, 11}, acc);
        break;
      case 37:
        if (branch_l) {
          execute_order_list({0}, acc);
        } else {
          execute_order_list({5}, acc);
        }
        break;
      case 38:
        execute_order_list({5, 11}, acc);
        break;
      case 39:
        execute_order_list({0}, acc);
        break;
      case 40:
        if (branch_l) {
          execute_order_list({0}, acc);
        } else {
          execute_order_list({5}, acc);
        }
        break;
      case 41:
        execute_order_list({0}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void execute_0e162c_gamma_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 26:
        execute_order_list({12, 13}, acc);
        break;
      case 35:
        execute_order_list({12, 13}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void execute_0e162c_post_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 0:
        execute_order_list({27}, acc);
        break;
      case 1:
        execute_order_list({22, 24}, acc);
        break;
      case 3:
        execute_order_list({27}, acc);
        break;
      case 4:
        execute_order_list({22, 24}, acc);
        break;
      case 6:
        execute_order_list({27}, acc);
        break;
      case 7:
        execute_order_list({22, 24}, acc);
        break;
      case 9:
        execute_order_list({27}, acc);
        break;
      case 10:
        execute_order_list({22, 24}, acc);
        break;
      case 12:
        execute_order_list({27}, acc);
        break;
      case 13:
        execute_order_list({22, 24}, acc);
        break;
      case 15:
        execute_order_list({27}, acc);
        break;
      case 16:
        execute_order_list({22, 24}, acc);
        break;
      case 18:
        execute_order_list({27}, acc);
        break;
      case 19:
        execute_order_list({22, 24}, acc);
        break;
      case 21:
        execute_order_list({27}, acc);
        break;
      case 22:
        execute_order_list({22, 24}, acc);
        break;
      case 24:
        execute_order_list({27}, acc);
        break;
      case 25:
        execute_order_list({22, 24}, acc);
        break;
      case 26:
        execute_order_list({25, 27}, acc);
        break;
      case 27:
        execute_order_list({27}, acc);
        break;
      case 28:
        execute_order_list({22, 24}, acc);
        break;
      case 30:
        execute_order_list({27}, acc);
        break;
      case 31:
        execute_order_list({22, 24}, acc);
        break;
      case 33:
        execute_order_list({27}, acc);
        break;
      case 34:
        execute_order_list({22, 24}, acc);
        break;
      case 35:
        execute_order_list({25, 27}, acc);
        break;
      case 36:
        execute_order_list({27}, acc);
        break;
      case 37:
        if (branch_l) {
          execute_order_list({17, 22, 27}, acc);
        } else {
          execute_order_list({22}, acc);
        }
        break;
      case 38:
        execute_order_list({23}, acc);
        break;
      case 39:
        execute_order_list({27}, acc);
        break;
      case 40:
        if (branch_l) {
          execute_order_list({17, 22, 27}, acc);
        } else {
          execute_order_list({22}, acc);
        }
        break;
      case 41:
        execute_order_list({27}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void tick_ik1303_0e162c_decoded() {
    static constexpr uint32_t kCommand = 0x0e162c;
    if (tick_index == 0) command = kCommand;

    const int current_tick = tick_index;
    const bool branch_l = l != 0;
    if (current_tick < 27) {
      sync_address = 44;
    } else if (current_tick < 36) {
      sync_address = 44;
    } else {
      if (current_tick == 36) {
        r[37] = 8 & 0xF;
        r[40] = 56 >> 4;
      }
      sync_address = 95;
    }

    Accumulator acc;
    execute_0e162c_pre_add_orders(current_tick, branch_l, acc);

    if (key_y == 0) t = 0;

    execute_0e162c_gamma_orders(current_tick, branch_l, acc);

    int sum = acc.alpha + acc.beta + acc.gamma;
    acc.sigma = sum & 0xF;
    carry = (sum >> 4) & 1;

    execute_0e162c_post_add_orders(current_tick, branch_l, acc);

    output = memory[current_tick];
    memory[current_tick] = input;
    ++tick_index;
    if (tick_index == 42) tick_index = 0;
  }


  void execute_0bf5d6_pre_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 3:
        execute_order_list({0}, acc);
        break;
      case 5:
        execute_order_list({7, 8}, acc);
        break;
      case 6:
        execute_order_list({0}, acc);
        break;
      case 8:
        execute_order_list({7, 8}, acc);
        break;
      case 9:
        execute_order_list({0}, acc);
        break;
      case 11:
        execute_order_list({7, 8}, acc);
        break;
      case 12:
        execute_order_list({0}, acc);
        break;
      case 14:
        execute_order_list({7, 8}, acc);
        break;
      case 15:
        execute_order_list({0}, acc);
        break;
      case 17:
        execute_order_list({7, 8}, acc);
        break;
      case 18:
        execute_order_list({0}, acc);
        break;
      case 20:
        execute_order_list({7, 8}, acc);
        break;
      case 21:
        execute_order_list({0}, acc);
        break;
      case 23:
        execute_order_list({7, 8}, acc);
        break;
      case 24:
        execute_order_list({0}, acc);
        break;
      case 25:
        execute_order_list({0, 7}, acc);
        break;
      case 26:
        execute_order_list({6, 8, 10}, acc);
        break;
      case 27:
        execute_order_list({4, 7, 8}, acc);
        break;
      case 28:
        execute_order_list({5}, acc);
        break;
      case 29:
        execute_order_list({7, 8}, acc);
        break;
      case 30:
        execute_order_list({5, 10, 11}, acc);
        break;
      case 31:
        execute_order_list({5}, acc);
        break;
      case 32:
        execute_order_list({7, 8}, acc);
        break;
      case 35:
        execute_order_list({9}, acc);
        break;
      case 36:
        execute_order_list({0, 11}, acc);
        break;
      case 37:
        if (branch_l) {
          execute_order_list({0}, acc);
        } else {
          execute_order_list({5}, acc);
        }
        break;
      case 38:
        execute_order_list({5, 11}, acc);
        break;
      case 39:
        execute_order_list({0}, acc);
        break;
      case 40:
        if (branch_l) {
          execute_order_list({0}, acc);
        } else {
          execute_order_list({5}, acc);
        }
        break;
      case 41:
        execute_order_list({0}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void execute_0bf5d6_gamma_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 5:
        execute_order_list({12, 13}, acc);
        break;
      case 8:
        execute_order_list({12, 13}, acc);
        break;
      case 11:
        execute_order_list({12, 13}, acc);
        break;
      case 14:
        execute_order_list({12, 13}, acc);
        break;
      case 17:
        execute_order_list({12, 13}, acc);
        break;
      case 20:
        execute_order_list({12, 13}, acc);
        break;
      case 23:
        execute_order_list({12, 13}, acc);
        break;
      case 27:
        execute_order_list({12}, acc);
        break;
      case 29:
        execute_order_list({12, 13}, acc);
        break;
      case 30:
        execute_order_list({12, 13}, acc);
        break;
      case 32:
        execute_order_list({12, 13}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void execute_0bf5d6_post_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 3:
        execute_order_list({29}, acc);
        break;
      case 5:
        execute_order_list({25, 27}, acc);
        break;
      case 6:
        execute_order_list({29}, acc);
        break;
      case 8:
        execute_order_list({25, 27}, acc);
        break;
      case 9:
        execute_order_list({29}, acc);
        break;
      case 11:
        execute_order_list({25, 27}, acc);
        break;
      case 12:
        execute_order_list({29}, acc);
        break;
      case 14:
        execute_order_list({25, 27}, acc);
        break;
      case 15:
        execute_order_list({29}, acc);
        break;
      case 17:
        execute_order_list({25, 27}, acc);
        break;
      case 18:
        execute_order_list({29}, acc);
        break;
      case 20:
        execute_order_list({25, 27}, acc);
        break;
      case 21:
        execute_order_list({29}, acc);
        break;
      case 23:
        execute_order_list({25, 27}, acc);
        break;
      case 24:
        execute_order_list({17, 27}, acc);
        break;
      case 25:
        execute_order_list({25, 27}, acc);
        break;
      case 26:
        execute_order_list({25}, acc);
        break;
      case 27:
        execute_order_list({27}, acc);
        break;
      case 28:
        execute_order_list({23}, acc);
        break;
      case 29:
        execute_order_list({25, 27}, acc);
        break;
      case 30:
        execute_order_list({25, 27}, acc);
        break;
      case 31:
        execute_order_list({26, 29}, acc);
        break;
      case 32:
        execute_order_list({25, 27}, acc);
        break;
      case 35:
        execute_order_list({16}, acc);
        break;
      case 36:
        execute_order_list({27}, acc);
        break;
      case 37:
        if (branch_l) {
          execute_order_list({17, 22, 27}, acc);
        } else {
          execute_order_list({22}, acc);
        }
        break;
      case 38:
        execute_order_list({23}, acc);
        break;
      case 39:
        execute_order_list({27}, acc);
        break;
      case 40:
        if (branch_l) {
          execute_order_list({17, 22, 27}, acc);
        } else {
          execute_order_list({22}, acc);
        }
        break;
      case 41:
        execute_order_list({27}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void tick_ik1303_0bf5d6_decoded() {
    static constexpr uint32_t kCommand = 0x0bf5d6;
    if (tick_index == 0) command = kCommand;

    const int current_tick = tick_index;
    const bool branch_l = l != 0;
    if (current_tick < 27) {
      sync_address = 86;
    } else if (current_tick < 36) {
      sync_address = 107;
    } else {
      if (current_tick == 36) {
        r[37] = 15 & 0xF;
        r[40] = 47 >> 4;
      }
      sync_address = 95;
    }

    Accumulator acc;
    execute_0bf5d6_pre_add_orders(current_tick, branch_l, acc);

    if (key_y == 0) t = 0;

    execute_0bf5d6_gamma_orders(current_tick, branch_l, acc);

    int sum = acc.alpha + acc.beta + acc.gamma;
    acc.sigma = sum & 0xF;
    carry = (sum >> 4) & 1;

    execute_0bf5d6_post_add_orders(current_tick, branch_l, acc);

    output = memory[current_tick];
    memory[current_tick] = input;
    ++tick_index;
    if (tick_index == 42) tick_index = 0;
  }


  void execute_025eec_pre_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 0:
        execute_order_list({5}, acc);
        break;
      case 3:
        execute_order_list({5, 10, 11}, acc);
        break;
      case 5:
        execute_order_list({5, 10, 11}, acc);
        break;
      case 6:
        execute_order_list({5, 10, 11}, acc);
        break;
      case 8:
        execute_order_list({5, 10, 11}, acc);
        break;
      case 9:
        execute_order_list({5, 10, 11}, acc);
        break;
      case 11:
        execute_order_list({5, 10, 11}, acc);
        break;
      case 12:
        execute_order_list({5, 10, 11}, acc);
        break;
      case 14:
        execute_order_list({5, 10, 11}, acc);
        break;
      case 15:
        execute_order_list({5, 10, 11}, acc);
        break;
      case 17:
        execute_order_list({5, 10, 11}, acc);
        break;
      case 18:
        execute_order_list({5, 10, 11}, acc);
        break;
      case 20:
        execute_order_list({5, 10, 11}, acc);
        break;
      case 21:
        execute_order_list({5, 10, 11}, acc);
        break;
      case 23:
        execute_order_list({5, 10, 11}, acc);
        break;
      case 24:
        execute_order_list({4, 7, 8}, acc);
        break;
      case 25:
        execute_order_list({0}, acc);
        break;
      case 26:
        execute_order_list({9}, acc);
        break;
      case 27:
        execute_order_list({10}, acc);
        break;
      case 28:
        execute_order_list({0, 7}, acc);
        break;
      case 29:
        execute_order_list({4, 7}, acc);
        break;
      case 30:
        execute_order_list({10}, acc);
        break;
      case 31:
        execute_order_list({0, 7}, acc);
        break;
      case 32:
        execute_order_list({4, 7}, acc);
        break;
      case 33:
        execute_order_list({10}, acc);
        break;
      case 34:
        execute_order_list({0, 7}, acc);
        break;
      case 35:
        execute_order_list({4, 7}, acc);
        break;
      case 36:
        execute_order_list({0, 11}, acc);
        break;
      case 37:
        execute_order_list({5}, acc);
        break;
      case 38:
        execute_order_list({3, 11}, acc);
        break;
      case 39:
        execute_order_list({0}, acc);
        break;
      case 40:
        execute_order_list({9}, acc);
        break;
      case 41:
        execute_order_list({7, 8}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void execute_025eec_gamma_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 3:
        execute_order_list({12, 13}, acc);
        break;
      case 5:
        execute_order_list({12, 13}, acc);
        break;
      case 6:
        execute_order_list({12, 13}, acc);
        break;
      case 8:
        execute_order_list({12, 13}, acc);
        break;
      case 9:
        execute_order_list({12, 13}, acc);
        break;
      case 11:
        execute_order_list({12, 13}, acc);
        break;
      case 12:
        execute_order_list({12, 13}, acc);
        break;
      case 14:
        execute_order_list({12, 13}, acc);
        break;
      case 15:
        execute_order_list({12, 13}, acc);
        break;
      case 17:
        execute_order_list({12, 13}, acc);
        break;
      case 18:
        execute_order_list({12, 13}, acc);
        break;
      case 20:
        execute_order_list({12, 13}, acc);
        break;
      case 21:
        execute_order_list({12, 13}, acc);
        break;
      case 23:
        execute_order_list({12, 13}, acc);
        break;
      case 24:
        execute_order_list({12}, acc);
        break;
      case 28:
        execute_order_list({12, 13}, acc);
        break;
      case 31:
        execute_order_list({12}, acc);
        break;
      case 34:
        execute_order_list({12}, acc);
        break;
      case 40:
        execute_order_list({13}, acc);
        break;
      case 41:
        execute_order_list({12, 13}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void execute_025eec_post_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 0:
        execute_order_list({26, 29}, acc);
        break;
      case 1:
        execute_order_list({15}, acc);
        break;
      case 2:
        execute_order_list({29}, acc);
        break;
      case 3:
        execute_order_list({25, 27}, acc);
        break;
      case 4:
        execute_order_list({15}, acc);
        break;
      case 5:
        execute_order_list({25, 27}, acc);
        break;
      case 6:
        execute_order_list({25, 27}, acc);
        break;
      case 7:
        execute_order_list({15}, acc);
        break;
      case 8:
        execute_order_list({25, 27}, acc);
        break;
      case 9:
        execute_order_list({25, 27}, acc);
        break;
      case 10:
        execute_order_list({15}, acc);
        break;
      case 11:
        execute_order_list({25, 27}, acc);
        break;
      case 12:
        execute_order_list({25, 27}, acc);
        break;
      case 13:
        execute_order_list({15}, acc);
        break;
      case 14:
        execute_order_list({25, 27}, acc);
        break;
      case 15:
        execute_order_list({25, 27}, acc);
        break;
      case 16:
        execute_order_list({15}, acc);
        break;
      case 17:
        execute_order_list({25, 27}, acc);
        break;
      case 18:
        execute_order_list({25, 27}, acc);
        break;
      case 19:
        execute_order_list({15}, acc);
        break;
      case 20:
        execute_order_list({25, 27}, acc);
        break;
      case 21:
        execute_order_list({25, 27}, acc);
        break;
      case 22:
        execute_order_list({15}, acc);
        break;
      case 23:
        execute_order_list({25, 27}, acc);
        break;
      case 24:
        execute_order_list({27}, acc);
        break;
      case 25:
        execute_order_list({17, 27}, acc);
        break;
      case 26:
        execute_order_list({16}, acc);
        break;
      case 27:
        execute_order_list({27}, acc);
        break;
      case 28:
        execute_order_list({25, 27}, acc);
        break;
      case 29:
        execute_order_list({22, 26, 29}, acc);
        break;
      case 30:
        execute_order_list({27}, acc);
        break;
      case 31:
        execute_order_list({25, 27}, acc);
        break;
      case 32:
        execute_order_list({22, 26, 29}, acc);
        break;
      case 33:
        execute_order_list({27}, acc);
        break;
      case 34:
        execute_order_list({25, 27}, acc);
        break;
      case 35:
        execute_order_list({22, 26, 29}, acc);
        break;
      case 36:
        execute_order_list({27}, acc);
        break;
      case 37:
        execute_order_list({22}, acc);
        break;
      case 38:
        execute_order_list({25, 27}, acc);
        break;
      case 39:
        execute_order_list({29}, acc);
        break;
      case 40:
        execute_order_list({29}, acc);
        break;
      case 41:
        execute_order_list({25, 27}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void tick_ik1303_025eec_decoded() {
    static constexpr uint32_t kCommand = 0x025eec;
    if (tick_index == 0) command = kCommand;

    const int current_tick = tick_index;
    const bool branch_l = l != 0;
    if (current_tick < 27) {
      sync_address = 108;
    } else if (current_tick < 36) {
      sync_address = 61;
    } else {
      sync_address = 9;
    }

    Accumulator acc;
    execute_025eec_pre_add_orders(current_tick, branch_l, acc);

    if (key_y == 0) t = 0;

    execute_025eec_gamma_orders(current_tick, branch_l, acc);

    int sum = acc.alpha + acc.beta + acc.gamma;
    acc.sigma = sum & 0xF;
    carry = (sum >> 4) & 1;

    execute_025eec_post_add_orders(current_tick, branch_l, acc);

    output = memory[current_tick];
    memory[current_tick] = input;
    ++tick_index;
    if (tick_index == 42) tick_index = 0;
  }


  void execute_3c26d0_pre_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 27:
        execute_order_list({3}, acc);
        break;
      case 28:
        execute_order_list({0, 7}, acc);
        break;
      case 30:
        execute_order_list({3}, acc);
        break;
      case 31:
        execute_order_list({0, 7}, acc);
        break;
      case 33:
        execute_order_list({3}, acc);
        break;
      case 34:
        execute_order_list({0, 7}, acc);
        break;
      case 35:
        execute_order_list({5, 10, 11}, acc);
        break;
      case 36:
        execute_order_list({0, 11}, acc);
        break;
      case 37:
        if (branch_l) {
          execute_order_list({0}, acc);
        } else {
          execute_order_list({5}, acc);
        }
        break;
      case 38:
        execute_order_list({5, 11}, acc);
        break;
      case 39:
        execute_order_list({0}, acc);
        break;
      case 40:
        if (branch_l) {
          execute_order_list({0}, acc);
        } else {
          execute_order_list({5}, acc);
        }
        break;
      case 41:
        execute_order_list({0}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void execute_3c26d0_gamma_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 28:
        execute_order_list({12, 13}, acc);
        break;
      case 31:
        execute_order_list({12}, acc);
        break;
      case 34:
        execute_order_list({12}, acc);
        break;
      case 35:
        execute_order_list({12, 13}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void execute_3c26d0_post_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 27:
        execute_order_list({27}, acc);
        break;
      case 28:
        execute_order_list({25, 27}, acc);
        break;
      case 30:
        execute_order_list({27}, acc);
        break;
      case 31:
        execute_order_list({25, 27}, acc);
        break;
      case 33:
        execute_order_list({27}, acc);
        break;
      case 34:
        execute_order_list({25, 27}, acc);
        break;
      case 35:
        execute_order_list({25, 27}, acc);
        break;
      case 36:
        execute_order_list({27}, acc);
        break;
      case 37:
        if (branch_l) {
          execute_order_list({17, 22, 27}, acc);
        } else {
          execute_order_list({22}, acc);
        }
        break;
      case 38:
        execute_order_list({23}, acc);
        break;
      case 39:
        execute_order_list({27}, acc);
        break;
      case 40:
        if (branch_l) {
          execute_order_list({17, 22, 27}, acc);
        } else {
          execute_order_list({22}, acc);
        }
        break;
      case 41:
        execute_order_list({27}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void tick_ik1303_3c26d0_decoded() {
    static constexpr uint32_t kCommand = 0x3c26d0;
    if (tick_index == 0) command = kCommand;

    const int current_tick = tick_index;
    const bool branch_l = l != 0;
    if (current_tick < 27) {
      sync_address = 80;
    } else if (current_tick < 36) {
      sync_address = 77;
    } else {
      if (current_tick == 36) {
        r[37] = 0 & 0xF;
        r[40] = 240 >> 4;
      }
      sync_address = 95;
    }

    Accumulator acc;
    execute_3c26d0_pre_add_orders(current_tick, branch_l, acc);

    if (key_y == 0) t = 0;

    execute_3c26d0_gamma_orders(current_tick, branch_l, acc);

    int sum = acc.alpha + acc.beta + acc.gamma;
    acc.sigma = sum & 0xF;
    carry = (sum >> 4) & 1;

    execute_3c26d0_post_add_orders(current_tick, branch_l, acc);

    output = memory[current_tick];
    memory[current_tick] = input;
    ++tick_index;
    if (tick_index == 42) tick_index = 0;
  }


  void execute_030bd0_pre_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 27:
        execute_order_list({0, 11}, acc);
        break;
      case 28:
        execute_order_list({5}, acc);
        break;
      case 29:
        execute_order_list({6, 7, 8}, acc);
        break;
      case 30:
        execute_order_list({5, 11}, acc);
        break;
      case 31:
        execute_order_list({3, 7}, acc);
        break;
      case 34:
        execute_order_list({3}, acc);
        break;
      case 35:
        execute_order_list({4, 7}, acc);
        break;
      case 36:
        execute_order_list({0, 11}, acc);
        break;
      case 38:
        execute_order_list({5, 11}, acc);
        break;
      case 39:
        execute_order_list({7, 8}, acc);
        break;
      case 40:
        execute_order_list({1}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void execute_030bd0_gamma_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 28:
        execute_order_list({12}, acc);
        break;
      case 31:
        execute_order_list({12, 13}, acc);
        break;
      case 34:
        execute_order_list({12}, acc);
        break;
      case 39:
        execute_order_list({12, 13}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void execute_030bd0_post_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 27:
        execute_order_list({27}, acc);
        break;
      case 28:
        execute_order_list({22, 27}, acc);
        break;
      case 29:
        execute_order_list({27}, acc);
        break;
      case 30:
        execute_order_list({25, 27}, acc);
        break;
      case 31:
        execute_order_list({25}, acc);
        break;
      case 34:
        execute_order_list({25, 27}, acc);
        break;
      case 35:
        execute_order_list({22, 26, 29}, acc);
        break;
      case 36:
        execute_order_list({27}, acc);
        break;
      case 38:
        execute_order_list({23}, acc);
        break;
      case 39:
        execute_order_list({25, 27}, acc);
        break;
      case 40:
        execute_order_list({27}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void tick_ik1303_030bd0_decoded() {
    static constexpr uint32_t kCommand = 0x030bd0;
    if (tick_index == 0) command = kCommand;

    const int current_tick = tick_index;
    const bool branch_l = l != 0;
    if (current_tick < 27) {
      sync_address = 80;
    } else if (current_tick < 36) {
      sync_address = 23;
    } else {
      sync_address = 12;
    }

    Accumulator acc;
    execute_030bd0_pre_add_orders(current_tick, branch_l, acc);

    if (key_y == 0) t = 0;

    execute_030bd0_gamma_orders(current_tick, branch_l, acc);

    int sum = acc.alpha + acc.beta + acc.gamma;
    acc.sigma = sum & 0xF;
    carry = (sum >> 4) & 1;

    execute_030bd0_post_add_orders(current_tick, branch_l, acc);

    output = memory[current_tick];
    memory[current_tick] = input;
    ++tick_index;
    if (tick_index == 42) tick_index = 0;
  }


  void execute_01e550_pre_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 27:
        execute_order_list({5}, acc);
        break;
      case 28:
        execute_order_list({0, 7}, acc);
        break;
      case 29:
        execute_order_list({4, 7}, acc);
        break;
      case 30:
        execute_order_list({3}, acc);
        break;
      case 31:
        execute_order_list({0, 7}, acc);
        break;
      case 32:
        execute_order_list({4, 7}, acc);
        break;
      case 33:
        execute_order_list({3}, acc);
        break;
      case 34:
        execute_order_list({0, 7}, acc);
        break;
      case 35:
        execute_order_list({4, 7}, acc);
        break;
      case 36:
        execute_order_list({0, 11}, acc);
        break;
      case 37:
        execute_order_list({0}, acc);
        break;
      case 38:
        execute_order_list({0}, acc);
        break;
      case 39:
        execute_order_list({0}, acc);
        break;
      case 40:
        execute_order_list({0}, acc);
        break;
      case 41:
        execute_order_list({4, 7, 8}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void execute_01e550_gamma_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 31:
        execute_order_list({12}, acc);
        break;
      case 34:
        execute_order_list({12}, acc);
        break;
      case 41:
        execute_order_list({12}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void execute_01e550_post_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 27:
        execute_order_list({26, 29}, acc);
        break;
      case 28:
        execute_order_list({25, 27}, acc);
        break;
      case 29:
        execute_order_list({22, 26, 29}, acc);
        break;
      case 30:
        execute_order_list({27}, acc);
        break;
      case 31:
        execute_order_list({25, 27}, acc);
        break;
      case 32:
        execute_order_list({22, 26, 29}, acc);
        break;
      case 33:
        execute_order_list({27}, acc);
        break;
      case 34:
        execute_order_list({25, 27}, acc);
        break;
      case 35:
        execute_order_list({22, 26, 29}, acc);
        break;
      case 36:
        execute_order_list({17, 32}, acc);
        break;
      case 37:
        execute_order_list({22}, acc);
        break;
      case 38:
        execute_order_list({17, 27}, acc);
        break;
      case 39:
        execute_order_list({17, 32}, acc);
        break;
      case 40:
        execute_order_list({22}, acc);
        break;
      case 41:
        execute_order_list({27}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void tick_ik1303_01e550_decoded() {
    static constexpr uint32_t kCommand = 0x01e550;
    if (tick_index == 0) command = kCommand;

    const int current_tick = tick_index;
    const bool branch_l = l != 0;
    if (current_tick < 27) {
      sync_address = 80;
    } else if (current_tick < 36) {
      sync_address = 74;
    } else {
      sync_address = 7;
    }

    Accumulator acc;
    execute_01e550_pre_add_orders(current_tick, branch_l, acc);

    if (key_y == 0) t = 0;

    execute_01e550_gamma_orders(current_tick, branch_l, acc);

    int sum = acc.alpha + acc.beta + acc.gamma;
    acc.sigma = sum & 0xF;
    carry = (sum >> 4) & 1;

    execute_01e550_post_add_orders(current_tick, branch_l, acc);

    output = memory[current_tick];
    memory[current_tick] = input;
    ++tick_index;
    if (tick_index == 42) tick_index = 0;
  }


  void execute_6d2847_pre_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 1:
        execute_order_list({3, 11}, acc);
        break;
      case 2:
        execute_order_list({4, 7}, acc);
        break;
      case 3:
        execute_order_list({5}, acc);
        break;
      case 4:
        execute_order_list({3}, acc);
        break;
      case 5:
        execute_order_list({4, 7}, acc);
        break;
      case 6:
        execute_order_list({5}, acc);
        break;
      case 7:
        execute_order_list({3}, acc);
        break;
      case 8:
        execute_order_list({4, 7}, acc);
        break;
      case 9:
        execute_order_list({5}, acc);
        break;
      case 10:
        execute_order_list({3}, acc);
        break;
      case 11:
        execute_order_list({4, 7}, acc);
        break;
      case 12:
        execute_order_list({5}, acc);
        break;
      case 13:
        execute_order_list({3}, acc);
        break;
      case 14:
        execute_order_list({4, 7}, acc);
        break;
      case 15:
        execute_order_list({5}, acc);
        break;
      case 16:
        execute_order_list({3}, acc);
        break;
      case 17:
        execute_order_list({4, 7}, acc);
        break;
      case 18:
        execute_order_list({5}, acc);
        break;
      case 19:
        execute_order_list({3}, acc);
        break;
      case 20:
        execute_order_list({4, 7}, acc);
        break;
      case 21:
        execute_order_list({5}, acc);
        break;
      case 22:
        execute_order_list({3}, acc);
        break;
      case 23:
        execute_order_list({4, 7}, acc);
        break;
      case 24:
        execute_order_list({5}, acc);
        break;
      case 25:
        execute_order_list({3}, acc);
        break;
      case 26:
        execute_order_list({4, 7}, acc);
        break;
      case 36:
        execute_order_list({0, 11}, acc);
        break;
      case 37:
        if (branch_l) {
          execute_order_list({0}, acc);
        } else {
          execute_order_list({5}, acc);
        }
        break;
      case 38:
        execute_order_list({5, 11}, acc);
        break;
      case 39:
        execute_order_list({0}, acc);
        break;
      case 40:
        if (branch_l) {
          execute_order_list({0}, acc);
        } else {
          execute_order_list({5}, acc);
        }
        break;
      case 41:
        execute_order_list({0}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void execute_6d2847_gamma_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 4:
        execute_order_list({12}, acc);
        break;
      case 7:
        execute_order_list({12}, acc);
        break;
      case 10:
        execute_order_list({12}, acc);
        break;
      case 13:
        execute_order_list({12}, acc);
        break;
      case 16:
        execute_order_list({12}, acc);
        break;
      case 19:
        execute_order_list({12}, acc);
        break;
      case 22:
        execute_order_list({12}, acc);
        break;
      case 25:
        execute_order_list({12}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void execute_6d2847_post_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 1:
        execute_order_list({25, 27}, acc);
        break;
      case 2:
        execute_order_list({26, 29}, acc);
        break;
      case 3:
        execute_order_list({26, 29}, acc);
        break;
      case 4:
        execute_order_list({25, 27}, acc);
        break;
      case 5:
        execute_order_list({26, 29}, acc);
        break;
      case 6:
        execute_order_list({26, 29}, acc);
        break;
      case 7:
        execute_order_list({25, 27}, acc);
        break;
      case 8:
        execute_order_list({26, 29}, acc);
        break;
      case 9:
        execute_order_list({26, 29}, acc);
        break;
      case 10:
        execute_order_list({25, 27}, acc);
        break;
      case 11:
        execute_order_list({26, 29}, acc);
        break;
      case 12:
        execute_order_list({26, 29}, acc);
        break;
      case 13:
        execute_order_list({25, 27}, acc);
        break;
      case 14:
        execute_order_list({26, 29}, acc);
        break;
      case 15:
        execute_order_list({26, 29}, acc);
        break;
      case 16:
        execute_order_list({25, 27}, acc);
        break;
      case 17:
        execute_order_list({26, 29}, acc);
        break;
      case 18:
        execute_order_list({26, 29}, acc);
        break;
      case 19:
        execute_order_list({25, 27}, acc);
        break;
      case 20:
        execute_order_list({26, 29}, acc);
        break;
      case 21:
        execute_order_list({26, 29}, acc);
        break;
      case 22:
        execute_order_list({25, 27}, acc);
        break;
      case 23:
        execute_order_list({26, 29}, acc);
        break;
      case 24:
        execute_order_list({26, 29}, acc);
        break;
      case 25:
        execute_order_list({25, 27}, acc);
        break;
      case 26:
        execute_order_list({26, 29}, acc);
        break;
      case 36:
        execute_order_list({27}, acc);
        break;
      case 37:
        if (branch_l) {
          execute_order_list({17, 22, 27}, acc);
        } else {
          execute_order_list({22}, acc);
        }
        break;
      case 38:
        execute_order_list({23}, acc);
        break;
      case 39:
        execute_order_list({27}, acc);
        break;
      case 40:
        if (branch_l) {
          execute_order_list({17, 22, 27}, acc);
        } else {
          execute_order_list({22}, acc);
        }
        break;
      case 41:
        execute_order_list({27}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void tick_ik1303_6d2847_decoded() {
    static constexpr uint32_t kCommand = 0x6d2847;
    if (tick_index == 0) command = kCommand;

    const int current_tick = tick_index;
    const bool branch_l = l != 0;
    if (current_tick < 27) {
      sync_address = 71;
    } else if (current_tick < 36) {
      sync_address = 80;
    } else {
      if (current_tick == 36) {
        r[37] = 4 & 0xF;
        r[40] = 180 >> 4;
      }
      sync_address = 95;
    }

    Accumulator acc;
    execute_6d2847_pre_add_orders(current_tick, branch_l, acc);

    if (key_y == 0) t = 0;

    execute_6d2847_gamma_orders(current_tick, branch_l, acc);

    int sum = acc.alpha + acc.beta + acc.gamma;
    acc.sigma = sum & 0xF;
    carry = (sum >> 4) & 1;

    execute_6d2847_post_add_orders(current_tick, branch_l, acc);

    output = memory[current_tick];
    memory[current_tick] = input;
    ++tick_index;
    if (tick_index == 42) tick_index = 0;
  }


  void execute_0f1020_pre_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 0:
        execute_order_list({0}, acc);
        break;
      case 1:
        execute_order_list({0}, acc);
        break;
      case 2:
        execute_order_list({0, 3, 9}, acc);
        break;
      case 3:
        execute_order_list({0}, acc);
        break;
      case 4:
        execute_order_list({0}, acc);
        break;
      case 6:
        execute_order_list({0}, acc);
        break;
      case 7:
        execute_order_list({0}, acc);
        break;
      case 9:
        execute_order_list({0}, acc);
        break;
      case 10:
        execute_order_list({0}, acc);
        break;
      case 12:
        execute_order_list({0}, acc);
        break;
      case 13:
        execute_order_list({0}, acc);
        break;
      case 15:
        execute_order_list({0}, acc);
        break;
      case 16:
        execute_order_list({0}, acc);
        break;
      case 18:
        execute_order_list({0}, acc);
        break;
      case 19:
        execute_order_list({0}, acc);
        break;
      case 21:
        execute_order_list({0}, acc);
        break;
      case 22:
        execute_order_list({0}, acc);
        break;
      case 24:
        execute_order_list({0}, acc);
        break;
      case 25:
        execute_order_list({0}, acc);
        break;
      case 26:
        execute_order_list({0, 3, 9}, acc);
        break;
      case 27:
        execute_order_list({0}, acc);
        break;
      case 28:
        execute_order_list({0}, acc);
        break;
      case 29:
        execute_order_list({0, 3, 9}, acc);
        break;
      case 30:
        execute_order_list({0}, acc);
        break;
      case 31:
        execute_order_list({0}, acc);
        break;
      case 33:
        execute_order_list({0}, acc);
        break;
      case 34:
        execute_order_list({0}, acc);
        break;
      case 35:
        execute_order_list({0, 3, 9}, acc);
        break;
      case 36:
        execute_order_list({0, 11}, acc);
        break;
      case 37:
        if (branch_l) {
          execute_order_list({0}, acc);
        } else {
          execute_order_list({5}, acc);
        }
        break;
      case 38:
        execute_order_list({5, 11}, acc);
        break;
      case 39:
        execute_order_list({0}, acc);
        break;
      case 40:
        if (branch_l) {
          execute_order_list({0}, acc);
        } else {
          execute_order_list({5}, acc);
        }
        break;
      case 41:
        execute_order_list({0}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void execute_0f1020_gamma_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      default:
        break;
    }
    (void)branch_l;
  }

  void execute_0f1020_post_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 0:
        execute_order_list({27}, acc);
        break;
      case 1:
        execute_order_list({17, 22}, acc);
        break;
      case 2:
        execute_order_list({25, 29}, acc);
        break;
      case 3:
        execute_order_list({27}, acc);
        break;
      case 4:
        execute_order_list({17, 22}, acc);
        break;
      case 6:
        execute_order_list({27}, acc);
        break;
      case 7:
        execute_order_list({17, 22}, acc);
        break;
      case 9:
        execute_order_list({27}, acc);
        break;
      case 10:
        execute_order_list({17, 22}, acc);
        break;
      case 12:
        execute_order_list({27}, acc);
        break;
      case 13:
        execute_order_list({17, 22}, acc);
        break;
      case 15:
        execute_order_list({27}, acc);
        break;
      case 16:
        execute_order_list({17, 22}, acc);
        break;
      case 18:
        execute_order_list({27}, acc);
        break;
      case 19:
        execute_order_list({17, 22}, acc);
        break;
      case 21:
        execute_order_list({27}, acc);
        break;
      case 22:
        execute_order_list({17, 22}, acc);
        break;
      case 24:
        execute_order_list({27}, acc);
        break;
      case 25:
        execute_order_list({17, 22}, acc);
        break;
      case 26:
        execute_order_list({25, 29}, acc);
        break;
      case 27:
        execute_order_list({27}, acc);
        break;
      case 28:
        execute_order_list({17, 22}, acc);
        break;
      case 29:
        execute_order_list({25, 29}, acc);
        break;
      case 30:
        execute_order_list({27}, acc);
        break;
      case 31:
        execute_order_list({17, 22}, acc);
        break;
      case 33:
        execute_order_list({27}, acc);
        break;
      case 34:
        execute_order_list({17, 22}, acc);
        break;
      case 35:
        execute_order_list({25, 29}, acc);
        break;
      case 36:
        execute_order_list({27}, acc);
        break;
      case 37:
        if (branch_l) {
          execute_order_list({17, 22, 27}, acc);
        } else {
          execute_order_list({22}, acc);
        }
        break;
      case 38:
        execute_order_list({23}, acc);
        break;
      case 39:
        execute_order_list({27}, acc);
        break;
      case 40:
        if (branch_l) {
          execute_order_list({17, 22, 27}, acc);
        } else {
          execute_order_list({22}, acc);
        }
        break;
      case 41:
        execute_order_list({27}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void tick_ik1303_0f1020_decoded() {
    static constexpr uint32_t kCommand = 0x0f1020;
    if (tick_index == 0) command = kCommand;

    const int current_tick = tick_index;
    const bool branch_l = l != 0;
    if (current_tick < 27) {
      sync_address = 32;
    } else if (current_tick < 36) {
      sync_address = 32;
    } else {
      if (current_tick == 36) {
        r[37] = 12 & 0xF;
        r[40] = 60 >> 4;
      }
      sync_address = 95;
    }

    Accumulator acc;
    execute_0f1020_pre_add_orders(current_tick, branch_l, acc);

    if (key_y == 0) t = 0;

    execute_0f1020_gamma_orders(current_tick, branch_l, acc);

    int sum = acc.alpha + acc.beta + acc.gamma;
    acc.sigma = sum & 0xF;
    carry = (sum >> 4) & 1;

    execute_0f1020_post_add_orders(current_tick, branch_l, acc);

    output = memory[current_tick];
    memory[current_tick] = input;
    ++tick_index;
    if (tick_index == 42) tick_index = 0;
  }


  void execute_0764b3_pre_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 0:
        execute_order_list({7, 8}, acc);
        break;
      case 1:
        execute_order_list({0}, acc);
        break;
      case 2:
        execute_order_list({0}, acc);
        break;
      case 3:
        execute_order_list({8}, acc);
        break;
      case 4:
        execute_order_list({0}, acc);
        break;
      case 5:
        execute_order_list({0}, acc);
        break;
      case 6:
        execute_order_list({8}, acc);
        break;
      case 7:
        execute_order_list({0}, acc);
        break;
      case 8:
        execute_order_list({0}, acc);
        break;
      case 9:
        execute_order_list({8}, acc);
        break;
      case 10:
        execute_order_list({0}, acc);
        break;
      case 11:
        execute_order_list({0}, acc);
        break;
      case 12:
        execute_order_list({8}, acc);
        break;
      case 13:
        execute_order_list({0}, acc);
        break;
      case 14:
        execute_order_list({0}, acc);
        break;
      case 15:
        execute_order_list({8}, acc);
        break;
      case 16:
        execute_order_list({0}, acc);
        break;
      case 17:
        execute_order_list({0}, acc);
        break;
      case 18:
        execute_order_list({8}, acc);
        break;
      case 19:
        execute_order_list({0}, acc);
        break;
      case 20:
        execute_order_list({0}, acc);
        break;
      case 21:
        execute_order_list({8}, acc);
        break;
      case 22:
        execute_order_list({0}, acc);
        break;
      case 23:
        execute_order_list({0}, acc);
        break;
      case 24:
        execute_order_list({8}, acc);
        break;
      case 25:
        execute_order_list({0}, acc);
        break;
      case 27:
        execute_order_list({5, 8}, acc);
        break;
      case 28:
        execute_order_list({0, 7}, acc);
        break;
      case 29:
        execute_order_list({4, 7}, acc);
        break;
      case 31:
        execute_order_list({0, 7, 8}, acc);
        break;
      case 32:
        execute_order_list({4, 7}, acc);
        break;
      case 34:
        execute_order_list({0, 7, 8}, acc);
        break;
      case 35:
        execute_order_list({4, 7}, acc);
        break;
      case 36:
        execute_order_list({0, 7, 8}, acc);
        break;
      case 37:
        execute_order_list({0, 3, 7}, acc);
        break;
      case 38:
        execute_order_list({5}, acc);
        break;
      case 40:
        execute_order_list({7, 8}, acc);
        break;
      case 41:
        execute_order_list({5}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void execute_0764b3_gamma_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 0:
        execute_order_list({12, 13}, acc);
        break;
      case 3:
        execute_order_list({12, 13}, acc);
        break;
      case 6:
        execute_order_list({12, 13}, acc);
        break;
      case 9:
        execute_order_list({12, 13}, acc);
        break;
      case 12:
        execute_order_list({12, 13}, acc);
        break;
      case 15:
        execute_order_list({12, 13}, acc);
        break;
      case 18:
        execute_order_list({12, 13}, acc);
        break;
      case 21:
        execute_order_list({12, 13}, acc);
        break;
      case 24:
        execute_order_list({12}, acc);
        break;
      case 31:
        execute_order_list({12}, acc);
        break;
      case 34:
        execute_order_list({12}, acc);
        break;
      case 40:
        execute_order_list({12, 13}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void execute_0764b3_post_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 0:
        execute_order_list({25, 27}, acc);
        break;
      case 1:
        execute_order_list({17, 26, 29}, acc);
        break;
      case 2:
        execute_order_list({17, 26, 29}, acc);
        break;
      case 3:
        execute_order_list({25}, acc);
        break;
      case 4:
        execute_order_list({17, 26, 29}, acc);
        break;
      case 5:
        execute_order_list({17, 26, 29}, acc);
        break;
      case 6:
        execute_order_list({25}, acc);
        break;
      case 7:
        execute_order_list({17, 26, 29}, acc);
        break;
      case 8:
        execute_order_list({17, 26, 29}, acc);
        break;
      case 9:
        execute_order_list({25}, acc);
        break;
      case 10:
        execute_order_list({17, 26, 29}, acc);
        break;
      case 11:
        execute_order_list({17, 26, 29}, acc);
        break;
      case 12:
        execute_order_list({25}, acc);
        break;
      case 13:
        execute_order_list({17, 26, 29}, acc);
        break;
      case 14:
        execute_order_list({17, 26, 29}, acc);
        break;
      case 15:
        execute_order_list({25}, acc);
        break;
      case 16:
        execute_order_list({17, 26, 29}, acc);
        break;
      case 17:
        execute_order_list({17, 26, 29}, acc);
        break;
      case 18:
        execute_order_list({25}, acc);
        break;
      case 19:
        execute_order_list({17, 26, 29}, acc);
        break;
      case 20:
        execute_order_list({17, 26, 29}, acc);
        break;
      case 21:
        execute_order_list({25}, acc);
        break;
      case 22:
        execute_order_list({17, 26, 29}, acc);
        break;
      case 23:
        execute_order_list({17, 26, 29}, acc);
        break;
      case 24:
        execute_order_list({25}, acc);
        break;
      case 25:
        execute_order_list({17, 26, 29}, acc);
        break;
      case 27:
        execute_order_list({27}, acc);
        break;
      case 28:
        execute_order_list({25, 27}, acc);
        break;
      case 29:
        execute_order_list({22, 26, 29}, acc);
        break;
      case 31:
        execute_order_list({25, 27}, acc);
        break;
      case 32:
        execute_order_list({22, 26, 29}, acc);
        break;
      case 34:
        execute_order_list({25, 27}, acc);
        break;
      case 35:
        execute_order_list({22, 26, 29}, acc);
        break;
      case 36:
        execute_order_list({16, 25, 27}, acc);
        break;
      case 37:
        execute_order_list({25, 27}, acc);
        break;
      case 38:
        execute_order_list({23}, acc);
        break;
      case 40:
        execute_order_list({25, 27}, acc);
        break;
      case 41:
        execute_order_list({26, 29}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void tick_ik1303_0764b3_decoded() {
    static constexpr uint32_t kCommand = 0x0764b3;
    if (tick_index == 0) command = kCommand;

    const int current_tick = tick_index;
    const bool branch_l = l != 0;
    if (current_tick < 27) {
      sync_address = 51;
    } else if (current_tick < 36) {
      sync_address = 73;
    } else {
      sync_address = 29;
    }

    Accumulator acc;
    execute_0764b3_pre_add_orders(current_tick, branch_l, acc);

    if (key_y == 0) t = 0;

    execute_0764b3_gamma_orders(current_tick, branch_l, acc);

    int sum = acc.alpha + acc.beta + acc.gamma;
    acc.sigma = sum & 0xF;
    carry = (sum >> 4) & 1;

    execute_0764b3_post_add_orders(current_tick, branch_l, acc);

    output = memory[current_tick];
    memory[current_tick] = input;
    ++tick_index;
    if (tick_index == 42) tick_index = 0;
  }


  void execute_0152d6_pre_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 3:
        execute_order_list({0}, acc);
        break;
      case 5:
        execute_order_list({7, 8}, acc);
        break;
      case 6:
        execute_order_list({0}, acc);
        break;
      case 8:
        execute_order_list({7, 8}, acc);
        break;
      case 9:
        execute_order_list({0}, acc);
        break;
      case 11:
        execute_order_list({7, 8}, acc);
        break;
      case 12:
        execute_order_list({0}, acc);
        break;
      case 14:
        execute_order_list({7, 8}, acc);
        break;
      case 15:
        execute_order_list({0}, acc);
        break;
      case 17:
        execute_order_list({7, 8}, acc);
        break;
      case 18:
        execute_order_list({0}, acc);
        break;
      case 20:
        execute_order_list({7, 8}, acc);
        break;
      case 21:
        execute_order_list({0}, acc);
        break;
      case 23:
        execute_order_list({7, 8}, acc);
        break;
      case 24:
        execute_order_list({0}, acc);
        break;
      case 25:
        execute_order_list({0, 7}, acc);
        break;
      case 26:
        execute_order_list({6, 8, 10}, acc);
        break;
      case 27:
        execute_order_list({6, 11}, acc);
        break;
      case 30:
        execute_order_list({9}, acc);
        break;
      case 31:
        execute_order_list({0, 3, 7}, acc);
        break;
      case 32:
        execute_order_list({9}, acc);
        break;
      case 33:
        execute_order_list({0, 3, 9}, acc);
        break;
      case 35:
        execute_order_list({5, 8}, acc);
        break;
      case 36:
        execute_order_list({7, 8}, acc);
        break;
      case 37:
        execute_order_list({0}, acc);
        break;
      case 40:
        execute_order_list({0}, acc);
        break;
      case 41:
        execute_order_list({0}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void execute_0152d6_gamma_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 5:
        execute_order_list({12, 13}, acc);
        break;
      case 8:
        execute_order_list({12, 13}, acc);
        break;
      case 11:
        execute_order_list({12, 13}, acc);
        break;
      case 14:
        execute_order_list({12, 13}, acc);
        break;
      case 17:
        execute_order_list({12, 13}, acc);
        break;
      case 20:
        execute_order_list({12, 13}, acc);
        break;
      case 23:
        execute_order_list({12, 13}, acc);
        break;
      case 32:
        execute_order_list({13}, acc);
        break;
      case 36:
        execute_order_list({12, 13}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void execute_0152d6_post_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 3:
        execute_order_list({29}, acc);
        break;
      case 5:
        execute_order_list({25, 27}, acc);
        break;
      case 6:
        execute_order_list({29}, acc);
        break;
      case 8:
        execute_order_list({25, 27}, acc);
        break;
      case 9:
        execute_order_list({29}, acc);
        break;
      case 11:
        execute_order_list({25, 27}, acc);
        break;
      case 12:
        execute_order_list({29}, acc);
        break;
      case 14:
        execute_order_list({25, 27}, acc);
        break;
      case 15:
        execute_order_list({29}, acc);
        break;
      case 17:
        execute_order_list({25, 27}, acc);
        break;
      case 18:
        execute_order_list({29}, acc);
        break;
      case 20:
        execute_order_list({25, 27}, acc);
        break;
      case 21:
        execute_order_list({29}, acc);
        break;
      case 23:
        execute_order_list({25, 27}, acc);
        break;
      case 24:
        execute_order_list({17, 27}, acc);
        break;
      case 25:
        execute_order_list({25, 27}, acc);
        break;
      case 26:
        execute_order_list({25}, acc);
        break;
      case 27:
        execute_order_list({27}, acc);
        break;
      case 28:
        execute_order_list({22}, acc);
        break;
      case 29:
        execute_order_list({29}, acc);
        break;
      case 30:
        execute_order_list({16}, acc);
        break;
      case 31:
        execute_order_list({25, 27}, acc);
        break;
      case 32:
        execute_order_list({29}, acc);
        break;
      case 33:
        execute_order_list({25, 29}, acc);
        break;
      case 34:
        execute_order_list({22}, acc);
        break;
      case 35:
        execute_order_list({27}, acc);
        break;
      case 36:
        execute_order_list({25, 27}, acc);
        break;
      case 37:
        execute_order_list({22}, acc);
        break;
      case 40:
        execute_order_list({22}, acc);
        break;
      case 41:
        execute_order_list({27}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void tick_ik1303_0152d6_decoded() {
    static constexpr uint32_t kCommand = 0x0152d6;
    if (tick_index == 0) command = kCommand;

    const int current_tick = tick_index;
    const bool branch_l = l != 0;
    if (current_tick < 27) {
      sync_address = 86;
    } else if (current_tick < 36) {
      sync_address = 37;
    } else {
      sync_address = 5;
    }

    Accumulator acc;
    execute_0152d6_pre_add_orders(current_tick, branch_l, acc);

    if (key_y == 0) t = 0;

    execute_0152d6_gamma_orders(current_tick, branch_l, acc);

    int sum = acc.alpha + acc.beta + acc.gamma;
    acc.sigma = sum & 0xF;
    carry = (sum >> 4) & 1;

    execute_0152d6_post_add_orders(current_tick, branch_l, acc);

    output = memory[current_tick];
    memory[current_tick] = input;
    ++tick_index;
    if (tick_index == 42) tick_index = 0;
  }


  void execute_1babd6_pre_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 3:
        execute_order_list({0}, acc);
        break;
      case 5:
        execute_order_list({7, 8}, acc);
        break;
      case 6:
        execute_order_list({0}, acc);
        break;
      case 8:
        execute_order_list({7, 8}, acc);
        break;
      case 9:
        execute_order_list({0}, acc);
        break;
      case 11:
        execute_order_list({7, 8}, acc);
        break;
      case 12:
        execute_order_list({0}, acc);
        break;
      case 14:
        execute_order_list({7, 8}, acc);
        break;
      case 15:
        execute_order_list({0}, acc);
        break;
      case 17:
        execute_order_list({7, 8}, acc);
        break;
      case 18:
        execute_order_list({0}, acc);
        break;
      case 20:
        execute_order_list({7, 8}, acc);
        break;
      case 21:
        execute_order_list({0}, acc);
        break;
      case 23:
        execute_order_list({7, 8}, acc);
        break;
      case 24:
        execute_order_list({0}, acc);
        break;
      case 25:
        execute_order_list({0, 7}, acc);
        break;
      case 26:
        execute_order_list({6, 8, 10}, acc);
        break;
      case 27:
        execute_order_list({4, 7, 8}, acc);
        break;
      case 28:
        execute_order_list({5}, acc);
        break;
      case 29:
        execute_order_list({7, 8}, acc);
        break;
      case 30:
        execute_order_list({5, 10, 11}, acc);
        break;
      case 31:
        execute_order_list({5}, acc);
        break;
      case 32:
        execute_order_list({8}, acc);
        break;
      case 33:
        execute_order_list({5}, acc);
        break;
      case 35:
        execute_order_list({0}, acc);
        break;
      case 36:
        execute_order_list({0, 11}, acc);
        break;
      case 37:
        if (branch_l) {
          execute_order_list({0}, acc);
        } else {
          execute_order_list({5}, acc);
        }
        break;
      case 38:
        execute_order_list({5, 11}, acc);
        break;
      case 39:
        execute_order_list({0}, acc);
        break;
      case 40:
        if (branch_l) {
          execute_order_list({0}, acc);
        } else {
          execute_order_list({5}, acc);
        }
        break;
      case 41:
        execute_order_list({0}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void execute_1babd6_gamma_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 5:
        execute_order_list({12, 13}, acc);
        break;
      case 8:
        execute_order_list({12, 13}, acc);
        break;
      case 11:
        execute_order_list({12, 13}, acc);
        break;
      case 14:
        execute_order_list({12, 13}, acc);
        break;
      case 17:
        execute_order_list({12, 13}, acc);
        break;
      case 20:
        execute_order_list({12, 13}, acc);
        break;
      case 23:
        execute_order_list({12, 13}, acc);
        break;
      case 27:
        execute_order_list({12}, acc);
        break;
      case 29:
        execute_order_list({12, 13}, acc);
        break;
      case 30:
        execute_order_list({12, 13}, acc);
        break;
      case 32:
        execute_order_list({12, 13}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void execute_1babd6_post_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 3:
        execute_order_list({29}, acc);
        break;
      case 5:
        execute_order_list({25, 27}, acc);
        break;
      case 6:
        execute_order_list({29}, acc);
        break;
      case 8:
        execute_order_list({25, 27}, acc);
        break;
      case 9:
        execute_order_list({29}, acc);
        break;
      case 11:
        execute_order_list({25, 27}, acc);
        break;
      case 12:
        execute_order_list({29}, acc);
        break;
      case 14:
        execute_order_list({25, 27}, acc);
        break;
      case 15:
        execute_order_list({29}, acc);
        break;
      case 17:
        execute_order_list({25, 27}, acc);
        break;
      case 18:
        execute_order_list({29}, acc);
        break;
      case 20:
        execute_order_list({25, 27}, acc);
        break;
      case 21:
        execute_order_list({29}, acc);
        break;
      case 23:
        execute_order_list({25, 27}, acc);
        break;
      case 24:
        execute_order_list({17, 27}, acc);
        break;
      case 25:
        execute_order_list({25, 27}, acc);
        break;
      case 26:
        execute_order_list({25}, acc);
        break;
      case 27:
        execute_order_list({27}, acc);
        break;
      case 28:
        execute_order_list({23}, acc);
        break;
      case 29:
        execute_order_list({25, 27}, acc);
        break;
      case 30:
        execute_order_list({25, 27}, acc);
        break;
      case 31:
        execute_order_list({26, 29}, acc);
        break;
      case 32:
        execute_order_list({25}, acc);
        break;
      case 33:
        execute_order_list({26, 29}, acc);
        break;
      case 35:
        execute_order_list({17, 27}, acc);
        break;
      case 36:
        execute_order_list({27}, acc);
        break;
      case 37:
        if (branch_l) {
          execute_order_list({17, 22, 27}, acc);
        } else {
          execute_order_list({22}, acc);
        }
        break;
      case 38:
        execute_order_list({23}, acc);
        break;
      case 39:
        execute_order_list({27}, acc);
        break;
      case 40:
        if (branch_l) {
          execute_order_list({17, 22, 27}, acc);
        } else {
          execute_order_list({22}, acc);
        }
        break;
      case 41:
        execute_order_list({27}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void tick_ik1303_1babd6_decoded() {
    static constexpr uint32_t kCommand = 0x1babd6;
    if (tick_index == 0) command = kCommand;

    const int current_tick = tick_index;
    const bool branch_l = l != 0;
    if (current_tick < 27) {
      sync_address = 86;
    } else if (current_tick < 36) {
      sync_address = 87;
    } else {
      if (current_tick == 36) {
        r[37] = 14 & 0xF;
        r[40] = 110 >> 4;
      }
      sync_address = 95;
    }

    Accumulator acc;
    execute_1babd6_pre_add_orders(current_tick, branch_l, acc);

    if (key_y == 0) t = 0;

    execute_1babd6_gamma_orders(current_tick, branch_l, acc);

    int sum = acc.alpha + acc.beta + acc.gamma;
    acc.sigma = sum & 0xF;
    carry = (sum >> 4) & 1;

    execute_1babd6_post_add_orders(current_tick, branch_l, acc);

    output = memory[current_tick];
    memory[current_tick] = input;
    ++tick_index;
    if (tick_index == 42) tick_index = 0;
  }


  void execute_397ed8_pre_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 0:
        execute_order_list({5}, acc);
        break;
      case 1:
        execute_order_list({8}, acc);
        break;
      case 3:
        execute_order_list({5, 7}, acc);
        break;
      case 4:
        execute_order_list({5, 7}, acc);
        break;
      case 6:
        execute_order_list({5, 7}, acc);
        break;
      case 7:
        execute_order_list({5, 7}, acc);
        break;
      case 9:
        execute_order_list({5, 7}, acc);
        break;
      case 10:
        execute_order_list({5, 7}, acc);
        break;
      case 12:
        execute_order_list({5, 7}, acc);
        break;
      case 13:
        execute_order_list({5, 7}, acc);
        break;
      case 15:
        execute_order_list({5, 7}, acc);
        break;
      case 16:
        execute_order_list({5, 7}, acc);
        break;
      case 18:
        execute_order_list({5, 7}, acc);
        break;
      case 19:
        execute_order_list({5, 7}, acc);
        break;
      case 21:
        execute_order_list({5, 7}, acc);
        break;
      case 22:
        execute_order_list({5, 7}, acc);
        break;
      case 24:
        execute_order_list({5}, acc);
        break;
      case 27:
        execute_order_list({0}, acc);
        break;
      case 28:
        execute_order_list({8}, acc);
        break;
      case 29:
        execute_order_list({5}, acc);
        break;
      case 30:
        execute_order_list({10}, acc);
        break;
      case 31:
        execute_order_list({5, 11}, acc);
        break;
      case 32:
        execute_order_list({0}, acc);
        break;
      case 33:
        execute_order_list({5, 8}, acc);
        break;
      case 34:
        execute_order_list({8}, acc);
        break;
      case 35:
        execute_order_list({0}, acc);
        break;
      case 36:
        execute_order_list({0, 11}, acc);
        break;
      case 37:
        if (branch_l) {
          execute_order_list({0}, acc);
        } else {
          execute_order_list({5}, acc);
        }
        break;
      case 38:
        execute_order_list({5, 11}, acc);
        break;
      case 39:
        execute_order_list({0}, acc);
        break;
      case 40:
        if (branch_l) {
          execute_order_list({0}, acc);
        } else {
          execute_order_list({5}, acc);
        }
        break;
      case 41:
        execute_order_list({0}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void execute_397ed8_gamma_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 1:
        execute_order_list({12, 13}, acc);
        break;
      case 3:
        execute_order_list({12}, acc);
        break;
      case 4:
        execute_order_list({12}, acc);
        break;
      case 6:
        execute_order_list({12}, acc);
        break;
      case 7:
        execute_order_list({12}, acc);
        break;
      case 9:
        execute_order_list({12}, acc);
        break;
      case 10:
        execute_order_list({12}, acc);
        break;
      case 12:
        execute_order_list({12}, acc);
        break;
      case 13:
        execute_order_list({12}, acc);
        break;
      case 15:
        execute_order_list({12}, acc);
        break;
      case 16:
        execute_order_list({12}, acc);
        break;
      case 18:
        execute_order_list({12}, acc);
        break;
      case 19:
        execute_order_list({12}, acc);
        break;
      case 21:
        execute_order_list({12}, acc);
        break;
      case 22:
        execute_order_list({12}, acc);
        break;
      case 24:
        execute_order_list({12}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void execute_397ed8_post_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 0:
        execute_order_list({26, 29}, acc);
        break;
      case 1:
        execute_order_list({25}, acc);
        break;
      case 3:
        execute_order_list({25, 27}, acc);
        break;
      case 4:
        execute_order_list({25, 27}, acc);
        break;
      case 6:
        execute_order_list({25, 27}, acc);
        break;
      case 7:
        execute_order_list({25, 27}, acc);
        break;
      case 9:
        execute_order_list({25, 27}, acc);
        break;
      case 10:
        execute_order_list({25, 27}, acc);
        break;
      case 12:
        execute_order_list({25, 27}, acc);
        break;
      case 13:
        execute_order_list({25, 27}, acc);
        break;
      case 15:
        execute_order_list({25, 27}, acc);
        break;
      case 16:
        execute_order_list({25, 27}, acc);
        break;
      case 18:
        execute_order_list({25, 27}, acc);
        break;
      case 19:
        execute_order_list({25, 27}, acc);
        break;
      case 21:
        execute_order_list({25, 27}, acc);
        break;
      case 22:
        execute_order_list({25, 27}, acc);
        break;
      case 24:
        execute_order_list({27}, acc);
        break;
      case 25:
        execute_order_list({22}, acc);
        break;
      case 26:
        execute_order_list({22}, acc);
        break;
      case 27:
        execute_order_list({17, 27}, acc);
        break;
      case 28:
        execute_order_list({27}, acc);
        break;
      case 29:
        execute_order_list({26, 29}, acc);
        break;
      case 30:
        execute_order_list({27}, acc);
        break;
      case 31:
        execute_order_list({25, 27}, acc);
        break;
      case 32:
        execute_order_list({17, 27}, acc);
        break;
      case 33:
        execute_order_list({27}, acc);
        break;
      case 34:
        execute_order_list({27}, acc);
        break;
      case 35:
        execute_order_list({17, 27}, acc);
        break;
      case 36:
        execute_order_list({27}, acc);
        break;
      case 37:
        if (branch_l) {
          execute_order_list({17, 22, 27}, acc);
        } else {
          execute_order_list({22}, acc);
        }
        break;
      case 38:
        execute_order_list({23}, acc);
        break;
      case 39:
        execute_order_list({27}, acc);
        break;
      case 40:
        if (branch_l) {
          execute_order_list({17, 22, 27}, acc);
        } else {
          execute_order_list({22}, acc);
        }
        break;
      case 41:
        execute_order_list({27}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void tick_ik1303_397ed8_decoded() {
    static constexpr uint32_t kCommand = 0x397ed8;
    if (tick_index == 0) command = kCommand;

    const int current_tick = tick_index;
    const bool branch_l = l != 0;
    if (current_tick < 27) {
      sync_address = 88;
    } else if (current_tick < 36) {
      sync_address = 125;
    } else {
      if (current_tick == 36) {
        r[37] = 5 & 0xF;
        r[40] = 229 >> 4;
      }
      sync_address = 95;
    }

    Accumulator acc;
    execute_397ed8_pre_add_orders(current_tick, branch_l, acc);

    if (key_y == 0) t = 0;

    execute_397ed8_gamma_orders(current_tick, branch_l, acc);

    int sum = acc.alpha + acc.beta + acc.gamma;
    acc.sigma = sum & 0xF;
    carry = (sum >> 4) & 1;

    execute_397ed8_post_add_orders(current_tick, branch_l, acc);

    output = memory[current_tick];
    memory[current_tick] = input;
    ++tick_index;
    if (tick_index == 42) tick_index = 0;
  }


  void execute_032050_pre_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 27:
        execute_order_list({10}, acc);
        break;
      case 28:
        execute_order_list({5, 11}, acc);
        break;
      case 29:
        execute_order_list({5, 11}, acc);
        break;
      case 30:
        execute_order_list({6, 11}, acc);
        break;
      case 31:
        execute_order_list({5}, acc);
        break;
      case 32:
        execute_order_list({7, 8}, acc);
        break;
      case 33:
        execute_order_list({5, 11}, acc);
        break;
      case 34:
        execute_order_list({5}, acc);
        break;
      case 36:
        execute_order_list({0, 11}, acc);
        break;
      case 38:
        execute_order_list({5, 11}, acc);
        break;
      case 39:
        execute_order_list({7, 8}, acc);
        break;
      case 40:
        execute_order_list({1}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void execute_032050_gamma_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 32:
        execute_order_list({12, 13}, acc);
        break;
      case 39:
        execute_order_list({12, 13}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void execute_032050_post_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 27:
        execute_order_list({27}, acc);
        break;
      case 28:
        execute_order_list({23}, acc);
        break;
      case 29:
        execute_order_list({23}, acc);
        break;
      case 30:
        execute_order_list({27}, acc);
        break;
      case 31:
        execute_order_list({22}, acc);
        break;
      case 32:
        execute_order_list({25, 27}, acc);
        break;
      case 33:
        execute_order_list({25, 27}, acc);
        break;
      case 34:
        execute_order_list({22}, acc);
        break;
      case 35:
        execute_order_list({29}, acc);
        break;
      case 36:
        execute_order_list({27}, acc);
        break;
      case 38:
        execute_order_list({23}, acc);
        break;
      case 39:
        execute_order_list({25, 27}, acc);
        break;
      case 40:
        execute_order_list({27}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void tick_ik1303_032050_decoded() {
    static constexpr uint32_t kCommand = 0x032050;
    if (tick_index == 0) command = kCommand;

    const int current_tick = tick_index;
    const bool branch_l = l != 0;
    if (current_tick < 27) {
      sync_address = 80;
    } else if (current_tick < 36) {
      sync_address = 64;
    } else {
      sync_address = 12;
    }

    Accumulator acc;
    execute_032050_pre_add_orders(current_tick, branch_l, acc);

    if (key_y == 0) t = 0;

    execute_032050_gamma_orders(current_tick, branch_l, acc);

    int sum = acc.alpha + acc.beta + acc.gamma;
    acc.sigma = sum & 0xF;
    carry = (sum >> 4) & 1;

    execute_032050_post_add_orders(current_tick, branch_l, acc);

    output = memory[current_tick];
    memory[current_tick] = input;
    ++tick_index;
    if (tick_index == 42) tick_index = 0;
  }


  void execute_04ee5c_pre_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 26:
        execute_order_list({0, 7, 8}, acc);
        break;
      case 35:
        execute_order_list({0, 7, 8}, acc);
        break;
      case 36:
        execute_order_list({5, 8}, acc);
        break;
      case 37:
        execute_order_list({5}, acc);
        break;
      case 38:
        execute_order_list({9}, acc);
        break;
      case 39:
        execute_order_list({0, 7}, acc);
        break;
      case 40:
        execute_order_list({0}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void execute_04ee5c_gamma_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 37:
        execute_order_list({12}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void execute_04ee5c_post_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 0:
        execute_order_list({15}, acc);
        break;
      case 3:
        execute_order_list({15}, acc);
        break;
      case 6:
        execute_order_list({15}, acc);
        break;
      case 9:
        execute_order_list({15}, acc);
        break;
      case 12:
        execute_order_list({15}, acc);
        break;
      case 15:
        execute_order_list({15}, acc);
        break;
      case 18:
        execute_order_list({15}, acc);
        break;
      case 21:
        execute_order_list({15}, acc);
        break;
      case 24:
        execute_order_list({15}, acc);
        break;
      case 26:
        execute_order_list({16, 25, 27}, acc);
        break;
      case 27:
        execute_order_list({15}, acc);
        break;
      case 30:
        execute_order_list({15}, acc);
        break;
      case 33:
        execute_order_list({15}, acc);
        break;
      case 35:
        execute_order_list({16, 25, 27}, acc);
        break;
      case 36:
        execute_order_list({27}, acc);
        break;
      case 37:
        execute_order_list({27}, acc);
        break;
      case 38:
        execute_order_list({16}, acc);
        break;
      case 39:
        execute_order_list({16, 27}, acc);
        break;
      case 40:
        execute_order_list({27}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void tick_ik1303_04ee5c_decoded() {
    static constexpr uint32_t kCommand = 0x04ee5c;
    if (tick_index == 0) command = kCommand;

    const int current_tick = tick_index;
    const bool branch_l = l != 0;
    if (current_tick < 27) {
      sync_address = 92;
    } else if (current_tick < 36) {
      sync_address = 92;
    } else {
      sync_address = 19;
    }

    Accumulator acc;
    execute_04ee5c_pre_add_orders(current_tick, branch_l, acc);

    if (key_y == 0) t = 0;

    execute_04ee5c_gamma_orders(current_tick, branch_l, acc);

    int sum = acc.alpha + acc.beta + acc.gamma;
    acc.sigma = sum & 0xF;
    carry = (sum >> 4) & 1;

    execute_04ee5c_post_add_orders(current_tick, branch_l, acc);

    output = memory[current_tick];
    memory[current_tick] = input;
    ++tick_index;
    if (tick_index == 42) tick_index = 0;
  }


  void execute_01a1d0_pre_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 27:
        execute_order_list({6, 7, 8}, acc);
        break;
      case 29:
        execute_order_list({0, 3, 7}, acc);
        break;
      case 32:
        execute_order_list({5, 11}, acc);
        break;
      case 33:
        execute_order_list({0}, acc);
        break;
      case 34:
        execute_order_list({6, 7, 8}, acc);
        break;
      case 35:
        execute_order_list({0}, acc);
        break;
      case 36:
        execute_order_list({0}, acc);
        break;
      case 37:
        execute_order_list({10}, acc);
        break;
      case 38:
        execute_order_list({0, 7}, acc);
        break;
      case 40:
        execute_order_list({0}, acc);
        break;
      case 41:
        execute_order_list({0}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void execute_01a1d0_gamma_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 38:
        execute_order_list({12, 13}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void execute_01a1d0_post_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 27:
        execute_order_list({27}, acc);
        break;
      case 28:
        execute_order_list({22}, acc);
        break;
      case 29:
        execute_order_list({25, 27}, acc);
        break;
      case 32:
        execute_order_list({23}, acc);
        break;
      case 33:
        execute_order_list({17, 27}, acc);
        break;
      case 34:
        execute_order_list({27}, acc);
        break;
      case 35:
        execute_order_list({17, 26, 29}, acc);
        break;
      case 36:
        execute_order_list({17, 27}, acc);
        break;
      case 37:
        execute_order_list({27}, acc);
        break;
      case 38:
        execute_order_list({25, 27}, acc);
        break;
      case 40:
        execute_order_list({27}, acc);
        break;
      case 41:
        execute_order_list({29}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void tick_ik1303_01a1d0_decoded() {
    static constexpr uint32_t kCommand = 0x01a1d0;
    if (tick_index == 0) command = kCommand;

    const int current_tick = tick_index;
    const bool branch_l = l != 0;
    if (current_tick < 27) {
      sync_address = 80;
    } else if (current_tick < 36) {
      sync_address = 67;
    } else {
      sync_address = 6;
    }

    Accumulator acc;
    execute_01a1d0_pre_add_orders(current_tick, branch_l, acc);

    if (key_y == 0) t = 0;

    execute_01a1d0_gamma_orders(current_tick, branch_l, acc);

    int sum = acc.alpha + acc.beta + acc.gamma;
    acc.sigma = sum & 0xF;
    carry = (sum >> 4) & 1;

    execute_01a1d0_post_add_orders(current_tick, branch_l, acc);

    output = memory[current_tick];
    memory[current_tick] = input;
    ++tick_index;
    if (tick_index == 42) tick_index = 0;
  }


  void execute_0c40fd_pre_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 0:
        execute_order_list({0}, acc);
        break;
      case 1:
        execute_order_list({8}, acc);
        break;
      case 2:
        execute_order_list({5}, acc);
        break;
      case 3:
        execute_order_list({10}, acc);
        break;
      case 4:
        execute_order_list({5, 11}, acc);
        break;
      case 5:
        execute_order_list({0}, acc);
        break;
      case 6:
        execute_order_list({10}, acc);
        break;
      case 7:
        execute_order_list({5, 11}, acc);
        break;
      case 8:
        execute_order_list({0}, acc);
        break;
      case 9:
        execute_order_list({10}, acc);
        break;
      case 10:
        execute_order_list({5, 11}, acc);
        break;
      case 11:
        execute_order_list({0}, acc);
        break;
      case 12:
        execute_order_list({10}, acc);
        break;
      case 13:
        execute_order_list({5, 11}, acc);
        break;
      case 14:
        execute_order_list({0}, acc);
        break;
      case 15:
        execute_order_list({10}, acc);
        break;
      case 16:
        execute_order_list({5, 11}, acc);
        break;
      case 17:
        execute_order_list({0}, acc);
        break;
      case 18:
        execute_order_list({10}, acc);
        break;
      case 19:
        execute_order_list({5, 11}, acc);
        break;
      case 20:
        execute_order_list({0}, acc);
        break;
      case 21:
        execute_order_list({10}, acc);
        break;
      case 22:
        execute_order_list({5, 11}, acc);
        break;
      case 23:
        execute_order_list({0}, acc);
        break;
      case 24:
        execute_order_list({5, 8}, acc);
        break;
      case 25:
        execute_order_list({8}, acc);
        break;
      case 26:
        execute_order_list({0}, acc);
        break;
      case 27:
        execute_order_list({0}, acc);
        break;
      case 28:
        execute_order_list({1}, acc);
        break;
      case 30:
        execute_order_list({0}, acc);
        break;
      case 31:
        execute_order_list({1}, acc);
        break;
      case 32:
        execute_order_list({6, 7, 8}, acc);
        break;
      case 33:
        execute_order_list({0}, acc);
        break;
      case 34:
        execute_order_list({1}, acc);
        break;
      case 35:
        execute_order_list({0, 3, 9}, acc);
        break;
      case 36:
        execute_order_list({0, 11}, acc);
        break;
      case 37:
        if (branch_l) {
          execute_order_list({0}, acc);
        } else {
          execute_order_list({5}, acc);
        }
        break;
      case 38:
        execute_order_list({5, 11}, acc);
        break;
      case 39:
        execute_order_list({0}, acc);
        break;
      case 40:
        if (branch_l) {
          execute_order_list({0}, acc);
        } else {
          execute_order_list({5}, acc);
        }
        break;
      case 41:
        execute_order_list({0}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void execute_0c40fd_gamma_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      default:
        break;
    }
    (void)branch_l;
  }

  void execute_0c40fd_post_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 0:
        execute_order_list({17, 27}, acc);
        break;
      case 1:
        execute_order_list({27}, acc);
        break;
      case 2:
        execute_order_list({26, 29}, acc);
        break;
      case 3:
        execute_order_list({27}, acc);
        break;
      case 4:
        execute_order_list({25, 27}, acc);
        break;
      case 5:
        execute_order_list({17, 27}, acc);
        break;
      case 6:
        execute_order_list({27}, acc);
        break;
      case 7:
        execute_order_list({25, 27}, acc);
        break;
      case 8:
        execute_order_list({17, 27}, acc);
        break;
      case 9:
        execute_order_list({27}, acc);
        break;
      case 10:
        execute_order_list({25, 27}, acc);
        break;
      case 11:
        execute_order_list({17, 27}, acc);
        break;
      case 12:
        execute_order_list({27}, acc);
        break;
      case 13:
        execute_order_list({25, 27}, acc);
        break;
      case 14:
        execute_order_list({17, 27}, acc);
        break;
      case 15:
        execute_order_list({27}, acc);
        break;
      case 16:
        execute_order_list({25, 27}, acc);
        break;
      case 17:
        execute_order_list({17, 27}, acc);
        break;
      case 18:
        execute_order_list({27}, acc);
        break;
      case 19:
        execute_order_list({25, 27}, acc);
        break;
      case 20:
        execute_order_list({17, 27}, acc);
        break;
      case 21:
        execute_order_list({27}, acc);
        break;
      case 22:
        execute_order_list({25, 27}, acc);
        break;
      case 23:
        execute_order_list({17, 27}, acc);
        break;
      case 24:
        execute_order_list({27}, acc);
        break;
      case 25:
        execute_order_list({27}, acc);
        break;
      case 26:
        execute_order_list({17, 27}, acc);
        break;
      case 27:
        execute_order_list({17, 32}, acc);
        break;
      case 28:
        execute_order_list({22}, acc);
        break;
      case 30:
        execute_order_list({17, 32}, acc);
        break;
      case 31:
        execute_order_list({22}, acc);
        break;
      case 32:
        execute_order_list({27}, acc);
        break;
      case 33:
        execute_order_list({17, 32}, acc);
        break;
      case 34:
        execute_order_list({22}, acc);
        break;
      case 35:
        execute_order_list({25, 29}, acc);
        break;
      case 36:
        execute_order_list({27}, acc);
        break;
      case 37:
        if (branch_l) {
          execute_order_list({17, 22, 27}, acc);
        } else {
          execute_order_list({22}, acc);
        }
        break;
      case 38:
        execute_order_list({23}, acc);
        break;
      case 39:
        execute_order_list({27}, acc);
        break;
      case 40:
        if (branch_l) {
          execute_order_list({17, 22, 27}, acc);
        } else {
          execute_order_list({22}, acc);
        }
        break;
      case 41:
        execute_order_list({27}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void tick_ik1303_0c40fd_decoded() {
    static constexpr uint32_t kCommand = 0x0c40fd;
    if (tick_index == 0) command = kCommand;

    const int current_tick = tick_index;
    const bool branch_l = l != 0;
    if (current_tick < 27) {
      sync_address = 125;
    } else if (current_tick < 36) {
      sync_address = 1;
    } else {
      if (current_tick == 36) {
        r[37] = 1 & 0xF;
        r[40] = 49 >> 4;
      }
      sync_address = 95;
    }

    Accumulator acc;
    execute_0c40fd_pre_add_orders(current_tick, branch_l, acc);

    if (key_y == 0) t = 0;

    execute_0c40fd_gamma_orders(current_tick, branch_l, acc);

    int sum = acc.alpha + acc.beta + acc.gamma;
    acc.sigma = sum & 0xF;
    carry = (sum >> 4) & 1;

    execute_0c40fd_post_add_orders(current_tick, branch_l, acc);

    output = memory[current_tick];
    memory[current_tick] = input;
    ++tick_index;
    if (tick_index == 42) tick_index = 0;
  }


  void execute_033977_pre_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 0:
        execute_order_list({10}, acc);
        break;
      case 1:
        execute_order_list({5, 11}, acc);
        break;
      case 2:
        execute_order_list({0}, acc);
        break;
      case 24:
        execute_order_list({5, 7}, acc);
        break;
      case 25:
        execute_order_list({5, 9}, acc);
        break;
      case 26:
        execute_order_list({6, 11}, acc);
        break;
      case 27:
        execute_order_list({6, 7, 8}, acc);
        break;
      case 28:
        execute_order_list({0, 3, 7}, acc);
        break;
      case 29:
        execute_order_list({8}, acc);
        break;
      case 30:
        execute_order_list({9}, acc);
        break;
      case 31:
        execute_order_list({7, 9}, acc);
        break;
      case 32:
        execute_order_list({0, 3, 9}, acc);
        break;
      case 33:
        execute_order_list({4, 7, 8}, acc);
        break;
      case 34:
        execute_order_list({5}, acc);
        break;
      case 35:
        execute_order_list({7, 8}, acc);
        break;
      case 36:
        execute_order_list({0, 11}, acc);
        break;
      case 38:
        execute_order_list({5, 11}, acc);
        break;
      case 39:
        execute_order_list({7, 8}, acc);
        break;
      case 40:
        execute_order_list({1}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void execute_033977_gamma_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 24:
        execute_order_list({12}, acc);
        break;
      case 31:
        execute_order_list({12}, acc);
        break;
      case 33:
        execute_order_list({12}, acc);
        break;
      case 35:
        execute_order_list({12, 13}, acc);
        break;
      case 39:
        execute_order_list({12, 13}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void execute_033977_post_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 0:
        execute_order_list({27}, acc);
        break;
      case 1:
        execute_order_list({25, 27}, acc);
        break;
      case 2:
        execute_order_list({27}, acc);
        break;
      case 24:
        execute_order_list({25, 27}, acc);
        break;
      case 25:
        execute_order_list({25, 27, 29}, acc);
        break;
      case 26:
        execute_order_list({27}, acc);
        break;
      case 27:
        execute_order_list({27}, acc);
        break;
      case 28:
        execute_order_list({25, 27}, acc);
        break;
      case 29:
        execute_order_list({27}, acc);
        break;
      case 30:
        execute_order_list({16}, acc);
        break;
      case 31:
        execute_order_list({29}, acc);
        break;
      case 32:
        execute_order_list({25, 29}, acc);
        break;
      case 33:
        execute_order_list({27}, acc);
        break;
      case 34:
        execute_order_list({26, 29}, acc);
        break;
      case 35:
        execute_order_list({25, 27}, acc);
        break;
      case 36:
        execute_order_list({27}, acc);
        break;
      case 38:
        execute_order_list({23}, acc);
        break;
      case 39:
        execute_order_list({25, 27}, acc);
        break;
      case 40:
        execute_order_list({27}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void tick_ik1303_033977_decoded() {
    static constexpr uint32_t kCommand = 0x033977;
    if (tick_index == 0) command = kCommand;

    const int current_tick = tick_index;
    const bool branch_l = l != 0;
    if (current_tick < 27) {
      sync_address = 119;
    } else if (current_tick < 36) {
      sync_address = 114;
    } else {
      sync_address = 12;
    }

    Accumulator acc;
    execute_033977_pre_add_orders(current_tick, branch_l, acc);

    if (key_y == 0) t = 0;

    execute_033977_gamma_orders(current_tick, branch_l, acc);

    int sum = acc.alpha + acc.beta + acc.gamma;
    acc.sigma = sum & 0xF;
    carry = (sum >> 4) & 1;

    execute_033977_post_add_orders(current_tick, branch_l, acc);

    output = memory[current_tick];
    memory[current_tick] = input;
    ++tick_index;
    if (tick_index == 42) tick_index = 0;
  }


  void execute_0d8204_pre_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 0:
        execute_order_list({2}, acc);
        break;
      case 1:
        execute_order_list({5}, acc);
        break;
      case 2:
        execute_order_list({8}, acc);
        break;
      case 3:
        execute_order_list({2}, acc);
        break;
      case 4:
        execute_order_list({5}, acc);
        break;
      case 5:
        execute_order_list({0}, acc);
        break;
      case 6:
        execute_order_list({2}, acc);
        break;
      case 7:
        execute_order_list({5}, acc);
        break;
      case 8:
        execute_order_list({0}, acc);
        break;
      case 9:
        execute_order_list({2}, acc);
        break;
      case 10:
        execute_order_list({5}, acc);
        break;
      case 11:
        execute_order_list({0}, acc);
        break;
      case 12:
        execute_order_list({2}, acc);
        break;
      case 13:
        execute_order_list({5}, acc);
        break;
      case 14:
        execute_order_list({0}, acc);
        break;
      case 15:
        execute_order_list({2}, acc);
        break;
      case 16:
        execute_order_list({5}, acc);
        break;
      case 17:
        execute_order_list({0}, acc);
        break;
      case 18:
        execute_order_list({2}, acc);
        break;
      case 19:
        execute_order_list({5}, acc);
        break;
      case 20:
        execute_order_list({0}, acc);
        break;
      case 21:
        execute_order_list({2}, acc);
        break;
      case 22:
        execute_order_list({5}, acc);
        break;
      case 23:
        execute_order_list({0}, acc);
        break;
      case 24:
        execute_order_list({2}, acc);
        break;
      case 25:
        execute_order_list({5}, acc);
        break;
      case 26:
        execute_order_list({7, 8}, acc);
        break;
      case 27:
        execute_order_list({2}, acc);
        break;
      case 28:
        execute_order_list({5}, acc);
        break;
      case 29:
        execute_order_list({8}, acc);
        break;
      case 30:
        execute_order_list({2}, acc);
        break;
      case 31:
        execute_order_list({5}, acc);
        break;
      case 32:
        execute_order_list({0}, acc);
        break;
      case 33:
        execute_order_list({2}, acc);
        break;
      case 34:
        execute_order_list({5}, acc);
        break;
      case 35:
        execute_order_list({7, 8}, acc);
        break;
      case 36:
        execute_order_list({0, 11}, acc);
        break;
      case 37:
        if (branch_l) {
          execute_order_list({0}, acc);
        } else {
          execute_order_list({5}, acc);
        }
        break;
      case 38:
        execute_order_list({5, 11}, acc);
        break;
      case 39:
        execute_order_list({0}, acc);
        break;
      case 40:
        if (branch_l) {
          execute_order_list({0}, acc);
        } else {
          execute_order_list({5}, acc);
        }
        break;
      case 41:
        execute_order_list({0}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void execute_0d8204_gamma_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 2:
        execute_order_list({12, 13}, acc);
        break;
      case 26:
        execute_order_list({12, 13}, acc);
        break;
      case 29:
        execute_order_list({12, 13}, acc);
        break;
      case 35:
        execute_order_list({12, 13}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void execute_0d8204_post_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 0:
        execute_order_list({27, 33}, acc);
        break;
      case 1:
        execute_order_list({22}, acc);
        break;
      case 2:
        execute_order_list({25}, acc);
        break;
      case 3:
        execute_order_list({27, 33}, acc);
        break;
      case 4:
        execute_order_list({22}, acc);
        break;
      case 5:
        execute_order_list({27}, acc);
        break;
      case 6:
        execute_order_list({27, 33}, acc);
        break;
      case 7:
        execute_order_list({22}, acc);
        break;
      case 8:
        execute_order_list({27}, acc);
        break;
      case 9:
        execute_order_list({27, 33}, acc);
        break;
      case 10:
        execute_order_list({22}, acc);
        break;
      case 11:
        execute_order_list({27}, acc);
        break;
      case 12:
        execute_order_list({27, 33}, acc);
        break;
      case 13:
        execute_order_list({22}, acc);
        break;
      case 14:
        execute_order_list({27}, acc);
        break;
      case 15:
        execute_order_list({27, 33}, acc);
        break;
      case 16:
        execute_order_list({22}, acc);
        break;
      case 17:
        execute_order_list({27}, acc);
        break;
      case 18:
        execute_order_list({27, 33}, acc);
        break;
      case 19:
        execute_order_list({22}, acc);
        break;
      case 20:
        execute_order_list({27}, acc);
        break;
      case 21:
        execute_order_list({27, 33}, acc);
        break;
      case 22:
        execute_order_list({22}, acc);
        break;
      case 23:
        execute_order_list({27}, acc);
        break;
      case 24:
        execute_order_list({27, 33}, acc);
        break;
      case 25:
        execute_order_list({22}, acc);
        break;
      case 26:
        execute_order_list({25, 27}, acc);
        break;
      case 27:
        execute_order_list({27, 33}, acc);
        break;
      case 28:
        execute_order_list({22}, acc);
        break;
      case 29:
        execute_order_list({25}, acc);
        break;
      case 30:
        execute_order_list({27, 33}, acc);
        break;
      case 31:
        execute_order_list({22}, acc);
        break;
      case 32:
        execute_order_list({27}, acc);
        break;
      case 33:
        execute_order_list({27, 33}, acc);
        break;
      case 34:
        execute_order_list({22}, acc);
        break;
      case 35:
        execute_order_list({25, 27}, acc);
        break;
      case 36:
        execute_order_list({27}, acc);
        break;
      case 37:
        if (branch_l) {
          execute_order_list({17, 22, 27}, acc);
        } else {
          execute_order_list({22}, acc);
        }
        break;
      case 38:
        execute_order_list({23}, acc);
        break;
      case 39:
        execute_order_list({27}, acc);
        break;
      case 40:
        if (branch_l) {
          execute_order_list({17, 22, 27}, acc);
        } else {
          execute_order_list({22}, acc);
        }
        break;
      case 41:
        execute_order_list({27}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void tick_ik1303_0d8204_decoded() {
    static constexpr uint32_t kCommand = 0x0d8204;
    if (tick_index == 0) command = kCommand;

    const int current_tick = tick_index;
    const bool branch_l = l != 0;
    if (current_tick < 27) {
      sync_address = 4;
    } else if (current_tick < 36) {
      sync_address = 4;
    } else {
      if (current_tick == 36) {
        r[37] = 6 & 0xF;
        r[40] = 54 >> 4;
      }
      sync_address = 95;
    }

    Accumulator acc;
    execute_0d8204_pre_add_orders(current_tick, branch_l, acc);

    if (key_y == 0) t = 0;

    execute_0d8204_gamma_orders(current_tick, branch_l, acc);

    int sum = acc.alpha + acc.beta + acc.gamma;
    acc.sigma = sum & 0xF;
    carry = (sum >> 4) & 1;

    execute_0d8204_post_add_orders(current_tick, branch_l, acc);

    output = memory[current_tick];
    memory[current_tick] = input;
    ++tick_index;
    if (tick_index == 42) tick_index = 0;
  }


  void execute_011020_pre_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 0:
        execute_order_list({0}, acc);
        break;
      case 1:
        execute_order_list({0}, acc);
        break;
      case 2:
        execute_order_list({0, 3, 9}, acc);
        break;
      case 3:
        execute_order_list({0}, acc);
        break;
      case 4:
        execute_order_list({0}, acc);
        break;
      case 6:
        execute_order_list({0}, acc);
        break;
      case 7:
        execute_order_list({0}, acc);
        break;
      case 9:
        execute_order_list({0}, acc);
        break;
      case 10:
        execute_order_list({0}, acc);
        break;
      case 12:
        execute_order_list({0}, acc);
        break;
      case 13:
        execute_order_list({0}, acc);
        break;
      case 15:
        execute_order_list({0}, acc);
        break;
      case 16:
        execute_order_list({0}, acc);
        break;
      case 18:
        execute_order_list({0}, acc);
        break;
      case 19:
        execute_order_list({0}, acc);
        break;
      case 21:
        execute_order_list({0}, acc);
        break;
      case 22:
        execute_order_list({0}, acc);
        break;
      case 24:
        execute_order_list({0}, acc);
        break;
      case 25:
        execute_order_list({0}, acc);
        break;
      case 26:
        execute_order_list({0, 3, 9}, acc);
        break;
      case 27:
        execute_order_list({0}, acc);
        break;
      case 28:
        execute_order_list({0}, acc);
        break;
      case 29:
        execute_order_list({0, 3, 9}, acc);
        break;
      case 30:
        execute_order_list({0}, acc);
        break;
      case 31:
        execute_order_list({0}, acc);
        break;
      case 33:
        execute_order_list({0}, acc);
        break;
      case 34:
        execute_order_list({0}, acc);
        break;
      case 35:
        execute_order_list({0, 3, 9}, acc);
        break;
      case 36:
        execute_order_list({2}, acc);
        break;
      case 37:
        execute_order_list({5}, acc);
        break;
      case 38:
        execute_order_list({8}, acc);
        break;
      case 39:
        execute_order_list({2}, acc);
        break;
      case 40:
        execute_order_list({5}, acc);
        break;
      case 41:
        execute_order_list({0}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void execute_011020_gamma_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 38:
        execute_order_list({12, 13}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void execute_011020_post_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 0:
        execute_order_list({27}, acc);
        break;
      case 1:
        execute_order_list({17, 22}, acc);
        break;
      case 2:
        execute_order_list({25, 29}, acc);
        break;
      case 3:
        execute_order_list({27}, acc);
        break;
      case 4:
        execute_order_list({17, 22}, acc);
        break;
      case 6:
        execute_order_list({27}, acc);
        break;
      case 7:
        execute_order_list({17, 22}, acc);
        break;
      case 9:
        execute_order_list({27}, acc);
        break;
      case 10:
        execute_order_list({17, 22}, acc);
        break;
      case 12:
        execute_order_list({27}, acc);
        break;
      case 13:
        execute_order_list({17, 22}, acc);
        break;
      case 15:
        execute_order_list({27}, acc);
        break;
      case 16:
        execute_order_list({17, 22}, acc);
        break;
      case 18:
        execute_order_list({27}, acc);
        break;
      case 19:
        execute_order_list({17, 22}, acc);
        break;
      case 21:
        execute_order_list({27}, acc);
        break;
      case 22:
        execute_order_list({17, 22}, acc);
        break;
      case 24:
        execute_order_list({27}, acc);
        break;
      case 25:
        execute_order_list({17, 22}, acc);
        break;
      case 26:
        execute_order_list({25, 29}, acc);
        break;
      case 27:
        execute_order_list({27}, acc);
        break;
      case 28:
        execute_order_list({17, 22}, acc);
        break;
      case 29:
        execute_order_list({25, 29}, acc);
        break;
      case 30:
        execute_order_list({27}, acc);
        break;
      case 31:
        execute_order_list({17, 22}, acc);
        break;
      case 33:
        execute_order_list({27}, acc);
        break;
      case 34:
        execute_order_list({17, 22}, acc);
        break;
      case 35:
        execute_order_list({25, 29}, acc);
        break;
      case 36:
        execute_order_list({27, 33}, acc);
        break;
      case 37:
        execute_order_list({22}, acc);
        break;
      case 38:
        execute_order_list({25}, acc);
        break;
      case 39:
        execute_order_list({27, 33}, acc);
        break;
      case 40:
        execute_order_list({22}, acc);
        break;
      case 41:
        execute_order_list({27}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void tick_ik1303_011020_decoded() {
    static constexpr uint32_t kCommand = 0x011020;
    if (tick_index == 0) command = kCommand;

    const int current_tick = tick_index;
    const bool branch_l = l != 0;
    if (current_tick < 27) {
      sync_address = 32;
    } else if (current_tick < 36) {
      sync_address = 32;
    } else {
      sync_address = 4;
    }

    Accumulator acc;
    execute_011020_pre_add_orders(current_tick, branch_l, acc);

    if (key_y == 0) t = 0;

    execute_011020_gamma_orders(current_tick, branch_l, acc);

    int sum = acc.alpha + acc.beta + acc.gamma;
    acc.sigma = sum & 0xF;
    carry = (sum >> 4) & 1;

    execute_011020_post_add_orders(current_tick, branch_l, acc);

    output = memory[current_tick];
    memory[current_tick] = input;
    ++tick_index;
    if (tick_index == 42) tick_index = 0;
  }


  void execute_1d3950_pre_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 27:
        execute_order_list({6, 7, 8}, acc);
        break;
      case 28:
        execute_order_list({0, 3, 7}, acc);
        break;
      case 29:
        execute_order_list({8}, acc);
        break;
      case 30:
        execute_order_list({9}, acc);
        break;
      case 31:
        execute_order_list({7, 9}, acc);
        break;
      case 32:
        execute_order_list({0, 3, 9}, acc);
        break;
      case 33:
        execute_order_list({4, 7, 8}, acc);
        break;
      case 34:
        execute_order_list({5}, acc);
        break;
      case 35:
        execute_order_list({7, 8}, acc);
        break;
      case 36:
        execute_order_list({0, 11}, acc);
        break;
      case 37:
        if (branch_l) {
          execute_order_list({0}, acc);
        } else {
          execute_order_list({5}, acc);
        }
        break;
      case 38:
        execute_order_list({5, 11}, acc);
        break;
      case 39:
        execute_order_list({0}, acc);
        break;
      case 40:
        if (branch_l) {
          execute_order_list({0}, acc);
        } else {
          execute_order_list({5}, acc);
        }
        break;
      case 41:
        execute_order_list({0}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void execute_1d3950_gamma_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 31:
        execute_order_list({12}, acc);
        break;
      case 33:
        execute_order_list({12}, acc);
        break;
      case 35:
        execute_order_list({12, 13}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void execute_1d3950_post_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 27:
        execute_order_list({27}, acc);
        break;
      case 28:
        execute_order_list({25, 27}, acc);
        break;
      case 29:
        execute_order_list({27}, acc);
        break;
      case 30:
        execute_order_list({16}, acc);
        break;
      case 31:
        execute_order_list({29}, acc);
        break;
      case 32:
        execute_order_list({25, 29}, acc);
        break;
      case 33:
        execute_order_list({27}, acc);
        break;
      case 34:
        execute_order_list({26, 29}, acc);
        break;
      case 35:
        execute_order_list({25, 27}, acc);
        break;
      case 36:
        execute_order_list({27}, acc);
        break;
      case 37:
        if (branch_l) {
          execute_order_list({17, 22, 27}, acc);
        } else {
          execute_order_list({22}, acc);
        }
        break;
      case 38:
        execute_order_list({23}, acc);
        break;
      case 39:
        execute_order_list({27}, acc);
        break;
      case 40:
        if (branch_l) {
          execute_order_list({17, 22, 27}, acc);
        } else {
          execute_order_list({22}, acc);
        }
        break;
      case 41:
        execute_order_list({27}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void tick_ik1303_1d3950_decoded() {
    static constexpr uint32_t kCommand = 0x1d3950;
    if (tick_index == 0) command = kCommand;

    const int current_tick = tick_index;
    const bool branch_l = l != 0;
    if (current_tick < 27) {
      sync_address = 80;
    } else if (current_tick < 36) {
      sync_address = 114;
    } else {
      if (current_tick == 36) {
        r[37] = 4 & 0xF;
        r[40] = 116 >> 4;
      }
      sync_address = 95;
    }

    Accumulator acc;
    execute_1d3950_pre_add_orders(current_tick, branch_l, acc);

    if (key_y == 0) t = 0;

    execute_1d3950_gamma_orders(current_tick, branch_l, acc);

    int sum = acc.alpha + acc.beta + acc.gamma;
    acc.sigma = sum & 0xF;
    carry = (sum >> 4) & 1;

    execute_1d3950_post_add_orders(current_tick, branch_l, acc);

    output = memory[current_tick];
    memory[current_tick] = input;
    ++tick_index;
    if (tick_index == 42) tick_index = 0;
  }


  void execute_1079f4_pre_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 0:
        execute_order_list({0, 7, 8}, acc);
        break;
      case 24:
        execute_order_list({9}, acc);
        break;
      case 25:
        execute_order_list({9}, acc);
        break;
      case 27:
        execute_order_list({0}, acc);
        break;
      case 29:
        execute_order_list({0, 3, 9}, acc);
        break;
      case 30:
        execute_order_list({0, 7}, acc);
        break;
      case 31:
        execute_order_list({0, 3, 9}, acc);
        break;
      case 32:
        execute_order_list({7, 9}, acc);
        break;
      case 33:
        execute_order_list({5}, acc);
        break;
      case 34:
        execute_order_list({8}, acc);
        break;
      case 36:
        execute_order_list({0, 11}, acc);
        break;
      case 37:
        if (branch_l) {
          execute_order_list({0}, acc);
        } else {
          execute_order_list({5}, acc);
        }
        break;
      case 38:
        execute_order_list({5, 11}, acc);
        break;
      case 39:
        execute_order_list({0}, acc);
        break;
      case 40:
        if (branch_l) {
          execute_order_list({0}, acc);
        } else {
          execute_order_list({5}, acc);
        }
        break;
      case 41:
        execute_order_list({0}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void execute_1079f4_gamma_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 0:
        execute_order_list({12}, acc);
        break;
      case 32:
        execute_order_list({12}, acc);
        break;
      case 34:
        execute_order_list({12, 13}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void execute_1079f4_post_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 0:
        execute_order_list({25, 27}, acc);
        break;
      case 24:
        execute_order_list({16}, acc);
        break;
      case 25:
        execute_order_list({16}, acc);
        break;
      case 27:
        execute_order_list({27}, acc);
        break;
      case 28:
        execute_order_list({29}, acc);
        break;
      case 29:
        execute_order_list({25, 29}, acc);
        break;
      case 30:
        execute_order_list({25, 27}, acc);
        break;
      case 31:
        execute_order_list({25, 29}, acc);
        break;
      case 32:
        execute_order_list({29}, acc);
        break;
      case 33:
        execute_order_list({26, 29}, acc);
        break;
      case 34:
        execute_order_list({25}, acc);
        break;
      case 36:
        execute_order_list({27}, acc);
        break;
      case 37:
        if (branch_l) {
          execute_order_list({17, 22, 27}, acc);
        } else {
          execute_order_list({22}, acc);
        }
        break;
      case 38:
        execute_order_list({23}, acc);
        break;
      case 39:
        execute_order_list({27}, acc);
        break;
      case 40:
        if (branch_l) {
          execute_order_list({17, 22, 27}, acc);
        } else {
          execute_order_list({22}, acc);
        }
        break;
      case 41:
        execute_order_list({27}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void tick_ik1303_1079f4_decoded() {
    static constexpr uint32_t kCommand = 0x1079f4;
    if (tick_index == 0) command = kCommand;

    const int current_tick = tick_index;
    const bool branch_l = l != 0;
    if (current_tick < 27) {
      sync_address = 116;
    } else if (current_tick < 36) {
      sync_address = 115;
    } else {
      if (current_tick == 36) {
        r[37] = 1 & 0xF;
        r[40] = 65 >> 4;
      }
      sync_address = 95;
    }

    Accumulator acc;
    execute_1079f4_pre_add_orders(current_tick, branch_l, acc);

    if (key_y == 0) t = 0;

    execute_1079f4_gamma_orders(current_tick, branch_l, acc);

    int sum = acc.alpha + acc.beta + acc.gamma;
    acc.sigma = sum & 0xF;
    carry = (sum >> 4) & 1;

    execute_1079f4_post_add_orders(current_tick, branch_l, acc);

    output = memory[current_tick];
    memory[current_tick] = input;
    ++tick_index;
    if (tick_index == 42) tick_index = 0;
  }


  void execute_2f4358_pre_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 0:
        execute_order_list({5}, acc);
        break;
      case 1:
        execute_order_list({8}, acc);
        break;
      case 3:
        execute_order_list({5, 7}, acc);
        break;
      case 4:
        execute_order_list({5, 7}, acc);
        break;
      case 6:
        execute_order_list({5, 7}, acc);
        break;
      case 7:
        execute_order_list({5, 7}, acc);
        break;
      case 9:
        execute_order_list({5, 7}, acc);
        break;
      case 10:
        execute_order_list({5, 7}, acc);
        break;
      case 12:
        execute_order_list({5, 7}, acc);
        break;
      case 13:
        execute_order_list({5, 7}, acc);
        break;
      case 15:
        execute_order_list({5, 7}, acc);
        break;
      case 16:
        execute_order_list({5, 7}, acc);
        break;
      case 18:
        execute_order_list({5, 7}, acc);
        break;
      case 19:
        execute_order_list({5, 7}, acc);
        break;
      case 21:
        execute_order_list({5, 7}, acc);
        break;
      case 22:
        execute_order_list({5, 7}, acc);
        break;
      case 24:
        execute_order_list({5}, acc);
        break;
      case 27:
        execute_order_list({0}, acc);
        break;
      case 28:
        execute_order_list({10}, acc);
        break;
      case 29:
        execute_order_list({0, 7}, acc);
        break;
      case 31:
        execute_order_list({0}, acc);
        break;
      case 32:
        execute_order_list({0}, acc);
        break;
      case 33:
        execute_order_list({5}, acc);
        break;
      case 34:
        execute_order_list({8}, acc);
        break;
      case 35:
        execute_order_list({5, 10, 11}, acc);
        break;
      case 36:
        execute_order_list({0, 11}, acc);
        break;
      case 37:
        if (branch_l) {
          execute_order_list({0}, acc);
        } else {
          execute_order_list({5}, acc);
        }
        break;
      case 38:
        execute_order_list({5, 11}, acc);
        break;
      case 39:
        execute_order_list({0}, acc);
        break;
      case 40:
        if (branch_l) {
          execute_order_list({0}, acc);
        } else {
          execute_order_list({5}, acc);
        }
        break;
      case 41:
        execute_order_list({0}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void execute_2f4358_gamma_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 1:
        execute_order_list({12, 13}, acc);
        break;
      case 3:
        execute_order_list({12}, acc);
        break;
      case 4:
        execute_order_list({12}, acc);
        break;
      case 6:
        execute_order_list({12}, acc);
        break;
      case 7:
        execute_order_list({12}, acc);
        break;
      case 9:
        execute_order_list({12}, acc);
        break;
      case 10:
        execute_order_list({12}, acc);
        break;
      case 12:
        execute_order_list({12}, acc);
        break;
      case 13:
        execute_order_list({12}, acc);
        break;
      case 15:
        execute_order_list({12}, acc);
        break;
      case 16:
        execute_order_list({12}, acc);
        break;
      case 18:
        execute_order_list({12}, acc);
        break;
      case 19:
        execute_order_list({12}, acc);
        break;
      case 21:
        execute_order_list({12}, acc);
        break;
      case 22:
        execute_order_list({12}, acc);
        break;
      case 24:
        execute_order_list({12}, acc);
        break;
      case 29:
        execute_order_list({12, 13}, acc);
        break;
      case 35:
        execute_order_list({12, 13}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void execute_2f4358_post_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 0:
        execute_order_list({26, 29}, acc);
        break;
      case 1:
        execute_order_list({25}, acc);
        break;
      case 3:
        execute_order_list({25, 27}, acc);
        break;
      case 4:
        execute_order_list({25, 27}, acc);
        break;
      case 6:
        execute_order_list({25, 27}, acc);
        break;
      case 7:
        execute_order_list({25, 27}, acc);
        break;
      case 9:
        execute_order_list({25, 27}, acc);
        break;
      case 10:
        execute_order_list({25, 27}, acc);
        break;
      case 12:
        execute_order_list({25, 27}, acc);
        break;
      case 13:
        execute_order_list({25, 27}, acc);
        break;
      case 15:
        execute_order_list({25, 27}, acc);
        break;
      case 16:
        execute_order_list({25, 27}, acc);
        break;
      case 18:
        execute_order_list({25, 27}, acc);
        break;
      case 19:
        execute_order_list({25, 27}, acc);
        break;
      case 21:
        execute_order_list({25, 27}, acc);
        break;
      case 22:
        execute_order_list({25, 27}, acc);
        break;
      case 24:
        execute_order_list({27}, acc);
        break;
      case 25:
        execute_order_list({22}, acc);
        break;
      case 26:
        execute_order_list({22}, acc);
        break;
      case 27:
        execute_order_list({17, 27}, acc);
        break;
      case 28:
        execute_order_list({27}, acc);
        break;
      case 29:
        execute_order_list({25, 27}, acc);
        break;
      case 31:
        execute_order_list({27}, acc);
        break;
      case 32:
        execute_order_list({29}, acc);
        break;
      case 33:
        execute_order_list({22}, acc);
        break;
      case 34:
        execute_order_list({27}, acc);
        break;
      case 35:
        execute_order_list({25, 27}, acc);
        break;
      case 36:
        execute_order_list({27}, acc);
        break;
      case 37:
        if (branch_l) {
          execute_order_list({17, 22, 27}, acc);
        } else {
          execute_order_list({22}, acc);
        }
        break;
      case 38:
        execute_order_list({23}, acc);
        break;
      case 39:
        execute_order_list({27}, acc);
        break;
      case 40:
        if (branch_l) {
          execute_order_list({17, 22, 27}, acc);
        } else {
          execute_order_list({22}, acc);
        }
        break;
      case 41:
        execute_order_list({27}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void tick_ik1303_2f4358_decoded() {
    static constexpr uint32_t kCommand = 0x2f4358;
    if (tick_index == 0) command = kCommand;

    const int current_tick = tick_index;
    const bool branch_l = l != 0;
    if (current_tick < 27) {
      sync_address = 88;
    } else if (current_tick < 36) {
      sync_address = 6;
    } else {
      if (current_tick == 36) {
        r[37] = 13 & 0xF;
        r[40] = 189 >> 4;
      }
      sync_address = 95;
    }

    Accumulator acc;
    execute_2f4358_pre_add_orders(current_tick, branch_l, acc);

    if (key_y == 0) t = 0;

    execute_2f4358_gamma_orders(current_tick, branch_l, acc);

    int sum = acc.alpha + acc.beta + acc.gamma;
    acc.sigma = sum & 0xF;
    carry = (sum >> 4) & 1;

    execute_2f4358_post_add_orders(current_tick, branch_l, acc);

    output = memory[current_tick];
    memory[current_tick] = input;
    ++tick_index;
    if (tick_index == 42) tick_index = 0;
  }


  void execute_3a9250_pre_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 27:
        execute_order_list({10}, acc);
        break;
      case 29:
        execute_order_list({5}, acc);
        break;
      case 30:
        execute_order_list({0, 3, 9}, acc);
        break;
      case 31:
        execute_order_list({4, 7, 8}, acc);
        break;
      case 32:
        execute_order_list({5}, acc);
        break;
      case 36:
        execute_order_list({0, 11}, acc);
        break;
      case 37:
        if (branch_l) {
          execute_order_list({0}, acc);
        } else {
          execute_order_list({5}, acc);
        }
        break;
      case 38:
        execute_order_list({5, 11}, acc);
        break;
      case 39:
        execute_order_list({0}, acc);
        break;
      case 40:
        if (branch_l) {
          execute_order_list({0}, acc);
        } else {
          execute_order_list({5}, acc);
        }
        break;
      case 41:
        execute_order_list({0}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void execute_3a9250_gamma_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 31:
        execute_order_list({12}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void execute_3a9250_post_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 27:
        execute_order_list({27}, acc);
        break;
      case 28:
        execute_order_list({22}, acc);
        break;
      case 29:
        execute_order_list({26, 29}, acc);
        break;
      case 30:
        execute_order_list({25, 29}, acc);
        break;
      case 31:
        execute_order_list({27}, acc);
        break;
      case 32:
        execute_order_list({23}, acc);
        break;
      case 34:
        execute_order_list({22}, acc);
        break;
      case 36:
        execute_order_list({27}, acc);
        break;
      case 37:
        if (branch_l) {
          execute_order_list({17, 22, 27}, acc);
        } else {
          execute_order_list({22}, acc);
        }
        break;
      case 38:
        execute_order_list({23}, acc);
        break;
      case 39:
        execute_order_list({27}, acc);
        break;
      case 40:
        if (branch_l) {
          execute_order_list({17, 22, 27}, acc);
        } else {
          execute_order_list({22}, acc);
        }
        break;
      case 41:
        execute_order_list({27}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void tick_ik1303_3a9250_decoded() {
    static constexpr uint32_t kCommand = 0x3a9250;
    if (tick_index == 0) command = kCommand;

    const int current_tick = tick_index;
    const bool branch_l = l != 0;
    if (current_tick < 27) {
      sync_address = 80;
    } else if (current_tick < 36) {
      sync_address = 36;
    } else {
      if (current_tick == 36) {
        r[37] = 10 & 0xF;
        r[40] = 234 >> 4;
      }
      sync_address = 95;
    }

    Accumulator acc;
    execute_3a9250_pre_add_orders(current_tick, branch_l, acc);

    if (key_y == 0) t = 0;

    execute_3a9250_gamma_orders(current_tick, branch_l, acc);

    int sum = acc.alpha + acc.beta + acc.gamma;
    acc.sigma = sum & 0xF;
    carry = (sum >> 4) & 1;

    execute_3a9250_post_add_orders(current_tick, branch_l, acc);

    output = memory[current_tick];
    memory[current_tick] = input;
    ++tick_index;
    if (tick_index == 42) tick_index = 0;
  }


  void execute_0238e6_pre_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 0:
        execute_order_list({0, 3, 9}, acc);
        break;
      case 1:
        execute_order_list({9}, acc);
        break;
      case 4:
        execute_order_list({5}, acc);
        break;
      case 7:
        execute_order_list({5}, acc);
        break;
      case 10:
        execute_order_list({5}, acc);
        break;
      case 13:
        execute_order_list({5}, acc);
        break;
      case 16:
        execute_order_list({5}, acc);
        break;
      case 19:
        execute_order_list({5}, acc);
        break;
      case 22:
        execute_order_list({5}, acc);
        break;
      case 24:
        execute_order_list({5, 11}, acc);
        break;
      case 31:
        execute_order_list({6, 11}, acc);
        break;
      case 34:
        execute_order_list({9}, acc);
        break;
      case 35:
        execute_order_list({9}, acc);
        break;
      case 36:
        execute_order_list({0, 11}, acc);
        break;
      case 37:
        execute_order_list({5}, acc);
        break;
      case 38:
        execute_order_list({5}, acc);
        break;
      case 39:
        execute_order_list({0}, acc);
        break;
      case 40:
        execute_order_list({5}, acc);
        break;
      case 41:
        execute_order_list({6, 7, 8}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void execute_0238e6_gamma_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 1:
        execute_order_list({13}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void execute_0238e6_post_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 0:
        execute_order_list({25, 29}, acc);
        break;
      case 1:
        execute_order_list({29}, acc);
        break;
      case 2:
        execute_order_list({22}, acc);
        break;
      case 3:
        execute_order_list({22}, acc);
        break;
      case 4:
        execute_order_list({26, 29}, acc);
        break;
      case 5:
        execute_order_list({22}, acc);
        break;
      case 6:
        execute_order_list({22}, acc);
        break;
      case 7:
        execute_order_list({26, 29}, acc);
        break;
      case 8:
        execute_order_list({22}, acc);
        break;
      case 9:
        execute_order_list({22}, acc);
        break;
      case 10:
        execute_order_list({26, 29}, acc);
        break;
      case 11:
        execute_order_list({22}, acc);
        break;
      case 12:
        execute_order_list({22}, acc);
        break;
      case 13:
        execute_order_list({26, 29}, acc);
        break;
      case 14:
        execute_order_list({22}, acc);
        break;
      case 15:
        execute_order_list({22}, acc);
        break;
      case 16:
        execute_order_list({26, 29}, acc);
        break;
      case 17:
        execute_order_list({22}, acc);
        break;
      case 18:
        execute_order_list({22}, acc);
        break;
      case 19:
        execute_order_list({26, 29}, acc);
        break;
      case 20:
        execute_order_list({22}, acc);
        break;
      case 21:
        execute_order_list({22}, acc);
        break;
      case 22:
        execute_order_list({26, 29}, acc);
        break;
      case 23:
        execute_order_list({22}, acc);
        break;
      case 24:
        execute_order_list({23}, acc);
        break;
      case 29:
        execute_order_list({22}, acc);
        break;
      case 31:
        execute_order_list({27}, acc);
        break;
      case 32:
        execute_order_list({22}, acc);
        break;
      case 33:
        execute_order_list({29}, acc);
        break;
      case 34:
        execute_order_list({16}, acc);
        break;
      case 35:
        execute_order_list({16}, acc);
        break;
      case 36:
        execute_order_list({17, 32}, acc);
        break;
      case 37:
        execute_order_list({22}, acc);
        break;
      case 38:
        execute_order_list({26, 29}, acc);
        break;
      case 39:
        execute_order_list({17, 32}, acc);
        break;
      case 40:
        execute_order_list({22}, acc);
        break;
      case 41:
        execute_order_list({27}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void tick_ik1303_0238e6_decoded() {
    static constexpr uint32_t kCommand = 0x0238e6;
    if (tick_index == 0) command = kCommand;

    const int current_tick = tick_index;
    const bool branch_l = l != 0;
    if (current_tick < 27) {
      sync_address = 102;
    } else if (current_tick < 36) {
      sync_address = 113;
    } else {
      sync_address = 8;
    }

    Accumulator acc;
    execute_0238e6_pre_add_orders(current_tick, branch_l, acc);

    if (key_y == 0) t = 0;

    execute_0238e6_gamma_orders(current_tick, branch_l, acc);

    int sum = acc.alpha + acc.beta + acc.gamma;
    acc.sigma = sum & 0xF;
    carry = (sum >> 4) & 1;

    execute_0238e6_post_add_orders(current_tick, branch_l, acc);

    output = memory[current_tick];
    memory[current_tick] = input;
    ++tick_index;
    if (tick_index == 42) tick_index = 0;
  }


  void execute_6f5cd0_pre_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 27:
        execute_order_list({5, 7}, acc);
        break;
      case 30:
        execute_order_list({4, 7}, acc);
        break;
      case 31:
        execute_order_list({5}, acc);
        break;
      case 32:
        execute_order_list({10}, acc);
        break;
      case 33:
        execute_order_list({5, 9}, acc);
        break;
      case 34:
        execute_order_list({10}, acc);
        break;
      case 35:
        execute_order_list({5, 9}, acc);
        break;
      case 36:
        execute_order_list({0, 11}, acc);
        break;
      case 37:
        if (branch_l) {
          execute_order_list({0}, acc);
        } else {
          execute_order_list({5}, acc);
        }
        break;
      case 38:
        execute_order_list({5, 11}, acc);
        break;
      case 39:
        execute_order_list({0}, acc);
        break;
      case 40:
        if (branch_l) {
          execute_order_list({0}, acc);
        } else {
          execute_order_list({5}, acc);
        }
        break;
      case 41:
        execute_order_list({0}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void execute_6f5cd0_gamma_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 27:
        execute_order_list({12}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void execute_6f5cd0_post_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 27:
        execute_order_list({25, 27}, acc);
        break;
      case 30:
        execute_order_list({26, 29}, acc);
        break;
      case 32:
        execute_order_list({27}, acc);
        break;
      case 33:
        execute_order_list({25, 27, 29}, acc);
        break;
      case 34:
        execute_order_list({27}, acc);
        break;
      case 35:
        execute_order_list({25, 27, 29}, acc);
        break;
      case 36:
        execute_order_list({27}, acc);
        break;
      case 37:
        if (branch_l) {
          execute_order_list({17, 22, 27}, acc);
        } else {
          execute_order_list({22}, acc);
        }
        break;
      case 38:
        execute_order_list({23}, acc);
        break;
      case 39:
        execute_order_list({27}, acc);
        break;
      case 40:
        if (branch_l) {
          execute_order_list({17, 22, 27}, acc);
        } else {
          execute_order_list({22}, acc);
        }
        break;
      case 41:
        execute_order_list({27}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void tick_ik1303_6f5cd0_decoded() {
    static constexpr uint32_t kCommand = 0x6f5cd0;
    if (tick_index == 0) command = kCommand;

    const int current_tick = tick_index;
    const bool branch_l = l != 0;
    if (current_tick < 27) {
      sync_address = 80;
    } else if (current_tick < 36) {
      sync_address = 57;
    } else {
      if (current_tick == 36) {
        r[37] = 13 & 0xF;
        r[40] = 189 >> 4;
      }
      sync_address = 95;
    }

    Accumulator acc;
    execute_6f5cd0_pre_add_orders(current_tick, branch_l, acc);

    if (key_y == 0) t = 0;

    execute_6f5cd0_gamma_orders(current_tick, branch_l, acc);

    int sum = acc.alpha + acc.beta + acc.gamma;
    acc.sigma = sum & 0xF;
    carry = (sum >> 4) & 1;

    execute_6f5cd0_post_add_orders(current_tick, branch_l, acc);

    output = memory[current_tick];
    memory[current_tick] = input;
    ++tick_index;
    if (tick_index == 42) tick_index = 0;
  }


  void execute_069750_pre_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 27:
        execute_order_list({0, 10}, acc);
        break;
      case 28:
        execute_order_list({0, 7}, acc);
        break;
      case 29:
        execute_order_list({4, 7}, acc);
        break;
      case 30:
        execute_order_list({0, 10}, acc);
        break;
      case 31:
        execute_order_list({0, 7}, acc);
        break;
      case 32:
        execute_order_list({4, 7}, acc);
        break;
      case 33:
        execute_order_list({0, 10}, acc);
        break;
      case 34:
        execute_order_list({0, 7}, acc);
        break;
      case 35:
        execute_order_list({4, 7}, acc);
        break;
      case 36:
        execute_order_list({5, 8}, acc);
        break;
      case 37:
        execute_order_list({0, 7}, acc);
        break;
      case 38:
        execute_order_list({8}, acc);
        break;
      case 39:
        execute_order_list({0}, acc);
        break;
      case 40:
        execute_order_list({5}, acc);
        break;
      case 41:
        execute_order_list({5, 8}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void execute_069750_gamma_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 31:
        execute_order_list({12}, acc);
        break;
      case 34:
        execute_order_list({12}, acc);
        break;
      case 38:
        execute_order_list({12, 13}, acc);
        break;
      case 40:
        execute_order_list({12}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void execute_069750_post_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 27:
        execute_order_list({27}, acc);
        break;
      case 28:
        execute_order_list({25, 27}, acc);
        break;
      case 29:
        execute_order_list({22, 26, 29}, acc);
        break;
      case 30:
        execute_order_list({27}, acc);
        break;
      case 31:
        execute_order_list({25, 27}, acc);
        break;
      case 32:
        execute_order_list({22, 26, 29}, acc);
        break;
      case 33:
        execute_order_list({27}, acc);
        break;
      case 34:
        execute_order_list({25, 27}, acc);
        break;
      case 35:
        execute_order_list({22, 26, 29}, acc);
        break;
      case 36:
        execute_order_list({27}, acc);
        break;
      case 37:
        execute_order_list({16, 27}, acc);
        break;
      case 38:
        execute_order_list({25}, acc);
        break;
      case 39:
        execute_order_list({27}, acc);
        break;
      case 40:
        execute_order_list({22, 27}, acc);
        break;
      case 41:
        execute_order_list({27}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void tick_ik1303_069750_decoded() {
    static constexpr uint32_t kCommand = 0x069750;
    if (tick_index == 0) command = kCommand;

    const int current_tick = tick_index;
    const bool branch_l = l != 0;
    if (current_tick < 27) {
      sync_address = 80;
    } else if (current_tick < 36) {
      sync_address = 46;
    } else {
      sync_address = 26;
    }

    Accumulator acc;
    execute_069750_pre_add_orders(current_tick, branch_l, acc);

    if (key_y == 0) t = 0;

    execute_069750_gamma_orders(current_tick, branch_l, acc);

    int sum = acc.alpha + acc.beta + acc.gamma;
    acc.sigma = sum & 0xF;
    carry = (sum >> 4) & 1;

    execute_069750_post_add_orders(current_tick, branch_l, acc);

    output = memory[current_tick];
    memory[current_tick] = input;
    ++tick_index;
    if (tick_index == 42) tick_index = 0;
  }


  void execute_2f7047_pre_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 1:
        execute_order_list({3, 11}, acc);
        break;
      case 2:
        execute_order_list({4, 7}, acc);
        break;
      case 3:
        execute_order_list({5}, acc);
        break;
      case 4:
        execute_order_list({3}, acc);
        break;
      case 5:
        execute_order_list({4, 7}, acc);
        break;
      case 6:
        execute_order_list({5}, acc);
        break;
      case 7:
        execute_order_list({3}, acc);
        break;
      case 8:
        execute_order_list({4, 7}, acc);
        break;
      case 9:
        execute_order_list({5}, acc);
        break;
      case 10:
        execute_order_list({3}, acc);
        break;
      case 11:
        execute_order_list({4, 7}, acc);
        break;
      case 12:
        execute_order_list({5}, acc);
        break;
      case 13:
        execute_order_list({3}, acc);
        break;
      case 14:
        execute_order_list({4, 7}, acc);
        break;
      case 15:
        execute_order_list({5}, acc);
        break;
      case 16:
        execute_order_list({3}, acc);
        break;
      case 17:
        execute_order_list({4, 7}, acc);
        break;
      case 18:
        execute_order_list({5}, acc);
        break;
      case 19:
        execute_order_list({3}, acc);
        break;
      case 20:
        execute_order_list({4, 7}, acc);
        break;
      case 21:
        execute_order_list({5}, acc);
        break;
      case 22:
        execute_order_list({3}, acc);
        break;
      case 23:
        execute_order_list({4, 7}, acc);
        break;
      case 24:
        execute_order_list({5}, acc);
        break;
      case 25:
        execute_order_list({3}, acc);
        break;
      case 26:
        execute_order_list({4, 7}, acc);
        break;
      case 35:
        execute_order_list({7, 8}, acc);
        break;
      case 36:
        execute_order_list({0, 11}, acc);
        break;
      case 37:
        if (branch_l) {
          execute_order_list({0}, acc);
        } else {
          execute_order_list({5}, acc);
        }
        break;
      case 38:
        execute_order_list({5, 11}, acc);
        break;
      case 39:
        execute_order_list({0}, acc);
        break;
      case 40:
        if (branch_l) {
          execute_order_list({0}, acc);
        } else {
          execute_order_list({5}, acc);
        }
        break;
      case 41:
        execute_order_list({0}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void execute_2f7047_gamma_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 4:
        execute_order_list({12}, acc);
        break;
      case 7:
        execute_order_list({12}, acc);
        break;
      case 10:
        execute_order_list({12}, acc);
        break;
      case 13:
        execute_order_list({12}, acc);
        break;
      case 16:
        execute_order_list({12}, acc);
        break;
      case 19:
        execute_order_list({12}, acc);
        break;
      case 22:
        execute_order_list({12}, acc);
        break;
      case 25:
        execute_order_list({12}, acc);
        break;
      case 35:
        execute_order_list({12, 13}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void execute_2f7047_post_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 1:
        execute_order_list({25, 27}, acc);
        break;
      case 2:
        execute_order_list({22, 26, 29}, acc);
        break;
      case 3:
        execute_order_list({26, 29}, acc);
        break;
      case 4:
        execute_order_list({25, 27}, acc);
        break;
      case 5:
        execute_order_list({22, 26, 29}, acc);
        break;
      case 6:
        execute_order_list({26, 29}, acc);
        break;
      case 7:
        execute_order_list({25, 27}, acc);
        break;
      case 8:
        execute_order_list({22, 26, 29}, acc);
        break;
      case 9:
        execute_order_list({26, 29}, acc);
        break;
      case 10:
        execute_order_list({25, 27}, acc);
        break;
      case 11:
        execute_order_list({22, 26, 29}, acc);
        break;
      case 12:
        execute_order_list({26, 29}, acc);
        break;
      case 13:
        execute_order_list({25, 27}, acc);
        break;
      case 14:
        execute_order_list({22, 26, 29}, acc);
        break;
      case 15:
        execute_order_list({26, 29}, acc);
        break;
      case 16:
        execute_order_list({25, 27}, acc);
        break;
      case 17:
        execute_order_list({22, 26, 29}, acc);
        break;
      case 18:
        execute_order_list({26, 29}, acc);
        break;
      case 19:
        execute_order_list({25, 27}, acc);
        break;
      case 20:
        execute_order_list({22, 26, 29}, acc);
        break;
      case 21:
        execute_order_list({26, 29}, acc);
        break;
      case 22:
        execute_order_list({25, 27}, acc);
        break;
      case 23:
        execute_order_list({22, 26, 29}, acc);
        break;
      case 24:
        execute_order_list({26, 29}, acc);
        break;
      case 25:
        execute_order_list({25, 27}, acc);
        break;
      case 26:
        execute_order_list({22, 26, 29}, acc);
        break;
      case 35:
        execute_order_list({25, 27}, acc);
        break;
      case 36:
        execute_order_list({27}, acc);
        break;
      case 37:
        if (branch_l) {
          execute_order_list({17, 22, 27}, acc);
        } else {
          execute_order_list({22}, acc);
        }
        break;
      case 38:
        execute_order_list({23}, acc);
        break;
      case 39:
        execute_order_list({27}, acc);
        break;
      case 40:
        if (branch_l) {
          execute_order_list({17, 22, 27}, acc);
        } else {
          execute_order_list({22}, acc);
        }
        break;
      case 41:
        execute_order_list({27}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void tick_ik1303_2f7047_decoded() {
    static constexpr uint32_t kCommand = 0x2f7047;
    if (tick_index == 0) command = kCommand;

    const int current_tick = tick_index;
    const bool branch_l = l != 0;
    if (current_tick < 27) {
      sync_address = 71;
    } else if (current_tick < 36) {
      sync_address = 96;
    } else {
      if (current_tick == 36) {
        r[37] = 13 & 0xF;
        r[40] = 189 >> 4;
      }
      sync_address = 95;
    }

    Accumulator acc;
    execute_2f7047_pre_add_orders(current_tick, branch_l, acc);

    if (key_y == 0) t = 0;

    execute_2f7047_gamma_orders(current_tick, branch_l, acc);

    int sum = acc.alpha + acc.beta + acc.gamma;
    acc.sigma = sum & 0xF;
    carry = (sum >> 4) & 1;

    execute_2f7047_post_add_orders(current_tick, branch_l, acc);

    output = memory[current_tick];
    memory[current_tick] = input;
    ++tick_index;
    if (tick_index == 42) tick_index = 0;
  }


  void execute_05e879_pre_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 0:
        execute_order_list({5, 8}, acc);
        break;
      case 1:
        execute_order_list({5}, acc);
        break;
      case 2:
        execute_order_list({9}, acc);
        break;
      case 3:
        execute_order_list({6, 11}, acc);
        break;
      case 4:
        execute_order_list({5, 7}, acc);
        break;
      case 5:
        execute_order_list({5, 11}, acc);
        break;
      case 6:
        execute_order_list({6, 11}, acc);
        break;
      case 7:
        execute_order_list({5, 7}, acc);
        break;
      case 8:
        execute_order_list({5, 11}, acc);
        break;
      case 9:
        execute_order_list({6, 11}, acc);
        break;
      case 10:
        execute_order_list({5, 7}, acc);
        break;
      case 11:
        execute_order_list({5, 11}, acc);
        break;
      case 12:
        execute_order_list({6, 11}, acc);
        break;
      case 13:
        execute_order_list({5, 7}, acc);
        break;
      case 14:
        execute_order_list({5, 11}, acc);
        break;
      case 15:
        execute_order_list({6, 11}, acc);
        break;
      case 16:
        execute_order_list({5, 7}, acc);
        break;
      case 17:
        execute_order_list({5, 11}, acc);
        break;
      case 18:
        execute_order_list({6, 11}, acc);
        break;
      case 19:
        execute_order_list({5, 7}, acc);
        break;
      case 20:
        execute_order_list({5, 11}, acc);
        break;
      case 21:
        execute_order_list({6, 11}, acc);
        break;
      case 22:
        execute_order_list({5, 7}, acc);
        break;
      case 23:
        execute_order_list({5, 11}, acc);
        break;
      case 24:
        if (branch_l) {
          execute_order_list({5}, acc);
        } else {
          execute_order_list({0, 7}, acc);
        }
        break;
      case 25:
        execute_order_list({0, 7}, acc);
        break;
      case 26:
        execute_order_list({5}, acc);
        break;
      case 36:
        execute_order_list({0, 11}, acc);
        break;
      case 37:
        execute_order_list({5}, acc);
        break;
      case 38:
        execute_order_list({6, 7, 8}, acc);
        break;
      case 39:
        execute_order_list({5, 11}, acc);
        break;
      case 40:
        execute_order_list({3, 7}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void execute_05e879_gamma_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 2:
        execute_order_list({13}, acc);
        break;
      case 4:
        execute_order_list({12}, acc);
        break;
      case 7:
        execute_order_list({12}, acc);
        break;
      case 10:
        execute_order_list({12}, acc);
        break;
      case 13:
        execute_order_list({12}, acc);
        break;
      case 16:
        execute_order_list({12}, acc);
        break;
      case 19:
        execute_order_list({12}, acc);
        break;
      case 22:
        execute_order_list({12}, acc);
        break;
      case 26:
        execute_order_list({12}, acc);
        break;
      case 37:
        execute_order_list({12}, acc);
        break;
      case 40:
        execute_order_list({12, 13}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void execute_05e879_post_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 0:
        execute_order_list({27}, acc);
        break;
      case 1:
        execute_order_list({26, 29}, acc);
        break;
      case 2:
        execute_order_list({29}, acc);
        break;
      case 3:
        execute_order_list({27}, acc);
        break;
      case 4:
        execute_order_list({25, 27}, acc);
        break;
      case 5:
        execute_order_list({25, 27}, acc);
        break;
      case 6:
        execute_order_list({27}, acc);
        break;
      case 7:
        execute_order_list({25, 27}, acc);
        break;
      case 8:
        execute_order_list({25, 27}, acc);
        break;
      case 9:
        execute_order_list({27}, acc);
        break;
      case 10:
        execute_order_list({25, 27}, acc);
        break;
      case 11:
        execute_order_list({25, 27}, acc);
        break;
      case 12:
        execute_order_list({27}, acc);
        break;
      case 13:
        execute_order_list({25, 27}, acc);
        break;
      case 14:
        execute_order_list({25, 27}, acc);
        break;
      case 15:
        execute_order_list({27}, acc);
        break;
      case 16:
        execute_order_list({25, 27}, acc);
        break;
      case 17:
        execute_order_list({25, 27}, acc);
        break;
      case 18:
        execute_order_list({27}, acc);
        break;
      case 19:
        execute_order_list({25, 27}, acc);
        break;
      case 20:
        execute_order_list({25, 27}, acc);
        break;
      case 21:
        execute_order_list({27}, acc);
        break;
      case 22:
        execute_order_list({25, 27}, acc);
        break;
      case 23:
        execute_order_list({25, 27}, acc);
        break;
      case 24:
        if (branch_l) {
          execute_order_list({32}, acc);
        } else {
          execute_order_list({25, 26}, acc);
        }
        break;
      case 25:
        execute_order_list({16, 27}, acc);
        break;
      case 26:
        execute_order_list({22, 27}, acc);
        break;
      case 36:
        execute_order_list({27}, acc);
        break;
      case 37:
        execute_order_list({22, 27}, acc);
        break;
      case 38:
        execute_order_list({27}, acc);
        break;
      case 39:
        execute_order_list({25, 27}, acc);
        break;
      case 40:
        execute_order_list({25}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void tick_ik1303_05e879_decoded() {
    static constexpr uint32_t kCommand = 0x05e879;
    if (tick_index == 0) command = kCommand;

    const int current_tick = tick_index;
    const bool branch_l = l != 0;
    if (current_tick < 27) {
      sync_address = 121;
    } else if (current_tick < 36) {
      sync_address = 80;
    } else {
      sync_address = 23;
    }

    Accumulator acc;
    execute_05e879_pre_add_orders(current_tick, branch_l, acc);

    if (key_y == 0) t = 0;

    execute_05e879_gamma_orders(current_tick, branch_l, acc);

    int sum = acc.alpha + acc.beta + acc.gamma;
    acc.sigma = sum & 0xF;
    carry = (sum >> 4) & 1;

    execute_05e879_post_add_orders(current_tick, branch_l, acc);

    output = memory[current_tick];
    memory[current_tick] = input;
    ++tick_index;
    if (tick_index == 42) tick_index = 0;
  }


  void execute_17b035_pre_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 0:
        execute_order_list({4, 7, 8}, acc);
        break;
      case 2:
        execute_order_list({0}, acc);
        break;
      case 24:
        execute_order_list({9}, acc);
        break;
      case 25:
        execute_order_list({0}, acc);
        break;
      case 26:
        execute_order_list({5, 8}, acc);
        break;
      case 35:
        execute_order_list({7, 8}, acc);
        break;
      case 36:
        execute_order_list({0, 11}, acc);
        break;
      case 37:
        if (branch_l) {
          execute_order_list({0}, acc);
        } else {
          execute_order_list({5}, acc);
        }
        break;
      case 38:
        execute_order_list({5, 11}, acc);
        break;
      case 39:
        execute_order_list({0}, acc);
        break;
      case 40:
        if (branch_l) {
          execute_order_list({0}, acc);
        } else {
          execute_order_list({5}, acc);
        }
        break;
      case 41:
        execute_order_list({0}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void execute_17b035_gamma_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 0:
        execute_order_list({12}, acc);
        break;
      case 24:
        execute_order_list({13}, acc);
        break;
      case 35:
        execute_order_list({12, 13}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void execute_17b035_post_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 0:
        execute_order_list({27}, acc);
        break;
      case 1:
        execute_order_list({15}, acc);
        break;
      case 2:
        execute_order_list({15, 29}, acc);
        break;
      case 4:
        execute_order_list({15}, acc);
        break;
      case 5:
        execute_order_list({15}, acc);
        break;
      case 7:
        execute_order_list({15}, acc);
        break;
      case 8:
        execute_order_list({15}, acc);
        break;
      case 10:
        execute_order_list({15}, acc);
        break;
      case 11:
        execute_order_list({15}, acc);
        break;
      case 13:
        execute_order_list({15}, acc);
        break;
      case 14:
        execute_order_list({15}, acc);
        break;
      case 16:
        execute_order_list({15}, acc);
        break;
      case 17:
        execute_order_list({15}, acc);
        break;
      case 19:
        execute_order_list({15}, acc);
        break;
      case 20:
        execute_order_list({15}, acc);
        break;
      case 22:
        execute_order_list({15}, acc);
        break;
      case 23:
        execute_order_list({15}, acc);
        break;
      case 24:
        execute_order_list({29}, acc);
        break;
      case 25:
        execute_order_list({17, 27}, acc);
        break;
      case 26:
        execute_order_list({27}, acc);
        break;
      case 35:
        execute_order_list({25, 27}, acc);
        break;
      case 36:
        execute_order_list({27}, acc);
        break;
      case 37:
        if (branch_l) {
          execute_order_list({17, 22, 27}, acc);
        } else {
          execute_order_list({22}, acc);
        }
        break;
      case 38:
        execute_order_list({23}, acc);
        break;
      case 39:
        execute_order_list({27}, acc);
        break;
      case 40:
        if (branch_l) {
          execute_order_list({17, 22, 27}, acc);
        } else {
          execute_order_list({22}, acc);
        }
        break;
      case 41:
        execute_order_list({27}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void tick_ik1303_17b035_decoded() {
    static constexpr uint32_t kCommand = 0x17b035;
    if (tick_index == 0) command = kCommand;

    const int current_tick = tick_index;
    const bool branch_l = l != 0;
    if (current_tick < 27) {
      sync_address = 53;
    } else if (current_tick < 36) {
      sync_address = 96;
    } else {
      if (current_tick == 36) {
        r[37] = 14 & 0xF;
        r[40] = 94 >> 4;
      }
      sync_address = 95;
    }

    Accumulator acc;
    execute_17b035_pre_add_orders(current_tick, branch_l, acc);

    if (key_y == 0) t = 0;

    execute_17b035_gamma_orders(current_tick, branch_l, acc);

    int sum = acc.alpha + acc.beta + acc.gamma;
    acc.sigma = sum & 0xF;
    carry = (sum >> 4) & 1;

    execute_17b035_post_add_orders(current_tick, branch_l, acc);

    output = memory[current_tick];
    memory[current_tick] = input;
    ++tick_index;
    if (tick_index == 42) tick_index = 0;
  }


  void execute_41bfc7_pre_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 1:
        execute_order_list({3, 11}, acc);
        break;
      case 2:
        execute_order_list({4, 7}, acc);
        break;
      case 3:
        execute_order_list({5}, acc);
        break;
      case 4:
        execute_order_list({3}, acc);
        break;
      case 5:
        execute_order_list({4, 7}, acc);
        break;
      case 6:
        execute_order_list({5}, acc);
        break;
      case 7:
        execute_order_list({3}, acc);
        break;
      case 8:
        execute_order_list({4, 7}, acc);
        break;
      case 9:
        execute_order_list({5}, acc);
        break;
      case 10:
        execute_order_list({3}, acc);
        break;
      case 11:
        execute_order_list({4, 7}, acc);
        break;
      case 12:
        execute_order_list({5}, acc);
        break;
      case 13:
        execute_order_list({3}, acc);
        break;
      case 14:
        execute_order_list({4, 7}, acc);
        break;
      case 15:
        execute_order_list({5}, acc);
        break;
      case 16:
        execute_order_list({3}, acc);
        break;
      case 17:
        execute_order_list({4, 7}, acc);
        break;
      case 18:
        execute_order_list({5}, acc);
        break;
      case 19:
        execute_order_list({3}, acc);
        break;
      case 20:
        execute_order_list({4, 7}, acc);
        break;
      case 21:
        execute_order_list({5}, acc);
        break;
      case 22:
        execute_order_list({3}, acc);
        break;
      case 23:
        execute_order_list({4, 7}, acc);
        break;
      case 24:
        execute_order_list({5}, acc);
        break;
      case 25:
        execute_order_list({3}, acc);
        break;
      case 26:
        execute_order_list({4, 7}, acc);
        break;
      case 27:
        execute_order_list({5}, acc);
        break;
      case 28:
        execute_order_list({5}, acc);
        break;
      case 29:
        execute_order_list({5}, acc);
        break;
      case 32:
        execute_order_list({6, 7, 8}, acc);
        break;
      case 33:
        execute_order_list({8}, acc);
        break;
      case 34:
        execute_order_list({5}, acc);
        break;
      case 35:
        execute_order_list({5}, acc);
        break;
      case 36:
        execute_order_list({0}, acc);
        break;
      case 37:
        execute_order_list({10}, acc);
        break;
      case 38:
        execute_order_list({0, 7}, acc);
        break;
      case 40:
        execute_order_list({0}, acc);
        break;
      case 41:
        execute_order_list({0}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void execute_41bfc7_gamma_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 4:
        execute_order_list({12}, acc);
        break;
      case 7:
        execute_order_list({12}, acc);
        break;
      case 10:
        execute_order_list({12}, acc);
        break;
      case 13:
        execute_order_list({12}, acc);
        break;
      case 16:
        execute_order_list({12}, acc);
        break;
      case 19:
        execute_order_list({12}, acc);
        break;
      case 22:
        execute_order_list({12}, acc);
        break;
      case 25:
        execute_order_list({12}, acc);
        break;
      case 34:
        execute_order_list({12}, acc);
        break;
      case 35:
        execute_order_list({12}, acc);
        break;
      case 38:
        execute_order_list({12, 13}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void execute_41bfc7_post_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 1:
        execute_order_list({25, 27}, acc);
        break;
      case 2:
        execute_order_list({26, 29}, acc);
        break;
      case 3:
        execute_order_list({26, 29}, acc);
        break;
      case 4:
        execute_order_list({25, 27}, acc);
        break;
      case 5:
        execute_order_list({26, 29}, acc);
        break;
      case 6:
        execute_order_list({26, 29}, acc);
        break;
      case 7:
        execute_order_list({25, 27}, acc);
        break;
      case 8:
        execute_order_list({26, 29}, acc);
        break;
      case 9:
        execute_order_list({26, 29}, acc);
        break;
      case 10:
        execute_order_list({25, 27}, acc);
        break;
      case 11:
        execute_order_list({26, 29}, acc);
        break;
      case 12:
        execute_order_list({26, 29}, acc);
        break;
      case 13:
        execute_order_list({25, 27}, acc);
        break;
      case 14:
        execute_order_list({26, 29}, acc);
        break;
      case 15:
        execute_order_list({26, 29}, acc);
        break;
      case 16:
        execute_order_list({25, 27}, acc);
        break;
      case 17:
        execute_order_list({26, 29}, acc);
        break;
      case 18:
        execute_order_list({26, 29}, acc);
        break;
      case 19:
        execute_order_list({25, 27}, acc);
        break;
      case 20:
        execute_order_list({26, 29}, acc);
        break;
      case 21:
        execute_order_list({26, 29}, acc);
        break;
      case 22:
        execute_order_list({25, 27}, acc);
        break;
      case 23:
        execute_order_list({26, 29}, acc);
        break;
      case 24:
        execute_order_list({26, 29}, acc);
        break;
      case 25:
        execute_order_list({25, 27}, acc);
        break;
      case 26:
        execute_order_list({26, 29}, acc);
        break;
      case 27:
        execute_order_list({26, 29}, acc);
        break;
      case 32:
        execute_order_list({27}, acc);
        break;
      case 33:
        execute_order_list({27}, acc);
        break;
      case 34:
        execute_order_list({27}, acc);
        break;
      case 35:
        execute_order_list({27}, acc);
        break;
      case 36:
        execute_order_list({17, 27}, acc);
        break;
      case 37:
        execute_order_list({27}, acc);
        break;
      case 38:
        execute_order_list({25, 27}, acc);
        break;
      case 40:
        execute_order_list({27}, acc);
        break;
      case 41:
        execute_order_list({29}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void tick_ik1303_41bfc7_decoded() {
    static constexpr uint32_t kCommand = 0x41bfc7;
    if (tick_index == 0) command = kCommand;

    const int current_tick = tick_index;
    const bool branch_l = l != 0;
    if (current_tick < 27) {
      sync_address = 71;
    } else if (current_tick < 36) {
      sync_address = 127;
    } else {
      sync_address = 6;
    }

    Accumulator acc;
    execute_41bfc7_pre_add_orders(current_tick, branch_l, acc);

    if (key_y == 0) t = 0;

    execute_41bfc7_gamma_orders(current_tick, branch_l, acc);

    int sum = acc.alpha + acc.beta + acc.gamma;
    acc.sigma = sum & 0xF;
    carry = (sum >> 4) & 1;

    execute_41bfc7_post_add_orders(current_tick, branch_l, acc);

    output = memory[current_tick];
    memory[current_tick] = input;
    ++tick_index;
    if (tick_index == 42) tick_index = 0;
  }


  void execute_232951_pre_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 1:
        execute_order_list({0, 10}, acc);
        break;
      case 2:
        execute_order_list({0, 7}, acc);
        break;
      case 3:
        execute_order_list({4, 7}, acc);
        break;
      case 4:
        execute_order_list({0, 10}, acc);
        break;
      case 5:
        execute_order_list({0, 7}, acc);
        break;
      case 6:
        execute_order_list({4, 7}, acc);
        break;
      case 7:
        execute_order_list({0, 10}, acc);
        break;
      case 8:
        execute_order_list({0, 7}, acc);
        break;
      case 9:
        execute_order_list({4, 7}, acc);
        break;
      case 10:
        execute_order_list({0, 10}, acc);
        break;
      case 11:
        execute_order_list({0, 7}, acc);
        break;
      case 12:
        execute_order_list({4, 7}, acc);
        break;
      case 13:
        execute_order_list({0, 10}, acc);
        break;
      case 14:
        execute_order_list({0, 7}, acc);
        break;
      case 15:
        execute_order_list({4, 7}, acc);
        break;
      case 16:
        execute_order_list({0, 10}, acc);
        break;
      case 17:
        execute_order_list({0, 7}, acc);
        break;
      case 18:
        execute_order_list({4, 7}, acc);
        break;
      case 19:
        execute_order_list({0, 10}, acc);
        break;
      case 20:
        execute_order_list({0, 7}, acc);
        break;
      case 21:
        execute_order_list({4, 7}, acc);
        break;
      case 22:
        execute_order_list({0, 10}, acc);
        break;
      case 23:
        execute_order_list({0, 7}, acc);
        break;
      case 24:
        execute_order_list({4, 7}, acc);
        break;
      case 25:
        execute_order_list({0, 10}, acc);
        break;
      case 26:
        execute_order_list({0, 7}, acc);
        break;
      case 27:
        execute_order_list({4, 7}, acc);
        break;
      case 28:
        execute_order_list({6, 7, 8}, acc);
        break;
      case 29:
        execute_order_list({5}, acc);
        break;
      case 30:
        execute_order_list({5}, acc);
        break;
      case 31:
        execute_order_list({0, 3, 7}, acc);
        break;
      case 32:
        execute_order_list({5}, acc);
        break;
      case 33:
        execute_order_list({6, 7, 8}, acc);
        break;
      case 34:
        execute_order_list({5}, acc);
        break;
      case 36:
        execute_order_list({0, 11}, acc);
        break;
      case 37:
        if (branch_l) {
          execute_order_list({0}, acc);
        } else {
          execute_order_list({5}, acc);
        }
        break;
      case 38:
        execute_order_list({5, 11}, acc);
        break;
      case 39:
        execute_order_list({0}, acc);
        break;
      case 40:
        if (branch_l) {
          execute_order_list({0}, acc);
        } else {
          execute_order_list({5}, acc);
        }
        break;
      case 41:
        execute_order_list({0}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void execute_232951_gamma_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 5:
        execute_order_list({12}, acc);
        break;
      case 8:
        execute_order_list({12}, acc);
        break;
      case 11:
        execute_order_list({12}, acc);
        break;
      case 14:
        execute_order_list({12}, acc);
        break;
      case 17:
        execute_order_list({12}, acc);
        break;
      case 20:
        execute_order_list({12}, acc);
        break;
      case 23:
        execute_order_list({12}, acc);
        break;
      case 26:
        execute_order_list({12}, acc);
        break;
      case 29:
        execute_order_list({12}, acc);
        break;
      case 30:
        execute_order_list({12}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void execute_232951_post_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 1:
        execute_order_list({27}, acc);
        break;
      case 2:
        execute_order_list({25, 27}, acc);
        break;
      case 3:
        execute_order_list({22, 26, 29}, acc);
        break;
      case 4:
        execute_order_list({27}, acc);
        break;
      case 5:
        execute_order_list({25, 27}, acc);
        break;
      case 6:
        execute_order_list({22, 26, 29}, acc);
        break;
      case 7:
        execute_order_list({27}, acc);
        break;
      case 8:
        execute_order_list({25, 27}, acc);
        break;
      case 9:
        execute_order_list({22, 26, 29}, acc);
        break;
      case 10:
        execute_order_list({27}, acc);
        break;
      case 11:
        execute_order_list({25, 27}, acc);
        break;
      case 12:
        execute_order_list({22, 26, 29}, acc);
        break;
      case 13:
        execute_order_list({27}, acc);
        break;
      case 14:
        execute_order_list({25, 27}, acc);
        break;
      case 15:
        execute_order_list({22, 26, 29}, acc);
        break;
      case 16:
        execute_order_list({27}, acc);
        break;
      case 17:
        execute_order_list({25, 27}, acc);
        break;
      case 18:
        execute_order_list({22, 26, 29}, acc);
        break;
      case 19:
        execute_order_list({27}, acc);
        break;
      case 20:
        execute_order_list({25, 27}, acc);
        break;
      case 21:
        execute_order_list({22, 26, 29}, acc);
        break;
      case 22:
        execute_order_list({27}, acc);
        break;
      case 23:
        execute_order_list({25, 27}, acc);
        break;
      case 24:
        execute_order_list({22, 26, 29}, acc);
        break;
      case 25:
        execute_order_list({27}, acc);
        break;
      case 26:
        execute_order_list({25, 27}, acc);
        break;
      case 27:
        execute_order_list({22, 26, 29}, acc);
        break;
      case 28:
        execute_order_list({27}, acc);
        break;
      case 29:
        execute_order_list({27}, acc);
        break;
      case 30:
        execute_order_list({27}, acc);
        break;
      case 31:
        execute_order_list({25, 27}, acc);
        break;
      case 32:
        execute_order_list({26, 29}, acc);
        break;
      case 33:
        execute_order_list({27}, acc);
        break;
      case 34:
        execute_order_list({26, 29}, acc);
        break;
      case 36:
        execute_order_list({27}, acc);
        break;
      case 37:
        if (branch_l) {
          execute_order_list({17, 22, 27}, acc);
        } else {
          execute_order_list({22}, acc);
        }
        break;
      case 38:
        execute_order_list({23}, acc);
        break;
      case 39:
        execute_order_list({27}, acc);
        break;
      case 40:
        if (branch_l) {
          execute_order_list({17, 22, 27}, acc);
        } else {
          execute_order_list({22}, acc);
        }
        break;
      case 41:
        execute_order_list({27}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void tick_ik1303_232951_decoded() {
    static constexpr uint32_t kCommand = 0x232951;
    if (tick_index == 0) command = kCommand;

    const int current_tick = tick_index;
    const bool branch_l = l != 0;
    if (current_tick < 27) {
      sync_address = 81;
    } else if (current_tick < 36) {
      sync_address = 82;
    } else {
      if (current_tick == 36) {
        r[37] = 12 & 0xF;
        r[40] = 140 >> 4;
      }
      sync_address = 95;
    }

    Accumulator acc;
    execute_232951_pre_add_orders(current_tick, branch_l, acc);

    if (key_y == 0) t = 0;

    execute_232951_gamma_orders(current_tick, branch_l, acc);

    int sum = acc.alpha + acc.beta + acc.gamma;
    acc.sigma = sum & 0xF;
    carry = (sum >> 4) & 1;

    execute_232951_post_add_orders(current_tick, branch_l, acc);

    output = memory[current_tick];
    memory[current_tick] = input;
    ++tick_index;
    if (tick_index == 42) tick_index = 0;
  }


  void execute_032951_pre_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 1:
        execute_order_list({0, 10}, acc);
        break;
      case 2:
        execute_order_list({0, 7}, acc);
        break;
      case 3:
        execute_order_list({4, 7}, acc);
        break;
      case 4:
        execute_order_list({0, 10}, acc);
        break;
      case 5:
        execute_order_list({0, 7}, acc);
        break;
      case 6:
        execute_order_list({4, 7}, acc);
        break;
      case 7:
        execute_order_list({0, 10}, acc);
        break;
      case 8:
        execute_order_list({0, 7}, acc);
        break;
      case 9:
        execute_order_list({4, 7}, acc);
        break;
      case 10:
        execute_order_list({0, 10}, acc);
        break;
      case 11:
        execute_order_list({0, 7}, acc);
        break;
      case 12:
        execute_order_list({4, 7}, acc);
        break;
      case 13:
        execute_order_list({0, 10}, acc);
        break;
      case 14:
        execute_order_list({0, 7}, acc);
        break;
      case 15:
        execute_order_list({4, 7}, acc);
        break;
      case 16:
        execute_order_list({0, 10}, acc);
        break;
      case 17:
        execute_order_list({0, 7}, acc);
        break;
      case 18:
        execute_order_list({4, 7}, acc);
        break;
      case 19:
        execute_order_list({0, 10}, acc);
        break;
      case 20:
        execute_order_list({0, 7}, acc);
        break;
      case 21:
        execute_order_list({4, 7}, acc);
        break;
      case 22:
        execute_order_list({0, 10}, acc);
        break;
      case 23:
        execute_order_list({0, 7}, acc);
        break;
      case 24:
        execute_order_list({4, 7}, acc);
        break;
      case 25:
        execute_order_list({0, 10}, acc);
        break;
      case 26:
        execute_order_list({0, 7}, acc);
        break;
      case 27:
        execute_order_list({4, 7}, acc);
        break;
      case 28:
        execute_order_list({6, 7, 8}, acc);
        break;
      case 29:
        execute_order_list({5}, acc);
        break;
      case 30:
        execute_order_list({5}, acc);
        break;
      case 31:
        execute_order_list({0, 3, 7}, acc);
        break;
      case 32:
        execute_order_list({5}, acc);
        break;
      case 33:
        execute_order_list({6, 7, 8}, acc);
        break;
      case 34:
        execute_order_list({5}, acc);
        break;
      case 36:
        execute_order_list({0, 11}, acc);
        break;
      case 38:
        execute_order_list({5, 11}, acc);
        break;
      case 39:
        execute_order_list({7, 8}, acc);
        break;
      case 40:
        execute_order_list({1}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void execute_032951_gamma_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 5:
        execute_order_list({12}, acc);
        break;
      case 8:
        execute_order_list({12}, acc);
        break;
      case 11:
        execute_order_list({12}, acc);
        break;
      case 14:
        execute_order_list({12}, acc);
        break;
      case 17:
        execute_order_list({12}, acc);
        break;
      case 20:
        execute_order_list({12}, acc);
        break;
      case 23:
        execute_order_list({12}, acc);
        break;
      case 26:
        execute_order_list({12}, acc);
        break;
      case 29:
        execute_order_list({12}, acc);
        break;
      case 30:
        execute_order_list({12}, acc);
        break;
      case 39:
        execute_order_list({12, 13}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void execute_032951_post_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 1:
        execute_order_list({27}, acc);
        break;
      case 2:
        execute_order_list({25, 27}, acc);
        break;
      case 3:
        execute_order_list({22, 26, 29}, acc);
        break;
      case 4:
        execute_order_list({27}, acc);
        break;
      case 5:
        execute_order_list({25, 27}, acc);
        break;
      case 6:
        execute_order_list({22, 26, 29}, acc);
        break;
      case 7:
        execute_order_list({27}, acc);
        break;
      case 8:
        execute_order_list({25, 27}, acc);
        break;
      case 9:
        execute_order_list({22, 26, 29}, acc);
        break;
      case 10:
        execute_order_list({27}, acc);
        break;
      case 11:
        execute_order_list({25, 27}, acc);
        break;
      case 12:
        execute_order_list({22, 26, 29}, acc);
        break;
      case 13:
        execute_order_list({27}, acc);
        break;
      case 14:
        execute_order_list({25, 27}, acc);
        break;
      case 15:
        execute_order_list({22, 26, 29}, acc);
        break;
      case 16:
        execute_order_list({27}, acc);
        break;
      case 17:
        execute_order_list({25, 27}, acc);
        break;
      case 18:
        execute_order_list({22, 26, 29}, acc);
        break;
      case 19:
        execute_order_list({27}, acc);
        break;
      case 20:
        execute_order_list({25, 27}, acc);
        break;
      case 21:
        execute_order_list({22, 26, 29}, acc);
        break;
      case 22:
        execute_order_list({27}, acc);
        break;
      case 23:
        execute_order_list({25, 27}, acc);
        break;
      case 24:
        execute_order_list({22, 26, 29}, acc);
        break;
      case 25:
        execute_order_list({27}, acc);
        break;
      case 26:
        execute_order_list({25, 27}, acc);
        break;
      case 27:
        execute_order_list({22, 26, 29}, acc);
        break;
      case 28:
        execute_order_list({27}, acc);
        break;
      case 29:
        execute_order_list({27}, acc);
        break;
      case 30:
        execute_order_list({27}, acc);
        break;
      case 31:
        execute_order_list({25, 27}, acc);
        break;
      case 32:
        execute_order_list({26, 29}, acc);
        break;
      case 33:
        execute_order_list({27}, acc);
        break;
      case 34:
        execute_order_list({26, 29}, acc);
        break;
      case 36:
        execute_order_list({27}, acc);
        break;
      case 38:
        execute_order_list({23}, acc);
        break;
      case 39:
        execute_order_list({25, 27}, acc);
        break;
      case 40:
        execute_order_list({27}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void tick_ik1303_032951_decoded() {
    static constexpr uint32_t kCommand = 0x032951;
    if (tick_index == 0) command = kCommand;

    const int current_tick = tick_index;
    const bool branch_l = l != 0;
    if (current_tick < 27) {
      sync_address = 81;
    } else if (current_tick < 36) {
      sync_address = 82;
    } else {
      sync_address = 12;
    }

    Accumulator acc;
    execute_032951_pre_add_orders(current_tick, branch_l, acc);

    if (key_y == 0) t = 0;

    execute_032951_gamma_orders(current_tick, branch_l, acc);

    int sum = acc.alpha + acc.beta + acc.gamma;
    acc.sigma = sum & 0xF;
    carry = (sum >> 4) & 1;

    execute_032951_post_add_orders(current_tick, branch_l, acc);

    output = memory[current_tick];
    memory[current_tick] = input;
    ++tick_index;
    if (tick_index == 42) tick_index = 0;
  }


  void execute_1b94a6_pre_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 0:
        execute_order_list({7, 8}, acc);
        break;
      case 1:
        execute_order_list({0}, acc);
        break;
      case 2:
        execute_order_list({0}, acc);
        break;
      case 3:
        execute_order_list({7, 8}, acc);
        break;
      case 4:
        execute_order_list({0}, acc);
        break;
      case 5:
        execute_order_list({0}, acc);
        break;
      case 6:
        execute_order_list({7, 8}, acc);
        break;
      case 7:
        execute_order_list({0}, acc);
        break;
      case 8:
        execute_order_list({0}, acc);
        break;
      case 9:
        execute_order_list({7, 8}, acc);
        break;
      case 10:
        execute_order_list({0}, acc);
        break;
      case 11:
        execute_order_list({0}, acc);
        break;
      case 12:
        execute_order_list({7, 8}, acc);
        break;
      case 13:
        execute_order_list({0}, acc);
        break;
      case 14:
        execute_order_list({0}, acc);
        break;
      case 15:
        execute_order_list({7, 8}, acc);
        break;
      case 16:
        execute_order_list({0}, acc);
        break;
      case 17:
        execute_order_list({0}, acc);
        break;
      case 18:
        execute_order_list({7, 8}, acc);
        break;
      case 19:
        execute_order_list({0}, acc);
        break;
      case 20:
        execute_order_list({0}, acc);
        break;
      case 21:
        execute_order_list({7, 8}, acc);
        break;
      case 22:
        execute_order_list({0}, acc);
        break;
      case 23:
        execute_order_list({0}, acc);
        break;
      case 24:
        execute_order_list({7, 8}, acc);
        break;
      case 25:
        execute_order_list({0}, acc);
        break;
      case 28:
        execute_order_list({0}, acc);
        break;
      case 29:
        execute_order_list({0, 3, 7}, acc);
        break;
      case 30:
        execute_order_list({6, 11}, acc);
        break;
      case 31:
        execute_order_list({5}, acc);
        break;
      case 32:
        execute_order_list({5}, acc);
        break;
      case 33:
        execute_order_list({0, 3, 7}, acc);
        break;
      case 35:
        execute_order_list({4, 7, 8}, acc);
        break;
      case 36:
        execute_order_list({0, 11}, acc);
        break;
      case 37:
        if (branch_l) {
          execute_order_list({0}, acc);
        } else {
          execute_order_list({5}, acc);
        }
        break;
      case 38:
        execute_order_list({5, 11}, acc);
        break;
      case 39:
        execute_order_list({0}, acc);
        break;
      case 40:
        if (branch_l) {
          execute_order_list({0}, acc);
        } else {
          execute_order_list({5}, acc);
        }
        break;
      case 41:
        execute_order_list({0}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void execute_1b94a6_gamma_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 0:
        execute_order_list({12, 13}, acc);
        break;
      case 3:
        execute_order_list({12, 13}, acc);
        break;
      case 6:
        execute_order_list({12, 13}, acc);
        break;
      case 9:
        execute_order_list({12, 13}, acc);
        break;
      case 12:
        execute_order_list({12, 13}, acc);
        break;
      case 15:
        execute_order_list({12, 13}, acc);
        break;
      case 18:
        execute_order_list({12, 13}, acc);
        break;
      case 21:
        execute_order_list({12, 13}, acc);
        break;
      case 24:
        execute_order_list({12, 13}, acc);
        break;
      case 31:
        execute_order_list({12}, acc);
        break;
      case 35:
        execute_order_list({12}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void execute_1b94a6_post_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 0:
        execute_order_list({25, 27}, acc);
        break;
      case 1:
        execute_order_list({17, 27}, acc);
        break;
      case 2:
        execute_order_list({17, 27}, acc);
        break;
      case 3:
        execute_order_list({25, 27}, acc);
        break;
      case 4:
        execute_order_list({17, 27}, acc);
        break;
      case 5:
        execute_order_list({17, 27}, acc);
        break;
      case 6:
        execute_order_list({25, 27}, acc);
        break;
      case 7:
        execute_order_list({17, 27}, acc);
        break;
      case 8:
        execute_order_list({17, 27}, acc);
        break;
      case 9:
        execute_order_list({25, 27}, acc);
        break;
      case 10:
        execute_order_list({17, 27}, acc);
        break;
      case 11:
        execute_order_list({17, 27}, acc);
        break;
      case 12:
        execute_order_list({25, 27}, acc);
        break;
      case 13:
        execute_order_list({17, 27}, acc);
        break;
      case 14:
        execute_order_list({17, 27}, acc);
        break;
      case 15:
        execute_order_list({25, 27}, acc);
        break;
      case 16:
        execute_order_list({17, 27}, acc);
        break;
      case 17:
        execute_order_list({17, 27}, acc);
        break;
      case 18:
        execute_order_list({25, 27}, acc);
        break;
      case 19:
        execute_order_list({17, 27}, acc);
        break;
      case 20:
        execute_order_list({17, 27}, acc);
        break;
      case 21:
        execute_order_list({25, 27}, acc);
        break;
      case 22:
        execute_order_list({17, 27}, acc);
        break;
      case 23:
        execute_order_list({17, 27}, acc);
        break;
      case 24:
        execute_order_list({25, 27}, acc);
        break;
      case 25:
        execute_order_list({29}, acc);
        break;
      case 26:
        execute_order_list({22}, acc);
        break;
      case 28:
        execute_order_list({17, 27}, acc);
        break;
      case 29:
        execute_order_list({25, 27}, acc);
        break;
      case 30:
        execute_order_list({27}, acc);
        break;
      case 31:
        execute_order_list({27}, acc);
        break;
      case 32:
        execute_order_list({26, 29}, acc);
        break;
      case 33:
        execute_order_list({25, 27}, acc);
        break;
      case 35:
        execute_order_list({16}, acc);
        break;
      case 36:
        execute_order_list({27}, acc);
        break;
      case 37:
        if (branch_l) {
          execute_order_list({17, 22, 27}, acc);
        } else {
          execute_order_list({22}, acc);
        }
        break;
      case 38:
        execute_order_list({23}, acc);
        break;
      case 39:
        execute_order_list({27}, acc);
        break;
      case 40:
        if (branch_l) {
          execute_order_list({17, 22, 27}, acc);
        } else {
          execute_order_list({22}, acc);
        }
        break;
      case 41:
        execute_order_list({27}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void tick_ik1303_1b94a6_decoded() {
    static constexpr uint32_t kCommand = 0x1b94a6;
    if (tick_index == 0) command = kCommand;

    const int current_tick = tick_index;
    const bool branch_l = l != 0;
    if (current_tick < 27) {
      sync_address = 38;
    } else if (current_tick < 36) {
      sync_address = 41;
    } else {
      if (current_tick == 36) {
        r[37] = 14 & 0xF;
        r[40] = 110 >> 4;
      }
      sync_address = 95;
    }

    Accumulator acc;
    execute_1b94a6_pre_add_orders(current_tick, branch_l, acc);

    if (key_y == 0) t = 0;

    execute_1b94a6_gamma_orders(current_tick, branch_l, acc);

    int sum = acc.alpha + acc.beta + acc.gamma;
    acc.sigma = sum & 0xF;
    carry = (sum >> 4) & 1;

    execute_1b94a6_post_add_orders(current_tick, branch_l, acc);

    output = memory[current_tick];
    memory[current_tick] = input;
    ++tick_index;
    if (tick_index == 42) tick_index = 0;
  }


  void execute_23f02f_pre_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 0:
        execute_order_list({3}, acc);
        break;
      case 1:
        execute_order_list({0, 7}, acc);
        break;
      case 2:
        execute_order_list({4, 7}, acc);
        break;
      case 3:
        execute_order_list({3}, acc);
        break;
      case 4:
        execute_order_list({0, 7}, acc);
        break;
      case 5:
        execute_order_list({4, 7}, acc);
        break;
      case 6:
        execute_order_list({3}, acc);
        break;
      case 7:
        execute_order_list({0, 7}, acc);
        break;
      case 8:
        execute_order_list({4, 7}, acc);
        break;
      case 9:
        execute_order_list({3}, acc);
        break;
      case 10:
        execute_order_list({0, 7}, acc);
        break;
      case 11:
        execute_order_list({4, 7}, acc);
        break;
      case 12:
        execute_order_list({3}, acc);
        break;
      case 13:
        execute_order_list({0, 7}, acc);
        break;
      case 14:
        execute_order_list({4, 7}, acc);
        break;
      case 15:
        execute_order_list({3}, acc);
        break;
      case 16:
        execute_order_list({0, 7}, acc);
        break;
      case 17:
        execute_order_list({4, 7}, acc);
        break;
      case 18:
        execute_order_list({3}, acc);
        break;
      case 19:
        execute_order_list({0, 7}, acc);
        break;
      case 20:
        execute_order_list({4, 7}, acc);
        break;
      case 21:
        execute_order_list({3}, acc);
        break;
      case 22:
        execute_order_list({0, 7}, acc);
        break;
      case 23:
        execute_order_list({4, 7}, acc);
        break;
      case 24:
        execute_order_list({3}, acc);
        break;
      case 25:
        execute_order_list({0, 7}, acc);
        break;
      case 26:
        execute_order_list({4, 7}, acc);
        break;
      case 35:
        execute_order_list({7, 8}, acc);
        break;
      case 36:
        execute_order_list({0, 11}, acc);
        break;
      case 37:
        if (branch_l) {
          execute_order_list({0}, acc);
        } else {
          execute_order_list({5}, acc);
        }
        break;
      case 38:
        execute_order_list({5, 11}, acc);
        break;
      case 39:
        execute_order_list({0}, acc);
        break;
      case 40:
        if (branch_l) {
          execute_order_list({0}, acc);
        } else {
          execute_order_list({5}, acc);
        }
        break;
      case 41:
        execute_order_list({0}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void execute_23f02f_gamma_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 1:
        execute_order_list({12, 13}, acc);
        break;
      case 4:
        execute_order_list({12}, acc);
        break;
      case 7:
        execute_order_list({12}, acc);
        break;
      case 10:
        execute_order_list({12}, acc);
        break;
      case 13:
        execute_order_list({12}, acc);
        break;
      case 16:
        execute_order_list({12}, acc);
        break;
      case 19:
        execute_order_list({12}, acc);
        break;
      case 22:
        execute_order_list({12}, acc);
        break;
      case 25:
        execute_order_list({12}, acc);
        break;
      case 35:
        execute_order_list({12, 13}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void execute_23f02f_post_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 0:
        execute_order_list({27}, acc);
        break;
      case 1:
        execute_order_list({25, 27}, acc);
        break;
      case 2:
        execute_order_list({22, 26, 29}, acc);
        break;
      case 3:
        execute_order_list({27}, acc);
        break;
      case 4:
        execute_order_list({25, 27}, acc);
        break;
      case 5:
        execute_order_list({22, 26, 29}, acc);
        break;
      case 6:
        execute_order_list({27}, acc);
        break;
      case 7:
        execute_order_list({25, 27}, acc);
        break;
      case 8:
        execute_order_list({22, 26, 29}, acc);
        break;
      case 9:
        execute_order_list({27}, acc);
        break;
      case 10:
        execute_order_list({25, 27}, acc);
        break;
      case 11:
        execute_order_list({22, 26, 29}, acc);
        break;
      case 12:
        execute_order_list({27}, acc);
        break;
      case 13:
        execute_order_list({25, 27}, acc);
        break;
      case 14:
        execute_order_list({22, 26, 29}, acc);
        break;
      case 15:
        execute_order_list({27}, acc);
        break;
      case 16:
        execute_order_list({25, 27}, acc);
        break;
      case 17:
        execute_order_list({22, 26, 29}, acc);
        break;
      case 18:
        execute_order_list({27}, acc);
        break;
      case 19:
        execute_order_list({25, 27}, acc);
        break;
      case 20:
        execute_order_list({22, 26, 29}, acc);
        break;
      case 21:
        execute_order_list({27}, acc);
        break;
      case 22:
        execute_order_list({25, 27}, acc);
        break;
      case 23:
        execute_order_list({22, 26, 29}, acc);
        break;
      case 24:
        execute_order_list({27}, acc);
        break;
      case 25:
        execute_order_list({25, 27}, acc);
        break;
      case 26:
        execute_order_list({22, 26, 29}, acc);
        break;
      case 35:
        execute_order_list({25, 27}, acc);
        break;
      case 36:
        execute_order_list({27}, acc);
        break;
      case 37:
        if (branch_l) {
          execute_order_list({17, 22, 27}, acc);
        } else {
          execute_order_list({22}, acc);
        }
        break;
      case 38:
        execute_order_list({23}, acc);
        break;
      case 39:
        execute_order_list({27}, acc);
        break;
      case 40:
        if (branch_l) {
          execute_order_list({17, 22, 27}, acc);
        } else {
          execute_order_list({22}, acc);
        }
        break;
      case 41:
        execute_order_list({27}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void tick_ik1303_23f02f_decoded() {
    static constexpr uint32_t kCommand = 0x23f02f;
    if (tick_index == 0) command = kCommand;

    const int current_tick = tick_index;
    const bool branch_l = l != 0;
    if (current_tick < 27) {
      sync_address = 47;
    } else if (current_tick < 36) {
      sync_address = 96;
    } else {
      if (current_tick == 36) {
        r[37] = 15 & 0xF;
        r[40] = 143 >> 4;
      }
      sync_address = 95;
    }

    Accumulator acc;
    execute_23f02f_pre_add_orders(current_tick, branch_l, acc);

    if (key_y == 0) t = 0;

    execute_23f02f_gamma_orders(current_tick, branch_l, acc);

    int sum = acc.alpha + acc.beta + acc.gamma;
    acc.sigma = sum & 0xF;
    carry = (sum >> 4) & 1;

    execute_23f02f_post_add_orders(current_tick, branch_l, acc);

    output = memory[current_tick];
    memory[current_tick] = input;
    ++tick_index;
    if (tick_index == 42) tick_index = 0;
  }


  void execute_23282a_pre_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 0:
        execute_order_list({5}, acc);
        break;
      case 3:
        execute_order_list({9}, acc);
        break;
      case 4:
        execute_order_list({0, 3, 7}, acc);
        break;
      case 5:
        execute_order_list({9}, acc);
        break;
      case 6:
        execute_order_list({9}, acc);
        break;
      case 7:
        execute_order_list({0, 3, 7}, acc);
        break;
      case 8:
        execute_order_list({9}, acc);
        break;
      case 9:
        execute_order_list({9}, acc);
        break;
      case 10:
        execute_order_list({0, 3, 7}, acc);
        break;
      case 11:
        execute_order_list({9}, acc);
        break;
      case 12:
        execute_order_list({9}, acc);
        break;
      case 13:
        execute_order_list({0, 3, 7}, acc);
        break;
      case 14:
        execute_order_list({9}, acc);
        break;
      case 15:
        execute_order_list({9}, acc);
        break;
      case 16:
        execute_order_list({0, 3, 7}, acc);
        break;
      case 17:
        execute_order_list({9}, acc);
        break;
      case 18:
        execute_order_list({9}, acc);
        break;
      case 19:
        execute_order_list({0, 3, 7}, acc);
        break;
      case 20:
        execute_order_list({9}, acc);
        break;
      case 21:
        execute_order_list({9}, acc);
        break;
      case 22:
        execute_order_list({0, 3, 7}, acc);
        break;
      case 23:
        execute_order_list({9}, acc);
        break;
      case 24:
        execute_order_list({5, 9}, acc);
        break;
      case 26:
        execute_order_list({5, 11}, acc);
        break;
      case 36:
        execute_order_list({0, 11}, acc);
        break;
      case 37:
        if (branch_l) {
          execute_order_list({0}, acc);
        } else {
          execute_order_list({5}, acc);
        }
        break;
      case 38:
        execute_order_list({5, 11}, acc);
        break;
      case 39:
        execute_order_list({0}, acc);
        break;
      case 40:
        if (branch_l) {
          execute_order_list({0}, acc);
        } else {
          execute_order_list({5}, acc);
        }
        break;
      case 41:
        execute_order_list({0}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void execute_23282a_gamma_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 5:
        execute_order_list({13}, acc);
        break;
      case 8:
        execute_order_list({13}, acc);
        break;
      case 11:
        execute_order_list({13}, acc);
        break;
      case 14:
        execute_order_list({13}, acc);
        break;
      case 17:
        execute_order_list({13}, acc);
        break;
      case 20:
        execute_order_list({13}, acc);
        break;
      case 23:
        execute_order_list({13}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void execute_23282a_post_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 0:
        execute_order_list({26, 29}, acc);
        break;
      case 1:
        execute_order_list({22}, acc);
        break;
      case 2:
        execute_order_list({29}, acc);
        break;
      case 3:
        execute_order_list({16}, acc);
        break;
      case 4:
        execute_order_list({25, 27}, acc);
        break;
      case 5:
        execute_order_list({29}, acc);
        break;
      case 6:
        execute_order_list({16}, acc);
        break;
      case 7:
        execute_order_list({25, 27}, acc);
        break;
      case 8:
        execute_order_list({29}, acc);
        break;
      case 9:
        execute_order_list({16}, acc);
        break;
      case 10:
        execute_order_list({25, 27}, acc);
        break;
      case 11:
        execute_order_list({29}, acc);
        break;
      case 12:
        execute_order_list({16}, acc);
        break;
      case 13:
        execute_order_list({25, 27}, acc);
        break;
      case 14:
        execute_order_list({29}, acc);
        break;
      case 15:
        execute_order_list({16}, acc);
        break;
      case 16:
        execute_order_list({25, 27}, acc);
        break;
      case 17:
        execute_order_list({29}, acc);
        break;
      case 18:
        execute_order_list({16}, acc);
        break;
      case 19:
        execute_order_list({25, 27}, acc);
        break;
      case 20:
        execute_order_list({29}, acc);
        break;
      case 21:
        execute_order_list({16}, acc);
        break;
      case 22:
        execute_order_list({25, 27}, acc);
        break;
      case 23:
        execute_order_list({29}, acc);
        break;
      case 24:
        execute_order_list({25, 27, 29}, acc);
        break;
      case 26:
        execute_order_list({23}, acc);
        break;
      case 36:
        execute_order_list({27}, acc);
        break;
      case 37:
        if (branch_l) {
          execute_order_list({17, 22, 27}, acc);
        } else {
          execute_order_list({22}, acc);
        }
        break;
      case 38:
        execute_order_list({23}, acc);
        break;
      case 39:
        execute_order_list({27}, acc);
        break;
      case 40:
        if (branch_l) {
          execute_order_list({17, 22, 27}, acc);
        } else {
          execute_order_list({22}, acc);
        }
        break;
      case 41:
        execute_order_list({27}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void tick_ik1303_23282a_decoded() {
    static constexpr uint32_t kCommand = 0x23282a;
    if (tick_index == 0) command = kCommand;

    const int current_tick = tick_index;
    const bool branch_l = l != 0;
    if (current_tick < 27) {
      sync_address = 42;
    } else if (current_tick < 36) {
      sync_address = 80;
    } else {
      if (current_tick == 36) {
        r[37] = 12 & 0xF;
        r[40] = 140 >> 4;
      }
      sync_address = 95;
    }

    Accumulator acc;
    execute_23282a_pre_add_orders(current_tick, branch_l, acc);

    if (key_y == 0) t = 0;

    execute_23282a_gamma_orders(current_tick, branch_l, acc);

    int sum = acc.alpha + acc.beta + acc.gamma;
    acc.sigma = sum & 0xF;
    carry = (sum >> 4) & 1;

    execute_23282a_post_add_orders(current_tick, branch_l, acc);

    output = memory[current_tick];
    memory[current_tick] = input;
    ++tick_index;
    if (tick_index == 42) tick_index = 0;
  }


  void execute_05d4a8_pre_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 0:
        execute_order_list({0}, acc);
        break;
      case 1:
        execute_order_list({7, 8}, acc);
        break;
      case 2:
        execute_order_list({0}, acc);
        break;
      case 5:
        execute_order_list({0}, acc);
        break;
      case 8:
        execute_order_list({0}, acc);
        break;
      case 11:
        execute_order_list({0}, acc);
        break;
      case 14:
        execute_order_list({0}, acc);
        break;
      case 17:
        execute_order_list({0}, acc);
        break;
      case 20:
        execute_order_list({0}, acc);
        break;
      case 23:
        execute_order_list({0}, acc);
        break;
      case 26:
        execute_order_list({0}, acc);
        break;
      case 28:
        execute_order_list({0}, acc);
        break;
      case 29:
        execute_order_list({0, 3, 7}, acc);
        break;
      case 30:
        execute_order_list({6, 11}, acc);
        break;
      case 31:
        execute_order_list({5}, acc);
        break;
      case 32:
        execute_order_list({5}, acc);
        break;
      case 33:
        execute_order_list({0, 3, 7}, acc);
        break;
      case 35:
        execute_order_list({4, 7, 8}, acc);
        break;
      case 36:
        execute_order_list({0, 11}, acc);
        break;
      case 37:
        execute_order_list({5}, acc);
        break;
      case 38:
        execute_order_list({6, 7, 8}, acc);
        break;
      case 39:
        execute_order_list({5, 11}, acc);
        break;
      case 40:
        execute_order_list({3, 7}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void execute_05d4a8_gamma_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 1:
        execute_order_list({12, 13}, acc);
        break;
      case 31:
        execute_order_list({12}, acc);
        break;
      case 35:
        execute_order_list({12}, acc);
        break;
      case 37:
        execute_order_list({12}, acc);
        break;
      case 40:
        execute_order_list({12, 13}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void execute_05d4a8_post_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 0:
        execute_order_list({15, 29}, acc);
        break;
      case 1:
        execute_order_list({25, 27}, acc);
        break;
      case 2:
        execute_order_list({17, 27}, acc);
        break;
      case 3:
        execute_order_list({15}, acc);
        break;
      case 5:
        execute_order_list({17, 27}, acc);
        break;
      case 6:
        execute_order_list({15}, acc);
        break;
      case 8:
        execute_order_list({17, 27}, acc);
        break;
      case 9:
        execute_order_list({15}, acc);
        break;
      case 11:
        execute_order_list({17, 27}, acc);
        break;
      case 12:
        execute_order_list({15}, acc);
        break;
      case 14:
        execute_order_list({17, 27}, acc);
        break;
      case 15:
        execute_order_list({15}, acc);
        break;
      case 17:
        execute_order_list({17, 27}, acc);
        break;
      case 18:
        execute_order_list({15}, acc);
        break;
      case 20:
        execute_order_list({17, 27}, acc);
        break;
      case 21:
        execute_order_list({15}, acc);
        break;
      case 23:
        execute_order_list({17, 27}, acc);
        break;
      case 25:
        execute_order_list({22}, acc);
        break;
      case 26:
        execute_order_list({17, 27}, acc);
        break;
      case 28:
        execute_order_list({17, 27}, acc);
        break;
      case 29:
        execute_order_list({25, 27}, acc);
        break;
      case 30:
        execute_order_list({27}, acc);
        break;
      case 31:
        execute_order_list({27}, acc);
        break;
      case 32:
        execute_order_list({26, 29}, acc);
        break;
      case 33:
        execute_order_list({25, 27}, acc);
        break;
      case 35:
        execute_order_list({16}, acc);
        break;
      case 36:
        execute_order_list({27}, acc);
        break;
      case 37:
        execute_order_list({22, 27}, acc);
        break;
      case 38:
        execute_order_list({27}, acc);
        break;
      case 39:
        execute_order_list({25, 27}, acc);
        break;
      case 40:
        execute_order_list({25}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void tick_ik1303_05d4a8_decoded() {
    static constexpr uint32_t kCommand = 0x05d4a8;
    if (tick_index == 0) command = kCommand;

    const int current_tick = tick_index;
    const bool branch_l = l != 0;
    if (current_tick < 27) {
      sync_address = 40;
    } else if (current_tick < 36) {
      sync_address = 41;
    } else {
      sync_address = 23;
    }

    Accumulator acc;
    execute_05d4a8_pre_add_orders(current_tick, branch_l, acc);

    if (key_y == 0) t = 0;

    execute_05d4a8_gamma_orders(current_tick, branch_l, acc);

    int sum = acc.alpha + acc.beta + acc.gamma;
    acc.sigma = sum & 0xF;
    carry = (sum >> 4) & 1;

    execute_05d4a8_post_add_orders(current_tick, branch_l, acc);

    output = memory[current_tick];
    memory[current_tick] = input;
    ++tick_index;
    if (tick_index == 42) tick_index = 0;
  }


  void execute_0fe9cb_pre_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 0:
        execute_order_list({10}, acc);
        break;
      case 2:
        execute_order_list({8}, acc);
        break;
      case 3:
        execute_order_list({5, 10, 11}, acc);
        break;
      case 4:
        execute_order_list({4, 7}, acc);
        break;
      case 5:
        execute_order_list({5}, acc);
        break;
      case 6:
        execute_order_list({5, 10, 11}, acc);
        break;
      case 7:
        execute_order_list({4, 7}, acc);
        break;
      case 8:
        execute_order_list({5}, acc);
        break;
      case 9:
        execute_order_list({5, 10, 11}, acc);
        break;
      case 10:
        execute_order_list({4, 7}, acc);
        break;
      case 11:
        execute_order_list({5}, acc);
        break;
      case 12:
        execute_order_list({5, 10, 11}, acc);
        break;
      case 13:
        execute_order_list({4, 7}, acc);
        break;
      case 14:
        execute_order_list({5}, acc);
        break;
      case 15:
        execute_order_list({5, 10, 11}, acc);
        break;
      case 16:
        execute_order_list({4, 7}, acc);
        break;
      case 17:
        execute_order_list({5}, acc);
        break;
      case 18:
        execute_order_list({5, 10, 11}, acc);
        break;
      case 19:
        execute_order_list({4, 7}, acc);
        break;
      case 20:
        execute_order_list({5}, acc);
        break;
      case 21:
        execute_order_list({5, 10, 11}, acc);
        break;
      case 22:
        execute_order_list({4, 7}, acc);
        break;
      case 23:
        execute_order_list({5}, acc);
        break;
      case 24:
        execute_order_list({7, 8}, acc);
        break;
      case 25:
        execute_order_list({5}, acc);
        break;
      case 26:
        execute_order_list({0}, acc);
        break;
      case 31:
        execute_order_list({0}, acc);
        break;
      case 33:
        execute_order_list({5}, acc);
        break;
      case 36:
        execute_order_list({0, 11}, acc);
        break;
      case 37:
        if (branch_l) {
          execute_order_list({0}, acc);
        } else {
          execute_order_list({5}, acc);
        }
        break;
      case 38:
        execute_order_list({5, 11}, acc);
        break;
      case 39:
        execute_order_list({0}, acc);
        break;
      case 40:
        if (branch_l) {
          execute_order_list({0}, acc);
        } else {
          execute_order_list({5}, acc);
        }
        break;
      case 41:
        execute_order_list({0}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void execute_0fe9cb_gamma_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 3:
        execute_order_list({12, 13}, acc);
        break;
      case 6:
        execute_order_list({12, 13}, acc);
        break;
      case 9:
        execute_order_list({12, 13}, acc);
        break;
      case 12:
        execute_order_list({12, 13}, acc);
        break;
      case 15:
        execute_order_list({12, 13}, acc);
        break;
      case 18:
        execute_order_list({12, 13}, acc);
        break;
      case 21:
        execute_order_list({12, 13}, acc);
        break;
      case 24:
        execute_order_list({12, 13}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void execute_0fe9cb_post_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 0:
        execute_order_list({27}, acc);
        break;
      case 1:
        execute_order_list({22}, acc);
        break;
      case 2:
        execute_order_list({27}, acc);
        break;
      case 3:
        execute_order_list({25, 27}, acc);
        break;
      case 4:
        execute_order_list({22, 26, 29}, acc);
        break;
      case 5:
        execute_order_list({26, 29}, acc);
        break;
      case 6:
        execute_order_list({25, 27}, acc);
        break;
      case 7:
        execute_order_list({22, 26, 29}, acc);
        break;
      case 8:
        execute_order_list({26, 29}, acc);
        break;
      case 9:
        execute_order_list({25, 27}, acc);
        break;
      case 10:
        execute_order_list({22, 26, 29}, acc);
        break;
      case 11:
        execute_order_list({26, 29}, acc);
        break;
      case 12:
        execute_order_list({25, 27}, acc);
        break;
      case 13:
        execute_order_list({22, 26, 29}, acc);
        break;
      case 14:
        execute_order_list({26, 29}, acc);
        break;
      case 15:
        execute_order_list({25, 27}, acc);
        break;
      case 16:
        execute_order_list({22, 26, 29}, acc);
        break;
      case 17:
        execute_order_list({26, 29}, acc);
        break;
      case 18:
        execute_order_list({25, 27}, acc);
        break;
      case 19:
        execute_order_list({22, 26, 29}, acc);
        break;
      case 20:
        execute_order_list({26, 29}, acc);
        break;
      case 21:
        execute_order_list({25, 27}, acc);
        break;
      case 22:
        execute_order_list({22, 26, 29}, acc);
        break;
      case 23:
        execute_order_list({26, 29}, acc);
        break;
      case 24:
        execute_order_list({25, 27}, acc);
        break;
      case 25:
        execute_order_list({22}, acc);
        break;
      case 26:
        execute_order_list({17, 26, 29}, acc);
        break;
      case 27:
        execute_order_list({15}, acc);
        break;
      case 28:
        execute_order_list({15}, acc);
        break;
      case 30:
        execute_order_list({15}, acc);
        break;
      case 31:
        execute_order_list({15, 29}, acc);
        break;
      case 33:
        execute_order_list({26, 29}, acc);
        break;
      case 34:
        execute_order_list({22}, acc);
        break;
      case 35:
        execute_order_list({22}, acc);
        break;
      case 36:
        execute_order_list({27}, acc);
        break;
      case 37:
        if (branch_l) {
          execute_order_list({17, 22, 27}, acc);
        } else {
          execute_order_list({22}, acc);
        }
        break;
      case 38:
        execute_order_list({23}, acc);
        break;
      case 39:
        execute_order_list({27}, acc);
        break;
      case 40:
        if (branch_l) {
          execute_order_list({17, 22, 27}, acc);
        } else {
          execute_order_list({22}, acc);
        }
        break;
      case 41:
        execute_order_list({27}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void tick_ik1303_0fe9cb_decoded() {
    static constexpr uint32_t kCommand = 0x0fe9cb;
    if (tick_index == 0) command = kCommand;

    const int current_tick = tick_index;
    const bool branch_l = l != 0;
    if (current_tick < 27) {
      sync_address = 75;
    } else if (current_tick < 36) {
      sync_address = 83;
    } else {
      if (current_tick == 36) {
        r[37] = 15 & 0xF;
        r[40] = 63 >> 4;
      }
      sync_address = 95;
    }

    Accumulator acc;
    execute_0fe9cb_pre_add_orders(current_tick, branch_l, acc);

    if (key_y == 0) t = 0;

    execute_0fe9cb_gamma_orders(current_tick, branch_l, acc);

    int sum = acc.alpha + acc.beta + acc.gamma;
    acc.sigma = sum & 0xF;
    carry = (sum >> 4) & 1;

    execute_0fe9cb_post_add_orders(current_tick, branch_l, acc);

    output = memory[current_tick];
    memory[current_tick] = input;
    ++tick_index;
    if (tick_index == 42) tick_index = 0;
  }


  void execute_1e4285_pre_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 0:
        execute_order_list({7, 8}, acc);
        break;
      case 1:
        execute_order_list({0}, acc);
        break;
      case 4:
        execute_order_list({0}, acc);
        break;
      case 5:
        execute_order_list({0}, acc);
        break;
      case 7:
        execute_order_list({0}, acc);
        break;
      case 8:
        execute_order_list({0}, acc);
        break;
      case 10:
        execute_order_list({0}, acc);
        break;
      case 11:
        execute_order_list({0}, acc);
        break;
      case 13:
        execute_order_list({0}, acc);
        break;
      case 14:
        execute_order_list({0}, acc);
        break;
      case 16:
        execute_order_list({0}, acc);
        break;
      case 17:
        execute_order_list({0}, acc);
        break;
      case 19:
        execute_order_list({0}, acc);
        break;
      case 20:
        execute_order_list({0}, acc);
        break;
      case 22:
        execute_order_list({0}, acc);
        break;
      case 23:
        execute_order_list({0}, acc);
        break;
      case 25:
        execute_order_list({0}, acc);
        break;
      case 26:
        execute_order_list({5, 8}, acc);
        break;
      case 27:
        execute_order_list({7, 8}, acc);
        break;
      case 28:
        execute_order_list({0}, acc);
        break;
      case 31:
        execute_order_list({0}, acc);
        break;
      case 32:
        execute_order_list({0}, acc);
        break;
      case 34:
        execute_order_list({0}, acc);
        break;
      case 35:
        execute_order_list({5, 8}, acc);
        break;
      case 36:
        execute_order_list({0, 11}, acc);
        break;
      case 37:
        if (branch_l) {
          execute_order_list({0}, acc);
        } else {
          execute_order_list({5}, acc);
        }
        break;
      case 38:
        execute_order_list({5, 11}, acc);
        break;
      case 39:
        execute_order_list({0}, acc);
        break;
      case 40:
        if (branch_l) {
          execute_order_list({0}, acc);
        } else {
          execute_order_list({5}, acc);
        }
        break;
      case 41:
        execute_order_list({0}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void execute_1e4285_gamma_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 0:
        execute_order_list({12, 13}, acc);
        break;
      case 27:
        execute_order_list({12, 13}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void execute_1e4285_post_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 0:
        execute_order_list({25, 27}, acc);
        break;
      case 1:
        execute_order_list({22}, acc);
        break;
      case 4:
        execute_order_list({22}, acc);
        break;
      case 5:
        execute_order_list({27}, acc);
        break;
      case 7:
        execute_order_list({22}, acc);
        break;
      case 8:
        execute_order_list({27}, acc);
        break;
      case 10:
        execute_order_list({22}, acc);
        break;
      case 11:
        execute_order_list({27}, acc);
        break;
      case 13:
        execute_order_list({22}, acc);
        break;
      case 14:
        execute_order_list({27}, acc);
        break;
      case 16:
        execute_order_list({22}, acc);
        break;
      case 17:
        execute_order_list({27}, acc);
        break;
      case 19:
        execute_order_list({22}, acc);
        break;
      case 20:
        execute_order_list({27}, acc);
        break;
      case 22:
        execute_order_list({22}, acc);
        break;
      case 23:
        execute_order_list({27}, acc);
        break;
      case 24:
        execute_order_list({29}, acc);
        break;
      case 25:
        execute_order_list({22}, acc);
        break;
      case 26:
        execute_order_list({27}, acc);
        break;
      case 27:
        execute_order_list({25, 27}, acc);
        break;
      case 28:
        execute_order_list({22}, acc);
        break;
      case 31:
        execute_order_list({22}, acc);
        break;
      case 32:
        execute_order_list({27}, acc);
        break;
      case 33:
        execute_order_list({29}, acc);
        break;
      case 34:
        execute_order_list({22}, acc);
        break;
      case 35:
        execute_order_list({27}, acc);
        break;
      case 36:
        execute_order_list({27}, acc);
        break;
      case 37:
        if (branch_l) {
          execute_order_list({17, 22, 27}, acc);
        } else {
          execute_order_list({22}, acc);
        }
        break;
      case 38:
        execute_order_list({23}, acc);
        break;
      case 39:
        execute_order_list({27}, acc);
        break;
      case 40:
        if (branch_l) {
          execute_order_list({17, 22, 27}, acc);
        } else {
          execute_order_list({22}, acc);
        }
        break;
      case 41:
        execute_order_list({27}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void tick_ik1303_1e4285_decoded() {
    static constexpr uint32_t kCommand = 0x1e4285;
    if (tick_index == 0) command = kCommand;

    const int current_tick = tick_index;
    const bool branch_l = l != 0;
    if (current_tick < 27) {
      sync_address = 5;
    } else if (current_tick < 36) {
      sync_address = 5;
    } else {
      if (current_tick == 36) {
        r[37] = 9 & 0xF;
        r[40] = 121 >> 4;
      }
      sync_address = 95;
    }

    Accumulator acc;
    execute_1e4285_pre_add_orders(current_tick, branch_l, acc);

    if (key_y == 0) t = 0;

    execute_1e4285_gamma_orders(current_tick, branch_l, acc);

    int sum = acc.alpha + acc.beta + acc.gamma;
    acc.sigma = sum & 0xF;
    carry = (sum >> 4) & 1;

    execute_1e4285_post_add_orders(current_tick, branch_l, acc);

    output = memory[current_tick];
    memory[current_tick] = input;
    ++tick_index;
    if (tick_index == 42) tick_index = 0;
  }


  void execute_041c2f_pre_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 0:
        execute_order_list({3}, acc);
        break;
      case 1:
        execute_order_list({0, 7}, acc);
        break;
      case 2:
        execute_order_list({4, 7}, acc);
        break;
      case 3:
        execute_order_list({3}, acc);
        break;
      case 4:
        execute_order_list({0, 7}, acc);
        break;
      case 5:
        execute_order_list({4, 7}, acc);
        break;
      case 6:
        execute_order_list({3}, acc);
        break;
      case 7:
        execute_order_list({0, 7}, acc);
        break;
      case 8:
        execute_order_list({4, 7}, acc);
        break;
      case 9:
        execute_order_list({3}, acc);
        break;
      case 10:
        execute_order_list({0, 7}, acc);
        break;
      case 11:
        execute_order_list({4, 7}, acc);
        break;
      case 12:
        execute_order_list({3}, acc);
        break;
      case 13:
        execute_order_list({0, 7}, acc);
        break;
      case 14:
        execute_order_list({4, 7}, acc);
        break;
      case 15:
        execute_order_list({3}, acc);
        break;
      case 16:
        execute_order_list({0, 7}, acc);
        break;
      case 17:
        execute_order_list({4, 7}, acc);
        break;
      case 18:
        execute_order_list({3}, acc);
        break;
      case 19:
        execute_order_list({0, 7}, acc);
        break;
      case 20:
        execute_order_list({4, 7}, acc);
        break;
      case 21:
        execute_order_list({3}, acc);
        break;
      case 22:
        execute_order_list({0, 7}, acc);
        break;
      case 23:
        execute_order_list({4, 7}, acc);
        break;
      case 24:
        execute_order_list({3}, acc);
        break;
      case 25:
        execute_order_list({0, 7}, acc);
        break;
      case 26:
        execute_order_list({4, 7}, acc);
        break;
      case 27:
        execute_order_list({0}, acc);
        break;
      case 28:
        execute_order_list({0, 3, 9}, acc);
        break;
      case 29:
        execute_order_list({5, 11}, acc);
        break;
      case 30:
        if (!branch_l) {
          execute_order_list({7, 8}, acc);
        }
        break;
      case 31:
        execute_order_list({7, 9}, acc);
        break;
      case 32:
        execute_order_list({5, 8}, acc);
        break;
      case 33:
        execute_order_list({5, 9}, acc);
        break;
      case 34:
        execute_order_list({5, 10, 11}, acc);
        break;
      case 35:
        execute_order_list({0}, acc);
        break;
      case 36:
        execute_order_list({8}, acc);
        break;
      case 37:
        execute_order_list({6, 7, 8}, acc);
        break;
      case 38:
        execute_order_list({7, 9}, acc);
        break;
      case 39:
        execute_order_list({5, 9}, acc);
        break;
      case 40:
        execute_order_list({5}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void execute_041c2f_gamma_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 1:
        execute_order_list({12, 13}, acc);
        break;
      case 4:
        execute_order_list({12}, acc);
        break;
      case 7:
        execute_order_list({12}, acc);
        break;
      case 10:
        execute_order_list({12}, acc);
        break;
      case 13:
        execute_order_list({12}, acc);
        break;
      case 16:
        execute_order_list({12}, acc);
        break;
      case 19:
        execute_order_list({12}, acc);
        break;
      case 22:
        execute_order_list({12}, acc);
        break;
      case 25:
        execute_order_list({12}, acc);
        break;
      case 31:
        execute_order_list({12}, acc);
        break;
      case 34:
        execute_order_list({12, 13}, acc);
        break;
      case 36:
        execute_order_list({12}, acc);
        break;
      case 38:
        execute_order_list({12}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void execute_041c2f_post_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 0:
        execute_order_list({27}, acc);
        break;
      case 1:
        execute_order_list({25, 27}, acc);
        break;
      case 2:
        execute_order_list({22, 26, 29}, acc);
        break;
      case 3:
        execute_order_list({27}, acc);
        break;
      case 4:
        execute_order_list({25, 27}, acc);
        break;
      case 5:
        execute_order_list({22, 26, 29}, acc);
        break;
      case 6:
        execute_order_list({27}, acc);
        break;
      case 7:
        execute_order_list({25, 27}, acc);
        break;
      case 8:
        execute_order_list({22, 26, 29}, acc);
        break;
      case 9:
        execute_order_list({27}, acc);
        break;
      case 10:
        execute_order_list({25, 27}, acc);
        break;
      case 11:
        execute_order_list({22, 26, 29}, acc);
        break;
      case 12:
        execute_order_list({27}, acc);
        break;
      case 13:
        execute_order_list({25, 27}, acc);
        break;
      case 14:
        execute_order_list({22, 26, 29}, acc);
        break;
      case 15:
        execute_order_list({27}, acc);
        break;
      case 16:
        execute_order_list({25, 27}, acc);
        break;
      case 17:
        execute_order_list({22, 26, 29}, acc);
        break;
      case 18:
        execute_order_list({27}, acc);
        break;
      case 19:
        execute_order_list({25, 27}, acc);
        break;
      case 20:
        execute_order_list({22, 26, 29}, acc);
        break;
      case 21:
        execute_order_list({27}, acc);
        break;
      case 22:
        execute_order_list({25, 27}, acc);
        break;
      case 23:
        execute_order_list({22, 26, 29}, acc);
        break;
      case 24:
        execute_order_list({27}, acc);
        break;
      case 25:
        execute_order_list({25, 27}, acc);
        break;
      case 26:
        execute_order_list({22, 26, 29}, acc);
        break;
      case 27:
        execute_order_list({27}, acc);
        break;
      case 28:
        execute_order_list({25, 29}, acc);
        break;
      case 29:
        execute_order_list({23}, acc);
        break;
      case 30:
        execute_order_list({29}, acc);
        break;
      case 31:
        execute_order_list({29}, acc);
        break;
      case 32:
        execute_order_list({27}, acc);
        break;
      case 33:
        execute_order_list({25, 27, 29}, acc);
        break;
      case 34:
        execute_order_list({25, 27}, acc);
        break;
      case 35:
        execute_order_list({27}, acc);
        break;
      case 36:
        execute_order_list({25}, acc);
        break;
      case 37:
        execute_order_list({27}, acc);
        break;
      case 38:
        execute_order_list({29}, acc);
        break;
      case 39:
        execute_order_list({25, 27, 29}, acc);
        break;
      case 40:
        execute_order_list({22}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void tick_ik1303_041c2f_decoded() {
    static constexpr uint32_t kCommand = 0x041c2f;
    if (tick_index == 0) command = kCommand;

    const int current_tick = tick_index;
    const bool branch_l = l != 0;
    if (current_tick < 27) {
      sync_address = 47;
    } else if (current_tick < 36) {
      sync_address = 56;
    } else {
      sync_address = 16;
    }

    Accumulator acc;
    execute_041c2f_pre_add_orders(current_tick, branch_l, acc);

    if (key_y == 0) t = 0;

    execute_041c2f_gamma_orders(current_tick, branch_l, acc);

    int sum = acc.alpha + acc.beta + acc.gamma;
    acc.sigma = sum & 0xF;
    carry = (sum >> 4) & 1;

    execute_041c2f_post_add_orders(current_tick, branch_l, acc);

    output = memory[current_tick];
    memory[current_tick] = input;
    ++tick_index;
    if (tick_index == 42) tick_index = 0;
  }


  void execute_058285_pre_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 0:
        execute_order_list({7, 8}, acc);
        break;
      case 1:
        execute_order_list({0}, acc);
        break;
      case 4:
        execute_order_list({0}, acc);
        break;
      case 5:
        execute_order_list({0}, acc);
        break;
      case 7:
        execute_order_list({0}, acc);
        break;
      case 8:
        execute_order_list({0}, acc);
        break;
      case 10:
        execute_order_list({0}, acc);
        break;
      case 11:
        execute_order_list({0}, acc);
        break;
      case 13:
        execute_order_list({0}, acc);
        break;
      case 14:
        execute_order_list({0}, acc);
        break;
      case 16:
        execute_order_list({0}, acc);
        break;
      case 17:
        execute_order_list({0}, acc);
        break;
      case 19:
        execute_order_list({0}, acc);
        break;
      case 20:
        execute_order_list({0}, acc);
        break;
      case 22:
        execute_order_list({0}, acc);
        break;
      case 23:
        execute_order_list({0}, acc);
        break;
      case 25:
        execute_order_list({0}, acc);
        break;
      case 26:
        execute_order_list({5, 8}, acc);
        break;
      case 27:
        execute_order_list({7, 8}, acc);
        break;
      case 28:
        execute_order_list({0}, acc);
        break;
      case 31:
        execute_order_list({0}, acc);
        break;
      case 32:
        execute_order_list({0}, acc);
        break;
      case 34:
        execute_order_list({0}, acc);
        break;
      case 35:
        execute_order_list({5, 8}, acc);
        break;
      case 36:
        execute_order_list({0, 11}, acc);
        break;
      case 37:
        execute_order_list({6, 7, 8}, acc);
        break;
      case 38:
        execute_order_list({8}, acc);
        break;
      case 39:
        execute_order_list({0}, acc);
        break;
      case 40:
        execute_order_list({0, 3, 7}, acc);
        break;
      case 41:
        execute_order_list({5}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void execute_058285_gamma_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 0:
        execute_order_list({12, 13}, acc);
        break;
      case 27:
        execute_order_list({12, 13}, acc);
        break;
      case 38:
        execute_order_list({12, 13}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void execute_058285_post_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 0:
        execute_order_list({25, 27}, acc);
        break;
      case 1:
        execute_order_list({22}, acc);
        break;
      case 4:
        execute_order_list({22}, acc);
        break;
      case 5:
        execute_order_list({27}, acc);
        break;
      case 7:
        execute_order_list({22}, acc);
        break;
      case 8:
        execute_order_list({27}, acc);
        break;
      case 10:
        execute_order_list({22}, acc);
        break;
      case 11:
        execute_order_list({27}, acc);
        break;
      case 13:
        execute_order_list({22}, acc);
        break;
      case 14:
        execute_order_list({27}, acc);
        break;
      case 16:
        execute_order_list({22}, acc);
        break;
      case 17:
        execute_order_list({27}, acc);
        break;
      case 19:
        execute_order_list({22}, acc);
        break;
      case 20:
        execute_order_list({27}, acc);
        break;
      case 22:
        execute_order_list({22}, acc);
        break;
      case 23:
        execute_order_list({27}, acc);
        break;
      case 24:
        execute_order_list({29}, acc);
        break;
      case 25:
        execute_order_list({22}, acc);
        break;
      case 26:
        execute_order_list({27}, acc);
        break;
      case 27:
        execute_order_list({25, 27}, acc);
        break;
      case 28:
        execute_order_list({22}, acc);
        break;
      case 31:
        execute_order_list({22}, acc);
        break;
      case 32:
        execute_order_list({27}, acc);
        break;
      case 33:
        execute_order_list({29}, acc);
        break;
      case 34:
        execute_order_list({22}, acc);
        break;
      case 35:
        execute_order_list({27}, acc);
        break;
      case 36:
        execute_order_list({17, 32}, acc);
        break;
      case 37:
        execute_order_list({27}, acc);
        break;
      case 38:
        execute_order_list({23}, acc);
        break;
      case 39:
        execute_order_list({17, 32}, acc);
        break;
      case 40:
        execute_order_list({25, 27}, acc);
        break;
      case 41:
        execute_order_list({23}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void tick_ik1303_058285_decoded() {
    static constexpr uint32_t kCommand = 0x058285;
    if (tick_index == 0) command = kCommand;

    const int current_tick = tick_index;
    const bool branch_l = l != 0;
    if (current_tick < 27) {
      sync_address = 5;
    } else if (current_tick < 36) {
      sync_address = 5;
    } else {
      sync_address = 22;
    }

    Accumulator acc;
    execute_058285_pre_add_orders(current_tick, branch_l, acc);

    if (key_y == 0) t = 0;

    execute_058285_gamma_orders(current_tick, branch_l, acc);

    int sum = acc.alpha + acc.beta + acc.gamma;
    acc.sigma = sum & 0xF;
    carry = (sum >> 4) & 1;

    execute_058285_post_add_orders(current_tick, branch_l, acc);

    output = memory[current_tick];
    memory[current_tick] = input;
    ++tick_index;
    if (tick_index == 42) tick_index = 0;
  }


  void execute_7b5d50_pre_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 27:
        execute_order_list({8}, acc);
        break;
      case 28:
        execute_order_list({5, 7}, acc);
        break;
      case 29:
        execute_order_list({5, 10}, acc);
        break;
      case 30:
        execute_order_list({5, 8}, acc);
        break;
      case 31:
        execute_order_list({5}, acc);
        break;
      case 32:
        execute_order_list({5, 11}, acc);
        break;
      case 33:
        if (branch_l) {
          execute_order_list({0}, acc);
        } else {
          execute_order_list({5}, acc);
        }
        break;
      case 34:
        execute_order_list({0}, acc);
        break;
      case 36:
        execute_order_list({0, 11}, acc);
        break;
      case 37:
        if (branch_l) {
          execute_order_list({0}, acc);
        } else {
          execute_order_list({5}, acc);
        }
        break;
      case 38:
        execute_order_list({5, 11}, acc);
        break;
      case 39:
        execute_order_list({0}, acc);
        break;
      case 40:
        if (branch_l) {
          execute_order_list({0}, acc);
        } else {
          execute_order_list({5}, acc);
        }
        break;
      case 41:
        execute_order_list({0}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void execute_7b5d50_gamma_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 27:
        execute_order_list({12, 13}, acc);
        break;
      case 28:
        execute_order_list({12}, acc);
        break;
      case 29:
        execute_order_list({12, 13}, acc);
        break;
      case 31:
        execute_order_list({12}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void execute_7b5d50_post_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 27:
        execute_order_list({25}, acc);
        break;
      case 28:
        execute_order_list({25, 27}, acc);
        break;
      case 29:
        execute_order_list({25}, acc);
        break;
      case 30:
        execute_order_list({27}, acc);
        break;
      case 31:
        execute_order_list({27}, acc);
        break;
      case 32:
        execute_order_list({25, 27}, acc);
        break;
      case 33:
        if (branch_l) {
          execute_order_list({27}, acc);
        }
        break;
      case 35:
        execute_order_list({29}, acc);
        break;
      case 36:
        execute_order_list({27}, acc);
        break;
      case 37:
        if (branch_l) {
          execute_order_list({17, 22, 27}, acc);
        } else {
          execute_order_list({22}, acc);
        }
        break;
      case 38:
        execute_order_list({23}, acc);
        break;
      case 39:
        execute_order_list({27}, acc);
        break;
      case 40:
        if (branch_l) {
          execute_order_list({17, 22, 27}, acc);
        } else {
          execute_order_list({22}, acc);
        }
        break;
      case 41:
        execute_order_list({27}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void tick_ik1303_7b5d50_decoded() {
    static constexpr uint32_t kCommand = 0x7b5d50;
    if (tick_index == 0) command = kCommand;

    const int current_tick = tick_index;
    const bool branch_l = l != 0;
    if (current_tick < 27) {
      sync_address = 80;
    } else if (current_tick < 36) {
      sync_address = 58;
    } else {
      if (current_tick == 36) {
        r[37] = 13 & 0xF;
        r[40] = 237 >> 4;
      }
      sync_address = 95;
    }

    Accumulator acc;
    execute_7b5d50_pre_add_orders(current_tick, branch_l, acc);

    if (key_y == 0) t = 0;

    execute_7b5d50_gamma_orders(current_tick, branch_l, acc);

    int sum = acc.alpha + acc.beta + acc.gamma;
    acc.sigma = sum & 0xF;
    carry = (sum >> 4) & 1;

    execute_7b5d50_post_add_orders(current_tick, branch_l, acc);

    output = memory[current_tick];
    memory[current_tick] = input;
    ++tick_index;
    if (tick_index == 42) tick_index = 0;
  }


  void execute_010285_pre_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 0:
        execute_order_list({7, 8}, acc);
        break;
      case 1:
        execute_order_list({0}, acc);
        break;
      case 4:
        execute_order_list({0}, acc);
        break;
      case 5:
        execute_order_list({0}, acc);
        break;
      case 7:
        execute_order_list({0}, acc);
        break;
      case 8:
        execute_order_list({0}, acc);
        break;
      case 10:
        execute_order_list({0}, acc);
        break;
      case 11:
        execute_order_list({0}, acc);
        break;
      case 13:
        execute_order_list({0}, acc);
        break;
      case 14:
        execute_order_list({0}, acc);
        break;
      case 16:
        execute_order_list({0}, acc);
        break;
      case 17:
        execute_order_list({0}, acc);
        break;
      case 19:
        execute_order_list({0}, acc);
        break;
      case 20:
        execute_order_list({0}, acc);
        break;
      case 22:
        execute_order_list({0}, acc);
        break;
      case 23:
        execute_order_list({0}, acc);
        break;
      case 25:
        execute_order_list({0}, acc);
        break;
      case 26:
        execute_order_list({5, 8}, acc);
        break;
      case 27:
        execute_order_list({7, 8}, acc);
        break;
      case 28:
        execute_order_list({0}, acc);
        break;
      case 31:
        execute_order_list({0}, acc);
        break;
      case 32:
        execute_order_list({0}, acc);
        break;
      case 34:
        execute_order_list({0}, acc);
        break;
      case 35:
        execute_order_list({5, 8}, acc);
        break;
      case 36:
        execute_order_list({2}, acc);
        break;
      case 37:
        execute_order_list({5}, acc);
        break;
      case 38:
        execute_order_list({8}, acc);
        break;
      case 39:
        execute_order_list({2}, acc);
        break;
      case 40:
        execute_order_list({5}, acc);
        break;
      case 41:
        execute_order_list({0}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void execute_010285_gamma_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 0:
        execute_order_list({12, 13}, acc);
        break;
      case 27:
        execute_order_list({12, 13}, acc);
        break;
      case 38:
        execute_order_list({12, 13}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void execute_010285_post_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 0:
        execute_order_list({25, 27}, acc);
        break;
      case 1:
        execute_order_list({22}, acc);
        break;
      case 4:
        execute_order_list({22}, acc);
        break;
      case 5:
        execute_order_list({27}, acc);
        break;
      case 7:
        execute_order_list({22}, acc);
        break;
      case 8:
        execute_order_list({27}, acc);
        break;
      case 10:
        execute_order_list({22}, acc);
        break;
      case 11:
        execute_order_list({27}, acc);
        break;
      case 13:
        execute_order_list({22}, acc);
        break;
      case 14:
        execute_order_list({27}, acc);
        break;
      case 16:
        execute_order_list({22}, acc);
        break;
      case 17:
        execute_order_list({27}, acc);
        break;
      case 19:
        execute_order_list({22}, acc);
        break;
      case 20:
        execute_order_list({27}, acc);
        break;
      case 22:
        execute_order_list({22}, acc);
        break;
      case 23:
        execute_order_list({27}, acc);
        break;
      case 24:
        execute_order_list({29}, acc);
        break;
      case 25:
        execute_order_list({22}, acc);
        break;
      case 26:
        execute_order_list({27}, acc);
        break;
      case 27:
        execute_order_list({25, 27}, acc);
        break;
      case 28:
        execute_order_list({22}, acc);
        break;
      case 31:
        execute_order_list({22}, acc);
        break;
      case 32:
        execute_order_list({27}, acc);
        break;
      case 33:
        execute_order_list({29}, acc);
        break;
      case 34:
        execute_order_list({22}, acc);
        break;
      case 35:
        execute_order_list({27}, acc);
        break;
      case 36:
        execute_order_list({27, 33}, acc);
        break;
      case 37:
        execute_order_list({22}, acc);
        break;
      case 38:
        execute_order_list({25}, acc);
        break;
      case 39:
        execute_order_list({27, 33}, acc);
        break;
      case 40:
        execute_order_list({22}, acc);
        break;
      case 41:
        execute_order_list({27}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void tick_ik1303_010285_decoded() {
    static constexpr uint32_t kCommand = 0x010285;
    if (tick_index == 0) command = kCommand;

    const int current_tick = tick_index;
    const bool branch_l = l != 0;
    if (current_tick < 27) {
      sync_address = 5;
    } else if (current_tick < 36) {
      sync_address = 5;
    } else {
      sync_address = 4;
    }

    Accumulator acc;
    execute_010285_pre_add_orders(current_tick, branch_l, acc);

    if (key_y == 0) t = 0;

    execute_010285_gamma_orders(current_tick, branch_l, acc);

    int sum = acc.alpha + acc.beta + acc.gamma;
    acc.sigma = sum & 0xF;
    carry = (sum >> 4) & 1;

    execute_010285_post_add_orders(current_tick, branch_l, acc);

    output = memory[current_tick];
    memory[current_tick] = input;
    ++tick_index;
    if (tick_index == 42) tick_index = 0;
  }


  void execute_0212a5_pre_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 0:
        execute_order_list({6, 11}, acc);
        break;
      case 3:
        execute_order_list({9}, acc);
        break;
      case 4:
        execute_order_list({0, 3, 7}, acc);
        break;
      case 5:
        execute_order_list({9}, acc);
        break;
      case 6:
        execute_order_list({9}, acc);
        break;
      case 7:
        execute_order_list({0, 3, 7}, acc);
        break;
      case 8:
        execute_order_list({9}, acc);
        break;
      case 9:
        execute_order_list({9}, acc);
        break;
      case 10:
        execute_order_list({0, 3, 7}, acc);
        break;
      case 11:
        execute_order_list({9}, acc);
        break;
      case 12:
        execute_order_list({9}, acc);
        break;
      case 13:
        execute_order_list({0, 3, 7}, acc);
        break;
      case 14:
        execute_order_list({9}, acc);
        break;
      case 15:
        execute_order_list({9}, acc);
        break;
      case 16:
        execute_order_list({0, 3, 7}, acc);
        break;
      case 17:
        execute_order_list({9}, acc);
        break;
      case 18:
        execute_order_list({9}, acc);
        break;
      case 19:
        execute_order_list({0, 3, 7}, acc);
        break;
      case 20:
        execute_order_list({9}, acc);
        break;
      case 21:
        execute_order_list({9}, acc);
        break;
      case 22:
        execute_order_list({0, 3, 7}, acc);
        break;
      case 23:
        execute_order_list({9}, acc);
        break;
      case 24:
        execute_order_list({0, 3, 9}, acc);
        break;
      case 26:
        execute_order_list({5, 8}, acc);
        break;
      case 27:
        execute_order_list({6, 11}, acc);
        break;
      case 30:
        execute_order_list({9}, acc);
        break;
      case 31:
        execute_order_list({0, 3, 7}, acc);
        break;
      case 32:
        execute_order_list({9}, acc);
        break;
      case 33:
        execute_order_list({0, 3, 9}, acc);
        break;
      case 35:
        execute_order_list({5, 8}, acc);
        break;
      case 36:
        execute_order_list({0, 11}, acc);
        break;
      case 37:
        execute_order_list({5}, acc);
        break;
      case 38:
        execute_order_list({5}, acc);
        break;
      case 39:
        execute_order_list({0}, acc);
        break;
      case 40:
        execute_order_list({5}, acc);
        break;
      case 41:
        execute_order_list({6, 7, 8}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void execute_0212a5_gamma_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 5:
        execute_order_list({13}, acc);
        break;
      case 8:
        execute_order_list({13}, acc);
        break;
      case 11:
        execute_order_list({13}, acc);
        break;
      case 14:
        execute_order_list({13}, acc);
        break;
      case 17:
        execute_order_list({13}, acc);
        break;
      case 20:
        execute_order_list({13}, acc);
        break;
      case 23:
        execute_order_list({13}, acc);
        break;
      case 32:
        execute_order_list({13}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void execute_0212a5_post_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 0:
        execute_order_list({27}, acc);
        break;
      case 1:
        execute_order_list({22}, acc);
        break;
      case 2:
        execute_order_list({29}, acc);
        break;
      case 3:
        execute_order_list({16}, acc);
        break;
      case 4:
        execute_order_list({25, 27}, acc);
        break;
      case 5:
        execute_order_list({29}, acc);
        break;
      case 6:
        execute_order_list({16}, acc);
        break;
      case 7:
        execute_order_list({25, 27}, acc);
        break;
      case 8:
        execute_order_list({29}, acc);
        break;
      case 9:
        execute_order_list({16}, acc);
        break;
      case 10:
        execute_order_list({25, 27}, acc);
        break;
      case 11:
        execute_order_list({29}, acc);
        break;
      case 12:
        execute_order_list({16}, acc);
        break;
      case 13:
        execute_order_list({25, 27}, acc);
        break;
      case 14:
        execute_order_list({29}, acc);
        break;
      case 15:
        execute_order_list({16}, acc);
        break;
      case 16:
        execute_order_list({25, 27}, acc);
        break;
      case 17:
        execute_order_list({29}, acc);
        break;
      case 18:
        execute_order_list({16}, acc);
        break;
      case 19:
        execute_order_list({25, 27}, acc);
        break;
      case 20:
        execute_order_list({29}, acc);
        break;
      case 21:
        execute_order_list({16}, acc);
        break;
      case 22:
        execute_order_list({25, 27}, acc);
        break;
      case 23:
        execute_order_list({29}, acc);
        break;
      case 24:
        execute_order_list({25, 29}, acc);
        break;
      case 25:
        execute_order_list({22}, acc);
        break;
      case 26:
        execute_order_list({27}, acc);
        break;
      case 27:
        execute_order_list({27}, acc);
        break;
      case 28:
        execute_order_list({22}, acc);
        break;
      case 29:
        execute_order_list({29}, acc);
        break;
      case 30:
        execute_order_list({16}, acc);
        break;
      case 31:
        execute_order_list({25, 27}, acc);
        break;
      case 32:
        execute_order_list({29}, acc);
        break;
      case 33:
        execute_order_list({25, 29}, acc);
        break;
      case 34:
        execute_order_list({22}, acc);
        break;
      case 35:
        execute_order_list({27}, acc);
        break;
      case 36:
        execute_order_list({17, 32}, acc);
        break;
      case 37:
        execute_order_list({22}, acc);
        break;
      case 38:
        execute_order_list({26, 29}, acc);
        break;
      case 39:
        execute_order_list({17, 32}, acc);
        break;
      case 40:
        execute_order_list({22}, acc);
        break;
      case 41:
        execute_order_list({27}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void tick_ik1303_0212a5_decoded() {
    static constexpr uint32_t kCommand = 0x0212a5;
    if (tick_index == 0) command = kCommand;

    const int current_tick = tick_index;
    const bool branch_l = l != 0;
    if (current_tick < 27) {
      sync_address = 37;
    } else if (current_tick < 36) {
      sync_address = 37;
    } else {
      sync_address = 8;
    }

    Accumulator acc;
    execute_0212a5_pre_add_orders(current_tick, branch_l, acc);

    if (key_y == 0) t = 0;

    execute_0212a5_gamma_orders(current_tick, branch_l, acc);

    int sum = acc.alpha + acc.beta + acc.gamma;
    acc.sigma = sum & 0xF;
    carry = (sum >> 4) & 1;

    execute_0212a5_post_add_orders(current_tick, branch_l, acc);

    output = memory[current_tick];
    memory[current_tick] = input;
    ++tick_index;
    if (tick_index == 42) tick_index = 0;
  }


  void execute_4207d0_pre_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 27:
        execute_order_list({0}, acc);
        break;
      case 28:
        execute_order_list({7, 8}, acc);
        break;
      case 29:
        execute_order_list({5, 11}, acc);
        break;
      case 30:
        execute_order_list({0}, acc);
        break;
      case 32:
        execute_order_list({0}, acc);
        break;
      case 33:
        execute_order_list({4, 7, 8}, acc);
        break;
      case 34:
        execute_order_list({5}, acc);
        break;
      case 35:
        execute_order_list({6, 7, 8}, acc);
        break;
      case 36:
        execute_order_list({0, 11}, acc);
        break;
      case 37:
        execute_order_list({5}, acc);
        break;
      case 38:
        execute_order_list({5}, acc);
        break;
      case 39:
        execute_order_list({0}, acc);
        break;
      case 40:
        execute_order_list({5}, acc);
        break;
      case 41:
        execute_order_list({6, 7, 8}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void execute_4207d0_gamma_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 28:
        execute_order_list({12, 13}, acc);
        break;
      case 33:
        execute_order_list({12}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void execute_4207d0_post_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 27:
        execute_order_list({27}, acc);
        break;
      case 28:
        execute_order_list({25, 27}, acc);
        break;
      case 29:
        execute_order_list({25, 27}, acc);
        break;
      case 30:
        execute_order_list({27}, acc);
        break;
      case 32:
        execute_order_list({27}, acc);
        break;
      case 33:
        execute_order_list({27}, acc);
        break;
      case 34:
        execute_order_list({26, 29}, acc);
        break;
      case 35:
        execute_order_list({27}, acc);
        break;
      case 36:
        execute_order_list({17, 32}, acc);
        break;
      case 37:
        execute_order_list({22}, acc);
        break;
      case 38:
        execute_order_list({26, 29}, acc);
        break;
      case 39:
        execute_order_list({17, 32}, acc);
        break;
      case 40:
        execute_order_list({22}, acc);
        break;
      case 41:
        execute_order_list({27}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void tick_ik1303_4207d0_decoded() {
    static constexpr uint32_t kCommand = 0x4207d0;
    if (tick_index == 0) command = kCommand;

    const int current_tick = tick_index;
    const bool branch_l = l != 0;
    if (current_tick < 27) {
      sync_address = 80;
    } else if (current_tick < 36) {
      sync_address = 15;
    } else {
      sync_address = 8;
    }

    Accumulator acc;
    execute_4207d0_pre_add_orders(current_tick, branch_l, acc);

    if (key_y == 0) t = 0;

    execute_4207d0_gamma_orders(current_tick, branch_l, acc);

    int sum = acc.alpha + acc.beta + acc.gamma;
    acc.sigma = sum & 0xF;
    carry = (sum >> 4) & 1;

    execute_4207d0_post_add_orders(current_tick, branch_l, acc);

    output = memory[current_tick];
    memory[current_tick] = input;
    ++tick_index;
    if (tick_index == 42) tick_index = 0;
  }


  void execute_6ce847_pre_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 1:
        execute_order_list({3, 11}, acc);
        break;
      case 2:
        execute_order_list({4, 7}, acc);
        break;
      case 3:
        execute_order_list({5}, acc);
        break;
      case 4:
        execute_order_list({3}, acc);
        break;
      case 5:
        execute_order_list({4, 7}, acc);
        break;
      case 6:
        execute_order_list({5}, acc);
        break;
      case 7:
        execute_order_list({3}, acc);
        break;
      case 8:
        execute_order_list({4, 7}, acc);
        break;
      case 9:
        execute_order_list({5}, acc);
        break;
      case 10:
        execute_order_list({3}, acc);
        break;
      case 11:
        execute_order_list({4, 7}, acc);
        break;
      case 12:
        execute_order_list({5}, acc);
        break;
      case 13:
        execute_order_list({3}, acc);
        break;
      case 14:
        execute_order_list({4, 7}, acc);
        break;
      case 15:
        execute_order_list({5}, acc);
        break;
      case 16:
        execute_order_list({3}, acc);
        break;
      case 17:
        execute_order_list({4, 7}, acc);
        break;
      case 18:
        execute_order_list({5}, acc);
        break;
      case 19:
        execute_order_list({3}, acc);
        break;
      case 20:
        execute_order_list({4, 7}, acc);
        break;
      case 21:
        execute_order_list({5}, acc);
        break;
      case 22:
        execute_order_list({3}, acc);
        break;
      case 23:
        execute_order_list({4, 7}, acc);
        break;
      case 24:
        execute_order_list({5}, acc);
        break;
      case 25:
        execute_order_list({3}, acc);
        break;
      case 26:
        execute_order_list({4, 7}, acc);
        break;
      case 36:
        execute_order_list({0, 11}, acc);
        break;
      case 37:
        if (branch_l) {
          execute_order_list({0}, acc);
        } else {
          execute_order_list({5}, acc);
        }
        break;
      case 38:
        execute_order_list({5, 11}, acc);
        break;
      case 39:
        execute_order_list({0}, acc);
        break;
      case 40:
        if (branch_l) {
          execute_order_list({0}, acc);
        } else {
          execute_order_list({5}, acc);
        }
        break;
      case 41:
        execute_order_list({0}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void execute_6ce847_gamma_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 4:
        execute_order_list({12}, acc);
        break;
      case 7:
        execute_order_list({12}, acc);
        break;
      case 10:
        execute_order_list({12}, acc);
        break;
      case 13:
        execute_order_list({12}, acc);
        break;
      case 16:
        execute_order_list({12}, acc);
        break;
      case 19:
        execute_order_list({12}, acc);
        break;
      case 22:
        execute_order_list({12}, acc);
        break;
      case 25:
        execute_order_list({12}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void execute_6ce847_post_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 1:
        execute_order_list({25, 27}, acc);
        break;
      case 2:
        execute_order_list({26, 29}, acc);
        break;
      case 3:
        execute_order_list({26, 29}, acc);
        break;
      case 4:
        execute_order_list({25, 27}, acc);
        break;
      case 5:
        execute_order_list({26, 29}, acc);
        break;
      case 6:
        execute_order_list({26, 29}, acc);
        break;
      case 7:
        execute_order_list({25, 27}, acc);
        break;
      case 8:
        execute_order_list({26, 29}, acc);
        break;
      case 9:
        execute_order_list({26, 29}, acc);
        break;
      case 10:
        execute_order_list({25, 27}, acc);
        break;
      case 11:
        execute_order_list({26, 29}, acc);
        break;
      case 12:
        execute_order_list({26, 29}, acc);
        break;
      case 13:
        execute_order_list({25, 27}, acc);
        break;
      case 14:
        execute_order_list({26, 29}, acc);
        break;
      case 15:
        execute_order_list({26, 29}, acc);
        break;
      case 16:
        execute_order_list({25, 27}, acc);
        break;
      case 17:
        execute_order_list({26, 29}, acc);
        break;
      case 18:
        execute_order_list({26, 29}, acc);
        break;
      case 19:
        execute_order_list({25, 27}, acc);
        break;
      case 20:
        execute_order_list({26, 29}, acc);
        break;
      case 21:
        execute_order_list({26, 29}, acc);
        break;
      case 22:
        execute_order_list({25, 27}, acc);
        break;
      case 23:
        execute_order_list({26, 29}, acc);
        break;
      case 24:
        execute_order_list({26, 29}, acc);
        break;
      case 25:
        execute_order_list({25, 27}, acc);
        break;
      case 26:
        execute_order_list({26, 29}, acc);
        break;
      case 36:
        execute_order_list({27}, acc);
        break;
      case 37:
        if (branch_l) {
          execute_order_list({17, 22, 27}, acc);
        } else {
          execute_order_list({22}, acc);
        }
        break;
      case 38:
        execute_order_list({23}, acc);
        break;
      case 39:
        execute_order_list({27}, acc);
        break;
      case 40:
        if (branch_l) {
          execute_order_list({17, 22, 27}, acc);
        } else {
          execute_order_list({22}, acc);
        }
        break;
      case 41:
        execute_order_list({27}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void tick_ik1303_6ce847_decoded() {
    static constexpr uint32_t kCommand = 0x6ce847;
    if (tick_index == 0) command = kCommand;

    const int current_tick = tick_index;
    const bool branch_l = l != 0;
    if (current_tick < 27) {
      sync_address = 71;
    } else if (current_tick < 36) {
      sync_address = 80;
    } else {
      if (current_tick == 36) {
        r[37] = 3 & 0xF;
        r[40] = 179 >> 4;
      }
      sync_address = 95;
    }

    Accumulator acc;
    execute_6ce847_pre_add_orders(current_tick, branch_l, acc);

    if (key_y == 0) t = 0;

    execute_6ce847_gamma_orders(current_tick, branch_l, acc);

    int sum = acc.alpha + acc.beta + acc.gamma;
    acc.sigma = sum & 0xF;
    carry = (sum >> 4) & 1;

    execute_6ce847_post_add_orders(current_tick, branch_l, acc);

    output = memory[current_tick];
    memory[current_tick] = input;
    ++tick_index;
    if (tick_index == 42) tick_index = 0;
  }


  void execute_035ecc_pre_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 0:
        execute_order_list({6, 7, 8}, acc);
        break;
      case 1:
        execute_order_list({0, 7}, acc);
        break;
      case 2:
        execute_order_list({5, 7}, acc);
        break;
      case 3:
        execute_order_list({10}, acc);
        break;
      case 4:
        execute_order_list({0, 7}, acc);
        break;
      case 5:
        execute_order_list({4, 7}, acc);
        break;
      case 6:
        execute_order_list({10}, acc);
        break;
      case 7:
        execute_order_list({0, 7}, acc);
        break;
      case 8:
        execute_order_list({4, 7}, acc);
        break;
      case 9:
        execute_order_list({10}, acc);
        break;
      case 10:
        execute_order_list({0, 7}, acc);
        break;
      case 11:
        execute_order_list({4, 7}, acc);
        break;
      case 12:
        execute_order_list({10}, acc);
        break;
      case 13:
        execute_order_list({0, 7}, acc);
        break;
      case 14:
        execute_order_list({4, 7}, acc);
        break;
      case 15:
        execute_order_list({10}, acc);
        break;
      case 16:
        execute_order_list({0, 7}, acc);
        break;
      case 17:
        execute_order_list({4, 7}, acc);
        break;
      case 18:
        execute_order_list({10}, acc);
        break;
      case 19:
        execute_order_list({0, 7}, acc);
        break;
      case 20:
        execute_order_list({4, 7}, acc);
        break;
      case 21:
        execute_order_list({10}, acc);
        break;
      case 22:
        execute_order_list({0, 7}, acc);
        break;
      case 23:
        execute_order_list({4, 7}, acc);
        break;
      case 24:
        execute_order_list({10}, acc);
        break;
      case 25:
        execute_order_list({0, 7}, acc);
        break;
      case 26:
        execute_order_list({4, 7}, acc);
        break;
      case 27:
        execute_order_list({10}, acc);
        break;
      case 28:
        execute_order_list({0, 7}, acc);
        break;
      case 29:
        execute_order_list({4, 7}, acc);
        break;
      case 30:
        execute_order_list({10}, acc);
        break;
      case 31:
        execute_order_list({0, 7}, acc);
        break;
      case 32:
        execute_order_list({4, 7}, acc);
        break;
      case 33:
        execute_order_list({10}, acc);
        break;
      case 34:
        execute_order_list({0, 7}, acc);
        break;
      case 35:
        execute_order_list({4, 7}, acc);
        break;
      case 39:
        execute_order_list({0, 11}, acc);
        break;
      case 40:
        execute_order_list({5}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void execute_035ecc_gamma_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 2:
        execute_order_list({12}, acc);
        break;
      case 4:
        execute_order_list({12}, acc);
        break;
      case 7:
        execute_order_list({12}, acc);
        break;
      case 10:
        execute_order_list({12}, acc);
        break;
      case 13:
        execute_order_list({12}, acc);
        break;
      case 16:
        execute_order_list({12}, acc);
        break;
      case 19:
        execute_order_list({12}, acc);
        break;
      case 22:
        execute_order_list({12}, acc);
        break;
      case 25:
        execute_order_list({12}, acc);
        break;
      case 28:
        execute_order_list({12, 13}, acc);
        break;
      case 31:
        execute_order_list({12}, acc);
        break;
      case 34:
        execute_order_list({12}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void execute_035ecc_post_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 0:
        execute_order_list({27}, acc);
        break;
      case 1:
        execute_order_list({25, 27}, acc);
        break;
      case 2:
        execute_order_list({25, 27}, acc);
        break;
      case 3:
        execute_order_list({27}, acc);
        break;
      case 4:
        execute_order_list({25, 27}, acc);
        break;
      case 5:
        execute_order_list({22, 26, 29}, acc);
        break;
      case 6:
        execute_order_list({27}, acc);
        break;
      case 7:
        execute_order_list({25, 27}, acc);
        break;
      case 8:
        execute_order_list({22, 26, 29}, acc);
        break;
      case 9:
        execute_order_list({27}, acc);
        break;
      case 10:
        execute_order_list({25, 27}, acc);
        break;
      case 11:
        execute_order_list({22, 26, 29}, acc);
        break;
      case 12:
        execute_order_list({27}, acc);
        break;
      case 13:
        execute_order_list({25, 27}, acc);
        break;
      case 14:
        execute_order_list({22, 26, 29}, acc);
        break;
      case 15:
        execute_order_list({27}, acc);
        break;
      case 16:
        execute_order_list({25, 27}, acc);
        break;
      case 17:
        execute_order_list({22, 26, 29}, acc);
        break;
      case 18:
        execute_order_list({27}, acc);
        break;
      case 19:
        execute_order_list({25, 27}, acc);
        break;
      case 20:
        execute_order_list({22, 26, 29}, acc);
        break;
      case 21:
        execute_order_list({27}, acc);
        break;
      case 22:
        execute_order_list({25, 27}, acc);
        break;
      case 23:
        execute_order_list({22, 26, 29}, acc);
        break;
      case 24:
        execute_order_list({27}, acc);
        break;
      case 25:
        execute_order_list({25, 27}, acc);
        break;
      case 26:
        execute_order_list({22, 26, 29}, acc);
        break;
      case 27:
        execute_order_list({27}, acc);
        break;
      case 28:
        execute_order_list({25, 27}, acc);
        break;
      case 29:
        execute_order_list({22, 26, 29}, acc);
        break;
      case 30:
        execute_order_list({27}, acc);
        break;
      case 31:
        execute_order_list({25, 27}, acc);
        break;
      case 32:
        execute_order_list({22, 26, 29}, acc);
        break;
      case 33:
        execute_order_list({27}, acc);
        break;
      case 34:
        execute_order_list({25, 27}, acc);
        break;
      case 35:
        execute_order_list({22, 26, 29}, acc);
        break;
      case 36:
        execute_order_list({22}, acc);
        break;
      case 39:
        execute_order_list({27}, acc);
        break;
      case 40:
        execute_order_list({22}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void tick_ik1303_035ecc_decoded() {
    static constexpr uint32_t kCommand = 0x035ecc;
    if (tick_index == 0) command = kCommand;

    const int current_tick = tick_index;
    const bool branch_l = l != 0;
    if (current_tick < 27) {
      sync_address = 76;
    } else if (current_tick < 36) {
      sync_address = 61;
    } else {
      sync_address = 13;
    }

    Accumulator acc;
    execute_035ecc_pre_add_orders(current_tick, branch_l, acc);

    if (key_y == 0) t = 0;

    execute_035ecc_gamma_orders(current_tick, branch_l, acc);

    int sum = acc.alpha + acc.beta + acc.gamma;
    acc.sigma = sum & 0xF;
    carry = (sum >> 4) & 1;

    execute_035ecc_post_add_orders(current_tick, branch_l, acc);

    output = memory[current_tick];
    memory[current_tick] = input;
    ++tick_index;
    if (tick_index == 42) tick_index = 0;
  }


  void execute_060204_pre_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 0:
        execute_order_list({2}, acc);
        break;
      case 1:
        execute_order_list({5}, acc);
        break;
      case 2:
        execute_order_list({8}, acc);
        break;
      case 3:
        execute_order_list({2}, acc);
        break;
      case 4:
        execute_order_list({5}, acc);
        break;
      case 5:
        execute_order_list({0}, acc);
        break;
      case 6:
        execute_order_list({2}, acc);
        break;
      case 7:
        execute_order_list({5}, acc);
        break;
      case 8:
        execute_order_list({0}, acc);
        break;
      case 9:
        execute_order_list({2}, acc);
        break;
      case 10:
        execute_order_list({5}, acc);
        break;
      case 11:
        execute_order_list({0}, acc);
        break;
      case 12:
        execute_order_list({2}, acc);
        break;
      case 13:
        execute_order_list({5}, acc);
        break;
      case 14:
        execute_order_list({0}, acc);
        break;
      case 15:
        execute_order_list({2}, acc);
        break;
      case 16:
        execute_order_list({5}, acc);
        break;
      case 17:
        execute_order_list({0}, acc);
        break;
      case 18:
        execute_order_list({2}, acc);
        break;
      case 19:
        execute_order_list({5}, acc);
        break;
      case 20:
        execute_order_list({0}, acc);
        break;
      case 21:
        execute_order_list({2}, acc);
        break;
      case 22:
        execute_order_list({5}, acc);
        break;
      case 23:
        execute_order_list({0}, acc);
        break;
      case 24:
        execute_order_list({2}, acc);
        break;
      case 25:
        execute_order_list({5}, acc);
        break;
      case 26:
        execute_order_list({7, 8}, acc);
        break;
      case 27:
        execute_order_list({2}, acc);
        break;
      case 28:
        execute_order_list({5}, acc);
        break;
      case 29:
        execute_order_list({8}, acc);
        break;
      case 30:
        execute_order_list({2}, acc);
        break;
      case 31:
        execute_order_list({5}, acc);
        break;
      case 32:
        execute_order_list({0}, acc);
        break;
      case 33:
        execute_order_list({2}, acc);
        break;
      case 34:
        execute_order_list({5}, acc);
        break;
      case 35:
        execute_order_list({7, 8}, acc);
        break;
      case 36:
        execute_order_list({0, 11}, acc);
        break;
      case 37:
        execute_order_list({6, 7, 8}, acc);
        break;
      case 38:
        execute_order_list({5}, acc);
        break;
      case 39:
        execute_order_list({0}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void execute_060204_gamma_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 2:
        execute_order_list({12, 13}, acc);
        break;
      case 26:
        execute_order_list({12, 13}, acc);
        break;
      case 29:
        execute_order_list({12, 13}, acc);
        break;
      case 35:
        execute_order_list({12, 13}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void execute_060204_post_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 0:
        execute_order_list({27, 33}, acc);
        break;
      case 1:
        execute_order_list({22}, acc);
        break;
      case 2:
        execute_order_list({25}, acc);
        break;
      case 3:
        execute_order_list({27, 33}, acc);
        break;
      case 4:
        execute_order_list({22}, acc);
        break;
      case 5:
        execute_order_list({27}, acc);
        break;
      case 6:
        execute_order_list({27, 33}, acc);
        break;
      case 7:
        execute_order_list({22}, acc);
        break;
      case 8:
        execute_order_list({27}, acc);
        break;
      case 9:
        execute_order_list({27, 33}, acc);
        break;
      case 10:
        execute_order_list({22}, acc);
        break;
      case 11:
        execute_order_list({27}, acc);
        break;
      case 12:
        execute_order_list({27, 33}, acc);
        break;
      case 13:
        execute_order_list({22}, acc);
        break;
      case 14:
        execute_order_list({27}, acc);
        break;
      case 15:
        execute_order_list({27, 33}, acc);
        break;
      case 16:
        execute_order_list({22}, acc);
        break;
      case 17:
        execute_order_list({27}, acc);
        break;
      case 18:
        execute_order_list({27, 33}, acc);
        break;
      case 19:
        execute_order_list({22}, acc);
        break;
      case 20:
        execute_order_list({27}, acc);
        break;
      case 21:
        execute_order_list({27, 33}, acc);
        break;
      case 22:
        execute_order_list({22}, acc);
        break;
      case 23:
        execute_order_list({27}, acc);
        break;
      case 24:
        execute_order_list({27, 33}, acc);
        break;
      case 25:
        execute_order_list({22}, acc);
        break;
      case 26:
        execute_order_list({25, 27}, acc);
        break;
      case 27:
        execute_order_list({27, 33}, acc);
        break;
      case 28:
        execute_order_list({22}, acc);
        break;
      case 29:
        execute_order_list({25}, acc);
        break;
      case 30:
        execute_order_list({27, 33}, acc);
        break;
      case 31:
        execute_order_list({22}, acc);
        break;
      case 32:
        execute_order_list({27}, acc);
        break;
      case 33:
        execute_order_list({27, 33}, acc);
        break;
      case 34:
        execute_order_list({22}, acc);
        break;
      case 35:
        execute_order_list({25, 27}, acc);
        break;
      case 36:
        execute_order_list({17, 32}, acc);
        break;
      case 37:
        execute_order_list({27}, acc);
        break;
      case 38:
        execute_order_list({23}, acc);
        break;
      case 39:
        execute_order_list({17, 32}, acc);
        break;
      case 40:
        execute_order_list({29}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void tick_ik1303_060204_decoded() {
    static constexpr uint32_t kCommand = 0x060204;
    if (tick_index == 0) command = kCommand;

    const int current_tick = tick_index;
    const bool branch_l = l != 0;
    if (current_tick < 27) {
      sync_address = 4;
    } else if (current_tick < 36) {
      sync_address = 4;
    } else {
      sync_address = 24;
    }

    Accumulator acc;
    execute_060204_pre_add_orders(current_tick, branch_l, acc);

    if (key_y == 0) t = 0;

    execute_060204_gamma_orders(current_tick, branch_l, acc);

    int sum = acc.alpha + acc.beta + acc.gamma;
    acc.sigma = sum & 0xF;
    carry = (sum >> 4) & 1;

    execute_060204_post_add_orders(current_tick, branch_l, acc);

    output = memory[current_tick];
    memory[current_tick] = input;
    ++tick_index;
    if (tick_index == 42) tick_index = 0;
  }


  void execute_701d50_pre_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 27:
        execute_order_list({8}, acc);
        break;
      case 28:
        execute_order_list({5, 7}, acc);
        break;
      case 29:
        execute_order_list({5, 10}, acc);
        break;
      case 30:
        execute_order_list({5, 8}, acc);
        break;
      case 31:
        execute_order_list({5}, acc);
        break;
      case 32:
        execute_order_list({5, 11}, acc);
        break;
      case 33:
        if (branch_l) {
          execute_order_list({0}, acc);
        } else {
          execute_order_list({5}, acc);
        }
        break;
      case 34:
        execute_order_list({0}, acc);
        break;
      case 36:
        execute_order_list({0, 11}, acc);
        break;
      case 37:
        if (branch_l) {
          execute_order_list({0}, acc);
        } else {
          execute_order_list({5}, acc);
        }
        break;
      case 38:
        execute_order_list({5, 11}, acc);
        break;
      case 39:
        execute_order_list({0}, acc);
        break;
      case 40:
        if (branch_l) {
          execute_order_list({0}, acc);
        } else {
          execute_order_list({5}, acc);
        }
        break;
      case 41:
        execute_order_list({0}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void execute_701d50_gamma_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 27:
        execute_order_list({12, 13}, acc);
        break;
      case 28:
        execute_order_list({12}, acc);
        break;
      case 29:
        execute_order_list({12, 13}, acc);
        break;
      case 31:
        execute_order_list({12}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void execute_701d50_post_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 27:
        execute_order_list({25}, acc);
        break;
      case 28:
        execute_order_list({25, 27}, acc);
        break;
      case 29:
        execute_order_list({25}, acc);
        break;
      case 30:
        execute_order_list({27}, acc);
        break;
      case 31:
        execute_order_list({27}, acc);
        break;
      case 32:
        execute_order_list({25, 27}, acc);
        break;
      case 33:
        if (branch_l) {
          execute_order_list({27}, acc);
        }
        break;
      case 35:
        execute_order_list({29}, acc);
        break;
      case 36:
        execute_order_list({27}, acc);
        break;
      case 37:
        if (branch_l) {
          execute_order_list({17, 22, 27}, acc);
        } else {
          execute_order_list({22}, acc);
        }
        break;
      case 38:
        execute_order_list({23}, acc);
        break;
      case 39:
        execute_order_list({27}, acc);
        break;
      case 40:
        if (branch_l) {
          execute_order_list({17, 22, 27}, acc);
        } else {
          execute_order_list({22}, acc);
        }
        break;
      case 41:
        execute_order_list({27}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void tick_ik1303_701d50_decoded() {
    static constexpr uint32_t kCommand = 0x701d50;
    if (tick_index == 0) command = kCommand;

    const int current_tick = tick_index;
    const bool branch_l = l != 0;
    if (current_tick < 27) {
      sync_address = 80;
    } else if (current_tick < 36) {
      sync_address = 58;
    } else {
      if (current_tick == 36) {
        r[37] = 0 & 0xF;
        r[40] = 192 >> 4;
      }
      sync_address = 95;
    }

    Accumulator acc;
    execute_701d50_pre_add_orders(current_tick, branch_l, acc);

    if (key_y == 0) t = 0;

    execute_701d50_gamma_orders(current_tick, branch_l, acc);

    int sum = acc.alpha + acc.beta + acc.gamma;
    acc.sigma = sum & 0xF;
    carry = (sum >> 4) & 1;

    execute_701d50_post_add_orders(current_tick, branch_l, acc);

    output = memory[current_tick];
    memory[current_tick] = input;
    ++tick_index;
    if (tick_index == 42) tick_index = 0;
  }


  void execute_388221_pre_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 0:
        execute_order_list({0}, acc);
        break;
      case 1:
        execute_order_list({9}, acc);
        break;
      case 2:
        execute_order_list({5}, acc);
        break;
      case 3:
        execute_order_list({0}, acc);
        break;
      case 4:
        execute_order_list({0}, acc);
        break;
      case 6:
        execute_order_list({0}, acc);
        break;
      case 7:
        execute_order_list({0}, acc);
        break;
      case 9:
        execute_order_list({0}, acc);
        break;
      case 10:
        execute_order_list({0}, acc);
        break;
      case 12:
        execute_order_list({0}, acc);
        break;
      case 13:
        execute_order_list({0}, acc);
        break;
      case 15:
        execute_order_list({0}, acc);
        break;
      case 16:
        execute_order_list({0}, acc);
        break;
      case 18:
        execute_order_list({0}, acc);
        break;
      case 19:
        execute_order_list({0}, acc);
        break;
      case 21:
        execute_order_list({0}, acc);
        break;
      case 22:
        execute_order_list({0}, acc);
        break;
      case 24:
        execute_order_list({0}, acc);
        break;
      case 25:
        execute_order_list({0}, acc);
        break;
      case 26:
        execute_order_list({7, 8}, acc);
        break;
      case 27:
        execute_order_list({2}, acc);
        break;
      case 28:
        execute_order_list({5}, acc);
        break;
      case 29:
        execute_order_list({8}, acc);
        break;
      case 30:
        execute_order_list({2}, acc);
        break;
      case 31:
        execute_order_list({5}, acc);
        break;
      case 32:
        execute_order_list({0}, acc);
        break;
      case 33:
        execute_order_list({2}, acc);
        break;
      case 34:
        execute_order_list({5}, acc);
        break;
      case 35:
        execute_order_list({7, 8}, acc);
        break;
      case 36:
        execute_order_list({0, 11}, acc);
        break;
      case 37:
        if (branch_l) {
          execute_order_list({0}, acc);
        } else {
          execute_order_list({5}, acc);
        }
        break;
      case 38:
        execute_order_list({5, 11}, acc);
        break;
      case 39:
        execute_order_list({0}, acc);
        break;
      case 40:
        if (branch_l) {
          execute_order_list({0}, acc);
        } else {
          execute_order_list({5}, acc);
        }
        break;
      case 41:
        execute_order_list({0}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void execute_388221_gamma_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 26:
        execute_order_list({12, 13}, acc);
        break;
      case 29:
        execute_order_list({12, 13}, acc);
        break;
      case 35:
        execute_order_list({12, 13}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void execute_388221_post_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 0:
        execute_order_list({29}, acc);
        break;
      case 1:
        execute_order_list({16}, acc);
        break;
      case 2:
        execute_order_list({26, 29}, acc);
        break;
      case 3:
        execute_order_list({27}, acc);
        break;
      case 4:
        execute_order_list({17, 27}, acc);
        break;
      case 6:
        execute_order_list({27}, acc);
        break;
      case 7:
        execute_order_list({17, 27}, acc);
        break;
      case 9:
        execute_order_list({27}, acc);
        break;
      case 10:
        execute_order_list({17, 27}, acc);
        break;
      case 12:
        execute_order_list({27}, acc);
        break;
      case 13:
        execute_order_list({17, 27}, acc);
        break;
      case 15:
        execute_order_list({27}, acc);
        break;
      case 16:
        execute_order_list({17, 27}, acc);
        break;
      case 18:
        execute_order_list({27}, acc);
        break;
      case 19:
        execute_order_list({17, 27}, acc);
        break;
      case 21:
        execute_order_list({27}, acc);
        break;
      case 22:
        execute_order_list({17, 27}, acc);
        break;
      case 24:
        execute_order_list({27}, acc);
        break;
      case 25:
        execute_order_list({17, 27}, acc);
        break;
      case 26:
        execute_order_list({25, 27}, acc);
        break;
      case 27:
        execute_order_list({27, 33}, acc);
        break;
      case 28:
        execute_order_list({22}, acc);
        break;
      case 29:
        execute_order_list({25}, acc);
        break;
      case 30:
        execute_order_list({27, 33}, acc);
        break;
      case 31:
        execute_order_list({22}, acc);
        break;
      case 32:
        execute_order_list({27}, acc);
        break;
      case 33:
        execute_order_list({27, 33}, acc);
        break;
      case 34:
        execute_order_list({22}, acc);
        break;
      case 35:
        execute_order_list({25, 27}, acc);
        break;
      case 36:
        execute_order_list({27}, acc);
        break;
      case 37:
        if (branch_l) {
          execute_order_list({17, 22, 27}, acc);
        } else {
          execute_order_list({22}, acc);
        }
        break;
      case 38:
        execute_order_list({23}, acc);
        break;
      case 39:
        execute_order_list({27}, acc);
        break;
      case 40:
        if (branch_l) {
          execute_order_list({17, 22, 27}, acc);
        } else {
          execute_order_list({22}, acc);
        }
        break;
      case 41:
        execute_order_list({27}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void tick_ik1303_388221_decoded() {
    static constexpr uint32_t kCommand = 0x388221;
    if (tick_index == 0) command = kCommand;

    const int current_tick = tick_index;
    const bool branch_l = l != 0;
    if (current_tick < 27) {
      sync_address = 33;
    } else if (current_tick < 36) {
      sync_address = 4;
    } else {
      if (current_tick == 36) {
        r[37] = 2 & 0xF;
        r[40] = 226 >> 4;
      }
      sync_address = 95;
    }

    Accumulator acc;
    execute_388221_pre_add_orders(current_tick, branch_l, acc);

    if (key_y == 0) t = 0;

    execute_388221_gamma_orders(current_tick, branch_l, acc);

    int sum = acc.alpha + acc.beta + acc.gamma;
    acc.sigma = sum & 0xF;
    carry = (sum >> 4) & 1;

    execute_388221_post_add_orders(current_tick, branch_l, acc);

    output = memory[current_tick];
    memory[current_tick] = input;
    ++tick_index;
    if (tick_index == 42) tick_index = 0;
  }


  void execute_0a3dd0_pre_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 28:
        execute_order_list({3, 11}, acc);
        break;
      case 31:
        execute_order_list({3}, acc);
        break;
      case 34:
        execute_order_list({4, 7, 8}, acc);
        break;
      case 35:
        execute_order_list({7, 8}, acc);
        break;
      case 36:
        execute_order_list({0, 11}, acc);
        break;
      case 37:
        if (branch_l) {
          execute_order_list({0}, acc);
        } else {
          execute_order_list({5}, acc);
        }
        break;
      case 38:
        execute_order_list({5, 11}, acc);
        break;
      case 39:
        execute_order_list({0}, acc);
        break;
      case 40:
        if (branch_l) {
          execute_order_list({0}, acc);
        } else {
          execute_order_list({5}, acc);
        }
        break;
      case 41:
        execute_order_list({0}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void execute_0a3dd0_gamma_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 31:
        execute_order_list({12}, acc);
        break;
      case 34:
        execute_order_list({12}, acc);
        break;
      case 35:
        execute_order_list({12, 13}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void execute_0a3dd0_post_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 28:
        execute_order_list({25, 27}, acc);
        break;
      case 31:
        execute_order_list({25, 27}, acc);
        break;
      case 34:
        execute_order_list({16}, acc);
        break;
      case 35:
        execute_order_list({25, 27}, acc);
        break;
      case 36:
        execute_order_list({27}, acc);
        break;
      case 37:
        if (branch_l) {
          execute_order_list({17, 22, 27}, acc);
        } else {
          execute_order_list({22}, acc);
        }
        break;
      case 38:
        execute_order_list({23}, acc);
        break;
      case 39:
        execute_order_list({27}, acc);
        break;
      case 40:
        if (branch_l) {
          execute_order_list({17, 22, 27}, acc);
        } else {
          execute_order_list({22}, acc);
        }
        break;
      case 41:
        execute_order_list({27}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void tick_ik1303_0a3dd0_decoded() {
    static constexpr uint32_t kCommand = 0x0a3dd0;
    if (tick_index == 0) command = kCommand;

    const int current_tick = tick_index;
    const bool branch_l = l != 0;
    if (current_tick < 27) {
      sync_address = 80;
    } else if (current_tick < 36) {
      sync_address = 123;
    } else {
      if (current_tick == 36) {
        r[37] = 8 & 0xF;
        r[40] = 40 >> 4;
      }
      sync_address = 95;
    }

    Accumulator acc;
    execute_0a3dd0_pre_add_orders(current_tick, branch_l, acc);

    if (key_y == 0) t = 0;

    execute_0a3dd0_gamma_orders(current_tick, branch_l, acc);

    int sum = acc.alpha + acc.beta + acc.gamma;
    acc.sigma = sum & 0xF;
    carry = (sum >> 4) & 1;

    execute_0a3dd0_post_add_orders(current_tick, branch_l, acc);

    output = memory[current_tick];
    memory[current_tick] = input;
    ++tick_index;
    if (tick_index == 42) tick_index = 0;
  }


  void execute_1cf7dc_pre_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 26:
        execute_order_list({0, 7, 8}, acc);
        break;
      case 31:
        execute_order_list({0, 11}, acc);
        break;
      case 32:
        execute_order_list({5}, acc);
        break;
      case 33:
        execute_order_list({7, 8}, acc);
        break;
      case 34:
        execute_order_list({5}, acc);
        break;
      case 36:
        execute_order_list({0, 11}, acc);
        break;
      case 37:
        if (branch_l) {
          execute_order_list({0}, acc);
        } else {
          execute_order_list({5}, acc);
        }
        break;
      case 38:
        execute_order_list({5, 11}, acc);
        break;
      case 39:
        execute_order_list({0}, acc);
        break;
      case 40:
        if (branch_l) {
          execute_order_list({0}, acc);
        } else {
          execute_order_list({5}, acc);
        }
        break;
      case 41:
        execute_order_list({0}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void execute_1cf7dc_gamma_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 33:
        execute_order_list({12, 13}, acc);
        break;
      case 34:
        execute_order_list({12}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void execute_1cf7dc_post_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 0:
        execute_order_list({15}, acc);
        break;
      case 3:
        execute_order_list({15}, acc);
        break;
      case 6:
        execute_order_list({15}, acc);
        break;
      case 9:
        execute_order_list({15}, acc);
        break;
      case 12:
        execute_order_list({15}, acc);
        break;
      case 15:
        execute_order_list({15}, acc);
        break;
      case 18:
        execute_order_list({15}, acc);
        break;
      case 21:
        execute_order_list({15}, acc);
        break;
      case 24:
        execute_order_list({15}, acc);
        break;
      case 26:
        execute_order_list({16, 25, 27}, acc);
        break;
      case 27:
        execute_order_list({22}, acc);
        break;
      case 28:
        execute_order_list({22}, acc);
        break;
      case 31:
        execute_order_list({27}, acc);
        break;
      case 32:
        execute_order_list({22}, acc);
        break;
      case 33:
        execute_order_list({25, 27}, acc);
        break;
      case 34:
        execute_order_list({27}, acc);
        break;
      case 36:
        execute_order_list({27}, acc);
        break;
      case 37:
        if (branch_l) {
          execute_order_list({17, 22, 27}, acc);
        } else {
          execute_order_list({22}, acc);
        }
        break;
      case 38:
        execute_order_list({23}, acc);
        break;
      case 39:
        execute_order_list({27}, acc);
        break;
      case 40:
        if (branch_l) {
          execute_order_list({17, 22, 27}, acc);
        } else {
          execute_order_list({22}, acc);
        }
        break;
      case 41:
        execute_order_list({27}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void tick_ik1303_1cf7dc_decoded() {
    static constexpr uint32_t kCommand = 0x1cf7dc;
    if (tick_index == 0) command = kCommand;

    const int current_tick = tick_index;
    const bool branch_l = l != 0;
    if (current_tick < 27) {
      sync_address = 92;
    } else if (current_tick < 36) {
      sync_address = 111;
    } else {
      if (current_tick == 36) {
        r[37] = 3 & 0xF;
        r[40] = 115 >> 4;
      }
      sync_address = 95;
    }

    Accumulator acc;
    execute_1cf7dc_pre_add_orders(current_tick, branch_l, acc);

    if (key_y == 0) t = 0;

    execute_1cf7dc_gamma_orders(current_tick, branch_l, acc);

    int sum = acc.alpha + acc.beta + acc.gamma;
    acc.sigma = sum & 0xF;
    carry = (sum >> 4) & 1;

    execute_1cf7dc_post_add_orders(current_tick, branch_l, acc);

    output = memory[current_tick];
    memory[current_tick] = input;
    ++tick_index;
    if (tick_index == 42) tick_index = 0;
  }


  void execute_29bae9_pre_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 0:
        execute_order_list({7, 8}, acc);
        break;
      case 2:
        execute_order_list({0}, acc);
        break;
      case 26:
        execute_order_list({0}, acc);
        break;
      case 27:
        execute_order_list({10}, acc);
        break;
      case 28:
        execute_order_list({5}, acc);
        break;
      case 29:
        execute_order_list({5, 10}, acc);
        break;
      case 30:
        execute_order_list({4, 7, 8}, acc);
        break;
      case 33:
        execute_order_list({5, 10, 11}, acc);
        break;
      case 35:
        execute_order_list({4, 7, 8}, acc);
        break;
      case 36:
        execute_order_list({0, 11}, acc);
        break;
      case 37:
        if (branch_l) {
          execute_order_list({0}, acc);
        } else {
          execute_order_list({5}, acc);
        }
        break;
      case 38:
        execute_order_list({5, 11}, acc);
        break;
      case 39:
        execute_order_list({0}, acc);
        break;
      case 40:
        if (branch_l) {
          execute_order_list({0}, acc);
        } else {
          execute_order_list({5}, acc);
        }
        break;
      case 41:
        execute_order_list({0}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void execute_29bae9_gamma_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 0:
        execute_order_list({12, 13}, acc);
        break;
      case 29:
        execute_order_list({12, 13}, acc);
        break;
      case 30:
        execute_order_list({12}, acc);
        break;
      case 33:
        execute_order_list({12, 13}, acc);
        break;
      case 35:
        execute_order_list({12}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void execute_29bae9_post_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 0:
        execute_order_list({25, 27}, acc);
        break;
      case 1:
        execute_order_list({15}, acc);
        break;
      case 2:
        execute_order_list({17, 27}, acc);
        break;
      case 4:
        execute_order_list({15}, acc);
        break;
      case 7:
        execute_order_list({15}, acc);
        break;
      case 10:
        execute_order_list({15}, acc);
        break;
      case 13:
        execute_order_list({15}, acc);
        break;
      case 16:
        execute_order_list({15}, acc);
        break;
      case 19:
        execute_order_list({15}, acc);
        break;
      case 22:
        execute_order_list({15}, acc);
        break;
      case 25:
        execute_order_list({29}, acc);
        break;
      case 26:
        execute_order_list({22}, acc);
        break;
      case 27:
        execute_order_list({27}, acc);
        break;
      case 28:
        execute_order_list({26, 29}, acc);
        break;
      case 29:
        execute_order_list({25}, acc);
        break;
      case 30:
        execute_order_list({27}, acc);
        break;
      case 33:
        execute_order_list({25, 27}, acc);
        break;
      case 35:
        execute_order_list({16}, acc);
        break;
      case 36:
        execute_order_list({27}, acc);
        break;
      case 37:
        if (branch_l) {
          execute_order_list({17, 22, 27}, acc);
        } else {
          execute_order_list({22}, acc);
        }
        break;
      case 38:
        execute_order_list({23}, acc);
        break;
      case 39:
        execute_order_list({27}, acc);
        break;
      case 40:
        if (branch_l) {
          execute_order_list({17, 22, 27}, acc);
        } else {
          execute_order_list({22}, acc);
        }
        break;
      case 41:
        execute_order_list({27}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void tick_ik1303_29bae9_decoded() {
    static constexpr uint32_t kCommand = 0x29bae9;
    if (tick_index == 0) command = kCommand;

    const int current_tick = tick_index;
    const bool branch_l = l != 0;
    if (current_tick < 27) {
      sync_address = 105;
    } else if (current_tick < 36) {
      sync_address = 117;
    } else {
      if (current_tick == 36) {
        r[37] = 6 & 0xF;
        r[40] = 166 >> 4;
      }
      sync_address = 95;
    }

    Accumulator acc;
    execute_29bae9_pre_add_orders(current_tick, branch_l, acc);

    if (key_y == 0) t = 0;

    execute_29bae9_gamma_orders(current_tick, branch_l, acc);

    int sum = acc.alpha + acc.beta + acc.gamma;
    acc.sigma = sum & 0xF;
    carry = (sum >> 4) & 1;

    execute_29bae9_post_add_orders(current_tick, branch_l, acc);

    output = memory[current_tick];
    memory[current_tick] = input;
    ++tick_index;
    if (tick_index == 42) tick_index = 0;
  }


  void execute_2b53a6_pre_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 0:
        execute_order_list({7, 8}, acc);
        break;
      case 1:
        execute_order_list({0}, acc);
        break;
      case 2:
        execute_order_list({0}, acc);
        break;
      case 3:
        execute_order_list({7, 8}, acc);
        break;
      case 4:
        execute_order_list({0}, acc);
        break;
      case 5:
        execute_order_list({0}, acc);
        break;
      case 6:
        execute_order_list({7, 8}, acc);
        break;
      case 7:
        execute_order_list({0}, acc);
        break;
      case 8:
        execute_order_list({0}, acc);
        break;
      case 9:
        execute_order_list({7, 8}, acc);
        break;
      case 10:
        execute_order_list({0}, acc);
        break;
      case 11:
        execute_order_list({0}, acc);
        break;
      case 12:
        execute_order_list({7, 8}, acc);
        break;
      case 13:
        execute_order_list({0}, acc);
        break;
      case 14:
        execute_order_list({0}, acc);
        break;
      case 15:
        execute_order_list({7, 8}, acc);
        break;
      case 16:
        execute_order_list({0}, acc);
        break;
      case 17:
        execute_order_list({0}, acc);
        break;
      case 18:
        execute_order_list({7, 8}, acc);
        break;
      case 19:
        execute_order_list({0}, acc);
        break;
      case 20:
        execute_order_list({0}, acc);
        break;
      case 21:
        execute_order_list({7, 8}, acc);
        break;
      case 22:
        execute_order_list({0}, acc);
        break;
      case 23:
        execute_order_list({0}, acc);
        break;
      case 24:
        execute_order_list({7, 8}, acc);
        break;
      case 25:
        execute_order_list({0}, acc);
        break;
      case 27:
        execute_order_list({0, 7}, acc);
        break;
      case 28:
        execute_order_list({5, 7}, acc);
        break;
      case 29:
        execute_order_list({5}, acc);
        break;
      case 30:
        execute_order_list({0, 7, 8}, acc);
        break;
      case 31:
        execute_order_list({5}, acc);
        break;
      case 32:
        execute_order_list({5}, acc);
        break;
      case 33:
        execute_order_list({0}, acc);
        break;
      case 34:
        execute_order_list({4, 7}, acc);
        break;
      case 36:
        execute_order_list({0, 11}, acc);
        break;
      case 37:
        if (branch_l) {
          execute_order_list({0}, acc);
        } else {
          execute_order_list({5}, acc);
        }
        break;
      case 38:
        execute_order_list({5, 11}, acc);
        break;
      case 39:
        execute_order_list({0}, acc);
        break;
      case 40:
        if (branch_l) {
          execute_order_list({0}, acc);
        } else {
          execute_order_list({5}, acc);
        }
        break;
      case 41:
        execute_order_list({0}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void execute_2b53a6_gamma_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 0:
        execute_order_list({12, 13}, acc);
        break;
      case 3:
        execute_order_list({12, 13}, acc);
        break;
      case 6:
        execute_order_list({12, 13}, acc);
        break;
      case 9:
        execute_order_list({12, 13}, acc);
        break;
      case 12:
        execute_order_list({12, 13}, acc);
        break;
      case 15:
        execute_order_list({12, 13}, acc);
        break;
      case 18:
        execute_order_list({12, 13}, acc);
        break;
      case 21:
        execute_order_list({12, 13}, acc);
        break;
      case 24:
        execute_order_list({12, 13}, acc);
        break;
      case 28:
        execute_order_list({12}, acc);
        break;
      case 30:
        execute_order_list({12}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void execute_2b53a6_post_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 0:
        execute_order_list({25, 27}, acc);
        break;
      case 1:
        execute_order_list({17, 27}, acc);
        break;
      case 2:
        execute_order_list({17, 27}, acc);
        break;
      case 3:
        execute_order_list({25, 27}, acc);
        break;
      case 4:
        execute_order_list({17, 27}, acc);
        break;
      case 5:
        execute_order_list({17, 27}, acc);
        break;
      case 6:
        execute_order_list({25, 27}, acc);
        break;
      case 7:
        execute_order_list({17, 27}, acc);
        break;
      case 8:
        execute_order_list({17, 27}, acc);
        break;
      case 9:
        execute_order_list({25, 27}, acc);
        break;
      case 10:
        execute_order_list({17, 27}, acc);
        break;
      case 11:
        execute_order_list({17, 27}, acc);
        break;
      case 12:
        execute_order_list({25, 27}, acc);
        break;
      case 13:
        execute_order_list({17, 27}, acc);
        break;
      case 14:
        execute_order_list({17, 27}, acc);
        break;
      case 15:
        execute_order_list({25, 27}, acc);
        break;
      case 16:
        execute_order_list({17, 27}, acc);
        break;
      case 17:
        execute_order_list({17, 27}, acc);
        break;
      case 18:
        execute_order_list({25, 27}, acc);
        break;
      case 19:
        execute_order_list({17, 27}, acc);
        break;
      case 20:
        execute_order_list({17, 27}, acc);
        break;
      case 21:
        execute_order_list({25, 27}, acc);
        break;
      case 22:
        execute_order_list({17, 27}, acc);
        break;
      case 23:
        execute_order_list({17, 27}, acc);
        break;
      case 24:
        execute_order_list({25, 27}, acc);
        break;
      case 25:
        execute_order_list({29}, acc);
        break;
      case 26:
        execute_order_list({22}, acc);
        break;
      case 27:
        execute_order_list({25, 27}, acc);
        break;
      case 28:
        execute_order_list({25, 27}, acc);
        break;
      case 29:
        execute_order_list({26, 29}, acc);
        break;
      case 30:
        execute_order_list({25, 27}, acc);
        break;
      case 31:
        execute_order_list({26, 29}, acc);
        break;
      case 32:
        execute_order_list({23}, acc);
        break;
      case 33:
        execute_order_list({27}, acc);
        break;
      case 34:
        execute_order_list({22, 26, 29}, acc);
        break;
      case 36:
        execute_order_list({27}, acc);
        break;
      case 37:
        if (branch_l) {
          execute_order_list({17, 22, 27}, acc);
        } else {
          execute_order_list({22}, acc);
        }
        break;
      case 38:
        execute_order_list({23}, acc);
        break;
      case 39:
        execute_order_list({27}, acc);
        break;
      case 40:
        if (branch_l) {
          execute_order_list({17, 22, 27}, acc);
        } else {
          execute_order_list({22}, acc);
        }
        break;
      case 41:
        execute_order_list({27}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void tick_ik1303_2b53a6_decoded() {
    static constexpr uint32_t kCommand = 0x2b53a6;
    if (tick_index == 0) command = kCommand;

    const int current_tick = tick_index;
    const bool branch_l = l != 0;
    if (current_tick < 27) {
      sync_address = 38;
    } else if (current_tick < 36) {
      sync_address = 39;
    } else {
      if (current_tick == 36) {
        r[37] = 13 & 0xF;
        r[40] = 173 >> 4;
      }
      sync_address = 95;
    }

    Accumulator acc;
    execute_2b53a6_pre_add_orders(current_tick, branch_l, acc);

    if (key_y == 0) t = 0;

    execute_2b53a6_gamma_orders(current_tick, branch_l, acc);

    int sum = acc.alpha + acc.beta + acc.gamma;
    acc.sigma = sum & 0xF;
    carry = (sum >> 4) & 1;

    execute_2b53a6_post_add_orders(current_tick, branch_l, acc);

    output = memory[current_tick];
    memory[current_tick] = input;
    ++tick_index;
    if (tick_index == 42) tick_index = 0;
  }


  void execute_0c5084_pre_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 0:
        execute_order_list({2}, acc);
        break;
      case 1:
        execute_order_list({5}, acc);
        break;
      case 2:
        execute_order_list({8}, acc);
        break;
      case 3:
        execute_order_list({2}, acc);
        break;
      case 4:
        execute_order_list({5}, acc);
        break;
      case 5:
        execute_order_list({0}, acc);
        break;
      case 6:
        execute_order_list({2}, acc);
        break;
      case 7:
        execute_order_list({5}, acc);
        break;
      case 8:
        execute_order_list({0}, acc);
        break;
      case 9:
        execute_order_list({2}, acc);
        break;
      case 10:
        execute_order_list({5}, acc);
        break;
      case 11:
        execute_order_list({0}, acc);
        break;
      case 12:
        execute_order_list({2}, acc);
        break;
      case 13:
        execute_order_list({5}, acc);
        break;
      case 14:
        execute_order_list({0}, acc);
        break;
      case 15:
        execute_order_list({2}, acc);
        break;
      case 16:
        execute_order_list({5}, acc);
        break;
      case 17:
        execute_order_list({0}, acc);
        break;
      case 18:
        execute_order_list({2}, acc);
        break;
      case 19:
        execute_order_list({5}, acc);
        break;
      case 20:
        execute_order_list({0}, acc);
        break;
      case 21:
        execute_order_list({2}, acc);
        break;
      case 22:
        execute_order_list({5}, acc);
        break;
      case 23:
        execute_order_list({0}, acc);
        break;
      case 24:
        execute_order_list({2}, acc);
        break;
      case 25:
        execute_order_list({5}, acc);
        break;
      case 26:
        execute_order_list({7, 8}, acc);
        break;
      case 27:
        execute_order_list({0}, acc);
        break;
      case 28:
        execute_order_list({9}, acc);
        break;
      case 29:
        execute_order_list({5}, acc);
        break;
      case 30:
        execute_order_list({0}, acc);
        break;
      case 31:
        execute_order_list({0}, acc);
        break;
      case 33:
        execute_order_list({0}, acc);
        break;
      case 34:
        execute_order_list({0}, acc);
        break;
      case 35:
        execute_order_list({7, 8}, acc);
        break;
      case 36:
        execute_order_list({0, 11}, acc);
        break;
      case 37:
        if (branch_l) {
          execute_order_list({0}, acc);
        } else {
          execute_order_list({5}, acc);
        }
        break;
      case 38:
        execute_order_list({5, 11}, acc);
        break;
      case 39:
        execute_order_list({0}, acc);
        break;
      case 40:
        if (branch_l) {
          execute_order_list({0}, acc);
        } else {
          execute_order_list({5}, acc);
        }
        break;
      case 41:
        execute_order_list({0}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void execute_0c5084_gamma_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 2:
        execute_order_list({12, 13}, acc);
        break;
      case 26:
        execute_order_list({12, 13}, acc);
        break;
      case 35:
        execute_order_list({12, 13}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void execute_0c5084_post_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 0:
        execute_order_list({27, 33}, acc);
        break;
      case 1:
        execute_order_list({22}, acc);
        break;
      case 2:
        execute_order_list({25}, acc);
        break;
      case 3:
        execute_order_list({27, 33}, acc);
        break;
      case 4:
        execute_order_list({22}, acc);
        break;
      case 5:
        execute_order_list({27}, acc);
        break;
      case 6:
        execute_order_list({27, 33}, acc);
        break;
      case 7:
        execute_order_list({22}, acc);
        break;
      case 8:
        execute_order_list({27}, acc);
        break;
      case 9:
        execute_order_list({27, 33}, acc);
        break;
      case 10:
        execute_order_list({22}, acc);
        break;
      case 11:
        execute_order_list({27}, acc);
        break;
      case 12:
        execute_order_list({27, 33}, acc);
        break;
      case 13:
        execute_order_list({22}, acc);
        break;
      case 14:
        execute_order_list({27}, acc);
        break;
      case 15:
        execute_order_list({27, 33}, acc);
        break;
      case 16:
        execute_order_list({22}, acc);
        break;
      case 17:
        execute_order_list({27}, acc);
        break;
      case 18:
        execute_order_list({27, 33}, acc);
        break;
      case 19:
        execute_order_list({22}, acc);
        break;
      case 20:
        execute_order_list({27}, acc);
        break;
      case 21:
        execute_order_list({27, 33}, acc);
        break;
      case 22:
        execute_order_list({22}, acc);
        break;
      case 23:
        execute_order_list({27}, acc);
        break;
      case 24:
        execute_order_list({27, 33}, acc);
        break;
      case 25:
        execute_order_list({22}, acc);
        break;
      case 26:
        execute_order_list({25, 27}, acc);
        break;
      case 27:
        execute_order_list({29}, acc);
        break;
      case 28:
        execute_order_list({16}, acc);
        break;
      case 29:
        execute_order_list({26, 29}, acc);
        break;
      case 30:
        execute_order_list({27}, acc);
        break;
      case 31:
        execute_order_list({17, 27}, acc);
        break;
      case 33:
        execute_order_list({27}, acc);
        break;
      case 34:
        execute_order_list({17, 27}, acc);
        break;
      case 35:
        execute_order_list({25, 27}, acc);
        break;
      case 36:
        execute_order_list({27}, acc);
        break;
      case 37:
        if (branch_l) {
          execute_order_list({17, 22, 27}, acc);
        } else {
          execute_order_list({22}, acc);
        }
        break;
      case 38:
        execute_order_list({23}, acc);
        break;
      case 39:
        execute_order_list({27}, acc);
        break;
      case 40:
        if (branch_l) {
          execute_order_list({17, 22, 27}, acc);
        } else {
          execute_order_list({22}, acc);
        }
        break;
      case 41:
        execute_order_list({27}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void tick_ik1303_0c5084_decoded() {
    static constexpr uint32_t kCommand = 0x0c5084;
    if (tick_index == 0) command = kCommand;

    const int current_tick = tick_index;
    const bool branch_l = l != 0;
    if (current_tick < 27) {
      sync_address = 4;
    } else if (current_tick < 36) {
      sync_address = 33;
    } else {
      if (current_tick == 36) {
        r[37] = 1 & 0xF;
        r[40] = 49 >> 4;
      }
      sync_address = 95;
    }

    Accumulator acc;
    execute_0c5084_pre_add_orders(current_tick, branch_l, acc);

    if (key_y == 0) t = 0;

    execute_0c5084_gamma_orders(current_tick, branch_l, acc);

    int sum = acc.alpha + acc.beta + acc.gamma;
    acc.sigma = sum & 0xF;
    carry = (sum >> 4) & 1;

    execute_0c5084_post_add_orders(current_tick, branch_l, acc);

    output = memory[current_tick];
    memory[current_tick] = input;
    ++tick_index;
    if (tick_index == 42) tick_index = 0;
  }


  void execute_046d2f_pre_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 0:
        execute_order_list({3}, acc);
        break;
      case 1:
        execute_order_list({0, 7}, acc);
        break;
      case 2:
        execute_order_list({4, 7}, acc);
        break;
      case 3:
        execute_order_list({3}, acc);
        break;
      case 4:
        execute_order_list({0, 7}, acc);
        break;
      case 5:
        execute_order_list({4, 7}, acc);
        break;
      case 6:
        execute_order_list({3}, acc);
        break;
      case 7:
        execute_order_list({0, 7}, acc);
        break;
      case 8:
        execute_order_list({4, 7}, acc);
        break;
      case 9:
        execute_order_list({3}, acc);
        break;
      case 10:
        execute_order_list({0, 7}, acc);
        break;
      case 11:
        execute_order_list({4, 7}, acc);
        break;
      case 12:
        execute_order_list({3}, acc);
        break;
      case 13:
        execute_order_list({0, 7}, acc);
        break;
      case 14:
        execute_order_list({4, 7}, acc);
        break;
      case 15:
        execute_order_list({3}, acc);
        break;
      case 16:
        execute_order_list({0, 7}, acc);
        break;
      case 17:
        execute_order_list({4, 7}, acc);
        break;
      case 18:
        execute_order_list({3}, acc);
        break;
      case 19:
        execute_order_list({0, 7}, acc);
        break;
      case 20:
        execute_order_list({4, 7}, acc);
        break;
      case 21:
        execute_order_list({3}, acc);
        break;
      case 22:
        execute_order_list({0, 7}, acc);
        break;
      case 23:
        execute_order_list({4, 7}, acc);
        break;
      case 24:
        execute_order_list({3}, acc);
        break;
      case 25:
        execute_order_list({0, 7}, acc);
        break;
      case 26:
        execute_order_list({4, 7}, acc);
        break;
      case 28:
        execute_order_list({9}, acc);
        break;
      case 29:
        execute_order_list({9}, acc);
        break;
      case 30:
        execute_order_list({9}, acc);
        break;
      case 31:
        execute_order_list({9}, acc);
        break;
      case 32:
        execute_order_list({0}, acc);
        break;
      case 33:
        execute_order_list({5}, acc);
        break;
      case 34:
        execute_order_list({5, 11}, acc);
        break;
      case 35:
        execute_order_list({9}, acc);
        break;
      case 36:
        execute_order_list({5}, acc);
        break;
      case 37:
        execute_order_list({5}, acc);
        break;
      case 38:
        execute_order_list({4, 7, 8}, acc);
        break;
      case 39:
        execute_order_list({0, 7}, acc);
        break;
      case 40:
        execute_order_list({10}, acc);
        break;
      case 41:
        execute_order_list({5}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void execute_046d2f_gamma_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 1:
        execute_order_list({12, 13}, acc);
        break;
      case 4:
        execute_order_list({12}, acc);
        break;
      case 7:
        execute_order_list({12}, acc);
        break;
      case 10:
        execute_order_list({12}, acc);
        break;
      case 13:
        execute_order_list({12}, acc);
        break;
      case 16:
        execute_order_list({12}, acc);
        break;
      case 19:
        execute_order_list({12}, acc);
        break;
      case 22:
        execute_order_list({12}, acc);
        break;
      case 25:
        execute_order_list({12}, acc);
        break;
      case 28:
        execute_order_list({13}, acc);
        break;
      case 29:
        execute_order_list({13}, acc);
        break;
      case 30:
        execute_order_list({13}, acc);
        break;
      case 31:
        execute_order_list({13}, acc);
        break;
      case 33:
        execute_order_list({12}, acc);
        break;
      case 35:
        execute_order_list({13}, acc);
        break;
      case 36:
        execute_order_list({12}, acc);
        break;
      case 38:
        execute_order_list({12}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void execute_046d2f_post_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 0:
        execute_order_list({27}, acc);
        break;
      case 1:
        execute_order_list({25, 27}, acc);
        break;
      case 2:
        execute_order_list({22, 26, 29}, acc);
        break;
      case 3:
        execute_order_list({27}, acc);
        break;
      case 4:
        execute_order_list({25, 27}, acc);
        break;
      case 5:
        execute_order_list({22, 26, 29}, acc);
        break;
      case 6:
        execute_order_list({27}, acc);
        break;
      case 7:
        execute_order_list({25, 27}, acc);
        break;
      case 8:
        execute_order_list({22, 26, 29}, acc);
        break;
      case 9:
        execute_order_list({27}, acc);
        break;
      case 10:
        execute_order_list({25, 27}, acc);
        break;
      case 11:
        execute_order_list({22, 26, 29}, acc);
        break;
      case 12:
        execute_order_list({27}, acc);
        break;
      case 13:
        execute_order_list({25, 27}, acc);
        break;
      case 14:
        execute_order_list({22, 26, 29}, acc);
        break;
      case 15:
        execute_order_list({27}, acc);
        break;
      case 16:
        execute_order_list({25, 27}, acc);
        break;
      case 17:
        execute_order_list({22, 26, 29}, acc);
        break;
      case 18:
        execute_order_list({27}, acc);
        break;
      case 19:
        execute_order_list({25, 27}, acc);
        break;
      case 20:
        execute_order_list({22, 26, 29}, acc);
        break;
      case 21:
        execute_order_list({27}, acc);
        break;
      case 22:
        execute_order_list({25, 27}, acc);
        break;
      case 23:
        execute_order_list({22, 26, 29}, acc);
        break;
      case 24:
        execute_order_list({27}, acc);
        break;
      case 25:
        execute_order_list({25, 27}, acc);
        break;
      case 26:
        execute_order_list({22, 26, 29}, acc);
        break;
      case 27:
        execute_order_list({29}, acc);
        break;
      case 28:
        execute_order_list({29}, acc);
        break;
      case 29:
        execute_order_list({29}, acc);
        break;
      case 30:
        execute_order_list({29}, acc);
        break;
      case 31:
        execute_order_list({29}, acc);
        break;
      case 32:
        execute_order_list({27}, acc);
        break;
      case 33:
        execute_order_list({22, 27}, acc);
        break;
      case 34:
        execute_order_list({25, 27}, acc);
        break;
      case 35:
        execute_order_list({29}, acc);
        break;
      case 36:
        execute_order_list({22, 27}, acc);
        break;
      case 37:
        execute_order_list({26, 29}, acc);
        break;
      case 38:
        execute_order_list({16}, acc);
        break;
      case 39:
        execute_order_list({16, 27}, acc);
        break;
      case 40:
        execute_order_list({27}, acc);
        break;
      case 41:
        execute_order_list({22}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void tick_ik1303_046d2f_decoded() {
    static constexpr uint32_t kCommand = 0x046d2f;
    if (tick_index == 0) command = kCommand;

    const int current_tick = tick_index;
    const bool branch_l = l != 0;
    if (current_tick < 27) {
      sync_address = 47;
    } else if (current_tick < 36) {
      sync_address = 90;
    } else {
      sync_address = 17;
    }

    Accumulator acc;
    execute_046d2f_pre_add_orders(current_tick, branch_l, acc);

    if (key_y == 0) t = 0;

    execute_046d2f_gamma_orders(current_tick, branch_l, acc);

    int sum = acc.alpha + acc.beta + acc.gamma;
    acc.sigma = sum & 0xF;
    carry = (sum >> 4) & 1;

    execute_046d2f_post_add_orders(current_tick, branch_l, acc);

    output = memory[current_tick];
    memory[current_tick] = input;
    ++tick_index;
    if (tick_index == 42) tick_index = 0;
  }


  void execute_691122_pre_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 0:
        execute_order_list({0}, acc);
        break;
      case 2:
        execute_order_list({9}, acc);
        break;
      case 3:
        execute_order_list({0}, acc);
        break;
      case 6:
        execute_order_list({0}, acc);
        break;
      case 9:
        execute_order_list({0}, acc);
        break;
      case 12:
        execute_order_list({0}, acc);
        break;
      case 15:
        execute_order_list({0}, acc);
        break;
      case 18:
        execute_order_list({0}, acc);
        break;
      case 21:
        execute_order_list({0}, acc);
        break;
      case 24:
        execute_order_list({0}, acc);
        break;
      case 26:
        execute_order_list({7, 8}, acc);
        break;
      case 27:
        execute_order_list({0}, acc);
        break;
      case 29:
        execute_order_list({9}, acc);
        break;
      case 30:
        execute_order_list({0}, acc);
        break;
      case 33:
        execute_order_list({0}, acc);
        break;
      case 35:
        execute_order_list({7, 8}, acc);
        break;
      case 36:
        execute_order_list({0, 11}, acc);
        break;
      case 37:
        if (branch_l) {
          execute_order_list({0}, acc);
        } else {
          execute_order_list({5}, acc);
        }
        break;
      case 38:
        execute_order_list({5, 11}, acc);
        break;
      case 39:
        execute_order_list({0}, acc);
        break;
      case 40:
        if (branch_l) {
          execute_order_list({0}, acc);
        } else {
          execute_order_list({5}, acc);
        }
        break;
      case 41:
        execute_order_list({0}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void execute_691122_gamma_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 26:
        execute_order_list({12, 13}, acc);
        break;
      case 35:
        execute_order_list({12, 13}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void execute_691122_post_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 0:
        execute_order_list({32}, acc);
        break;
      case 3:
        execute_order_list({32}, acc);
        break;
      case 5:
        execute_order_list({29}, acc);
        break;
      case 6:
        execute_order_list({32}, acc);
        break;
      case 8:
        execute_order_list({29}, acc);
        break;
      case 9:
        execute_order_list({32}, acc);
        break;
      case 11:
        execute_order_list({29}, acc);
        break;
      case 12:
        execute_order_list({32}, acc);
        break;
      case 14:
        execute_order_list({29}, acc);
        break;
      case 15:
        execute_order_list({32}, acc);
        break;
      case 17:
        execute_order_list({29}, acc);
        break;
      case 18:
        execute_order_list({32}, acc);
        break;
      case 20:
        execute_order_list({29}, acc);
        break;
      case 21:
        execute_order_list({32}, acc);
        break;
      case 23:
        execute_order_list({29}, acc);
        break;
      case 24:
        execute_order_list({32}, acc);
        break;
      case 26:
        execute_order_list({25, 27}, acc);
        break;
      case 27:
        execute_order_list({32}, acc);
        break;
      case 30:
        execute_order_list({32}, acc);
        break;
      case 32:
        execute_order_list({29}, acc);
        break;
      case 33:
        execute_order_list({32}, acc);
        break;
      case 35:
        execute_order_list({25, 27}, acc);
        break;
      case 36:
        execute_order_list({27}, acc);
        break;
      case 37:
        if (branch_l) {
          execute_order_list({17, 22, 27}, acc);
        } else {
          execute_order_list({22}, acc);
        }
        break;
      case 38:
        execute_order_list({23}, acc);
        break;
      case 39:
        execute_order_list({27}, acc);
        break;
      case 40:
        if (branch_l) {
          execute_order_list({17, 22, 27}, acc);
        } else {
          execute_order_list({22}, acc);
        }
        break;
      case 41:
        execute_order_list({27}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void tick_ik1303_691122_decoded() {
    static constexpr uint32_t kCommand = 0x691122;
    if (tick_index == 0) command = kCommand;

    const int current_tick = tick_index;
    const bool branch_l = l != 0;
    if (current_tick < 27) {
      sync_address = 34;
    } else if (current_tick < 36) {
      sync_address = 34;
    } else {
      if (current_tick == 36) {
        r[37] = 4 & 0xF;
        r[40] = 164 >> 4;
      }
      sync_address = 95;
    }

    Accumulator acc;
    execute_691122_pre_add_orders(current_tick, branch_l, acc);

    if (key_y == 0) t = 0;

    execute_691122_gamma_orders(current_tick, branch_l, acc);

    int sum = acc.alpha + acc.beta + acc.gamma;
    acc.sigma = sum & 0xF;
    carry = (sum >> 4) & 1;

    execute_691122_post_add_orders(current_tick, branch_l, acc);

    output = memory[current_tick];
    memory[current_tick] = input;
    ++tick_index;
    if (tick_index == 42) tick_index = 0;
  }


  void execute_0b9020_pre_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 0:
        execute_order_list({0}, acc);
        break;
      case 1:
        execute_order_list({0}, acc);
        break;
      case 2:
        execute_order_list({0, 3, 9}, acc);
        break;
      case 3:
        execute_order_list({0}, acc);
        break;
      case 4:
        execute_order_list({0}, acc);
        break;
      case 6:
        execute_order_list({0}, acc);
        break;
      case 7:
        execute_order_list({0}, acc);
        break;
      case 9:
        execute_order_list({0}, acc);
        break;
      case 10:
        execute_order_list({0}, acc);
        break;
      case 12:
        execute_order_list({0}, acc);
        break;
      case 13:
        execute_order_list({0}, acc);
        break;
      case 15:
        execute_order_list({0}, acc);
        break;
      case 16:
        execute_order_list({0}, acc);
        break;
      case 18:
        execute_order_list({0}, acc);
        break;
      case 19:
        execute_order_list({0}, acc);
        break;
      case 21:
        execute_order_list({0}, acc);
        break;
      case 22:
        execute_order_list({0}, acc);
        break;
      case 24:
        execute_order_list({0}, acc);
        break;
      case 25:
        execute_order_list({0}, acc);
        break;
      case 26:
        execute_order_list({0, 3, 9}, acc);
        break;
      case 27:
        execute_order_list({0}, acc);
        break;
      case 28:
        execute_order_list({0}, acc);
        break;
      case 29:
        execute_order_list({0, 3, 9}, acc);
        break;
      case 30:
        execute_order_list({0}, acc);
        break;
      case 31:
        execute_order_list({0}, acc);
        break;
      case 33:
        execute_order_list({0}, acc);
        break;
      case 34:
        execute_order_list({0}, acc);
        break;
      case 35:
        execute_order_list({0, 3, 9}, acc);
        break;
      case 36:
        execute_order_list({0, 11}, acc);
        break;
      case 37:
        if (branch_l) {
          execute_order_list({0}, acc);
        } else {
          execute_order_list({5}, acc);
        }
        break;
      case 38:
        execute_order_list({5, 11}, acc);
        break;
      case 39:
        execute_order_list({0}, acc);
        break;
      case 40:
        if (branch_l) {
          execute_order_list({0}, acc);
        } else {
          execute_order_list({5}, acc);
        }
        break;
      case 41:
        execute_order_list({0}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void execute_0b9020_gamma_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      default:
        break;
    }
    (void)branch_l;
  }

  void execute_0b9020_post_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 0:
        execute_order_list({27}, acc);
        break;
      case 1:
        execute_order_list({17, 22}, acc);
        break;
      case 2:
        execute_order_list({25, 29}, acc);
        break;
      case 3:
        execute_order_list({27}, acc);
        break;
      case 4:
        execute_order_list({17, 22}, acc);
        break;
      case 6:
        execute_order_list({27}, acc);
        break;
      case 7:
        execute_order_list({17, 22}, acc);
        break;
      case 9:
        execute_order_list({27}, acc);
        break;
      case 10:
        execute_order_list({17, 22}, acc);
        break;
      case 12:
        execute_order_list({27}, acc);
        break;
      case 13:
        execute_order_list({17, 22}, acc);
        break;
      case 15:
        execute_order_list({27}, acc);
        break;
      case 16:
        execute_order_list({17, 22}, acc);
        break;
      case 18:
        execute_order_list({27}, acc);
        break;
      case 19:
        execute_order_list({17, 22}, acc);
        break;
      case 21:
        execute_order_list({27}, acc);
        break;
      case 22:
        execute_order_list({17, 22}, acc);
        break;
      case 24:
        execute_order_list({27}, acc);
        break;
      case 25:
        execute_order_list({17, 22}, acc);
        break;
      case 26:
        execute_order_list({25, 29}, acc);
        break;
      case 27:
        execute_order_list({27}, acc);
        break;
      case 28:
        execute_order_list({17, 22}, acc);
        break;
      case 29:
        execute_order_list({25, 29}, acc);
        break;
      case 30:
        execute_order_list({27}, acc);
        break;
      case 31:
        execute_order_list({17, 22}, acc);
        break;
      case 33:
        execute_order_list({27}, acc);
        break;
      case 34:
        execute_order_list({17, 22}, acc);
        break;
      case 35:
        execute_order_list({25, 29}, acc);
        break;
      case 36:
        execute_order_list({27}, acc);
        break;
      case 37:
        if (branch_l) {
          execute_order_list({17, 22, 27}, acc);
        } else {
          execute_order_list({22}, acc);
        }
        break;
      case 38:
        execute_order_list({23}, acc);
        break;
      case 39:
        execute_order_list({27}, acc);
        break;
      case 40:
        if (branch_l) {
          execute_order_list({17, 22, 27}, acc);
        } else {
          execute_order_list({22}, acc);
        }
        break;
      case 41:
        execute_order_list({27}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void tick_ik1303_0b9020_decoded() {
    static constexpr uint32_t kCommand = 0x0b9020;
    if (tick_index == 0) command = kCommand;

    const int current_tick = tick_index;
    const bool branch_l = l != 0;
    if (current_tick < 27) {
      sync_address = 32;
    } else if (current_tick < 36) {
      sync_address = 32;
    } else {
      if (current_tick == 36) {
        r[37] = 14 & 0xF;
        r[40] = 46 >> 4;
      }
      sync_address = 95;
    }

    Accumulator acc;
    execute_0b9020_pre_add_orders(current_tick, branch_l, acc);

    if (key_y == 0) t = 0;

    execute_0b9020_gamma_orders(current_tick, branch_l, acc);

    int sum = acc.alpha + acc.beta + acc.gamma;
    acc.sigma = sum & 0xF;
    carry = (sum >> 4) & 1;

    execute_0b9020_post_add_orders(current_tick, branch_l, acc);

    output = memory[current_tick];
    memory[current_tick] = input;
    ++tick_index;
    if (tick_index == 42) tick_index = 0;
  }


  void execute_482848_pre_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 0:
        execute_order_list({3, 11}, acc);
        break;
      case 1:
        execute_order_list({4, 7}, acc);
        break;
      case 3:
        execute_order_list({3}, acc);
        break;
      case 4:
        execute_order_list({4, 7}, acc);
        break;
      case 6:
        execute_order_list({3}, acc);
        break;
      case 7:
        execute_order_list({4, 7}, acc);
        break;
      case 9:
        execute_order_list({3}, acc);
        break;
      case 10:
        execute_order_list({4, 7}, acc);
        break;
      case 12:
        execute_order_list({3}, acc);
        break;
      case 13:
        execute_order_list({4, 7}, acc);
        break;
      case 15:
        execute_order_list({3}, acc);
        break;
      case 16:
        execute_order_list({4, 7}, acc);
        break;
      case 18:
        execute_order_list({3}, acc);
        break;
      case 19:
        execute_order_list({4, 7}, acc);
        break;
      case 21:
        execute_order_list({3}, acc);
        break;
      case 22:
        execute_order_list({4, 7}, acc);
        break;
      case 24:
        execute_order_list({3}, acc);
        break;
      case 25:
        execute_order_list({4, 7}, acc);
        break;
      case 36:
        execute_order_list({0, 11}, acc);
        break;
      case 37:
        if (branch_l) {
          execute_order_list({0}, acc);
        } else {
          execute_order_list({5}, acc);
        }
        break;
      case 38:
        execute_order_list({5, 11}, acc);
        break;
      case 39:
        execute_order_list({0}, acc);
        break;
      case 40:
        if (branch_l) {
          execute_order_list({0}, acc);
        } else {
          execute_order_list({5}, acc);
        }
        break;
      case 41:
        execute_order_list({0}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void execute_482848_gamma_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 3:
        execute_order_list({12}, acc);
        break;
      case 6:
        execute_order_list({12}, acc);
        break;
      case 9:
        execute_order_list({12}, acc);
        break;
      case 12:
        execute_order_list({12}, acc);
        break;
      case 15:
        execute_order_list({12}, acc);
        break;
      case 18:
        execute_order_list({12}, acc);
        break;
      case 21:
        execute_order_list({12}, acc);
        break;
      case 24:
        execute_order_list({12}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void execute_482848_post_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 0:
        execute_order_list({25, 27}, acc);
        break;
      case 1:
        execute_order_list({26, 29}, acc);
        break;
      case 3:
        execute_order_list({25, 27}, acc);
        break;
      case 4:
        execute_order_list({26, 29}, acc);
        break;
      case 6:
        execute_order_list({25, 27}, acc);
        break;
      case 7:
        execute_order_list({26, 29}, acc);
        break;
      case 9:
        execute_order_list({25, 27}, acc);
        break;
      case 10:
        execute_order_list({26, 29}, acc);
        break;
      case 12:
        execute_order_list({25, 27}, acc);
        break;
      case 13:
        execute_order_list({26, 29}, acc);
        break;
      case 15:
        execute_order_list({25, 27}, acc);
        break;
      case 16:
        execute_order_list({26, 29}, acc);
        break;
      case 18:
        execute_order_list({25, 27}, acc);
        break;
      case 19:
        execute_order_list({26, 29}, acc);
        break;
      case 21:
        execute_order_list({25, 27}, acc);
        break;
      case 22:
        execute_order_list({26, 29}, acc);
        break;
      case 24:
        execute_order_list({25, 27}, acc);
        break;
      case 25:
        execute_order_list({26, 29}, acc);
        break;
      case 36:
        execute_order_list({27}, acc);
        break;
      case 37:
        if (branch_l) {
          execute_order_list({17, 22, 27}, acc);
        } else {
          execute_order_list({22}, acc);
        }
        break;
      case 38:
        execute_order_list({23}, acc);
        break;
      case 39:
        execute_order_list({27}, acc);
        break;
      case 40:
        if (branch_l) {
          execute_order_list({17, 22, 27}, acc);
        } else {
          execute_order_list({22}, acc);
        }
        break;
      case 41:
        execute_order_list({27}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void tick_ik1303_482848_decoded() {
    static constexpr uint32_t kCommand = 0x482848;
    if (tick_index == 0) command = kCommand;

    const int current_tick = tick_index;
    const bool branch_l = l != 0;
    if (current_tick < 27) {
      sync_address = 72;
    } else if (current_tick < 36) {
      sync_address = 80;
    } else {
      if (current_tick == 36) {
        r[37] = 0 & 0xF;
        r[40] = 32 >> 4;
      }
      sync_address = 95;
    }

    Accumulator acc;
    execute_482848_pre_add_orders(current_tick, branch_l, acc);

    if (key_y == 0) t = 0;

    execute_482848_gamma_orders(current_tick, branch_l, acc);

    int sum = acc.alpha + acc.beta + acc.gamma;
    acc.sigma = sum & 0xF;
    carry = (sum >> 4) & 1;

    execute_482848_post_add_orders(current_tick, branch_l, acc);

    output = memory[current_tick];
    memory[current_tick] = input;
    ++tick_index;
    if (tick_index == 42) tick_index = 0;
  }


  void execute_0b704c_pre_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 0:
        execute_order_list({6, 7, 8}, acc);
        break;
      case 1:
        execute_order_list({0, 7}, acc);
        break;
      case 2:
        execute_order_list({5, 7}, acc);
        break;
      case 3:
        execute_order_list({10}, acc);
        break;
      case 4:
        execute_order_list({0, 7}, acc);
        break;
      case 5:
        execute_order_list({4, 7}, acc);
        break;
      case 6:
        execute_order_list({10}, acc);
        break;
      case 7:
        execute_order_list({0, 7}, acc);
        break;
      case 8:
        execute_order_list({4, 7}, acc);
        break;
      case 9:
        execute_order_list({10}, acc);
        break;
      case 10:
        execute_order_list({0, 7}, acc);
        break;
      case 11:
        execute_order_list({4, 7}, acc);
        break;
      case 12:
        execute_order_list({10}, acc);
        break;
      case 13:
        execute_order_list({0, 7}, acc);
        break;
      case 14:
        execute_order_list({4, 7}, acc);
        break;
      case 15:
        execute_order_list({10}, acc);
        break;
      case 16:
        execute_order_list({0, 7}, acc);
        break;
      case 17:
        execute_order_list({4, 7}, acc);
        break;
      case 18:
        execute_order_list({10}, acc);
        break;
      case 19:
        execute_order_list({0, 7}, acc);
        break;
      case 20:
        execute_order_list({4, 7}, acc);
        break;
      case 21:
        execute_order_list({10}, acc);
        break;
      case 22:
        execute_order_list({0, 7}, acc);
        break;
      case 23:
        execute_order_list({4, 7}, acc);
        break;
      case 24:
        execute_order_list({10}, acc);
        break;
      case 25:
        execute_order_list({0, 7}, acc);
        break;
      case 26:
        execute_order_list({4, 7}, acc);
        break;
      case 35:
        execute_order_list({7, 8}, acc);
        break;
      case 36:
        execute_order_list({0, 11}, acc);
        break;
      case 37:
        if (branch_l) {
          execute_order_list({0}, acc);
        } else {
          execute_order_list({5}, acc);
        }
        break;
      case 38:
        execute_order_list({5, 11}, acc);
        break;
      case 39:
        execute_order_list({0}, acc);
        break;
      case 40:
        if (branch_l) {
          execute_order_list({0}, acc);
        } else {
          execute_order_list({5}, acc);
        }
        break;
      case 41:
        execute_order_list({0}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void execute_0b704c_gamma_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 2:
        execute_order_list({12}, acc);
        break;
      case 4:
        execute_order_list({12}, acc);
        break;
      case 7:
        execute_order_list({12}, acc);
        break;
      case 10:
        execute_order_list({12}, acc);
        break;
      case 13:
        execute_order_list({12}, acc);
        break;
      case 16:
        execute_order_list({12}, acc);
        break;
      case 19:
        execute_order_list({12}, acc);
        break;
      case 22:
        execute_order_list({12}, acc);
        break;
      case 25:
        execute_order_list({12}, acc);
        break;
      case 35:
        execute_order_list({12, 13}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void execute_0b704c_post_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 0:
        execute_order_list({27}, acc);
        break;
      case 1:
        execute_order_list({25, 27}, acc);
        break;
      case 2:
        execute_order_list({25, 27}, acc);
        break;
      case 3:
        execute_order_list({27}, acc);
        break;
      case 4:
        execute_order_list({25, 27}, acc);
        break;
      case 5:
        execute_order_list({22, 26, 29}, acc);
        break;
      case 6:
        execute_order_list({27}, acc);
        break;
      case 7:
        execute_order_list({25, 27}, acc);
        break;
      case 8:
        execute_order_list({22, 26, 29}, acc);
        break;
      case 9:
        execute_order_list({27}, acc);
        break;
      case 10:
        execute_order_list({25, 27}, acc);
        break;
      case 11:
        execute_order_list({22, 26, 29}, acc);
        break;
      case 12:
        execute_order_list({27}, acc);
        break;
      case 13:
        execute_order_list({25, 27}, acc);
        break;
      case 14:
        execute_order_list({22, 26, 29}, acc);
        break;
      case 15:
        execute_order_list({27}, acc);
        break;
      case 16:
        execute_order_list({25, 27}, acc);
        break;
      case 17:
        execute_order_list({22, 26, 29}, acc);
        break;
      case 18:
        execute_order_list({27}, acc);
        break;
      case 19:
        execute_order_list({25, 27}, acc);
        break;
      case 20:
        execute_order_list({22, 26, 29}, acc);
        break;
      case 21:
        execute_order_list({27}, acc);
        break;
      case 22:
        execute_order_list({25, 27}, acc);
        break;
      case 23:
        execute_order_list({22, 26, 29}, acc);
        break;
      case 24:
        execute_order_list({27}, acc);
        break;
      case 25:
        execute_order_list({25, 27}, acc);
        break;
      case 26:
        execute_order_list({22, 26, 29}, acc);
        break;
      case 35:
        execute_order_list({25, 27}, acc);
        break;
      case 36:
        execute_order_list({27}, acc);
        break;
      case 37:
        if (branch_l) {
          execute_order_list({17, 22, 27}, acc);
        } else {
          execute_order_list({22}, acc);
        }
        break;
      case 38:
        execute_order_list({23}, acc);
        break;
      case 39:
        execute_order_list({27}, acc);
        break;
      case 40:
        if (branch_l) {
          execute_order_list({17, 22, 27}, acc);
        } else {
          execute_order_list({22}, acc);
        }
        break;
      case 41:
        execute_order_list({27}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void tick_ik1303_0b704c_decoded() {
    static constexpr uint32_t kCommand = 0x0b704c;
    if (tick_index == 0) command = kCommand;

    const int current_tick = tick_index;
    const bool branch_l = l != 0;
    if (current_tick < 27) {
      sync_address = 76;
    } else if (current_tick < 36) {
      sync_address = 96;
    } else {
      if (current_tick == 36) {
        r[37] = 13 & 0xF;
        r[40] = 45 >> 4;
      }
      sync_address = 95;
    }

    Accumulator acc;
    execute_0b704c_pre_add_orders(current_tick, branch_l, acc);

    if (key_y == 0) t = 0;

    execute_0b704c_gamma_orders(current_tick, branch_l, acc);

    int sum = acc.alpha + acc.beta + acc.gamma;
    acc.sigma = sum & 0xF;
    carry = (sum >> 4) & 1;

    execute_0b704c_post_add_orders(current_tick, branch_l, acc);

    output = memory[current_tick];
    memory[current_tick] = input;
    ++tick_index;
    if (tick_index == 42) tick_index = 0;
  }


  void execute_43f7d0_pre_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 31:
        execute_order_list({0, 11}, acc);
        break;
      case 32:
        execute_order_list({5}, acc);
        break;
      case 33:
        execute_order_list({7, 8}, acc);
        break;
      case 34:
        execute_order_list({5}, acc);
        break;
      case 36:
        execute_order_list({0}, acc);
        break;
      case 37:
        execute_order_list({7, 8}, acc);
        break;
      case 38:
        execute_order_list({5, 11}, acc);
        break;
      case 39:
        execute_order_list({0}, acc);
        break;
      case 41:
        execute_order_list({0}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void execute_43f7d0_gamma_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 33:
        execute_order_list({12, 13}, acc);
        break;
      case 34:
        execute_order_list({12}, acc);
        break;
      case 37:
        execute_order_list({12, 13}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void execute_43f7d0_post_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 31:
        execute_order_list({27}, acc);
        break;
      case 33:
        execute_order_list({25, 27}, acc);
        break;
      case 34:
        execute_order_list({27}, acc);
        break;
      case 36:
        execute_order_list({17, 27}, acc);
        break;
      case 37:
        execute_order_list({25, 27}, acc);
        break;
      case 38:
        execute_order_list({25, 27}, acc);
        break;
      case 39:
        execute_order_list({17, 27}, acc);
        break;
      case 41:
        execute_order_list({27}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void tick_ik1303_43f7d0_decoded() {
    static constexpr uint32_t kCommand = 0x43f7d0;
    if (tick_index == 0) command = kCommand;

    const int current_tick = tick_index;
    const bool branch_l = l != 0;
    if (current_tick < 27) {
      sync_address = 80;
    } else if (current_tick < 36) {
      sync_address = 111;
    } else {
      sync_address = 15;
    }

    Accumulator acc;
    execute_43f7d0_pre_add_orders(current_tick, branch_l, acc);

    if (key_y == 0) t = 0;

    execute_43f7d0_gamma_orders(current_tick, branch_l, acc);

    int sum = acc.alpha + acc.beta + acc.gamma;
    acc.sigma = sum & 0xF;
    carry = (sum >> 4) & 1;

    execute_43f7d0_post_add_orders(current_tick, branch_l, acc);

    output = memory[current_tick];
    memory[current_tick] = input;
    ++tick_index;
    if (tick_index == 42) tick_index = 0;
  }


  void execute_05e6d0_pre_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 27:
        execute_order_list({3}, acc);
        break;
      case 28:
        execute_order_list({0, 7}, acc);
        break;
      case 30:
        execute_order_list({3}, acc);
        break;
      case 31:
        execute_order_list({0, 7}, acc);
        break;
      case 33:
        execute_order_list({3}, acc);
        break;
      case 34:
        execute_order_list({0, 7}, acc);
        break;
      case 35:
        execute_order_list({5, 10, 11}, acc);
        break;
      case 36:
        execute_order_list({0, 11}, acc);
        break;
      case 37:
        execute_order_list({5}, acc);
        break;
      case 38:
        execute_order_list({6, 7, 8}, acc);
        break;
      case 39:
        execute_order_list({5, 11}, acc);
        break;
      case 40:
        execute_order_list({3, 7}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void execute_05e6d0_gamma_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 28:
        execute_order_list({12, 13}, acc);
        break;
      case 31:
        execute_order_list({12}, acc);
        break;
      case 34:
        execute_order_list({12}, acc);
        break;
      case 35:
        execute_order_list({12, 13}, acc);
        break;
      case 37:
        execute_order_list({12}, acc);
        break;
      case 40:
        execute_order_list({12, 13}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void execute_05e6d0_post_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 27:
        execute_order_list({27}, acc);
        break;
      case 28:
        execute_order_list({25, 27}, acc);
        break;
      case 30:
        execute_order_list({27}, acc);
        break;
      case 31:
        execute_order_list({25, 27}, acc);
        break;
      case 33:
        execute_order_list({27}, acc);
        break;
      case 34:
        execute_order_list({25, 27}, acc);
        break;
      case 35:
        execute_order_list({25, 27}, acc);
        break;
      case 36:
        execute_order_list({27}, acc);
        break;
      case 37:
        execute_order_list({22, 27}, acc);
        break;
      case 38:
        execute_order_list({27}, acc);
        break;
      case 39:
        execute_order_list({25, 27}, acc);
        break;
      case 40:
        execute_order_list({25}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void tick_ik1303_05e6d0_decoded() {
    static constexpr uint32_t kCommand = 0x05e6d0;
    if (tick_index == 0) command = kCommand;

    const int current_tick = tick_index;
    const bool branch_l = l != 0;
    if (current_tick < 27) {
      sync_address = 80;
    } else if (current_tick < 36) {
      sync_address = 77;
    } else {
      sync_address = 23;
    }

    Accumulator acc;
    execute_05e6d0_pre_add_orders(current_tick, branch_l, acc);

    if (key_y == 0) t = 0;

    execute_05e6d0_gamma_orders(current_tick, branch_l, acc);

    int sum = acc.alpha + acc.beta + acc.gamma;
    acc.sigma = sum & 0xF;
    carry = (sum >> 4) & 1;

    execute_05e6d0_post_add_orders(current_tick, branch_l, acc);

    output = memory[current_tick];
    memory[current_tick] = input;
    ++tick_index;
    if (tick_index == 42) tick_index = 0;
  }


  void execute_ik1306_018040_pre_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 1:
        execute_order_list({1}, acc);
        break;
      case 4:
        execute_order_list({1}, acc);
        break;
      case 7:
        execute_order_list({1}, acc);
        break;
      case 10:
        execute_order_list({1}, acc);
        break;
      case 13:
        execute_order_list({1}, acc);
        break;
      case 16:
        execute_order_list({1}, acc);
        break;
      case 19:
        execute_order_list({1}, acc);
        break;
      case 22:
        execute_order_list({1}, acc);
        break;
      case 24:
        execute_order_list({5, 11}, acc);
        break;
      case 36:
        execute_order_list({0, 7}, acc);
        break;
      case 37:
        execute_order_list({5}, acc);
        break;
      case 39:
        execute_order_list({0}, acc);
        break;
      case 40:
        execute_order_list({5}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void execute_ik1306_018040_gamma_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 25:
        execute_order_list({12}, acc);
        break;
      case 39:
        execute_order_list({12}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void execute_ik1306_018040_post_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 1:
        execute_order_list({16, 27}, acc);
        break;
      case 4:
        execute_order_list({16, 27}, acc);
        break;
      case 7:
        execute_order_list({16, 27}, acc);
        break;
      case 10:
        execute_order_list({16, 27}, acc);
        break;
      case 13:
        execute_order_list({16, 27}, acc);
        break;
      case 16:
        execute_order_list({16, 27}, acc);
        break;
      case 19:
        execute_order_list({16, 27}, acc);
        break;
      case 22:
        execute_order_list({16, 27}, acc);
        break;
      case 24:
        execute_order_list({25, 27}, acc);
        break;
      case 25:
        execute_order_list({27}, acc);
        break;
      case 36:
        execute_order_list({25, 27}, acc);
        break;
      case 37:
        execute_order_list({22}, acc);
        break;
      case 39:
        execute_order_list({25, 27}, acc);
        break;
      case 40:
        execute_order_list({22}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void tick_ik1306_018040_decoded() {
    static constexpr uint32_t kCommand = 0x018040;
    if (tick_index == 0) command = kCommand;

    const int current_tick = tick_index;
    const bool branch_l = l != 0;
    if (current_tick < 27) {
      sync_address = 64;
    } else if (current_tick < 36) {
      sync_address = 0;
    } else {
      sync_address = 6;
    }

    Accumulator acc;
    execute_ik1306_018040_pre_add_orders(current_tick, branch_l, acc);

    if (key_y == 0) t = 0;

    execute_ik1306_018040_gamma_orders(current_tick, branch_l, acc);

    int sum = acc.alpha + acc.beta + acc.gamma;
    acc.sigma = sum & 0xF;
    carry = (sum >> 4) & 1;

    execute_ik1306_018040_post_add_orders(current_tick, branch_l, acc);

    output = memory[current_tick];
    memory[current_tick] = input;
    ++tick_index;
    if (tick_index == 42) tick_index = 0;
  }


  void execute_ik1306_01f52f_pre_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 1:
        execute_order_list({5}, acc);
        break;
      case 4:
        execute_order_list({5}, acc);
        break;
      case 7:
        execute_order_list({5}, acc);
        break;
      case 10:
        execute_order_list({5}, acc);
        break;
      case 13:
        execute_order_list({5}, acc);
        break;
      case 16:
        execute_order_list({5}, acc);
        break;
      case 19:
        execute_order_list({5}, acc);
        break;
      case 22:
        execute_order_list({5}, acc);
        break;
      case 25:
        execute_order_list({5}, acc);
        break;
      case 28:
        execute_order_list({1}, acc);
        break;
      case 29:
        execute_order_list({5}, acc);
        break;
      case 31:
        execute_order_list({1}, acc);
        break;
      case 32:
        execute_order_list({5}, acc);
        break;
      case 34:
        execute_order_list({1}, acc);
        break;
      case 35:
        execute_order_list({5}, acc);
        break;
      case 36:
        execute_order_list({0}, acc);
        break;
      case 37:
        execute_order_list({5, 11}, acc);
        break;
      case 38:
        execute_order_list({5}, acc);
        break;
      case 39:
        execute_order_list({0}, acc);
        break;
      case 40:
        execute_order_list({5}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void execute_ik1306_01f52f_gamma_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 39:
        execute_order_list({12}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void execute_ik1306_01f52f_post_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 0:
        execute_order_list({27}, acc);
        break;
      case 1:
        execute_order_list({16}, acc);
        break;
      case 3:
        execute_order_list({27}, acc);
        break;
      case 4:
        execute_order_list({16}, acc);
        break;
      case 6:
        execute_order_list({27}, acc);
        break;
      case 7:
        execute_order_list({16}, acc);
        break;
      case 9:
        execute_order_list({27}, acc);
        break;
      case 10:
        execute_order_list({16}, acc);
        break;
      case 12:
        execute_order_list({27}, acc);
        break;
      case 13:
        execute_order_list({16}, acc);
        break;
      case 15:
        execute_order_list({27}, acc);
        break;
      case 16:
        execute_order_list({16}, acc);
        break;
      case 18:
        execute_order_list({27}, acc);
        break;
      case 19:
        execute_order_list({16}, acc);
        break;
      case 21:
        execute_order_list({27}, acc);
        break;
      case 22:
        execute_order_list({16}, acc);
        break;
      case 24:
        execute_order_list({27}, acc);
        break;
      case 25:
        execute_order_list({16}, acc);
        break;
      case 28:
        execute_order_list({16, 27}, acc);
        break;
      case 29:
        execute_order_list({23}, acc);
        break;
      case 31:
        execute_order_list({16, 27}, acc);
        break;
      case 32:
        execute_order_list({23}, acc);
        break;
      case 34:
        execute_order_list({16, 27}, acc);
        break;
      case 35:
        execute_order_list({23}, acc);
        break;
      case 36:
        execute_order_list({27}, acc);
        break;
      case 37:
        execute_order_list({25, 27}, acc);
        break;
      case 38:
        execute_order_list({23}, acc);
        break;
      case 39:
        execute_order_list({25, 27}, acc);
        break;
      case 40:
        execute_order_list({22}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void tick_ik1306_01f52f_decoded() {
    static constexpr uint32_t kCommand = 0x01f52f;
    if (tick_index == 0) command = kCommand;

    const int current_tick = tick_index;
    const bool branch_l = l != 0;
    if (current_tick < 27) {
      sync_address = 47;
    } else if (current_tick < 36) {
      sync_address = 106;
    } else {
      sync_address = 7;
    }

    Accumulator acc;
    execute_ik1306_01f52f_pre_add_orders(current_tick, branch_l, acc);

    if (key_y == 0) t = 0;

    execute_ik1306_01f52f_gamma_orders(current_tick, branch_l, acc);

    int sum = acc.alpha + acc.beta + acc.gamma;
    acc.sigma = sum & 0xF;
    carry = (sum >> 4) & 1;

    execute_ik1306_01f52f_post_add_orders(current_tick, branch_l, acc);

    output = memory[current_tick];
    memory[current_tick] = input;
    ++tick_index;
    if (tick_index == 42) tick_index = 0;
  }


  void execute_ik1306_02e600_pre_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 30:
        execute_order_list({0}, acc);
        break;
      case 32:
        execute_order_list({5, 11}, acc);
        break;
      case 36:
        execute_order_list({0, 11}, acc);
        break;
      case 39:
        execute_order_list({0}, acc);
        break;
      case 40:
        execute_order_list({5}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void execute_ik1306_02e600_gamma_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 36:
        execute_order_list({13}, acc);
        break;
      case 39:
        execute_order_list({12}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void execute_ik1306_02e600_post_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 30:
        execute_order_list({27}, acc);
        break;
      case 32:
        execute_order_list({25, 27}, acc);
        break;
      case 36:
        execute_order_list({16, 25}, acc);
        break;
      case 39:
        execute_order_list({25, 27}, acc);
        break;
      case 40:
        execute_order_list({22}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void tick_ik1306_02e600_decoded() {
    static constexpr uint32_t kCommand = 0x02e600;
    if (tick_index == 0) command = kCommand;

    const int current_tick = tick_index;
    const bool branch_l = l != 0;
    if (current_tick < 27) {
      sync_address = 0;
    } else if (current_tick < 36) {
      sync_address = 76;
    } else {
      sync_address = 11;
    }

    Accumulator acc;
    execute_ik1306_02e600_pre_add_orders(current_tick, branch_l, acc);

    if (key_y == 0) t = 0;

    execute_ik1306_02e600_gamma_orders(current_tick, branch_l, acc);

    int sum = acc.alpha + acc.beta + acc.gamma;
    acc.sigma = sum & 0xF;
    carry = (sum >> 4) & 1;

    execute_ik1306_02e600_post_add_orders(current_tick, branch_l, acc);

    output = memory[current_tick];
    memory[current_tick] = input;
    ++tick_index;
    if (tick_index == 42) tick_index = 0;
  }


  void execute_ik1306_024000_pre_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 36:
        execute_order_list({11}, acc);
        break;
      case 37:
        execute_order_list({5}, acc);
        break;
      case 40:
        execute_order_list({5}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void execute_ik1306_024000_gamma_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      default:
        break;
    }
    (void)branch_l;
  }

  void execute_ik1306_024000_post_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 36:
        execute_order_list({27}, acc);
        break;
      case 37:
        execute_order_list({22}, acc);
        break;
      case 39:
        execute_order_list({27}, acc);
        break;
      case 40:
        execute_order_list({22}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void tick_ik1306_024000_decoded() {
    static constexpr uint32_t kCommand = 0x024000;
    if (tick_index == 0) command = kCommand;

    const int current_tick = tick_index;
    const bool branch_l = l != 0;
    if (current_tick < 27) {
      sync_address = 0;
    } else if (current_tick < 36) {
      sync_address = 0;
    } else {
      sync_address = 9;
    }

    Accumulator acc;
    execute_ik1306_024000_pre_add_orders(current_tick, branch_l, acc);

    if (key_y == 0) t = 0;

    execute_ik1306_024000_gamma_orders(current_tick, branch_l, acc);

    int sum = acc.alpha + acc.beta + acc.gamma;
    acc.sigma = sum & 0xF;
    carry = (sum >> 4) & 1;

    execute_ik1306_024000_post_add_orders(current_tick, branch_l, acc);

    output = memory[current_tick];
    memory[current_tick] = input;
    ++tick_index;
    if (tick_index == 42) tick_index = 0;
  }


  void execute_ik1306_02e680_pre_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 30:
        execute_order_list({0}, acc);
        break;
      case 31:
        execute_order_list({5, 11}, acc);
        break;
      case 32:
        execute_order_list({5, 11}, acc);
        break;
      case 33:
        execute_order_list({5, 11}, acc);
        break;
      case 34:
        execute_order_list({5, 11}, acc);
        break;
      case 36:
        execute_order_list({0, 11}, acc);
        break;
      case 39:
        execute_order_list({0}, acc);
        break;
      case 40:
        execute_order_list({5}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void execute_ik1306_02e680_gamma_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 36:
        execute_order_list({13}, acc);
        break;
      case 39:
        execute_order_list({12}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void execute_ik1306_02e680_post_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 30:
        execute_order_list({27}, acc);
        break;
      case 31:
        execute_order_list({25, 27}, acc);
        break;
      case 32:
        execute_order_list({25, 27}, acc);
        break;
      case 33:
        execute_order_list({25, 27}, acc);
        break;
      case 34:
        execute_order_list({25, 27}, acc);
        break;
      case 36:
        execute_order_list({16, 25}, acc);
        break;
      case 39:
        execute_order_list({25, 27}, acc);
        break;
      case 40:
        execute_order_list({22}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void tick_ik1306_02e680_decoded() {
    static constexpr uint32_t kCommand = 0x02e680;
    if (tick_index == 0) command = kCommand;

    const int current_tick = tick_index;
    const bool branch_l = l != 0;
    if (current_tick < 27) {
      sync_address = 0;
    } else if (current_tick < 36) {
      sync_address = 77;
    } else {
      sync_address = 11;
    }

    Accumulator acc;
    execute_ik1306_02e680_pre_add_orders(current_tick, branch_l, acc);

    if (key_y == 0) t = 0;

    execute_ik1306_02e680_gamma_orders(current_tick, branch_l, acc);

    int sum = acc.alpha + acc.beta + acc.gamma;
    acc.sigma = sum & 0xF;
    carry = (sum >> 4) & 1;

    execute_ik1306_02e680_post_add_orders(current_tick, branch_l, acc);

    output = memory[current_tick];
    memory[current_tick] = input;
    ++tick_index;
    if (tick_index == 42) tick_index = 0;
  }


  void execute_ik1302_0479e0_pre_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 4:
        execute_order_list({1}, acc);
        break;
      case 7:
        execute_order_list({1}, acc);
        break;
      case 10:
        execute_order_list({1}, acc);
        break;
      case 13:
        execute_order_list({1}, acc);
        break;
      case 16:
        execute_order_list({1}, acc);
        break;
      case 19:
        execute_order_list({1}, acc);
        break;
      case 22:
        execute_order_list({1}, acc);
        break;
      case 24:
        execute_order_list({5, 11}, acc);
        break;
      case 25:
        execute_order_list({6}, acc);
        break;
      case 26:
        execute_order_list({8}, acc);
        break;
      case 34:
        execute_order_list({10}, acc);
        break;
      case 35:
        execute_order_list({5, 11}, acc);
        break;
      case 36:
        execute_order_list({0}, acc);
        break;
      case 37:
        execute_order_list({5}, acc);
        break;
      case 38:
        execute_order_list({11}, acc);
        break;
      case 39:
        execute_order_list({5, 11}, acc);
        break;
      case 40:
        execute_order_list({8}, acc);
        break;
      case 41:
        execute_order_list({0, 7}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void execute_ik1302_0479e0_gamma_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 36:
        execute_order_list({12}, acc);
        break;
      case 38:
        execute_order_list({12, 13}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void execute_ik1302_0479e0_post_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 4:
        execute_order_list({27}, acc);
        break;
      case 7:
        execute_order_list({27}, acc);
        break;
      case 10:
        execute_order_list({27}, acc);
        break;
      case 13:
        execute_order_list({27}, acc);
        break;
      case 16:
        execute_order_list({27}, acc);
        break;
      case 19:
        execute_order_list({27}, acc);
        break;
      case 22:
        execute_order_list({27}, acc);
        break;
      case 24:
        execute_order_list({25, 27}, acc);
        break;
      case 25:
        execute_order_list({27}, acc);
        break;
      case 26:
        execute_order_list({27}, acc);
        break;
      case 30:
        execute_order_list({22}, acc);
        break;
      case 34:
        execute_order_list({27}, acc);
        break;
      case 35:
        execute_order_list({27}, acc);
        break;
      case 36:
        execute_order_list({27}, acc);
        break;
      case 37:
        execute_order_list({22}, acc);
        break;
      case 38:
        execute_order_list({27}, acc);
        break;
      case 39:
        execute_order_list({27}, acc);
        break;
      case 40:
        execute_order_list({27}, acc);
        break;
      case 41:
        execute_order_list({25, 27}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void tick_ik1302_0479e0_decoded() {
    static constexpr uint32_t kCommand = 0x0479e0;
    if (tick_index == 0) command = kCommand;

    const int current_tick = tick_index;
    const bool branch_l = l != 0;
    if (current_tick < 27) {
      sync_address = 96;
    } else if (current_tick < 36) {
      sync_address = 115;
    } else {
      sync_address = 17;
    }

    Accumulator acc;
    execute_ik1302_0479e0_pre_add_orders(current_tick, branch_l, acc);

    if (key_y == 0) t = 0;

    execute_ik1302_0479e0_gamma_orders(current_tick, branch_l, acc);

    int sum = acc.alpha + acc.beta + acc.gamma;
    acc.sigma = sum & 0xF;
    carry = (sum >> 4) & 1;

    execute_ik1302_0479e0_post_add_orders(current_tick, branch_l, acc);

    output = memory[current_tick];
    memory[current_tick] = input;
    ++tick_index;
    if (tick_index == 42) tick_index = 0;
  }


  void execute_ik1302_046440_pre_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 29:
        execute_order_list({0, 11}, acc);
        break;
      case 30:
        execute_order_list({5}, acc);
        break;
      case 31:
        execute_order_list({8}, acc);
        break;
      case 32:
        execute_order_list({0, 7}, acc);
        break;
      case 33:
        execute_order_list({5, 11}, acc);
        break;
      case 34:
        execute_order_list({5, 11}, acc);
        break;
      case 36:
        execute_order_list({0}, acc);
        break;
      case 37:
        execute_order_list({5}, acc);
        break;
      case 38:
        execute_order_list({11}, acc);
        break;
      case 39:
        execute_order_list({5, 11}, acc);
        break;
      case 40:
        execute_order_list({8}, acc);
        break;
      case 41:
        execute_order_list({0, 7}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void execute_ik1302_046440_gamma_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 32:
        execute_order_list({12, 13}, acc);
        break;
      case 36:
        execute_order_list({12}, acc);
        break;
      case 38:
        execute_order_list({12, 13}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void execute_ik1302_046440_post_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 29:
        execute_order_list({27}, acc);
        break;
      case 30:
        execute_order_list({22}, acc);
        break;
      case 31:
        execute_order_list({27}, acc);
        break;
      case 32:
        execute_order_list({25, 27}, acc);
        break;
      case 33:
        execute_order_list({25, 27}, acc);
        break;
      case 34:
        execute_order_list({27}, acc);
        break;
      case 36:
        execute_order_list({27}, acc);
        break;
      case 37:
        execute_order_list({22}, acc);
        break;
      case 38:
        execute_order_list({27}, acc);
        break;
      case 39:
        execute_order_list({27}, acc);
        break;
      case 40:
        execute_order_list({27}, acc);
        break;
      case 41:
        execute_order_list({25, 27}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void tick_ik1302_046440_decoded() {
    static constexpr uint32_t kCommand = 0x046440;
    if (tick_index == 0) command = kCommand;

    const int current_tick = tick_index;
    const bool branch_l = l != 0;
    if (current_tick < 27) {
      sync_address = 64;
    } else if (current_tick < 36) {
      sync_address = 72;
    } else {
      sync_address = 17;
    }

    Accumulator acc;
    execute_ik1302_046440_pre_add_orders(current_tick, branch_l, acc);

    if (key_y == 0) t = 0;

    execute_ik1302_046440_gamma_orders(current_tick, branch_l, acc);

    int sum = acc.alpha + acc.beta + acc.gamma;
    acc.sigma = sum & 0xF;
    carry = (sum >> 4) & 1;

    execute_ik1302_046440_post_add_orders(current_tick, branch_l, acc);

    output = memory[current_tick];
    memory[current_tick] = input;
    ++tick_index;
    if (tick_index == 42) tick_index = 0;
  }


  void execute_ik1302_410140_pre_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 27:
        execute_order_list({0, 7}, acc);
        break;
      case 28:
        execute_order_list({4, 7}, acc);
        break;
      case 29:
        execute_order_list({10}, acc);
        break;
      case 30:
        execute_order_list({0, 7}, acc);
        break;
      case 31:
        execute_order_list({4, 7}, acc);
        break;
      case 32:
        execute_order_list({10}, acc);
        break;
      case 33:
        execute_order_list({0, 7}, acc);
        break;
      case 34:
        execute_order_list({4, 7}, acc);
        break;
      case 35:
        execute_order_list({6}, acc);
        break;
      case 37:
        execute_order_list({5}, acc);
        break;
      case 38:
        execute_order_list({5, 8}, acc);
        break;
      case 39:
        execute_order_list({8}, acc);
        break;
      case 40:
        execute_order_list({4, 7}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void execute_ik1302_410140_gamma_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 27:
        execute_order_list({12, 13}, acc);
        break;
      case 30:
        execute_order_list({12}, acc);
        break;
      case 33:
        execute_order_list({12}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void execute_ik1302_410140_post_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 27:
        execute_order_list({25, 27}, acc);
        break;
      case 29:
        execute_order_list({27}, acc);
        break;
      case 30:
        execute_order_list({25, 27}, acc);
        break;
      case 32:
        execute_order_list({27}, acc);
        break;
      case 33:
        execute_order_list({25, 27}, acc);
        break;
      case 35:
        execute_order_list({27}, acc);
        break;
      case 37:
        execute_order_list({22}, acc);
        break;
      case 38:
        execute_order_list({27}, acc);
        break;
      case 39:
        execute_order_list({27}, acc);
        break;
      case 40:
        execute_order_list({25, 27}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void tick_ik1302_410140_decoded() {
    static constexpr uint32_t kCommand = 0x410140;
    if (tick_index == 0) command = kCommand;

    const int current_tick = tick_index;
    const bool branch_l = l != 0;
    if (current_tick < 27) {
      sync_address = 64;
    } else if (current_tick < 36) {
      sync_address = 2;
    } else {
      sync_address = 4;
    }

    Accumulator acc;
    execute_ik1302_410140_pre_add_orders(current_tick, branch_l, acc);

    if (key_y == 0) t = 0;

    execute_ik1302_410140_gamma_orders(current_tick, branch_l, acc);

    int sum = acc.alpha + acc.beta + acc.gamma;
    acc.sigma = sum & 0xF;
    carry = (sum >> 4) & 1;

    execute_ik1302_410140_post_add_orders(current_tick, branch_l, acc);

    output = memory[current_tick];
    memory[current_tick] = input;
    ++tick_index;
    if (tick_index == 42) tick_index = 0;
  }


  void execute_ik1302_05a040_pre_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 36:
        execute_order_list({11}, acc);
        break;
      case 37:
        execute_order_list({5, 10, 11}, acc);
        break;
      case 38:
        execute_order_list({5}, acc);
        break;
      case 39:
        execute_order_list({5, 11}, acc);
        break;
      case 40:
        execute_order_list({5}, acc);
        break;
      case 41:
        execute_order_list({5, 8}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void execute_ik1302_05a040_gamma_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 36:
        execute_order_list({12, 13}, acc);
        break;
      case 37:
        execute_order_list({12, 13}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void execute_ik1302_05a040_post_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 36:
        execute_order_list({27}, acc);
        break;
      case 37:
        execute_order_list({25, 27}, acc);
        break;
      case 38:
        execute_order_list({23}, acc);
        break;
      case 39:
        execute_order_list({27}, acc);
        break;
      case 40:
        execute_order_list({22}, acc);
        break;
      case 41:
        execute_order_list({27}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void tick_ik1302_05a040_decoded() {
    static constexpr uint32_t kCommand = 0x05a040;
    if (tick_index == 0) command = kCommand;

    const int current_tick = tick_index;
    const bool branch_l = l != 0;
    if (current_tick < 27) {
      sync_address = 64;
    } else if (current_tick < 36) {
      sync_address = 64;
    } else {
      sync_address = 22;
    }

    Accumulator acc;
    execute_ik1302_05a040_pre_add_orders(current_tick, branch_l, acc);

    if (key_y == 0) t = 0;

    execute_ik1302_05a040_gamma_orders(current_tick, branch_l, acc);

    int sum = acc.alpha + acc.beta + acc.gamma;
    acc.sigma = sum & 0xF;
    carry = (sum >> 4) & 1;

    execute_ik1302_05a040_post_add_orders(current_tick, branch_l, acc);

    output = memory[current_tick];
    memory[current_tick] = input;
    ++tick_index;
    if (tick_index == 42) tick_index = 0;
  }


  void execute_ik1302_06d940_pre_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 27:
        execute_order_list({10}, acc);
        break;
      case 28:
        execute_order_list({8}, acc);
        break;
      case 29:
        execute_order_list({5}, acc);
        break;
      case 32:
        execute_order_list({0}, acc);
        break;
      case 35:
        execute_order_list({0}, acc);
        break;
      case 36:
        execute_order_list({0, 11}, acc);
        break;
      case 37:
        execute_order_list({5, 8}, acc);
        break;
      case 38:
        execute_order_list({5, 11}, acc);
        break;
      case 39:
        execute_order_list({0}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void execute_ik1302_06d940_gamma_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 37:
        execute_order_list({12, 13}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void execute_ik1302_06d940_post_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 27:
        execute_order_list({27}, acc);
        break;
      case 28:
        execute_order_list({27}, acc);
        break;
      case 29:
        execute_order_list({26, 29}, acc);
        break;
      case 32:
        execute_order_list({15, 27}, acc);
        break;
      case 35:
        execute_order_list({17, 27}, acc);
        break;
      case 36:
        execute_order_list({27, 32}, acc);
        break;
      case 37:
        execute_order_list({25, 27}, acc);
        break;
      case 38:
        execute_order_list({23}, acc);
        break;
      case 39:
        execute_order_list({27, 32}, acc);
        break;
      case 40:
        execute_order_list({22}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void tick_ik1302_06d940_decoded() {
    static constexpr uint32_t kCommand = 0x06d940;
    if (tick_index == 0) command = kCommand;

    const int current_tick = tick_index;
    const bool branch_l = l != 0;
    if (current_tick < 27) {
      sync_address = 64;
    } else if (current_tick < 36) {
      sync_address = 50;
    } else {
      sync_address = 27;
    }

    Accumulator acc;
    execute_ik1302_06d940_pre_add_orders(current_tick, branch_l, acc);

    if (key_y == 0) t = 0;

    execute_ik1302_06d940_gamma_orders(current_tick, branch_l, acc);

    int sum = acc.alpha + acc.beta + acc.gamma;
    acc.sigma = sum & 0xF;
    carry = (sum >> 4) & 1;

    execute_ik1302_06d940_post_add_orders(current_tick, branch_l, acc);

    output = memory[current_tick];
    memory[current_tick] = input;
    ++tick_index;
    if (tick_index == 42) tick_index = 0;
  }


  void execute_ik1302_01a340_pre_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 30:
        execute_order_list({10}, acc);
        break;
      case 31:
        execute_order_list({0, 7}, acc);
        break;
      case 32:
        execute_order_list({4, 7}, acc);
        break;
      case 34:
        execute_order_list({0}, acc);
        break;
      case 35:
        execute_order_list({5}, acc);
        break;
      case 36:
        execute_order_list({0, 11}, acc);
        break;
      case 41:
        execute_order_list({5}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void execute_ik1302_01a340_gamma_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 31:
        execute_order_list({12, 13}, acc);
        break;
      case 34:
        execute_order_list({12}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void execute_ik1302_01a340_post_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 30:
        execute_order_list({27}, acc);
        break;
      case 31:
        execute_order_list({25, 27}, acc);
        break;
      case 32:
        execute_order_list({22}, acc);
        break;
      case 34:
        execute_order_list({27}, acc);
        break;
      case 35:
        execute_order_list({22}, acc);
        break;
      case 36:
        execute_order_list({16, 25}, acc);
        break;
      case 37:
        execute_order_list({15}, acc);
        break;
      case 41:
        execute_order_list({22}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void tick_ik1302_01a340_decoded() {
    static constexpr uint32_t kCommand = 0x01a340;
    if (tick_index == 0) command = kCommand;

    const int current_tick = tick_index;
    const bool branch_l = l != 0;
    if (current_tick < 27) {
      sync_address = 64;
    } else if (current_tick < 36) {
      sync_address = 70;
    } else {
      sync_address = 6;
    }

    Accumulator acc;
    execute_ik1302_01a340_pre_add_orders(current_tick, branch_l, acc);

    if (key_y == 0) t = 0;

    execute_ik1302_01a340_gamma_orders(current_tick, branch_l, acc);

    int sum = acc.alpha + acc.beta + acc.gamma;
    acc.sigma = sum & 0xF;
    carry = (sum >> 4) & 1;

    execute_ik1302_01a340_post_add_orders(current_tick, branch_l, acc);

    output = memory[current_tick];
    memory[current_tick] = input;
    ++tick_index;
    if (tick_index == 42) tick_index = 0;
  }


  void execute_ik1302_56e013_pre_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 0:
        execute_order_list({6}, acc);
        break;
      case 1:
        execute_order_list({8}, acc);
        break;
      case 2:
        execute_order_list({0, 7}, acc);
        break;
      case 3:
        execute_order_list({9}, acc);
        break;
      case 6:
        execute_order_list({9}, acc);
        break;
      case 9:
        execute_order_list({9}, acc);
        break;
      case 12:
        execute_order_list({9}, acc);
        break;
      case 15:
        execute_order_list({9}, acc);
        break;
      case 18:
        execute_order_list({9}, acc);
        break;
      case 21:
        execute_order_list({9}, acc);
        break;
      case 36:
        execute_order_list({0, 11}, acc);
        break;
      case 37:
        if (branch_l) {
          execute_order_list({0, 11}, acc);
        } else {
          execute_order_list({5}, acc);
        }
        break;
      case 38:
        execute_order_list({5, 11}, acc);
        break;
      case 39:
        execute_order_list({0}, acc);
        break;
      case 40:
        if (branch_l) {
          execute_order_list({0, 11}, acc);
        } else {
          execute_order_list({5}, acc);
        }
        break;
      case 41:
        execute_order_list({0}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void execute_ik1302_56e013_gamma_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      default:
        break;
    }
    (void)branch_l;
  }

  void execute_ik1302_56e013_post_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 0:
        execute_order_list({27}, acc);
        break;
      case 1:
        execute_order_list({27}, acc);
        break;
      case 2:
        execute_order_list({25, 27}, acc);
        break;
      case 4:
        execute_order_list({29}, acc);
        break;
      case 7:
        execute_order_list({29}, acc);
        break;
      case 10:
        execute_order_list({29}, acc);
        break;
      case 13:
        execute_order_list({29}, acc);
        break;
      case 16:
        execute_order_list({29}, acc);
        break;
      case 19:
        execute_order_list({29}, acc);
        break;
      case 22:
        execute_order_list({29}, acc);
        break;
      case 36:
        execute_order_list({27, 32}, acc);
        break;
      case 37:
        execute_order_list({22, 27}, acc);
        break;
      case 38:
        execute_order_list({23}, acc);
        break;
      case 39:
        execute_order_list({27, 32}, acc);
        break;
      case 40:
        execute_order_list({22, 27}, acc);
        break;
      case 41:
        execute_order_list({27}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void tick_ik1302_56e013_decoded() {
    static constexpr uint32_t kCommand = 0x56e013;
    if (tick_index == 0) command = kCommand;

    const int current_tick = tick_index;
    const bool branch_l = l != 0;
    if (current_tick < 27) {
      sync_address = 19;
    } else if (current_tick < 36) {
      sync_address = 64;
    } else {
      if (current_tick == 36) {
        r[37] = 11 & 0xF;
        r[40] = 91 >> 4;
      }
      sync_address = 95;
    }

    Accumulator acc;
    execute_ik1302_56e013_pre_add_orders(current_tick, branch_l, acc);

    if (key_y == 0) t = 0;

    execute_ik1302_56e013_gamma_orders(current_tick, branch_l, acc);

    int sum = acc.alpha + acc.beta + acc.gamma;
    acc.sigma = sum & 0xF;
    carry = (sum >> 4) & 1;

    execute_ik1302_56e013_post_add_orders(current_tick, branch_l, acc);

    output = memory[current_tick];
    memory[current_tick] = input;
    ++tick_index;
    if (tick_index == 42) tick_index = 0;
  }


  void execute_ik1302_000840_pre_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 27:
        execute_order_list({0, 7, 8}, acc);
        break;
      case 28:
        execute_order_list({5}, acc);
        break;
      case 29:
        execute_order_list({5, 8}, acc);
        break;
      case 30:
        execute_order_list({0}, acc);
        break;
      case 31:
        execute_order_list({5}, acc);
        break;
      case 32:
        execute_order_list({10}, acc);
        break;
      case 34:
        execute_order_list({8}, acc);
        break;
      case 35:
        execute_order_list({4, 7}, acc);
        break;
      case 39:
        execute_order_list({0, 7}, acc);
        break;
      case 40:
        execute_order_list({5}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void execute_ik1302_000840_gamma_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 31:
        execute_order_list({14}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void execute_ik1302_000840_post_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 27:
        execute_order_list({25, 27}, acc);
        break;
      case 28:
        execute_order_list({22}, acc);
        break;
      case 29:
        execute_order_list({27}, acc);
        break;
      case 30:
        execute_order_list({29}, acc);
        break;
      case 31:
        execute_order_list({25, 27}, acc);
        break;
      case 32:
        execute_order_list({27}, acc);
        break;
      case 33:
        execute_order_list({22}, acc);
        break;
      case 34:
        execute_order_list({27}, acc);
        break;
      case 35:
        execute_order_list({25, 27}, acc);
        break;
      case 39:
        execute_order_list({27}, acc);
        break;
      case 40:
        execute_order_list({22}, acc);
        break;
      case 41:
        execute_order_list({29}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void tick_ik1302_000840_decoded() {
    static constexpr uint32_t kCommand = 0x000840;
    if (tick_index == 0) command = kCommand;

    const int current_tick = tick_index;
    const bool branch_l = l != 0;
    if (current_tick < 27) {
      sync_address = 64;
    } else if (current_tick < 36) {
      sync_address = 16;
    } else {
      sync_address = 0;
    }

    Accumulator acc;
    execute_ik1302_000840_pre_add_orders(current_tick, branch_l, acc);

    if (key_y == 0) t = 0;

    execute_ik1302_000840_gamma_orders(current_tick, branch_l, acc);

    int sum = acc.alpha + acc.beta + acc.gamma;
    acc.sigma = sum & 0xF;
    carry = (sum >> 4) & 1;

    execute_ik1302_000840_post_add_orders(current_tick, branch_l, acc);

    output = memory[current_tick];
    memory[current_tick] = input;
    ++tick_index;
    if (tick_index == 42) tick_index = 0;
  }


  void execute_ik1302_3429ed_pre_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 25:
        execute_order_list({10}, acc);
        break;
      case 26:
        execute_order_list({0, 7}, acc);
        break;
      case 28:
        execute_order_list({5, 8}, acc);
        break;
      case 29:
        execute_order_list({0}, acc);
        break;
      case 30:
        execute_order_list({5}, acc);
        break;
      case 31:
        execute_order_list({11}, acc);
        break;
      case 32:
        execute_order_list({8}, acc);
        break;
      case 33:
        execute_order_list({5}, acc);
        break;
      case 35:
        execute_order_list({0}, acc);
        break;
      case 36:
        execute_order_list({0, 11}, acc);
        break;
      case 37:
        if (branch_l) {
          execute_order_list({0, 11}, acc);
        } else {
          execute_order_list({5}, acc);
        }
        break;
      case 38:
        execute_order_list({5, 11}, acc);
        break;
      case 39:
        execute_order_list({0}, acc);
        break;
      case 40:
        if (branch_l) {
          execute_order_list({0, 11}, acc);
        } else {
          execute_order_list({5}, acc);
        }
        break;
      case 41:
        execute_order_list({0}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void execute_ik1302_3429ed_gamma_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 26:
        execute_order_list({12, 13}, acc);
        break;
      case 28:
        execute_order_list({12, 13}, acc);
        break;
      case 31:
        execute_order_list({12, 13}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void execute_ik1302_3429ed_post_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 24:
        execute_order_list({22}, acc);
        break;
      case 25:
        execute_order_list({27}, acc);
        break;
      case 26:
        execute_order_list({25, 27}, acc);
        break;
      case 27:
        execute_order_list({22}, acc);
        break;
      case 28:
        execute_order_list({25, 27}, acc);
        break;
      case 29:
        execute_order_list({27}, acc);
        break;
      case 30:
        execute_order_list({26, 29}, acc);
        break;
      case 31:
        execute_order_list({27}, acc);
        break;
      case 32:
        execute_order_list({27}, acc);
        break;
      case 33:
        execute_order_list({26, 29}, acc);
        break;
      case 35:
        execute_order_list({27}, acc);
        break;
      case 36:
        execute_order_list({27, 32}, acc);
        break;
      case 37:
        execute_order_list({22, 27}, acc);
        break;
      case 38:
        execute_order_list({23}, acc);
        break;
      case 39:
        execute_order_list({27, 32}, acc);
        break;
      case 40:
        execute_order_list({22, 27}, acc);
        break;
      case 41:
        execute_order_list({27}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void tick_ik1302_3429ed_decoded() {
    static constexpr uint32_t kCommand = 0x3429ed;
    if (tick_index == 0) command = kCommand;

    const int current_tick = tick_index;
    const bool branch_l = l != 0;
    if (current_tick < 27) {
      sync_address = 109;
    } else if (current_tick < 36) {
      sync_address = 83;
    } else {
      if (current_tick == 36) {
        r[37] = 0 & 0xF;
        r[40] = 208 >> 4;
      }
      sync_address = 95;
    }

    Accumulator acc;
    execute_ik1302_3429ed_pre_add_orders(current_tick, branch_l, acc);

    if (key_y == 0) t = 0;

    execute_ik1302_3429ed_gamma_orders(current_tick, branch_l, acc);

    int sum = acc.alpha + acc.beta + acc.gamma;
    acc.sigma = sum & 0xF;
    carry = (sum >> 4) & 1;

    execute_ik1302_3429ed_post_add_orders(current_tick, branch_l, acc);

    output = memory[current_tick];
    memory[current_tick] = input;
    ++tick_index;
    if (tick_index == 42) tick_index = 0;
  }


  void execute_ik1302_0176c0_pre_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 34:
        execute_order_list({10}, acc);
        break;
      case 35:
        execute_order_list({0, 7}, acc);
        break;
      case 36:
        execute_order_list({9}, acc);
        break;
      case 40:
        execute_order_list({5, 11}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void execute_ik1302_0176c0_gamma_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 35:
        execute_order_list({12, 13}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void execute_ik1302_0176c0_post_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 33:
        execute_order_list({22}, acc);
        break;
      case 34:
        execute_order_list({27}, acc);
        break;
      case 35:
        execute_order_list({25, 27}, acc);
        break;
      case 36:
        execute_order_list({16}, acc);
        break;
      case 40:
        execute_order_list({23}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void tick_ik1302_0176c0_decoded() {
    static constexpr uint32_t kCommand = 0x0176c0;
    if (tick_index == 0) command = kCommand;

    const int current_tick = tick_index;
    const bool branch_l = l != 0;
    if (current_tick < 27) {
      sync_address = 64;
    } else if (current_tick < 36) {
      sync_address = 109;
    } else {
      sync_address = 5;
    }

    Accumulator acc;
    execute_ik1302_0176c0_pre_add_orders(current_tick, branch_l, acc);

    if (key_y == 0) t = 0;

    execute_ik1302_0176c0_gamma_orders(current_tick, branch_l, acc);

    int sum = acc.alpha + acc.beta + acc.gamma;
    acc.sigma = sum & 0xF;
    carry = (sum >> 4) & 1;

    execute_ik1302_0176c0_post_add_orders(current_tick, branch_l, acc);

    output = memory[current_tick];
    memory[current_tick] = input;
    ++tick_index;
    if (tick_index == 42) tick_index = 0;
  }


  void execute_ik1302_406e5b_pre_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 0:
        execute_order_list({5, 8}, acc);
        break;
      case 1:
        execute_order_list({8}, acc);
        break;
      case 2:
        execute_order_list({0, 7}, acc);
        break;
      case 5:
        execute_order_list({5, 11}, acc);
        break;
      case 8:
        execute_order_list({5, 11}, acc);
        break;
      case 11:
        execute_order_list({5, 11}, acc);
        break;
      case 14:
        execute_order_list({5, 11}, acc);
        break;
      case 17:
        execute_order_list({5, 11}, acc);
        break;
      case 20:
        execute_order_list({5, 11}, acc);
        break;
      case 23:
        execute_order_list({5, 11}, acc);
        break;
      case 26:
        execute_order_list({5, 11}, acc);
        break;
      case 28:
        execute_order_list({5, 8}, acc);
        break;
      case 29:
        execute_order_list({0}, acc);
        break;
      case 30:
        execute_order_list({5, 8}, acc);
        break;
      case 32:
        execute_order_list({9}, acc);
        break;
      case 33:
        execute_order_list({8}, acc);
        break;
      case 34:
        execute_order_list({5}, acc);
        break;
      case 35:
        execute_order_list({5}, acc);
        break;
      case 36:
        execute_order_list({0, 7}, acc);
        break;
      case 37:
        execute_order_list({5}, acc);
        break;
      case 38:
        execute_order_list({0, 11}, acc);
        break;
      case 39:
        execute_order_list({5, 10, 11}, acc);
        break;
      case 40:
        execute_order_list({11}, acc);
        break;
      case 41:
        execute_order_list({5, 11}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void execute_ik1302_406e5b_gamma_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 2:
        execute_order_list({12, 13}, acc);
        break;
      case 34:
        execute_order_list({14}, acc);
        break;
      case 35:
        execute_order_list({14}, acc);
        break;
      case 39:
        execute_order_list({12, 13}, acc);
        break;
      case 40:
        execute_order_list({12, 13}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void execute_ik1302_406e5b_post_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 0:
        execute_order_list({27}, acc);
        break;
      case 1:
        execute_order_list({27}, acc);
        break;
      case 2:
        execute_order_list({25, 27}, acc);
        break;
      case 5:
        execute_order_list({25, 27}, acc);
        break;
      case 8:
        execute_order_list({25, 27}, acc);
        break;
      case 11:
        execute_order_list({25, 27}, acc);
        break;
      case 14:
        execute_order_list({25, 27}, acc);
        break;
      case 17:
        execute_order_list({25, 27}, acc);
        break;
      case 20:
        execute_order_list({25, 27}, acc);
        break;
      case 23:
        execute_order_list({25, 27}, acc);
        break;
      case 26:
        execute_order_list({25, 27}, acc);
        break;
      case 28:
        execute_order_list({27}, acc);
        break;
      case 29:
        execute_order_list({27}, acc);
        break;
      case 30:
        execute_order_list({27}, acc);
        break;
      case 31:
        execute_order_list({29}, acc);
        break;
      case 33:
        execute_order_list({27}, acc);
        break;
      case 34:
        execute_order_list({25, 27}, acc);
        break;
      case 35:
        execute_order_list({25, 27}, acc);
        break;
      case 36:
        execute_order_list({27}, acc);
        break;
      case 37:
        execute_order_list({22}, acc);
        break;
      case 38:
        execute_order_list({27}, acc);
        break;
      case 39:
        execute_order_list({25, 27}, acc);
        break;
      case 40:
        execute_order_list({27}, acc);
        break;
      case 41:
        execute_order_list({27}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void tick_ik1302_406e5b_decoded() {
    static constexpr uint32_t kCommand = 0x406e5b;
    if (tick_index == 0) command = kCommand;

    const int current_tick = tick_index;
    const bool branch_l = l != 0;
    if (current_tick < 27) {
      sync_address = 91;
    } else if (current_tick < 36) {
      sync_address = 92;
    } else {
      sync_address = 1;
    }

    Accumulator acc;
    execute_ik1302_406e5b_pre_add_orders(current_tick, branch_l, acc);

    if (key_y == 0) t = 0;

    execute_ik1302_406e5b_gamma_orders(current_tick, branch_l, acc);

    int sum = acc.alpha + acc.beta + acc.gamma;
    acc.sigma = sum & 0xF;
    carry = (sum >> 4) & 1;

    execute_ik1302_406e5b_post_add_orders(current_tick, branch_l, acc);

    output = memory[current_tick];
    memory[current_tick] = input;
    ++tick_index;
    if (tick_index == 42) tick_index = 0;
  }


  void execute_ik1302_002f5d_pre_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 1:
        execute_order_list({5, 8}, acc);
        break;
      case 2:
        execute_order_list({0, 7}, acc);
        break;
      case 5:
        execute_order_list({5}, acc);
        break;
      case 8:
        execute_order_list({5}, acc);
        break;
      case 11:
        execute_order_list({5}, acc);
        break;
      case 14:
        execute_order_list({5}, acc);
        break;
      case 17:
        execute_order_list({5}, acc);
        break;
      case 20:
        execute_order_list({5}, acc);
        break;
      case 23:
        execute_order_list({5}, acc);
        break;
      case 26:
        execute_order_list({5}, acc);
        break;
      case 29:
        execute_order_list({5}, acc);
        break;
      case 31:
        execute_order_list({5}, acc);
        break;
      case 32:
        execute_order_list({5, 8}, acc);
        break;
      case 33:
        execute_order_list({5}, acc);
        break;
      case 34:
        execute_order_list({5}, acc);
        break;
      case 39:
        execute_order_list({0, 7}, acc);
        break;
      case 40:
        execute_order_list({5}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void execute_ik1302_002f5d_gamma_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 1:
        execute_order_list({12, 13}, acc);
        break;
      case 2:
        execute_order_list({12, 13}, acc);
        break;
      case 5:
        execute_order_list({14}, acc);
        break;
      case 8:
        execute_order_list({14}, acc);
        break;
      case 11:
        execute_order_list({14}, acc);
        break;
      case 14:
        execute_order_list({14}, acc);
        break;
      case 17:
        execute_order_list({14}, acc);
        break;
      case 20:
        execute_order_list({14}, acc);
        break;
      case 23:
        execute_order_list({14}, acc);
        break;
      case 26:
        execute_order_list({14}, acc);
        break;
      case 29:
        execute_order_list({14}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void execute_ik1302_002f5d_post_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 0:
        execute_order_list({29}, acc);
        break;
      case 1:
        execute_order_list({25, 27}, acc);
        break;
      case 2:
        execute_order_list({25, 27}, acc);
        break;
      case 5:
        execute_order_list({25, 27}, acc);
        break;
      case 8:
        execute_order_list({25, 27}, acc);
        break;
      case 11:
        execute_order_list({25, 27}, acc);
        break;
      case 14:
        execute_order_list({25, 27}, acc);
        break;
      case 17:
        execute_order_list({25, 27}, acc);
        break;
      case 20:
        execute_order_list({25, 27}, acc);
        break;
      case 23:
        execute_order_list({25, 27}, acc);
        break;
      case 26:
        execute_order_list({25, 27}, acc);
        break;
      case 29:
        execute_order_list({25, 27}, acc);
        break;
      case 31:
        execute_order_list({23}, acc);
        break;
      case 32:
        execute_order_list({27}, acc);
        break;
      case 33:
        execute_order_list({22}, acc);
        break;
      case 34:
        execute_order_list({26, 29}, acc);
        break;
      case 39:
        execute_order_list({27}, acc);
        break;
      case 40:
        execute_order_list({22}, acc);
        break;
      case 41:
        execute_order_list({29}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void tick_ik1302_002f5d_decoded() {
    static constexpr uint32_t kCommand = 0x002f5d;
    if (tick_index == 0) command = kCommand;

    const int current_tick = tick_index;
    const bool branch_l = l != 0;
    if (current_tick < 27) {
      sync_address = 93;
    } else if (current_tick < 36) {
      sync_address = 94;
    } else {
      sync_address = 0;
    }

    Accumulator acc;
    execute_ik1302_002f5d_pre_add_orders(current_tick, branch_l, acc);

    if (key_y == 0) t = 0;

    execute_ik1302_002f5d_gamma_orders(current_tick, branch_l, acc);

    int sum = acc.alpha + acc.beta + acc.gamma;
    acc.sigma = sum & 0xF;
    carry = (sum >> 4) & 1;

    execute_ik1302_002f5d_post_add_orders(current_tick, branch_l, acc);

    output = memory[current_tick];
    memory[current_tick] = input;
    ++tick_index;
    if (tick_index == 42) tick_index = 0;
  }


  void execute_ik1302_0199b3_pre_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 0:
        execute_order_list({0}, acc);
        break;
      case 3:
        execute_order_list({0}, acc);
        break;
      case 6:
        execute_order_list({0}, acc);
        break;
      case 9:
        execute_order_list({0}, acc);
        break;
      case 12:
        execute_order_list({0}, acc);
        break;
      case 15:
        execute_order_list({0}, acc);
        break;
      case 18:
        execute_order_list({0}, acc);
        break;
      case 21:
        execute_order_list({0}, acc);
        break;
      case 24:
        execute_order_list({0}, acc);
        break;
      case 27:
        execute_order_list({0}, acc);
        break;
      case 30:
        execute_order_list({0}, acc);
        break;
      case 33:
        execute_order_list({0}, acc);
        break;
      case 36:
        execute_order_list({0, 11}, acc);
        break;
      case 41:
        execute_order_list({5}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void execute_ik1302_0199b3_gamma_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      default:
        break;
    }
    (void)branch_l;
  }

  void execute_ik1302_0199b3_post_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 0:
        execute_order_list({27, 32}, acc);
        break;
      case 3:
        execute_order_list({27, 32}, acc);
        break;
      case 6:
        execute_order_list({27, 32}, acc);
        break;
      case 9:
        execute_order_list({27, 32}, acc);
        break;
      case 12:
        execute_order_list({27, 32}, acc);
        break;
      case 15:
        execute_order_list({27, 32}, acc);
        break;
      case 18:
        execute_order_list({27, 32}, acc);
        break;
      case 21:
        execute_order_list({27, 32}, acc);
        break;
      case 24:
        execute_order_list({27, 32}, acc);
        break;
      case 27:
        execute_order_list({27, 32}, acc);
        break;
      case 30:
        execute_order_list({27, 32}, acc);
        break;
      case 33:
        execute_order_list({27, 32}, acc);
        break;
      case 36:
        execute_order_list({16, 25}, acc);
        break;
      case 37:
        execute_order_list({15}, acc);
        break;
      case 41:
        execute_order_list({22}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void tick_ik1302_0199b3_decoded() {
    static constexpr uint32_t kCommand = 0x0199b3;
    if (tick_index == 0) command = kCommand;

    const int current_tick = tick_index;
    const bool branch_l = l != 0;
    if (current_tick < 27) {
      sync_address = 51;
    } else if (current_tick < 36) {
      sync_address = 51;
    } else {
      sync_address = 6;
    }

    Accumulator acc;
    execute_ik1302_0199b3_pre_add_orders(current_tick, branch_l, acc);

    if (key_y == 0) t = 0;

    execute_ik1302_0199b3_gamma_orders(current_tick, branch_l, acc);

    int sum = acc.alpha + acc.beta + acc.gamma;
    acc.sigma = sum & 0xF;
    carry = (sum >> 4) & 1;

    execute_ik1302_0199b3_post_add_orders(current_tick, branch_l, acc);

    output = memory[current_tick];
    memory[current_tick] = input;
    ++tick_index;
    if (tick_index == 42) tick_index = 0;
  }


  void execute_ik1302_42e03b_pre_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 0:
        execute_order_list({3, 11}, acc);
        break;
      case 3:
        execute_order_list({3}, acc);
        break;
      case 6:
        execute_order_list({3}, acc);
        break;
      case 9:
        execute_order_list({3}, acc);
        break;
      case 12:
        execute_order_list({3}, acc);
        break;
      case 15:
        execute_order_list({3}, acc);
        break;
      case 18:
        execute_order_list({3}, acc);
        break;
      case 21:
        execute_order_list({3}, acc);
        break;
      case 24:
        execute_order_list({10}, acc);
        break;
      case 25:
        execute_order_list({4, 7}, acc);
        break;
      case 36:
        execute_order_list({0, 11}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void execute_ik1302_42e03b_gamma_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 3:
        execute_order_list({12}, acc);
        break;
      case 6:
        execute_order_list({12}, acc);
        break;
      case 9:
        execute_order_list({12}, acc);
        break;
      case 12:
        execute_order_list({12}, acc);
        break;
      case 15:
        execute_order_list({12}, acc);
        break;
      case 18:
        execute_order_list({12}, acc);
        break;
      case 21:
        execute_order_list({12}, acc);
        break;
      case 36:
        execute_order_list({12}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void execute_ik1302_42e03b_post_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 0:
        execute_order_list({25, 27}, acc);
        break;
      case 3:
        execute_order_list({25, 27}, acc);
        break;
      case 6:
        execute_order_list({25, 27}, acc);
        break;
      case 9:
        execute_order_list({25, 27}, acc);
        break;
      case 12:
        execute_order_list({25, 27}, acc);
        break;
      case 15:
        execute_order_list({25, 27}, acc);
        break;
      case 18:
        execute_order_list({25, 27}, acc);
        break;
      case 21:
        execute_order_list({25, 27}, acc);
        break;
      case 24:
        execute_order_list({27}, acc);
        break;
      case 36:
        execute_order_list({16}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void tick_ik1302_42e03b_decoded() {
    static constexpr uint32_t kCommand = 0x42e03b;
    if (tick_index == 0) command = kCommand;

    const int current_tick = tick_index;
    const bool branch_l = l != 0;
    if (current_tick < 27) {
      sync_address = 59;
    } else if (current_tick < 36) {
      sync_address = 64;
    } else {
      sync_address = 11;
    }

    Accumulator acc;
    execute_ik1302_42e03b_pre_add_orders(current_tick, branch_l, acc);

    if (key_y == 0) t = 0;

    execute_ik1302_42e03b_gamma_orders(current_tick, branch_l, acc);

    int sum = acc.alpha + acc.beta + acc.gamma;
    acc.sigma = sum & 0xF;
    carry = (sum >> 4) & 1;

    execute_ik1302_42e03b_post_add_orders(current_tick, branch_l, acc);

    output = memory[current_tick];
    memory[current_tick] = input;
    ++tick_index;
    if (tick_index == 42) tick_index = 0;
  }


  void execute_ik1302_4d2043_pre_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 0:
        execute_order_list({0}, acc);
        break;
      case 3:
        execute_order_list({0}, acc);
        break;
      case 6:
        execute_order_list({0}, acc);
        break;
      case 9:
        execute_order_list({0}, acc);
        break;
      case 12:
        execute_order_list({0}, acc);
        break;
      case 15:
        execute_order_list({0}, acc);
        break;
      case 18:
        execute_order_list({0}, acc);
        break;
      case 21:
        execute_order_list({0}, acc);
        break;
      case 24:
        execute_order_list({0, 3, 7}, acc);
        break;
      case 36:
        execute_order_list({0, 11}, acc);
        break;
      case 37:
        if (branch_l) {
          execute_order_list({0, 11}, acc);
        } else {
          execute_order_list({5}, acc);
        }
        break;
      case 38:
        execute_order_list({5, 11}, acc);
        break;
      case 39:
        execute_order_list({0}, acc);
        break;
      case 40:
        if (branch_l) {
          execute_order_list({0, 11}, acc);
        } else {
          execute_order_list({5}, acc);
        }
        break;
      case 41:
        execute_order_list({0}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void execute_ik1302_4d2043_gamma_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      default:
        break;
    }
    (void)branch_l;
  }

  void execute_ik1302_4d2043_post_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 0:
        execute_order_list({27}, acc);
        break;
      case 3:
        execute_order_list({27}, acc);
        break;
      case 6:
        execute_order_list({27}, acc);
        break;
      case 9:
        execute_order_list({27}, acc);
        break;
      case 12:
        execute_order_list({27}, acc);
        break;
      case 15:
        execute_order_list({27}, acc);
        break;
      case 18:
        execute_order_list({27}, acc);
        break;
      case 21:
        execute_order_list({27}, acc);
        break;
      case 24:
        execute_order_list({25, 27}, acc);
        break;
      case 36:
        execute_order_list({27, 32}, acc);
        break;
      case 37:
        execute_order_list({22, 27}, acc);
        break;
      case 38:
        execute_order_list({23}, acc);
        break;
      case 39:
        execute_order_list({27, 32}, acc);
        break;
      case 40:
        execute_order_list({22, 27}, acc);
        break;
      case 41:
        execute_order_list({27}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void tick_ik1302_4d2043_decoded() {
    static constexpr uint32_t kCommand = 0x4d2043;
    if (tick_index == 0) command = kCommand;

    const int current_tick = tick_index;
    const bool branch_l = l != 0;
    if (current_tick < 27) {
      sync_address = 67;
    } else if (current_tick < 36) {
      sync_address = 64;
    } else {
      if (current_tick == 36) {
        r[37] = 4 & 0xF;
        r[40] = 52 >> 4;
      }
      sync_address = 95;
    }

    Accumulator acc;
    execute_ik1302_4d2043_pre_add_orders(current_tick, branch_l, acc);

    if (key_y == 0) t = 0;

    execute_ik1302_4d2043_gamma_orders(current_tick, branch_l, acc);

    int sum = acc.alpha + acc.beta + acc.gamma;
    acc.sigma = sum & 0xF;
    carry = (sum >> 4) & 1;

    execute_ik1302_4d2043_post_add_orders(current_tick, branch_l, acc);

    output = memory[current_tick];
    memory[current_tick] = input;
    ++tick_index;
    if (tick_index == 42) tick_index = 0;
  }


  void execute_ik1302_74d047_pre_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 0:
        execute_order_list({5, 8}, acc);
        break;
      case 1:
        execute_order_list({5}, acc);
        break;
      case 2:
        execute_order_list({10}, acc);
        break;
      case 3:
        execute_order_list({5, 11}, acc);
        break;
      case 4:
        execute_order_list({0}, acc);
        break;
      case 5:
        execute_order_list({8}, acc);
        break;
      case 6:
        execute_order_list({5, 11}, acc);
        break;
      case 7:
        execute_order_list({0}, acc);
        break;
      case 8:
        execute_order_list({8}, acc);
        break;
      case 9:
        execute_order_list({5, 11}, acc);
        break;
      case 10:
        execute_order_list({0}, acc);
        break;
      case 11:
        execute_order_list({8}, acc);
        break;
      case 12:
        execute_order_list({5, 11}, acc);
        break;
      case 13:
        execute_order_list({0}, acc);
        break;
      case 14:
        execute_order_list({8}, acc);
        break;
      case 15:
        execute_order_list({5, 11}, acc);
        break;
      case 16:
        execute_order_list({0}, acc);
        break;
      case 17:
        execute_order_list({8}, acc);
        break;
      case 18:
        execute_order_list({5, 11}, acc);
        break;
      case 19:
        execute_order_list({0}, acc);
        break;
      case 20:
        execute_order_list({8}, acc);
        break;
      case 21:
        execute_order_list({5, 11}, acc);
        break;
      case 22:
        execute_order_list({0}, acc);
        break;
      case 23:
        execute_order_list({8}, acc);
        break;
      case 24:
        execute_order_list({5}, acc);
        break;
      case 25:
        execute_order_list({0}, acc);
        break;
      case 26:
        execute_order_list({0}, acc);
        break;
      case 35:
        execute_order_list({5, 8}, acc);
        break;
      case 36:
        execute_order_list({0, 11}, acc);
        break;
      case 37:
        if (branch_l) {
          execute_order_list({0, 11}, acc);
        } else {
          execute_order_list({5}, acc);
        }
        break;
      case 38:
        execute_order_list({5, 11}, acc);
        break;
      case 39:
        execute_order_list({0}, acc);
        break;
      case 40:
        if (branch_l) {
          execute_order_list({0, 11}, acc);
        } else {
          execute_order_list({5}, acc);
        }
        break;
      case 41:
        execute_order_list({0}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void execute_ik1302_74d047_gamma_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 35:
        execute_order_list({12, 13}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void execute_ik1302_74d047_post_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 0:
        execute_order_list({27}, acc);
        break;
      case 2:
        execute_order_list({27}, acc);
        break;
      case 3:
        execute_order_list({27}, acc);
        break;
      case 5:
        execute_order_list({27}, acc);
        break;
      case 6:
        execute_order_list({27}, acc);
        break;
      case 8:
        execute_order_list({27}, acc);
        break;
      case 9:
        execute_order_list({27}, acc);
        break;
      case 11:
        execute_order_list({27}, acc);
        break;
      case 12:
        execute_order_list({27}, acc);
        break;
      case 14:
        execute_order_list({27}, acc);
        break;
      case 15:
        execute_order_list({27}, acc);
        break;
      case 17:
        execute_order_list({27}, acc);
        break;
      case 18:
        execute_order_list({27}, acc);
        break;
      case 20:
        execute_order_list({27}, acc);
        break;
      case 21:
        execute_order_list({27}, acc);
        break;
      case 23:
        execute_order_list({27}, acc);
        break;
      case 24:
        execute_order_list({26, 29}, acc);
        break;
      case 26:
        execute_order_list({27}, acc);
        break;
      case 35:
        execute_order_list({25, 27}, acc);
        break;
      case 36:
        execute_order_list({27, 32}, acc);
        break;
      case 37:
        execute_order_list({22, 27}, acc);
        break;
      case 38:
        execute_order_list({23}, acc);
        break;
      case 39:
        execute_order_list({27, 32}, acc);
        break;
      case 40:
        execute_order_list({22, 27}, acc);
        break;
      case 41:
        execute_order_list({27}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void tick_ik1302_74d047_decoded() {
    static constexpr uint32_t kCommand = 0x74d047;
    if (tick_index == 0) command = kCommand;

    const int current_tick = tick_index;
    const bool branch_l = l != 0;
    if (current_tick < 27) {
      sync_address = 71;
    } else if (current_tick < 36) {
      sync_address = 32;
    } else {
      if (current_tick == 36) {
        r[37] = 3 & 0xF;
        r[40] = 211 >> 4;
      }
      sync_address = 95;
    }

    Accumulator acc;
    execute_ik1302_74d047_pre_add_orders(current_tick, branch_l, acc);

    if (key_y == 0) t = 0;

    execute_ik1302_74d047_gamma_orders(current_tick, branch_l, acc);

    int sum = acc.alpha + acc.beta + acc.gamma;
    acc.sigma = sum & 0xF;
    carry = (sum >> 4) & 1;

    execute_ik1302_74d047_post_add_orders(current_tick, branch_l, acc);

    output = memory[current_tick];
    memory[current_tick] = input;
    ++tick_index;
    if (tick_index == 42) tick_index = 0;
  }


  void execute_ik1302_020f1e_pre_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 0:
        execute_order_list({2}, acc);
        break;
      case 3:
        execute_order_list({2}, acc);
        break;
      case 6:
        execute_order_list({2}, acc);
        break;
      case 9:
        execute_order_list({2}, acc);
        break;
      case 12:
        execute_order_list({2}, acc);
        break;
      case 15:
        execute_order_list({2}, acc);
        break;
      case 18:
        execute_order_list({2}, acc);
        break;
      case 21:
        execute_order_list({2}, acc);
        break;
      case 24:
        execute_order_list({2}, acc);
        break;
      case 27:
        execute_order_list({2}, acc);
        break;
      case 30:
        execute_order_list({2}, acc);
        break;
      case 33:
        execute_order_list({2}, acc);
        break;
      case 36:
        execute_order_list({0, 11}, acc);
        break;
      case 37:
        execute_order_list({5, 11}, acc);
        break;
      case 38:
        execute_order_list({5}, acc);
        break;
      case 39:
        execute_order_list({0}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void execute_ik1302_020f1e_gamma_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      default:
        break;
    }
    (void)branch_l;
  }

  void execute_ik1302_020f1e_post_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 0:
        execute_order_list({16, 33}, acc);
        break;
      case 3:
        execute_order_list({16, 33}, acc);
        break;
      case 6:
        execute_order_list({16, 33}, acc);
        break;
      case 9:
        execute_order_list({16, 33}, acc);
        break;
      case 12:
        execute_order_list({16, 33}, acc);
        break;
      case 15:
        execute_order_list({16, 33}, acc);
        break;
      case 18:
        execute_order_list({16, 33}, acc);
        break;
      case 21:
        execute_order_list({16, 33}, acc);
        break;
      case 24:
        execute_order_list({16, 33}, acc);
        break;
      case 27:
        execute_order_list({16, 33}, acc);
        break;
      case 30:
        execute_order_list({16, 33}, acc);
        break;
      case 33:
        execute_order_list({16, 33}, acc);
        break;
      case 36:
        execute_order_list({27}, acc);
        break;
      case 37:
        execute_order_list({27}, acc);
        break;
      case 38:
        execute_order_list({23}, acc);
        break;
      case 39:
        execute_order_list({27}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void tick_ik1302_020f1e_decoded() {
    static constexpr uint32_t kCommand = 0x020f1e;
    if (tick_index == 0) command = kCommand;

    const int current_tick = tick_index;
    const bool branch_l = l != 0;
    if (current_tick < 27) {
      sync_address = 30;
    } else if (current_tick < 36) {
      sync_address = 30;
    } else {
      sync_address = 8;
    }

    Accumulator acc;
    execute_ik1302_020f1e_pre_add_orders(current_tick, branch_l, acc);

    if (key_y == 0) t = 0;

    execute_ik1302_020f1e_gamma_orders(current_tick, branch_l, acc);

    int sum = acc.alpha + acc.beta + acc.gamma;
    acc.sigma = sum & 0xF;
    carry = (sum >> 4) & 1;

    execute_ik1302_020f1e_post_add_orders(current_tick, branch_l, acc);

    output = memory[current_tick];
    memory[current_tick] = input;
    ++tick_index;
    if (tick_index == 42) tick_index = 0;
  }


  void execute_ik1302_2ace9d_pre_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 0:
        execute_order_list({0}, acc);
        break;
      case 1:
        execute_order_list({0}, acc);
        break;
      case 3:
        execute_order_list({0}, acc);
        break;
      case 4:
        execute_order_list({0}, acc);
        break;
      case 6:
        execute_order_list({0}, acc);
        break;
      case 7:
        execute_order_list({0}, acc);
        break;
      case 9:
        execute_order_list({0}, acc);
        break;
      case 10:
        execute_order_list({0}, acc);
        break;
      case 12:
        execute_order_list({0}, acc);
        break;
      case 13:
        execute_order_list({0}, acc);
        break;
      case 15:
        execute_order_list({0}, acc);
        break;
      case 16:
        execute_order_list({0}, acc);
        break;
      case 18:
        execute_order_list({0}, acc);
        break;
      case 19:
        execute_order_list({0}, acc);
        break;
      case 21:
        execute_order_list({0}, acc);
        break;
      case 22:
        execute_order_list({0}, acc);
        break;
      case 24:
        execute_order_list({0}, acc);
        break;
      case 25:
        execute_order_list({0}, acc);
        break;
      case 26:
        execute_order_list({5, 8}, acc);
        break;
      case 27:
        execute_order_list({0}, acc);
        break;
      case 28:
        execute_order_list({0}, acc);
        break;
      case 30:
        execute_order_list({0}, acc);
        break;
      case 31:
        execute_order_list({0}, acc);
        break;
      case 33:
        execute_order_list({0}, acc);
        break;
      case 34:
        execute_order_list({0}, acc);
        break;
      case 35:
        execute_order_list({5, 8}, acc);
        break;
      case 36:
        execute_order_list({0, 11}, acc);
        break;
      case 37:
        if (branch_l) {
          execute_order_list({0, 11}, acc);
        } else {
          execute_order_list({5}, acc);
        }
        break;
      case 38:
        execute_order_list({5, 11}, acc);
        break;
      case 39:
        execute_order_list({0}, acc);
        break;
      case 40:
        if (branch_l) {
          execute_order_list({0, 11}, acc);
        } else {
          execute_order_list({5}, acc);
        }
        break;
      case 41:
        execute_order_list({0}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void execute_ik1302_2ace9d_gamma_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 26:
        execute_order_list({12, 13}, acc);
        break;
      case 35:
        execute_order_list({12, 13}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void execute_ik1302_2ace9d_post_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 0:
        execute_order_list({27}, acc);
        break;
      case 1:
        execute_order_list({17, 22}, acc);
        break;
      case 3:
        execute_order_list({27}, acc);
        break;
      case 4:
        execute_order_list({17, 22}, acc);
        break;
      case 6:
        execute_order_list({27}, acc);
        break;
      case 7:
        execute_order_list({17, 22}, acc);
        break;
      case 9:
        execute_order_list({27}, acc);
        break;
      case 10:
        execute_order_list({17, 22}, acc);
        break;
      case 12:
        execute_order_list({27}, acc);
        break;
      case 13:
        execute_order_list({17, 22}, acc);
        break;
      case 15:
        execute_order_list({27}, acc);
        break;
      case 16:
        execute_order_list({17, 22}, acc);
        break;
      case 18:
        execute_order_list({27}, acc);
        break;
      case 19:
        execute_order_list({17, 22}, acc);
        break;
      case 21:
        execute_order_list({27}, acc);
        break;
      case 22:
        execute_order_list({17, 22}, acc);
        break;
      case 24:
        execute_order_list({27}, acc);
        break;
      case 25:
        execute_order_list({17, 22}, acc);
        break;
      case 26:
        execute_order_list({25, 27}, acc);
        break;
      case 27:
        execute_order_list({27}, acc);
        break;
      case 28:
        execute_order_list({17, 22}, acc);
        break;
      case 30:
        execute_order_list({27}, acc);
        break;
      case 31:
        execute_order_list({17, 22}, acc);
        break;
      case 33:
        execute_order_list({27}, acc);
        break;
      case 34:
        execute_order_list({17, 22}, acc);
        break;
      case 35:
        execute_order_list({25, 27}, acc);
        break;
      case 36:
        execute_order_list({27, 32}, acc);
        break;
      case 37:
        execute_order_list({22, 27}, acc);
        break;
      case 38:
        execute_order_list({23}, acc);
        break;
      case 39:
        execute_order_list({27, 32}, acc);
        break;
      case 40:
        execute_order_list({22, 27}, acc);
        break;
      case 41:
        execute_order_list({27}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void tick_ik1302_2ace9d_decoded() {
    static constexpr uint32_t kCommand = 0x2ace9d;
    if (tick_index == 0) command = kCommand;

    const int current_tick = tick_index;
    const bool branch_l = l != 0;
    if (current_tick < 27) {
      sync_address = 29;
    } else if (current_tick < 36) {
      sync_address = 29;
    } else {
      if (current_tick == 36) {
        r[37] = 11 & 0xF;
        r[40] = 171 >> 4;
      }
      sync_address = 95;
    }

    Accumulator acc;
    execute_ik1302_2ace9d_pre_add_orders(current_tick, branch_l, acc);

    if (key_y == 0) t = 0;

    execute_ik1302_2ace9d_gamma_orders(current_tick, branch_l, acc);

    int sum = acc.alpha + acc.beta + acc.gamma;
    acc.sigma = sum & 0xF;
    carry = (sum >> 4) & 1;

    execute_ik1302_2ace9d_post_add_orders(current_tick, branch_l, acc);

    output = memory[current_tick];
    memory[current_tick] = input;
    ++tick_index;
    if (tick_index == 42) tick_index = 0;
  }


  void execute_ik1302_025940_pre_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 27:
        execute_order_list({10}, acc);
        break;
      case 28:
        execute_order_list({8}, acc);
        break;
      case 29:
        execute_order_list({5}, acc);
        break;
      case 32:
        execute_order_list({0}, acc);
        break;
      case 35:
        execute_order_list({0}, acc);
        break;
      case 36:
        execute_order_list({0, 11}, acc);
        break;
      case 37:
        execute_order_list({5, 11}, acc);
        break;
      case 38:
        execute_order_list({5, 11}, acc);
        break;
      case 39:
        execute_order_list({10}, acc);
        break;
      case 40:
        execute_order_list({8}, acc);
        break;
      case 41:
        execute_order_list({5, 9}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void execute_ik1302_025940_gamma_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      default:
        break;
    }
    (void)branch_l;
  }

  void execute_ik1302_025940_post_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 27:
        execute_order_list({27}, acc);
        break;
      case 28:
        execute_order_list({27}, acc);
        break;
      case 29:
        execute_order_list({26, 29}, acc);
        break;
      case 32:
        execute_order_list({15, 27}, acc);
        break;
      case 35:
        execute_order_list({17, 27}, acc);
        break;
      case 36:
        execute_order_list({27}, acc);
        break;
      case 37:
        execute_order_list({27}, acc);
        break;
      case 38:
        execute_order_list({23}, acc);
        break;
      case 39:
        execute_order_list({27}, acc);
        break;
      case 40:
        execute_order_list({27}, acc);
        break;
      case 41:
        execute_order_list({25, 27}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void tick_ik1302_025940_decoded() {
    static constexpr uint32_t kCommand = 0x025940;
    if (tick_index == 0) command = kCommand;

    const int current_tick = tick_index;
    const bool branch_l = l != 0;
    if (current_tick < 27) {
      sync_address = 64;
    } else if (current_tick < 36) {
      sync_address = 50;
    } else {
      sync_address = 9;
    }

    Accumulator acc;
    execute_ik1302_025940_pre_add_orders(current_tick, branch_l, acc);

    if (key_y == 0) t = 0;

    execute_ik1302_025940_gamma_orders(current_tick, branch_l, acc);

    int sum = acc.alpha + acc.beta + acc.gamma;
    acc.sigma = sum & 0xF;
    carry = (sum >> 4) & 1;

    execute_ik1302_025940_post_add_orders(current_tick, branch_l, acc);

    output = memory[current_tick];
    memory[current_tick] = input;
    ++tick_index;
    if (tick_index == 42) tick_index = 0;
  }


  void execute_ik1302_47c0c0_pre_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 27:
        execute_order_list({0, 7}, acc);
        break;
      case 28:
        execute_order_list({5}, acc);
        break;
      case 29:
        execute_order_list({0, 11}, acc);
        break;
      case 30:
        execute_order_list({5, 10, 11}, acc);
        break;
      case 31:
        execute_order_list({11}, acc);
        break;
      case 32:
        execute_order_list({5, 11}, acc);
        break;
      case 33:
        execute_order_list({8}, acc);
        break;
      case 36:
        if (branch_l) {
          execute_order_list({0, 7}, acc);
        } else {
          execute_order_list({0, 11}, acc);
        }
        break;
      case 37:
        execute_order_list({11}, acc);
        break;
      case 38:
        execute_order_list({0, 7}, acc);
        break;
      case 39:
        if (branch_l) {
          execute_order_list({0, 7}, acc);
        } else {
          execute_order_list({0}, acc);
        }
        break;
      case 40:
        execute_order_list({5}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void execute_ik1302_47c0c0_gamma_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 30:
        execute_order_list({12, 13}, acc);
        break;
      case 31:
        execute_order_list({12, 13}, acc);
        break;
      case 36:
        if (branch_l) {
          execute_order_list({12, 13}, acc);
        }
        break;
      case 37:
        execute_order_list({12, 13}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void execute_ik1302_47c0c0_post_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 27:
        execute_order_list({27}, acc);
        break;
      case 29:
        execute_order_list({27}, acc);
        break;
      case 30:
        execute_order_list({25, 27}, acc);
        break;
      case 31:
        execute_order_list({27}, acc);
        break;
      case 32:
        execute_order_list({27}, acc);
        break;
      case 33:
        execute_order_list({27}, acc);
        break;
      case 36:
        execute_order_list({16}, acc);
        break;
      case 37:
        execute_order_list({27}, acc);
        break;
      case 38:
        execute_order_list({27}, acc);
        break;
      case 39:
        if (branch_l) {
          execute_order_list({25, 27}, acc);
        } else {
          execute_order_list({27}, acc);
        }
        break;
      case 40:
        execute_order_list({22}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void tick_ik1302_47c0c0_decoded() {
    static constexpr uint32_t kCommand = 0x47c0c0;
    if (tick_index == 0) command = kCommand;

    const int current_tick = tick_index;
    const bool branch_l = l != 0;
    if (current_tick < 27) {
      sync_address = 64;
    } else if (current_tick < 36) {
      sync_address = 1;
    } else {
      sync_address = 31;
    }

    Accumulator acc;
    execute_ik1302_47c0c0_pre_add_orders(current_tick, branch_l, acc);

    if (key_y == 0) t = 0;

    execute_ik1302_47c0c0_gamma_orders(current_tick, branch_l, acc);

    int sum = acc.alpha + acc.beta + acc.gamma;
    acc.sigma = sum & 0xF;
    carry = (sum >> 4) & 1;

    execute_ik1302_47c0c0_post_add_orders(current_tick, branch_l, acc);

    output = memory[current_tick];
    memory[current_tick] = input;
    ++tick_index;
    if (tick_index == 42) tick_index = 0;
  }


  void execute_ik1302_056440_pre_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 29:
        execute_order_list({0, 11}, acc);
        break;
      case 30:
        execute_order_list({5}, acc);
        break;
      case 31:
        execute_order_list({8}, acc);
        break;
      case 32:
        execute_order_list({0, 7}, acc);
        break;
      case 33:
        execute_order_list({5, 11}, acc);
        break;
      case 34:
        execute_order_list({5, 11}, acc);
        break;
      case 36:
        if (branch_l) {
          execute_order_list({0, 7}, acc);
        } else {
          execute_order_list({0, 11}, acc);
        }
        break;
      case 37:
        execute_order_list({10}, acc);
        break;
      case 38:
        execute_order_list({8}, acc);
        break;
      case 39:
        if (branch_l) {
          execute_order_list({0, 7}, acc);
        } else {
          execute_order_list({0}, acc);
        }
        break;
      case 40:
        execute_order_list({5}, acc);
        break;
      case 41:
        execute_order_list({11}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void execute_ik1302_056440_gamma_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 32:
        execute_order_list({12, 13}, acc);
        break;
      case 36:
        if (branch_l) {
          execute_order_list({12, 13}, acc);
        }
        break;
      case 41:
        execute_order_list({12, 13}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void execute_ik1302_056440_post_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 29:
        execute_order_list({27}, acc);
        break;
      case 30:
        execute_order_list({22}, acc);
        break;
      case 31:
        execute_order_list({27}, acc);
        break;
      case 32:
        execute_order_list({25, 27}, acc);
        break;
      case 33:
        execute_order_list({25, 27}, acc);
        break;
      case 34:
        execute_order_list({27}, acc);
        break;
      case 36:
        execute_order_list({16}, acc);
        break;
      case 37:
        execute_order_list({27}, acc);
        break;
      case 38:
        execute_order_list({27}, acc);
        break;
      case 39:
        if (branch_l) {
          execute_order_list({25, 27}, acc);
        } else {
          execute_order_list({27}, acc);
        }
        break;
      case 40:
        execute_order_list({22}, acc);
        break;
      case 41:
        execute_order_list({27}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void tick_ik1302_056440_decoded() {
    static constexpr uint32_t kCommand = 0x056440;
    if (tick_index == 0) command = kCommand;

    const int current_tick = tick_index;
    const bool branch_l = l != 0;
    if (current_tick < 27) {
      sync_address = 64;
    } else if (current_tick < 36) {
      sync_address = 72;
    } else {
      sync_address = 21;
    }

    Accumulator acc;
    execute_ik1302_056440_pre_add_orders(current_tick, branch_l, acc);

    if (key_y == 0) t = 0;

    execute_ik1302_056440_gamma_orders(current_tick, branch_l, acc);

    int sum = acc.alpha + acc.beta + acc.gamma;
    acc.sigma = sum & 0xF;
    carry = (sum >> 4) & 1;

    execute_ik1302_056440_post_add_orders(current_tick, branch_l, acc);

    output = memory[current_tick];
    memory[current_tick] = input;
    ++tick_index;
    if (tick_index == 42) tick_index = 0;
  }


  void execute_ik1302_019223_pre_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 0:
        execute_order_list({5, 8}, acc);
        break;
      case 1:
        execute_order_list({0}, acc);
        break;
      case 26:
        execute_order_list({5, 11}, acc);
        break;
      case 27:
        execute_order_list({0, 3, 7}, acc);
        break;
      case 36:
        execute_order_list({0, 11}, acc);
        break;
      case 41:
        execute_order_list({5}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void execute_ik1302_019223_gamma_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 0:
        execute_order_list({12, 13}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void execute_ik1302_019223_post_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 0:
        execute_order_list({25, 27}, acc);
        break;
      case 1:
        execute_order_list({15, 27}, acc);
        break;
      case 4:
        execute_order_list({15}, acc);
        break;
      case 7:
        execute_order_list({15}, acc);
        break;
      case 10:
        execute_order_list({15}, acc);
        break;
      case 13:
        execute_order_list({15}, acc);
        break;
      case 16:
        execute_order_list({15}, acc);
        break;
      case 19:
        execute_order_list({15}, acc);
        break;
      case 22:
        execute_order_list({15}, acc);
        break;
      case 25:
        execute_order_list({15}, acc);
        break;
      case 26:
        execute_order_list({27}, acc);
        break;
      case 27:
        execute_order_list({25, 27}, acc);
        break;
      case 28:
        execute_order_list({15}, acc);
        break;
      case 31:
        execute_order_list({15}, acc);
        break;
      case 34:
        execute_order_list({15}, acc);
        break;
      case 36:
        execute_order_list({16, 25}, acc);
        break;
      case 37:
        execute_order_list({15}, acc);
        break;
      case 41:
        execute_order_list({22}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void tick_ik1302_019223_decoded() {
    static constexpr uint32_t kCommand = 0x019223;
    if (tick_index == 0) command = kCommand;

    const int current_tick = tick_index;
    const bool branch_l = l != 0;
    if (current_tick < 27) {
      sync_address = 35;
    } else if (current_tick < 36) {
      sync_address = 36;
    } else {
      sync_address = 6;
    }

    Accumulator acc;
    execute_ik1302_019223_pre_add_orders(current_tick, branch_l, acc);

    if (key_y == 0) t = 0;

    execute_ik1302_019223_gamma_orders(current_tick, branch_l, acc);

    int sum = acc.alpha + acc.beta + acc.gamma;
    acc.sigma = sum & 0xF;
    carry = (sum >> 4) & 1;

    execute_ik1302_019223_post_add_orders(current_tick, branch_l, acc);

    output = memory[current_tick];
    memory[current_tick] = input;
    ++tick_index;
    if (tick_index == 42) tick_index = 0;
  }


  void execute_ik1302_4179c0_pre_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 34:
        execute_order_list({10}, acc);
        break;
      case 35:
        execute_order_list({5, 11}, acc);
        break;
      case 36:
        execute_order_list({9}, acc);
        break;
      case 40:
        execute_order_list({5, 11}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void execute_ik1302_4179c0_gamma_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      default:
        break;
    }
    (void)branch_l;
  }

  void execute_ik1302_4179c0_post_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 34:
        execute_order_list({27}, acc);
        break;
      case 35:
        execute_order_list({27}, acc);
        break;
      case 36:
        execute_order_list({16}, acc);
        break;
      case 40:
        execute_order_list({23}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void tick_ik1302_4179c0_decoded() {
    static constexpr uint32_t kCommand = 0x4179c0;
    if (tick_index == 0) command = kCommand;

    const int current_tick = tick_index;
    const bool branch_l = l != 0;
    if (current_tick < 27) {
      sync_address = 64;
    } else if (current_tick < 36) {
      sync_address = 115;
    } else {
      sync_address = 5;
    }

    Accumulator acc;
    execute_ik1302_4179c0_pre_add_orders(current_tick, branch_l, acc);

    if (key_y == 0) t = 0;

    execute_ik1302_4179c0_gamma_orders(current_tick, branch_l, acc);

    int sum = acc.alpha + acc.beta + acc.gamma;
    acc.sigma = sum & 0xF;
    carry = (sum >> 4) & 1;

    execute_ik1302_4179c0_post_add_orders(current_tick, branch_l, acc);

    output = memory[current_tick];
    memory[current_tick] = input;
    ++tick_index;
    if (tick_index == 42) tick_index = 0;
  }


  void execute_ik1302_018e9d_pre_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 0:
        execute_order_list({0}, acc);
        break;
      case 1:
        execute_order_list({0}, acc);
        break;
      case 3:
        execute_order_list({0}, acc);
        break;
      case 4:
        execute_order_list({0}, acc);
        break;
      case 6:
        execute_order_list({0}, acc);
        break;
      case 7:
        execute_order_list({0}, acc);
        break;
      case 9:
        execute_order_list({0}, acc);
        break;
      case 10:
        execute_order_list({0}, acc);
        break;
      case 12:
        execute_order_list({0}, acc);
        break;
      case 13:
        execute_order_list({0}, acc);
        break;
      case 15:
        execute_order_list({0}, acc);
        break;
      case 16:
        execute_order_list({0}, acc);
        break;
      case 18:
        execute_order_list({0}, acc);
        break;
      case 19:
        execute_order_list({0}, acc);
        break;
      case 21:
        execute_order_list({0}, acc);
        break;
      case 22:
        execute_order_list({0}, acc);
        break;
      case 24:
        execute_order_list({0}, acc);
        break;
      case 25:
        execute_order_list({0}, acc);
        break;
      case 26:
        execute_order_list({5, 8}, acc);
        break;
      case 27:
        execute_order_list({0}, acc);
        break;
      case 28:
        execute_order_list({0}, acc);
        break;
      case 30:
        execute_order_list({0}, acc);
        break;
      case 31:
        execute_order_list({0}, acc);
        break;
      case 33:
        execute_order_list({0}, acc);
        break;
      case 34:
        execute_order_list({0}, acc);
        break;
      case 35:
        execute_order_list({5, 8}, acc);
        break;
      case 36:
        execute_order_list({0, 11}, acc);
        break;
      case 41:
        execute_order_list({5}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void execute_ik1302_018e9d_gamma_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 26:
        execute_order_list({12, 13}, acc);
        break;
      case 35:
        execute_order_list({12, 13}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void execute_ik1302_018e9d_post_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 0:
        execute_order_list({27}, acc);
        break;
      case 1:
        execute_order_list({17, 22}, acc);
        break;
      case 3:
        execute_order_list({27}, acc);
        break;
      case 4:
        execute_order_list({17, 22}, acc);
        break;
      case 6:
        execute_order_list({27}, acc);
        break;
      case 7:
        execute_order_list({17, 22}, acc);
        break;
      case 9:
        execute_order_list({27}, acc);
        break;
      case 10:
        execute_order_list({17, 22}, acc);
        break;
      case 12:
        execute_order_list({27}, acc);
        break;
      case 13:
        execute_order_list({17, 22}, acc);
        break;
      case 15:
        execute_order_list({27}, acc);
        break;
      case 16:
        execute_order_list({17, 22}, acc);
        break;
      case 18:
        execute_order_list({27}, acc);
        break;
      case 19:
        execute_order_list({17, 22}, acc);
        break;
      case 21:
        execute_order_list({27}, acc);
        break;
      case 22:
        execute_order_list({17, 22}, acc);
        break;
      case 24:
        execute_order_list({27}, acc);
        break;
      case 25:
        execute_order_list({17, 22}, acc);
        break;
      case 26:
        execute_order_list({25, 27}, acc);
        break;
      case 27:
        execute_order_list({27}, acc);
        break;
      case 28:
        execute_order_list({17, 22}, acc);
        break;
      case 30:
        execute_order_list({27}, acc);
        break;
      case 31:
        execute_order_list({17, 22}, acc);
        break;
      case 33:
        execute_order_list({27}, acc);
        break;
      case 34:
        execute_order_list({17, 22}, acc);
        break;
      case 35:
        execute_order_list({25, 27}, acc);
        break;
      case 36:
        execute_order_list({16, 25}, acc);
        break;
      case 37:
        execute_order_list({15}, acc);
        break;
      case 41:
        execute_order_list({22}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void tick_ik1302_018e9d_decoded() {
    static constexpr uint32_t kCommand = 0x018e9d;
    if (tick_index == 0) command = kCommand;

    const int current_tick = tick_index;
    const bool branch_l = l != 0;
    if (current_tick < 27) {
      sync_address = 29;
    } else if (current_tick < 36) {
      sync_address = 29;
    } else {
      sync_address = 6;
    }

    Accumulator acc;
    execute_ik1302_018e9d_pre_add_orders(current_tick, branch_l, acc);

    if (key_y == 0) t = 0;

    execute_ik1302_018e9d_gamma_orders(current_tick, branch_l, acc);

    int sum = acc.alpha + acc.beta + acc.gamma;
    acc.sigma = sum & 0xF;
    carry = (sum >> 4) & 1;

    execute_ik1302_018e9d_post_add_orders(current_tick, branch_l, acc);

    output = memory[current_tick];
    memory[current_tick] = input;
    ++tick_index;
    if (tick_index == 42) tick_index = 0;
  }


  void execute_ik1302_2062c5_pre_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 1:
        execute_order_list({0}, acc);
        break;
      case 2:
        execute_order_list({0}, acc);
        break;
      case 4:
        execute_order_list({0}, acc);
        break;
      case 5:
        execute_order_list({0}, acc);
        break;
      case 7:
        execute_order_list({0}, acc);
        break;
      case 8:
        execute_order_list({0}, acc);
        break;
      case 10:
        execute_order_list({0}, acc);
        break;
      case 11:
        execute_order_list({0}, acc);
        break;
      case 13:
        execute_order_list({0}, acc);
        break;
      case 14:
        execute_order_list({0}, acc);
        break;
      case 16:
        execute_order_list({0}, acc);
        break;
      case 17:
        execute_order_list({0}, acc);
        break;
      case 19:
        execute_order_list({0}, acc);
        break;
      case 20:
        execute_order_list({0}, acc);
        break;
      case 22:
        execute_order_list({0}, acc);
        break;
      case 23:
        execute_order_list({0}, acc);
        break;
      case 24:
        execute_order_list({5, 8}, acc);
        break;
      case 25:
        execute_order_list({0}, acc);
        break;
      case 26:
        execute_order_list({0}, acc);
        break;
      case 28:
        execute_order_list({0}, acc);
        break;
      case 29:
        execute_order_list({0}, acc);
        break;
      case 31:
        execute_order_list({0}, acc);
        break;
      case 32:
        execute_order_list({0}, acc);
        break;
      case 33:
        execute_order_list({5, 8}, acc);
        break;
      case 34:
        execute_order_list({0}, acc);
        break;
      case 35:
        execute_order_list({0}, acc);
        break;
      case 36:
        execute_order_list({0, 11}, acc);
        break;
      case 37:
        if (branch_l) {
          execute_order_list({0, 11}, acc);
        } else {
          execute_order_list({5}, acc);
        }
        break;
      case 38:
        execute_order_list({5, 11}, acc);
        break;
      case 39:
        execute_order_list({0}, acc);
        break;
      case 40:
        if (branch_l) {
          execute_order_list({0, 11}, acc);
        } else {
          execute_order_list({5}, acc);
        }
        break;
      case 41:
        execute_order_list({0}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void execute_ik1302_2062c5_gamma_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 24:
        execute_order_list({12, 13}, acc);
        break;
      case 33:
        execute_order_list({12, 13}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void execute_ik1302_2062c5_post_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 1:
        execute_order_list({27}, acc);
        break;
      case 2:
        execute_order_list({17, 22}, acc);
        break;
      case 4:
        execute_order_list({27}, acc);
        break;
      case 5:
        execute_order_list({17, 22}, acc);
        break;
      case 7:
        execute_order_list({27}, acc);
        break;
      case 8:
        execute_order_list({17, 22}, acc);
        break;
      case 10:
        execute_order_list({27}, acc);
        break;
      case 11:
        execute_order_list({17, 22}, acc);
        break;
      case 13:
        execute_order_list({27}, acc);
        break;
      case 14:
        execute_order_list({17, 22}, acc);
        break;
      case 16:
        execute_order_list({27}, acc);
        break;
      case 17:
        execute_order_list({17, 22}, acc);
        break;
      case 19:
        execute_order_list({27}, acc);
        break;
      case 20:
        execute_order_list({17, 22}, acc);
        break;
      case 22:
        execute_order_list({27}, acc);
        break;
      case 23:
        execute_order_list({17, 22}, acc);
        break;
      case 24:
        execute_order_list({25, 27}, acc);
        break;
      case 25:
        execute_order_list({27}, acc);
        break;
      case 26:
        execute_order_list({17, 22}, acc);
        break;
      case 28:
        execute_order_list({27}, acc);
        break;
      case 29:
        execute_order_list({17, 22}, acc);
        break;
      case 31:
        execute_order_list({27}, acc);
        break;
      case 32:
        execute_order_list({17, 22}, acc);
        break;
      case 33:
        execute_order_list({25, 27}, acc);
        break;
      case 34:
        execute_order_list({27}, acc);
        break;
      case 35:
        execute_order_list({17, 22}, acc);
        break;
      case 36:
        execute_order_list({27, 32}, acc);
        break;
      case 37:
        execute_order_list({22, 27}, acc);
        break;
      case 38:
        execute_order_list({23}, acc);
        break;
      case 39:
        execute_order_list({27, 32}, acc);
        break;
      case 40:
        execute_order_list({22, 27}, acc);
        break;
      case 41:
        execute_order_list({27}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void tick_ik1302_2062c5_decoded() {
    static constexpr uint32_t kCommand = 0x2062c5;
    if (tick_index == 0) command = kCommand;

    const int current_tick = tick_index;
    const bool branch_l = l != 0;
    if (current_tick < 27) {
      sync_address = 69;
    } else if (current_tick < 36) {
      sync_address = 69;
    } else {
      if (current_tick == 36) {
        r[37] = 1 & 0xF;
        r[40] = 129 >> 4;
      }
      sync_address = 95;
    }

    Accumulator acc;
    execute_ik1302_2062c5_pre_add_orders(current_tick, branch_l, acc);

    if (key_y == 0) t = 0;

    execute_ik1302_2062c5_gamma_orders(current_tick, branch_l, acc);

    int sum = acc.alpha + acc.beta + acc.gamma;
    acc.sigma = sum & 0xF;
    carry = (sum >> 4) & 1;

    execute_ik1302_2062c5_post_add_orders(current_tick, branch_l, acc);

    output = memory[current_tick];
    memory[current_tick] = input;
    ++tick_index;
    if (tick_index == 42) tick_index = 0;
  }


  void execute_ik1302_018f1e_pre_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 0:
        execute_order_list({2}, acc);
        break;
      case 3:
        execute_order_list({2}, acc);
        break;
      case 6:
        execute_order_list({2}, acc);
        break;
      case 9:
        execute_order_list({2}, acc);
        break;
      case 12:
        execute_order_list({2}, acc);
        break;
      case 15:
        execute_order_list({2}, acc);
        break;
      case 18:
        execute_order_list({2}, acc);
        break;
      case 21:
        execute_order_list({2}, acc);
        break;
      case 24:
        execute_order_list({2}, acc);
        break;
      case 27:
        execute_order_list({2}, acc);
        break;
      case 30:
        execute_order_list({2}, acc);
        break;
      case 33:
        execute_order_list({2}, acc);
        break;
      case 36:
        execute_order_list({0, 11}, acc);
        break;
      case 41:
        execute_order_list({5}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void execute_ik1302_018f1e_gamma_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      default:
        break;
    }
    (void)branch_l;
  }

  void execute_ik1302_018f1e_post_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 0:
        execute_order_list({16, 33}, acc);
        break;
      case 3:
        execute_order_list({16, 33}, acc);
        break;
      case 6:
        execute_order_list({16, 33}, acc);
        break;
      case 9:
        execute_order_list({16, 33}, acc);
        break;
      case 12:
        execute_order_list({16, 33}, acc);
        break;
      case 15:
        execute_order_list({16, 33}, acc);
        break;
      case 18:
        execute_order_list({16, 33}, acc);
        break;
      case 21:
        execute_order_list({16, 33}, acc);
        break;
      case 24:
        execute_order_list({16, 33}, acc);
        break;
      case 27:
        execute_order_list({16, 33}, acc);
        break;
      case 30:
        execute_order_list({16, 33}, acc);
        break;
      case 33:
        execute_order_list({16, 33}, acc);
        break;
      case 36:
        execute_order_list({16, 25}, acc);
        break;
      case 37:
        execute_order_list({15}, acc);
        break;
      case 41:
        execute_order_list({22}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void tick_ik1302_018f1e_decoded() {
    static constexpr uint32_t kCommand = 0x018f1e;
    if (tick_index == 0) command = kCommand;

    const int current_tick = tick_index;
    const bool branch_l = l != 0;
    if (current_tick < 27) {
      sync_address = 30;
    } else if (current_tick < 36) {
      sync_address = 30;
    } else {
      sync_address = 6;
    }

    Accumulator acc;
    execute_ik1302_018f1e_pre_add_orders(current_tick, branch_l, acc);

    if (key_y == 0) t = 0;

    execute_ik1302_018f1e_gamma_orders(current_tick, branch_l, acc);

    int sum = acc.alpha + acc.beta + acc.gamma;
    acc.sigma = sum & 0xF;
    carry = (sum >> 4) & 1;

    execute_ik1302_018f1e_post_add_orders(current_tick, branch_l, acc);

    output = memory[current_tick];
    memory[current_tick] = input;
    ++tick_index;
    if (tick_index == 42) tick_index = 0;
  }


  void execute_ik1302_024f1e_pre_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 0:
        execute_order_list({2}, acc);
        break;
      case 3:
        execute_order_list({2}, acc);
        break;
      case 6:
        execute_order_list({2}, acc);
        break;
      case 9:
        execute_order_list({2}, acc);
        break;
      case 12:
        execute_order_list({2}, acc);
        break;
      case 15:
        execute_order_list({2}, acc);
        break;
      case 18:
        execute_order_list({2}, acc);
        break;
      case 21:
        execute_order_list({2}, acc);
        break;
      case 24:
        execute_order_list({2}, acc);
        break;
      case 27:
        execute_order_list({2}, acc);
        break;
      case 30:
        execute_order_list({2}, acc);
        break;
      case 33:
        execute_order_list({2}, acc);
        break;
      case 36:
        execute_order_list({0, 11}, acc);
        break;
      case 37:
        execute_order_list({5, 11}, acc);
        break;
      case 38:
        execute_order_list({5, 11}, acc);
        break;
      case 39:
        execute_order_list({10}, acc);
        break;
      case 40:
        execute_order_list({8}, acc);
        break;
      case 41:
        execute_order_list({5, 9}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void execute_ik1302_024f1e_gamma_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      default:
        break;
    }
    (void)branch_l;
  }

  void execute_ik1302_024f1e_post_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 0:
        execute_order_list({16, 33}, acc);
        break;
      case 3:
        execute_order_list({16, 33}, acc);
        break;
      case 6:
        execute_order_list({16, 33}, acc);
        break;
      case 9:
        execute_order_list({16, 33}, acc);
        break;
      case 12:
        execute_order_list({16, 33}, acc);
        break;
      case 15:
        execute_order_list({16, 33}, acc);
        break;
      case 18:
        execute_order_list({16, 33}, acc);
        break;
      case 21:
        execute_order_list({16, 33}, acc);
        break;
      case 24:
        execute_order_list({16, 33}, acc);
        break;
      case 27:
        execute_order_list({16, 33}, acc);
        break;
      case 30:
        execute_order_list({16, 33}, acc);
        break;
      case 33:
        execute_order_list({16, 33}, acc);
        break;
      case 36:
        execute_order_list({27}, acc);
        break;
      case 37:
        execute_order_list({27}, acc);
        break;
      case 38:
        execute_order_list({23}, acc);
        break;
      case 39:
        execute_order_list({27}, acc);
        break;
      case 40:
        execute_order_list({27}, acc);
        break;
      case 41:
        execute_order_list({25, 27}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void tick_ik1302_024f1e_decoded() {
    static constexpr uint32_t kCommand = 0x024f1e;
    if (tick_index == 0) command = kCommand;

    const int current_tick = tick_index;
    const bool branch_l = l != 0;
    if (current_tick < 27) {
      sync_address = 30;
    } else if (current_tick < 36) {
      sync_address = 30;
    } else {
      sync_address = 9;
    }

    Accumulator acc;
    execute_ik1302_024f1e_pre_add_orders(current_tick, branch_l, acc);

    if (key_y == 0) t = 0;

    execute_ik1302_024f1e_gamma_orders(current_tick, branch_l, acc);

    int sum = acc.alpha + acc.beta + acc.gamma;
    acc.sigma = sum & 0xF;
    carry = (sum >> 4) & 1;

    execute_ik1302_024f1e_post_add_orders(current_tick, branch_l, acc);

    output = memory[current_tick];
    memory[current_tick] = input;
    ++tick_index;
    if (tick_index == 42) tick_index = 0;
  }


  void execute_ik1302_546078_pre_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 2:
        execute_order_list({0}, acc);
        break;
      case 3:
        execute_order_list({5, 10, 11}, acc);
        break;
      case 6:
        execute_order_list({5, 10, 11}, acc);
        break;
      case 9:
        execute_order_list({5, 10, 11}, acc);
        break;
      case 12:
        execute_order_list({5, 10, 11}, acc);
        break;
      case 15:
        execute_order_list({5, 10, 11}, acc);
        break;
      case 18:
        execute_order_list({5, 10, 11}, acc);
        break;
      case 21:
        execute_order_list({5, 10, 11}, acc);
        break;
      case 26:
        execute_order_list({10}, acc);
        break;
      case 36:
        execute_order_list({0, 11}, acc);
        break;
      case 37:
        if (branch_l) {
          execute_order_list({0, 11}, acc);
        } else {
          execute_order_list({5}, acc);
        }
        break;
      case 38:
        execute_order_list({5, 11}, acc);
        break;
      case 39:
        execute_order_list({0}, acc);
        break;
      case 40:
        if (branch_l) {
          execute_order_list({0, 11}, acc);
        } else {
          execute_order_list({5}, acc);
        }
        break;
      case 41:
        execute_order_list({0}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void execute_ik1302_546078_gamma_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 3:
        execute_order_list({12, 13}, acc);
        break;
      case 6:
        execute_order_list({12, 13}, acc);
        break;
      case 9:
        execute_order_list({12, 13}, acc);
        break;
      case 12:
        execute_order_list({12, 13}, acc);
        break;
      case 15:
        execute_order_list({12, 13}, acc);
        break;
      case 18:
        execute_order_list({12, 13}, acc);
        break;
      case 21:
        execute_order_list({12, 13}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void execute_ik1302_546078_post_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 2:
        execute_order_list({27}, acc);
        break;
      case 3:
        execute_order_list({25, 27}, acc);
        break;
      case 6:
        execute_order_list({25, 27}, acc);
        break;
      case 9:
        execute_order_list({25, 27}, acc);
        break;
      case 12:
        execute_order_list({25, 27}, acc);
        break;
      case 15:
        execute_order_list({25, 27}, acc);
        break;
      case 18:
        execute_order_list({25, 27}, acc);
        break;
      case 21:
        execute_order_list({25, 27}, acc);
        break;
      case 26:
        execute_order_list({27}, acc);
        break;
      case 36:
        execute_order_list({27, 32}, acc);
        break;
      case 37:
        execute_order_list({22, 27}, acc);
        break;
      case 38:
        execute_order_list({23}, acc);
        break;
      case 39:
        execute_order_list({27, 32}, acc);
        break;
      case 40:
        execute_order_list({22, 27}, acc);
        break;
      case 41:
        execute_order_list({27}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void tick_ik1302_546078_decoded() {
    static constexpr uint32_t kCommand = 0x546078;
    if (tick_index == 0) command = kCommand;

    const int current_tick = tick_index;
    const bool branch_l = l != 0;
    if (current_tick < 27) {
      sync_address = 120;
    } else if (current_tick < 36) {
      sync_address = 64;
    } else {
      if (current_tick == 36) {
        r[37] = 1 & 0xF;
        r[40] = 81 >> 4;
      }
      sync_address = 95;
    }

    Accumulator acc;
    execute_ik1302_546078_pre_add_orders(current_tick, branch_l, acc);

    if (key_y == 0) t = 0;

    execute_ik1302_546078_gamma_orders(current_tick, branch_l, acc);

    int sum = acc.alpha + acc.beta + acc.gamma;
    acc.sigma = sum & 0xF;
    carry = (sum >> 4) & 1;

    execute_ik1302_546078_post_add_orders(current_tick, branch_l, acc);

    output = memory[current_tick];
    memory[current_tick] = input;
    ++tick_index;
    if (tick_index == 42) tick_index = 0;
  }


  void execute_ik1302_020e9d_pre_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 0:
        execute_order_list({0}, acc);
        break;
      case 1:
        execute_order_list({0}, acc);
        break;
      case 3:
        execute_order_list({0}, acc);
        break;
      case 4:
        execute_order_list({0}, acc);
        break;
      case 6:
        execute_order_list({0}, acc);
        break;
      case 7:
        execute_order_list({0}, acc);
        break;
      case 9:
        execute_order_list({0}, acc);
        break;
      case 10:
        execute_order_list({0}, acc);
        break;
      case 12:
        execute_order_list({0}, acc);
        break;
      case 13:
        execute_order_list({0}, acc);
        break;
      case 15:
        execute_order_list({0}, acc);
        break;
      case 16:
        execute_order_list({0}, acc);
        break;
      case 18:
        execute_order_list({0}, acc);
        break;
      case 19:
        execute_order_list({0}, acc);
        break;
      case 21:
        execute_order_list({0}, acc);
        break;
      case 22:
        execute_order_list({0}, acc);
        break;
      case 24:
        execute_order_list({0}, acc);
        break;
      case 25:
        execute_order_list({0}, acc);
        break;
      case 26:
        execute_order_list({5, 8}, acc);
        break;
      case 27:
        execute_order_list({0}, acc);
        break;
      case 28:
        execute_order_list({0}, acc);
        break;
      case 30:
        execute_order_list({0}, acc);
        break;
      case 31:
        execute_order_list({0}, acc);
        break;
      case 33:
        execute_order_list({0}, acc);
        break;
      case 34:
        execute_order_list({0}, acc);
        break;
      case 35:
        execute_order_list({5, 8}, acc);
        break;
      case 36:
        execute_order_list({0, 11}, acc);
        break;
      case 37:
        execute_order_list({5, 11}, acc);
        break;
      case 38:
        execute_order_list({5}, acc);
        break;
      case 39:
        execute_order_list({0}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void execute_ik1302_020e9d_gamma_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 26:
        execute_order_list({12, 13}, acc);
        break;
      case 35:
        execute_order_list({12, 13}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void execute_ik1302_020e9d_post_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 0:
        execute_order_list({27}, acc);
        break;
      case 1:
        execute_order_list({17, 22}, acc);
        break;
      case 3:
        execute_order_list({27}, acc);
        break;
      case 4:
        execute_order_list({17, 22}, acc);
        break;
      case 6:
        execute_order_list({27}, acc);
        break;
      case 7:
        execute_order_list({17, 22}, acc);
        break;
      case 9:
        execute_order_list({27}, acc);
        break;
      case 10:
        execute_order_list({17, 22}, acc);
        break;
      case 12:
        execute_order_list({27}, acc);
        break;
      case 13:
        execute_order_list({17, 22}, acc);
        break;
      case 15:
        execute_order_list({27}, acc);
        break;
      case 16:
        execute_order_list({17, 22}, acc);
        break;
      case 18:
        execute_order_list({27}, acc);
        break;
      case 19:
        execute_order_list({17, 22}, acc);
        break;
      case 21:
        execute_order_list({27}, acc);
        break;
      case 22:
        execute_order_list({17, 22}, acc);
        break;
      case 24:
        execute_order_list({27}, acc);
        break;
      case 25:
        execute_order_list({17, 22}, acc);
        break;
      case 26:
        execute_order_list({25, 27}, acc);
        break;
      case 27:
        execute_order_list({27}, acc);
        break;
      case 28:
        execute_order_list({17, 22}, acc);
        break;
      case 30:
        execute_order_list({27}, acc);
        break;
      case 31:
        execute_order_list({17, 22}, acc);
        break;
      case 33:
        execute_order_list({27}, acc);
        break;
      case 34:
        execute_order_list({17, 22}, acc);
        break;
      case 35:
        execute_order_list({25, 27}, acc);
        break;
      case 36:
        execute_order_list({27}, acc);
        break;
      case 37:
        execute_order_list({27}, acc);
        break;
      case 38:
        execute_order_list({23}, acc);
        break;
      case 39:
        execute_order_list({27}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void tick_ik1302_020e9d_decoded() {
    static constexpr uint32_t kCommand = 0x020e9d;
    if (tick_index == 0) command = kCommand;

    const int current_tick = tick_index;
    const bool branch_l = l != 0;
    if (current_tick < 27) {
      sync_address = 29;
    } else if (current_tick < 36) {
      sync_address = 29;
    } else {
      sync_address = 8;
    }

    Accumulator acc;
    execute_ik1302_020e9d_pre_add_orders(current_tick, branch_l, acc);

    if (key_y == 0) t = 0;

    execute_ik1302_020e9d_gamma_orders(current_tick, branch_l, acc);

    int sum = acc.alpha + acc.beta + acc.gamma;
    acc.sigma = sum & 0xF;
    carry = (sum >> 4) & 1;

    execute_ik1302_020e9d_post_add_orders(current_tick, branch_l, acc);

    output = memory[current_tick];
    memory[current_tick] = input;
    ++tick_index;
    if (tick_index == 42) tick_index = 0;
  }


  void execute_ik1302_589106_pre_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 0:
        execute_order_list({0, 11}, acc);
        break;
      case 5:
        execute_order_list({5}, acc);
        break;
      case 8:
        execute_order_list({5}, acc);
        break;
      case 11:
        execute_order_list({5}, acc);
        break;
      case 14:
        execute_order_list({5}, acc);
        break;
      case 17:
        execute_order_list({5}, acc);
        break;
      case 20:
        execute_order_list({5}, acc);
        break;
      case 23:
        execute_order_list({5}, acc);
        break;
      case 25:
        execute_order_list({5, 8}, acc);
        break;
      case 26:
        execute_order_list({0, 7}, acc);
        break;
      case 27:
        execute_order_list({5}, acc);
        break;
      case 30:
        execute_order_list({0}, acc);
        break;
      case 31:
        execute_order_list({0}, acc);
        break;
      case 34:
        execute_order_list({0}, acc);
        break;
      case 36:
        execute_order_list({0, 11}, acc);
        break;
      case 37:
        if (branch_l) {
          execute_order_list({0, 11}, acc);
        } else {
          execute_order_list({5}, acc);
        }
        break;
      case 38:
        execute_order_list({5, 11}, acc);
        break;
      case 39:
        execute_order_list({0}, acc);
        break;
      case 40:
        if (branch_l) {
          execute_order_list({0, 11}, acc);
        } else {
          execute_order_list({5}, acc);
        }
        break;
      case 41:
        execute_order_list({0}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void execute_ik1302_589106_gamma_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      default:
        break;
    }
    (void)branch_l;
  }

  void execute_ik1302_589106_post_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 0:
        execute_order_list({25}, acc);
        break;
      case 25:
        execute_order_list({27}, acc);
        break;
      case 26:
        execute_order_list({25, 27}, acc);
        break;
      case 30:
        execute_order_list({29}, acc);
        break;
      case 31:
        execute_order_list({27}, acc);
        break;
      case 34:
        execute_order_list({27}, acc);
        break;
      case 36:
        execute_order_list({27, 32}, acc);
        break;
      case 37:
        execute_order_list({22, 27}, acc);
        break;
      case 38:
        execute_order_list({23}, acc);
        break;
      case 39:
        execute_order_list({27, 32}, acc);
        break;
      case 40:
        execute_order_list({22, 27}, acc);
        break;
      case 41:
        execute_order_list({27}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void tick_ik1302_589106_decoded() {
    static constexpr uint32_t kCommand = 0x589106;
    if (tick_index == 0) command = kCommand;

    const int current_tick = tick_index;
    const bool branch_l = l != 0;
    if (current_tick < 27) {
      sync_address = 6;
    } else if (current_tick < 36) {
      sync_address = 34;
    } else {
      if (current_tick == 36) {
        r[37] = 2 & 0xF;
        r[40] = 98 >> 4;
      }
      sync_address = 95;
    }

    Accumulator acc;
    execute_ik1302_589106_pre_add_orders(current_tick, branch_l, acc);

    if (key_y == 0) t = 0;

    execute_ik1302_589106_gamma_orders(current_tick, branch_l, acc);

    int sum = acc.alpha + acc.beta + acc.gamma;
    acc.sigma = sum & 0xF;
    carry = (sum >> 4) & 1;

    execute_ik1302_589106_post_add_orders(current_tick, branch_l, acc);

    output = memory[current_tick];
    memory[current_tick] = input;
    ++tick_index;
    if (tick_index == 42) tick_index = 0;
  }


  void execute_ik1302_07a2c5_pre_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 1:
        execute_order_list({0}, acc);
        break;
      case 2:
        execute_order_list({0}, acc);
        break;
      case 4:
        execute_order_list({0}, acc);
        break;
      case 5:
        execute_order_list({0}, acc);
        break;
      case 7:
        execute_order_list({0}, acc);
        break;
      case 8:
        execute_order_list({0}, acc);
        break;
      case 10:
        execute_order_list({0}, acc);
        break;
      case 11:
        execute_order_list({0}, acc);
        break;
      case 13:
        execute_order_list({0}, acc);
        break;
      case 14:
        execute_order_list({0}, acc);
        break;
      case 16:
        execute_order_list({0}, acc);
        break;
      case 17:
        execute_order_list({0}, acc);
        break;
      case 19:
        execute_order_list({0}, acc);
        break;
      case 20:
        execute_order_list({0}, acc);
        break;
      case 22:
        execute_order_list({0}, acc);
        break;
      case 23:
        execute_order_list({0}, acc);
        break;
      case 24:
        execute_order_list({5, 8}, acc);
        break;
      case 25:
        execute_order_list({0}, acc);
        break;
      case 26:
        execute_order_list({0}, acc);
        break;
      case 28:
        execute_order_list({0}, acc);
        break;
      case 29:
        execute_order_list({0}, acc);
        break;
      case 31:
        execute_order_list({0}, acc);
        break;
      case 32:
        execute_order_list({0}, acc);
        break;
      case 33:
        execute_order_list({5, 8}, acc);
        break;
      case 34:
        execute_order_list({0}, acc);
        break;
      case 35:
        execute_order_list({0}, acc);
        break;
      case 36:
        execute_order_list({2}, acc);
        break;
      case 39:
        execute_order_list({2}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void execute_ik1302_07a2c5_gamma_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 24:
        execute_order_list({12, 13}, acc);
        break;
      case 33:
        execute_order_list({12, 13}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void execute_ik1302_07a2c5_post_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 1:
        execute_order_list({27}, acc);
        break;
      case 2:
        execute_order_list({17, 22}, acc);
        break;
      case 4:
        execute_order_list({27}, acc);
        break;
      case 5:
        execute_order_list({17, 22}, acc);
        break;
      case 7:
        execute_order_list({27}, acc);
        break;
      case 8:
        execute_order_list({17, 22}, acc);
        break;
      case 10:
        execute_order_list({27}, acc);
        break;
      case 11:
        execute_order_list({17, 22}, acc);
        break;
      case 13:
        execute_order_list({27}, acc);
        break;
      case 14:
        execute_order_list({17, 22}, acc);
        break;
      case 16:
        execute_order_list({27}, acc);
        break;
      case 17:
        execute_order_list({17, 22}, acc);
        break;
      case 19:
        execute_order_list({27}, acc);
        break;
      case 20:
        execute_order_list({17, 22}, acc);
        break;
      case 22:
        execute_order_list({27}, acc);
        break;
      case 23:
        execute_order_list({17, 22}, acc);
        break;
      case 24:
        execute_order_list({25, 27}, acc);
        break;
      case 25:
        execute_order_list({27}, acc);
        break;
      case 26:
        execute_order_list({17, 22}, acc);
        break;
      case 28:
        execute_order_list({27}, acc);
        break;
      case 29:
        execute_order_list({17, 22}, acc);
        break;
      case 31:
        execute_order_list({27}, acc);
        break;
      case 32:
        execute_order_list({17, 22}, acc);
        break;
      case 33:
        execute_order_list({25, 27}, acc);
        break;
      case 34:
        execute_order_list({27}, acc);
        break;
      case 35:
        execute_order_list({17, 22}, acc);
        break;
      case 36:
        execute_order_list({16, 33}, acc);
        break;
      case 39:
        execute_order_list({16, 33}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void tick_ik1302_07a2c5_decoded() {
    static constexpr uint32_t kCommand = 0x07a2c5;
    if (tick_index == 0) command = kCommand;

    const int current_tick = tick_index;
    const bool branch_l = l != 0;
    if (current_tick < 27) {
      sync_address = 69;
    } else if (current_tick < 36) {
      sync_address = 69;
    } else {
      sync_address = 30;
    }

    Accumulator acc;
    execute_ik1302_07a2c5_pre_add_orders(current_tick, branch_l, acc);

    if (key_y == 0) t = 0;

    execute_ik1302_07a2c5_gamma_orders(current_tick, branch_l, acc);

    int sum = acc.alpha + acc.beta + acc.gamma;
    acc.sigma = sum & 0xF;
    carry = (sum >> 4) & 1;

    execute_ik1302_07a2c5_post_add_orders(current_tick, branch_l, acc);

    output = memory[current_tick];
    memory[current_tick] = input;
    ++tick_index;
    if (tick_index == 42) tick_index = 0;
  }


  void execute_ik1302_046060_pre_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 4:
        execute_order_list({1}, acc);
        break;
      case 7:
        execute_order_list({1}, acc);
        break;
      case 10:
        execute_order_list({1}, acc);
        break;
      case 13:
        execute_order_list({1}, acc);
        break;
      case 16:
        execute_order_list({1}, acc);
        break;
      case 19:
        execute_order_list({1}, acc);
        break;
      case 22:
        execute_order_list({1}, acc);
        break;
      case 24:
        execute_order_list({5, 11}, acc);
        break;
      case 25:
        execute_order_list({6}, acc);
        break;
      case 26:
        execute_order_list({8}, acc);
        break;
      case 36:
        execute_order_list({0}, acc);
        break;
      case 37:
        execute_order_list({5}, acc);
        break;
      case 38:
        execute_order_list({11}, acc);
        break;
      case 39:
        execute_order_list({5, 11}, acc);
        break;
      case 40:
        execute_order_list({8}, acc);
        break;
      case 41:
        execute_order_list({0, 7}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void execute_ik1302_046060_gamma_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 36:
        execute_order_list({12}, acc);
        break;
      case 38:
        execute_order_list({12, 13}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void execute_ik1302_046060_post_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 4:
        execute_order_list({27}, acc);
        break;
      case 7:
        execute_order_list({27}, acc);
        break;
      case 10:
        execute_order_list({27}, acc);
        break;
      case 13:
        execute_order_list({27}, acc);
        break;
      case 16:
        execute_order_list({27}, acc);
        break;
      case 19:
        execute_order_list({27}, acc);
        break;
      case 22:
        execute_order_list({27}, acc);
        break;
      case 24:
        execute_order_list({25, 27}, acc);
        break;
      case 25:
        execute_order_list({27}, acc);
        break;
      case 26:
        execute_order_list({27}, acc);
        break;
      case 36:
        execute_order_list({27}, acc);
        break;
      case 37:
        execute_order_list({22}, acc);
        break;
      case 38:
        execute_order_list({27}, acc);
        break;
      case 39:
        execute_order_list({27}, acc);
        break;
      case 40:
        execute_order_list({27}, acc);
        break;
      case 41:
        execute_order_list({25, 27}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void tick_ik1302_046060_decoded() {
    static constexpr uint32_t kCommand = 0x046060;
    if (tick_index == 0) command = kCommand;

    const int current_tick = tick_index;
    const bool branch_l = l != 0;
    if (current_tick < 27) {
      sync_address = 96;
    } else if (current_tick < 36) {
      sync_address = 64;
    } else {
      sync_address = 17;
    }

    Accumulator acc;
    execute_ik1302_046060_pre_add_orders(current_tick, branch_l, acc);

    if (key_y == 0) t = 0;

    execute_ik1302_046060_gamma_orders(current_tick, branch_l, acc);

    int sum = acc.alpha + acc.beta + acc.gamma;
    acc.sigma = sum & 0xF;
    carry = (sum >> 4) & 1;

    execute_ik1302_046060_post_add_orders(current_tick, branch_l, acc);

    output = memory[current_tick];
    memory[current_tick] = input;
    ++tick_index;
    if (tick_index == 42) tick_index = 0;
  }


  void execute_ik1302_02d740_pre_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 29:
        execute_order_list({11}, acc);
        break;
      case 30:
        execute_order_list({0, 3, 7}, acc);
        break;
      case 31:
        execute_order_list({1, 7}, acc);
        break;
      case 32:
        execute_order_list({7, 8}, acc);
        break;
      case 33:
        execute_order_list({4, 7}, acc);
        break;
      case 34:
        execute_order_list({1, 7}, acc);
        break;
      case 36:
        execute_order_list({0, 11}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void execute_ik1302_02d740_gamma_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 29:
        execute_order_list({12, 13}, acc);
        break;
      case 36:
        execute_order_list({12}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void execute_ik1302_02d740_post_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 29:
        execute_order_list({27}, acc);
        break;
      case 30:
        execute_order_list({25, 27}, acc);
        break;
      case 31:
        execute_order_list({25, 27}, acc);
        break;
      case 32:
        if (branch_l) {
          execute_order_list({17, 27}, acc);
        } else {
          execute_order_list({27}, acc);
        }
        break;
      case 33:
        execute_order_list({25, 27}, acc);
        break;
      case 34:
        execute_order_list({25, 27}, acc);
        break;
      case 36:
        execute_order_list({16}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void tick_ik1302_02d740_decoded() {
    static constexpr uint32_t kCommand = 0x02d740;
    if (tick_index == 0) command = kCommand;

    const int current_tick = tick_index;
    const bool branch_l = l != 0;
    if (current_tick < 27) {
      sync_address = 64;
    } else if (current_tick < 36) {
      sync_address = 46;
    } else {
      sync_address = 11;
    }

    Accumulator acc;
    execute_ik1302_02d740_pre_add_orders(current_tick, branch_l, acc);

    if (key_y == 0) t = 0;

    execute_ik1302_02d740_gamma_orders(current_tick, branch_l, acc);

    int sum = acc.alpha + acc.beta + acc.gamma;
    acc.sigma = sum & 0xF;
    carry = (sum >> 4) & 1;

    execute_ik1302_02d740_post_add_orders(current_tick, branch_l, acc);

    output = memory[current_tick];
    memory[current_tick] = input;
    ++tick_index;
    if (tick_index == 42) tick_index = 0;
  }


  void execute_ik1302_03d6c0_pre_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 29:
        execute_order_list({0, 11}, acc);
        break;
      case 31:
        execute_order_list({10}, acc);
        break;
      case 32:
        execute_order_list({0, 7}, acc);
        break;
      case 33:
        execute_order_list({4, 7}, acc);
        break;
      case 34:
        execute_order_list({10}, acc);
        break;
      case 35:
        execute_order_list({8}, acc);
        break;
      case 36:
        if (branch_l) {
          execute_order_list({0, 7}, acc);
        } else {
          execute_order_list({0}, acc);
        }
        break;
      case 37:
        execute_order_list({4, 7}, acc);
        break;
      case 38:
        execute_order_list({5}, acc);
        break;
      case 39:
        execute_order_list({0}, acc);
        break;
      case 40:
        execute_order_list({0}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void execute_ik1302_03d6c0_gamma_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 32:
        execute_order_list({12}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void execute_ik1302_03d6c0_post_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 29:
        execute_order_list({16, 25}, acc);
        break;
      case 31:
        execute_order_list({27}, acc);
        break;
      case 32:
        execute_order_list({25, 27}, acc);
        break;
      case 33:
        execute_order_list({22}, acc);
        break;
      case 34:
        execute_order_list({27}, acc);
        break;
      case 35:
        execute_order_list({27}, acc);
        break;
      case 36:
        if (branch_l) {
          execute_order_list({25, 27}, acc);
        } else {
          execute_order_list({27}, acc);
        }
        break;
      case 37:
        execute_order_list({25, 27}, acc);
        break;
      case 38:
        execute_order_list({23}, acc);
        break;
      case 39:
        execute_order_list({27}, acc);
        break;
      case 40:
        execute_order_list({17, 27}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void tick_ik1302_03d6c0_decoded() {
    static constexpr uint32_t kCommand = 0x03d6c0;
    if (tick_index == 0) command = kCommand;

    const int current_tick = tick_index;
    const bool branch_l = l != 0;
    if (current_tick < 27) {
      sync_address = 64;
    } else if (current_tick < 36) {
      sync_address = 45;
    } else {
      sync_address = 15;
    }

    Accumulator acc;
    execute_ik1302_03d6c0_pre_add_orders(current_tick, branch_l, acc);

    if (key_y == 0) t = 0;

    execute_ik1302_03d6c0_gamma_orders(current_tick, branch_l, acc);

    int sum = acc.alpha + acc.beta + acc.gamma;
    acc.sigma = sum & 0xF;
    carry = (sum >> 4) & 1;

    execute_ik1302_03d6c0_post_add_orders(current_tick, branch_l, acc);

    output = memory[current_tick];
    memory[current_tick] = input;
    ++tick_index;
    if (tick_index == 42) tick_index = 0;
  }


  void execute_ik1302_438fc0_pre_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 27:
        if (branch_l) {
          execute_order_list({0, 7}, acc);
        } else {
          execute_order_list({0, 11}, acc);
        }
        break;
      case 28:
        execute_order_list({11}, acc);
        break;
      case 29:
        execute_order_list({0, 7}, acc);
        break;
      case 30:
        if (branch_l) {
          execute_order_list({0, 7}, acc);
        } else {
          execute_order_list({0}, acc);
        }
        break;
      case 31:
        execute_order_list({5}, acc);
        break;
      case 33:
        execute_order_list({3, 11}, acc);
        break;
      case 34:
        execute_order_list({11}, acc);
        break;
      case 35:
        execute_order_list({8}, acc);
        break;
      case 36:
        if (branch_l) {
          execute_order_list({0, 7}, acc);
        } else {
          execute_order_list({0, 11}, acc);
        }
        break;
      case 38:
        execute_order_list({5, 8}, acc);
        break;
      case 39:
        if (branch_l) {
          execute_order_list({0, 7}, acc);
        } else {
          execute_order_list({0}, acc);
        }
        break;
      case 40:
        execute_order_list({5}, acc);
        break;
      case 41:
        execute_order_list({0}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void execute_ik1302_438fc0_gamma_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 27:
        if (branch_l) {
          execute_order_list({12, 13}, acc);
        }
        break;
      case 28:
        execute_order_list({12, 13}, acc);
        break;
      case 34:
        execute_order_list({12, 13}, acc);
        break;
      case 36:
        if (branch_l) {
          execute_order_list({12, 13}, acc);
        }
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void execute_ik1302_438fc0_post_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 28:
        execute_order_list({27}, acc);
        break;
      case 29:
        execute_order_list({27}, acc);
        break;
      case 30:
        if (branch_l) {
          execute_order_list({25, 27}, acc);
        } else {
          execute_order_list({27}, acc);
        }
        break;
      case 33:
        execute_order_list({25, 27}, acc);
        break;
      case 34:
        execute_order_list({27}, acc);
        break;
      case 35:
        execute_order_list({27}, acc);
        break;
      case 36:
        execute_order_list({16}, acc);
        break;
      case 38:
        execute_order_list({27}, acc);
        break;
      case 39:
        if (branch_l) {
          execute_order_list({25, 27}, acc);
        } else {
          execute_order_list({27}, acc);
        }
        break;
      case 40:
        execute_order_list({22}, acc);
        break;
      case 41:
        execute_order_list({27}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void tick_ik1302_438fc0_decoded() {
    static constexpr uint32_t kCommand = 0x438fc0;
    if (tick_index == 0) command = kCommand;

    const int current_tick = tick_index;
    const bool branch_l = l != 0;
    if (current_tick < 27) {
      sync_address = 64;
    } else if (current_tick < 36) {
      sync_address = 31;
    } else {
      sync_address = 14;
    }

    Accumulator acc;
    execute_ik1302_438fc0_pre_add_orders(current_tick, branch_l, acc);

    if (key_y == 0) t = 0;

    execute_ik1302_438fc0_gamma_orders(current_tick, branch_l, acc);

    int sum = acc.alpha + acc.beta + acc.gamma;
    acc.sigma = sum & 0xF;
    carry = (sum >> 4) & 1;

    execute_ik1302_438fc0_post_add_orders(current_tick, branch_l, acc);

    output = memory[current_tick];
    memory[current_tick] = input;
    ++tick_index;
    if (tick_index == 42) tick_index = 0;
  }


  void execute_ik1302_037b77_pre_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 0:
        execute_order_list({5, 8}, acc);
        break;
      case 1:
        execute_order_list({5}, acc);
        break;
      case 25:
        execute_order_list({5, 8}, acc);
        break;
      case 26:
        execute_order_list({0}, acc);
        break;
      case 27:
        execute_order_list({11}, acc);
        break;
      case 28:
        execute_order_list({5, 10, 11}, acc);
        break;
      case 29:
        execute_order_list({5}, acc);
        break;
      case 30:
        execute_order_list({11}, acc);
        break;
      case 31:
        execute_order_list({5, 11}, acc);
        break;
      case 32:
        execute_order_list({8}, acc);
        break;
      case 33:
        execute_order_list({0, 7}, acc);
        break;
      case 34:
        execute_order_list({5}, acc);
        break;
      case 36:
        if (branch_l) {
          execute_order_list({0, 7}, acc);
        } else {
          execute_order_list({0, 11}, acc);
        }
        break;
      case 39:
        execute_order_list({0, 11}, acc);
        break;
      case 40:
        execute_order_list({5}, acc);
        break;
      case 41:
        execute_order_list({5, 8}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void execute_ik1302_037b77_gamma_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 25:
        execute_order_list({12, 13}, acc);
        break;
      case 27:
        execute_order_list({12, 13}, acc);
        break;
      case 28:
        execute_order_list({12, 13}, acc);
        break;
      case 30:
        execute_order_list({12, 13}, acc);
        break;
      case 36:
        if (branch_l) {
          execute_order_list({12, 13}, acc);
        }
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void execute_ik1302_037b77_post_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 0:
        execute_order_list({27}, acc);
        break;
      case 1:
        execute_order_list({23}, acc);
        break;
      case 25:
        execute_order_list({25, 27}, acc);
        break;
      case 26:
        execute_order_list({17, 27}, acc);
        break;
      case 27:
        execute_order_list({27}, acc);
        break;
      case 28:
        execute_order_list({25, 27}, acc);
        break;
      case 29:
        execute_order_list({26, 29}, acc);
        break;
      case 30:
        execute_order_list({27}, acc);
        break;
      case 31:
        execute_order_list({27}, acc);
        break;
      case 32:
        execute_order_list({27}, acc);
        break;
      case 33:
        execute_order_list({25, 27}, acc);
        break;
      case 34:
        execute_order_list({26, 29}, acc);
        break;
      case 36:
        execute_order_list({16}, acc);
        break;
      case 39:
        execute_order_list({27}, acc);
        break;
      case 40:
        execute_order_list({22}, acc);
        break;
      case 41:
        execute_order_list({27}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void tick_ik1302_037b77_decoded() {
    static constexpr uint32_t kCommand = 0x037b77;
    if (tick_index == 0) command = kCommand;

    const int current_tick = tick_index;
    const bool branch_l = l != 0;
    if (current_tick < 27) {
      sync_address = 119;
    } else if (current_tick < 36) {
      sync_address = 118;
    } else {
      sync_address = 13;
    }

    Accumulator acc;
    execute_ik1302_037b77_pre_add_orders(current_tick, branch_l, acc);

    if (key_y == 0) t = 0;

    execute_ik1302_037b77_gamma_orders(current_tick, branch_l, acc);

    int sum = acc.alpha + acc.beta + acc.gamma;
    acc.sigma = sum & 0xF;
    carry = (sum >> 4) & 1;

    execute_ik1302_037b77_post_add_orders(current_tick, branch_l, acc);

    output = memory[current_tick];
    memory[current_tick] = input;
    ++tick_index;
    if (tick_index == 42) tick_index = 0;
  }


  void execute_ik1302_01d940_pre_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 27:
        execute_order_list({10}, acc);
        break;
      case 28:
        execute_order_list({8}, acc);
        break;
      case 29:
        execute_order_list({5}, acc);
        break;
      case 32:
        execute_order_list({0}, acc);
        break;
      case 35:
        execute_order_list({0}, acc);
        break;
      case 36:
        execute_order_list({5}, acc);
        break;
      case 37:
        execute_order_list({5, 8}, acc);
        break;
      case 38:
        execute_order_list({5}, acc);
        break;
      case 39:
        execute_order_list({0, 10}, acc);
        break;
      case 40:
        execute_order_list({5}, acc);
        break;
      case 41:
        execute_order_list({5, 11}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void execute_ik1302_01d940_gamma_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      default:
        break;
    }
    (void)branch_l;
  }

  void execute_ik1302_01d940_post_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 27:
        execute_order_list({27}, acc);
        break;
      case 28:
        execute_order_list({27}, acc);
        break;
      case 29:
        execute_order_list({26, 29}, acc);
        break;
      case 32:
        execute_order_list({15, 27}, acc);
        break;
      case 35:
        execute_order_list({17, 27}, acc);
        break;
      case 36:
        execute_order_list({26, 29}, acc);
        break;
      case 37:
        execute_order_list({27}, acc);
        break;
      case 38:
        execute_order_list({23}, acc);
        break;
      case 39:
        execute_order_list({27}, acc);
        break;
      case 40:
        execute_order_list({22}, acc);
        break;
      case 41:
        execute_order_list({23}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void tick_ik1302_01d940_decoded() {
    static constexpr uint32_t kCommand = 0x01d940;
    if (tick_index == 0) command = kCommand;

    const int current_tick = tick_index;
    const bool branch_l = l != 0;
    if (current_tick < 27) {
      sync_address = 64;
    } else if (current_tick < 36) {
      sync_address = 50;
    } else {
      sync_address = 7;
    }

    Accumulator acc;
    execute_ik1302_01d940_pre_add_orders(current_tick, branch_l, acc);

    if (key_y == 0) t = 0;

    execute_ik1302_01d940_gamma_orders(current_tick, branch_l, acc);

    int sum = acc.alpha + acc.beta + acc.gamma;
    acc.sigma = sum & 0xF;
    carry = (sum >> 4) & 1;

    execute_ik1302_01d940_post_add_orders(current_tick, branch_l, acc);

    output = memory[current_tick];
    memory[current_tick] = input;
    ++tick_index;
    if (tick_index == 42) tick_index = 0;
  }


  void execute_ik1302_34de40_pre_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 27:
        execute_order_list({11}, acc);
        break;
      case 28:
        execute_order_list({8}, acc);
        break;
      case 29:
        execute_order_list({5}, acc);
        break;
      case 30:
        execute_order_list({0}, acc);
        break;
      case 32:
        execute_order_list({0}, acc);
        break;
      case 33:
        execute_order_list({0, 11}, acc);
        break;
      case 34:
        execute_order_list({0, 3, 7}, acc);
        break;
      case 35:
        execute_order_list({0}, acc);
        break;
      case 36:
        execute_order_list({0, 11}, acc);
        break;
      case 37:
        if (branch_l) {
          execute_order_list({0, 11}, acc);
        } else {
          execute_order_list({5}, acc);
        }
        break;
      case 38:
        execute_order_list({5, 11}, acc);
        break;
      case 39:
        execute_order_list({0}, acc);
        break;
      case 40:
        if (branch_l) {
          execute_order_list({0, 11}, acc);
        } else {
          execute_order_list({5}, acc);
        }
        break;
      case 41:
        execute_order_list({0}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void execute_ik1302_34de40_gamma_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 27:
        execute_order_list({12, 13}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void execute_ik1302_34de40_post_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 27:
        execute_order_list({27}, acc);
        break;
      case 28:
        execute_order_list({27}, acc);
        break;
      case 29:
        execute_order_list({26, 29}, acc);
        break;
      case 30:
        execute_order_list({27}, acc);
        break;
      case 32:
        execute_order_list({17, 27}, acc);
        break;
      case 33:
        execute_order_list({27}, acc);
        break;
      case 34:
        execute_order_list({25, 27}, acc);
        break;
      case 35:
        execute_order_list({17, 27}, acc);
        break;
      case 36:
        execute_order_list({27, 32}, acc);
        break;
      case 37:
        execute_order_list({22, 27}, acc);
        break;
      case 38:
        execute_order_list({23}, acc);
        break;
      case 39:
        execute_order_list({27, 32}, acc);
        break;
      case 40:
        execute_order_list({22, 27}, acc);
        break;
      case 41:
        execute_order_list({27}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void tick_ik1302_34de40_decoded() {
    static constexpr uint32_t kCommand = 0x34de40;
    if (tick_index == 0) command = kCommand;

    const int current_tick = tick_index;
    const bool branch_l = l != 0;
    if (current_tick < 27) {
      sync_address = 64;
    } else if (current_tick < 36) {
      sync_address = 60;
    } else {
      if (current_tick == 36) {
        r[37] = 3 & 0xF;
        r[40] = 211 >> 4;
      }
      sync_address = 95;
    }

    Accumulator acc;
    execute_ik1302_34de40_pre_add_orders(current_tick, branch_l, acc);

    if (key_y == 0) t = 0;

    execute_ik1302_34de40_gamma_orders(current_tick, branch_l, acc);

    int sum = acc.alpha + acc.beta + acc.gamma;
    acc.sigma = sum & 0xF;
    carry = (sum >> 4) & 1;

    execute_ik1302_34de40_post_add_orders(current_tick, branch_l, acc);

    output = memory[current_tick];
    memory[current_tick] = input;
    ++tick_index;
    if (tick_index == 42) tick_index = 0;
  }


  void execute_ik1302_24fac0_pre_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 27:
        execute_order_list({5, 8}, acc);
        break;
      case 28:
        execute_order_list({5}, acc);
        break;
      case 33:
        execute_order_list({10}, acc);
        break;
      case 34:
        execute_order_list({8}, acc);
        break;
      case 35:
        execute_order_list({0, 7}, acc);
        break;
      case 36:
        execute_order_list({0, 11}, acc);
        break;
      case 37:
        if (branch_l) {
          execute_order_list({0, 11}, acc);
        } else {
          execute_order_list({5}, acc);
        }
        break;
      case 38:
        execute_order_list({5, 11}, acc);
        break;
      case 39:
        execute_order_list({0}, acc);
        break;
      case 40:
        if (branch_l) {
          execute_order_list({0, 11}, acc);
        } else {
          execute_order_list({5}, acc);
        }
        break;
      case 41:
        execute_order_list({0}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void execute_ik1302_24fac0_gamma_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      default:
        break;
    }
    (void)branch_l;
  }

  void execute_ik1302_24fac0_post_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 27:
        execute_order_list({27}, acc);
        break;
      case 28:
        execute_order_list({23}, acc);
        break;
      case 33:
        execute_order_list({27}, acc);
        break;
      case 34:
        execute_order_list({27}, acc);
        break;
      case 35:
        execute_order_list({25, 27}, acc);
        break;
      case 36:
        execute_order_list({27, 32}, acc);
        break;
      case 37:
        execute_order_list({22, 27}, acc);
        break;
      case 38:
        execute_order_list({23}, acc);
        break;
      case 39:
        execute_order_list({27, 32}, acc);
        break;
      case 40:
        execute_order_list({22, 27}, acc);
        break;
      case 41:
        execute_order_list({27}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void tick_ik1302_24fac0_decoded() {
    static constexpr uint32_t kCommand = 0x24fac0;
    if (tick_index == 0) command = kCommand;

    const int current_tick = tick_index;
    const bool branch_l = l != 0;
    if (current_tick < 27) {
      sync_address = 64;
    } else if (current_tick < 36) {
      sync_address = 117;
    } else {
      if (current_tick == 36) {
        r[37] = 3 & 0xF;
        r[40] = 147 >> 4;
      }
      sync_address = 95;
    }

    Accumulator acc;
    execute_ik1302_24fac0_pre_add_orders(current_tick, branch_l, acc);

    if (key_y == 0) t = 0;

    execute_ik1302_24fac0_gamma_orders(current_tick, branch_l, acc);

    int sum = acc.alpha + acc.beta + acc.gamma;
    acc.sigma = sum & 0xF;
    carry = (sum >> 4) & 1;

    execute_ik1302_24fac0_post_add_orders(current_tick, branch_l, acc);

    output = memory[current_tick];
    memory[current_tick] = input;
    ++tick_index;
    if (tick_index == 42) tick_index = 0;
  }


  void execute_ik1302_011977_pre_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 0:
        execute_order_list({5, 8}, acc);
        break;
      case 1:
        execute_order_list({5}, acc);
        break;
      case 25:
        execute_order_list({5, 8}, acc);
        break;
      case 26:
        execute_order_list({0}, acc);
        break;
      case 27:
        execute_order_list({10}, acc);
        break;
      case 28:
        execute_order_list({8}, acc);
        break;
      case 29:
        execute_order_list({5}, acc);
        break;
      case 32:
        execute_order_list({0}, acc);
        break;
      case 35:
        execute_order_list({0}, acc);
        break;
      case 37:
        execute_order_list({5}, acc);
        break;
      case 38:
        execute_order_list({5, 8}, acc);
        break;
      case 39:
        execute_order_list({8}, acc);
        break;
      case 40:
        execute_order_list({4, 7}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void execute_ik1302_011977_gamma_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 25:
        execute_order_list({12, 13}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void execute_ik1302_011977_post_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 0:
        execute_order_list({27}, acc);
        break;
      case 1:
        execute_order_list({23}, acc);
        break;
      case 25:
        execute_order_list({25, 27}, acc);
        break;
      case 26:
        execute_order_list({17, 27}, acc);
        break;
      case 27:
        execute_order_list({27}, acc);
        break;
      case 28:
        execute_order_list({27}, acc);
        break;
      case 29:
        execute_order_list({26, 29}, acc);
        break;
      case 32:
        execute_order_list({15, 27}, acc);
        break;
      case 35:
        execute_order_list({17, 27}, acc);
        break;
      case 37:
        execute_order_list({22}, acc);
        break;
      case 38:
        execute_order_list({27}, acc);
        break;
      case 39:
        execute_order_list({27}, acc);
        break;
      case 40:
        execute_order_list({25, 27}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void tick_ik1302_011977_decoded() {
    static constexpr uint32_t kCommand = 0x011977;
    if (tick_index == 0) command = kCommand;

    const int current_tick = tick_index;
    const bool branch_l = l != 0;
    if (current_tick < 27) {
      sync_address = 119;
    } else if (current_tick < 36) {
      sync_address = 50;
    } else {
      sync_address = 4;
    }

    Accumulator acc;
    execute_ik1302_011977_pre_add_orders(current_tick, branch_l, acc);

    if (key_y == 0) t = 0;

    execute_ik1302_011977_gamma_orders(current_tick, branch_l, acc);

    int sum = acc.alpha + acc.beta + acc.gamma;
    acc.sigma = sum & 0xF;
    carry = (sum >> 4) & 1;

    execute_ik1302_011977_post_add_orders(current_tick, branch_l, acc);

    output = memory[current_tick];
    memory[current_tick] = input;
    ++tick_index;
    if (tick_index == 42) tick_index = 0;
  }


  void execute_ik1302_085068_pre_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 2:
        execute_order_list({0}, acc);
        break;
      case 3:
        execute_order_list({5}, acc);
        break;
      case 4:
        execute_order_list({5}, acc);
        break;
      case 5:
        execute_order_list({0}, acc);
        break;
      case 6:
        execute_order_list({5}, acc);
        break;
      case 7:
        execute_order_list({5}, acc);
        break;
      case 8:
        execute_order_list({0}, acc);
        break;
      case 9:
        execute_order_list({5}, acc);
        break;
      case 10:
        execute_order_list({5}, acc);
        break;
      case 11:
        execute_order_list({0}, acc);
        break;
      case 12:
        execute_order_list({5}, acc);
        break;
      case 13:
        execute_order_list({5}, acc);
        break;
      case 14:
        execute_order_list({0}, acc);
        break;
      case 15:
        execute_order_list({5}, acc);
        break;
      case 16:
        execute_order_list({5}, acc);
        break;
      case 17:
        execute_order_list({0}, acc);
        break;
      case 18:
        execute_order_list({5}, acc);
        break;
      case 19:
        execute_order_list({5}, acc);
        break;
      case 20:
        execute_order_list({0}, acc);
        break;
      case 21:
        execute_order_list({5}, acc);
        break;
      case 22:
        execute_order_list({5}, acc);
        break;
      case 23:
        execute_order_list({0}, acc);
        break;
      case 24:
        execute_order_list({10}, acc);
        break;
      case 25:
        execute_order_list({8}, acc);
        break;
      case 26:
        execute_order_list({0}, acc);
        break;
      case 35:
        execute_order_list({5, 8}, acc);
        break;
      case 36:
        execute_order_list({0, 11}, acc);
        break;
      case 37:
        if (branch_l) {
          execute_order_list({0, 11}, acc);
        } else {
          execute_order_list({5}, acc);
        }
        break;
      case 38:
        execute_order_list({5, 11}, acc);
        break;
      case 39:
        execute_order_list({0}, acc);
        break;
      case 40:
        if (branch_l) {
          execute_order_list({0, 11}, acc);
        } else {
          execute_order_list({5}, acc);
        }
        break;
      case 41:
        execute_order_list({0}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void execute_ik1302_085068_gamma_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 35:
        execute_order_list({12, 13}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void execute_ik1302_085068_post_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 2:
        execute_order_list({27}, acc);
        break;
      case 3:
        execute_order_list({26, 29}, acc);
        break;
      case 4:
        execute_order_list({23}, acc);
        break;
      case 5:
        execute_order_list({27}, acc);
        break;
      case 6:
        execute_order_list({26, 29}, acc);
        break;
      case 7:
        execute_order_list({23}, acc);
        break;
      case 8:
        execute_order_list({27}, acc);
        break;
      case 9:
        execute_order_list({26, 29}, acc);
        break;
      case 10:
        execute_order_list({23}, acc);
        break;
      case 11:
        execute_order_list({27}, acc);
        break;
      case 12:
        execute_order_list({26, 29}, acc);
        break;
      case 13:
        execute_order_list({23}, acc);
        break;
      case 14:
        execute_order_list({27}, acc);
        break;
      case 15:
        execute_order_list({26, 29}, acc);
        break;
      case 16:
        execute_order_list({23}, acc);
        break;
      case 17:
        execute_order_list({27}, acc);
        break;
      case 18:
        execute_order_list({26, 29}, acc);
        break;
      case 19:
        execute_order_list({23}, acc);
        break;
      case 20:
        execute_order_list({27}, acc);
        break;
      case 21:
        execute_order_list({26, 29}, acc);
        break;
      case 22:
        execute_order_list({23}, acc);
        break;
      case 23:
        execute_order_list({27}, acc);
        break;
      case 24:
        execute_order_list({27}, acc);
        break;
      case 25:
        execute_order_list({27}, acc);
        break;
      case 26:
        execute_order_list({29}, acc);
        break;
      case 35:
        execute_order_list({25, 27}, acc);
        break;
      case 36:
        execute_order_list({27, 32}, acc);
        break;
      case 37:
        execute_order_list({22, 27}, acc);
        break;
      case 38:
        execute_order_list({23}, acc);
        break;
      case 39:
        execute_order_list({27, 32}, acc);
        break;
      case 40:
        execute_order_list({22, 27}, acc);
        break;
      case 41:
        execute_order_list({27}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void tick_ik1302_085068_decoded() {
    static constexpr uint32_t kCommand = 0x085068;
    if (tick_index == 0) command = kCommand;

    const int current_tick = tick_index;
    const bool branch_l = l != 0;
    if (current_tick < 27) {
      sync_address = 104;
    } else if (current_tick < 36) {
      sync_address = 32;
    } else {
      if (current_tick == 36) {
        r[37] = 1 & 0xF;
        r[40] = 33 >> 4;
      }
      sync_address = 95;
    }

    Accumulator acc;
    execute_ik1302_085068_pre_add_orders(current_tick, branch_l, acc);

    if (key_y == 0) t = 0;

    execute_ik1302_085068_gamma_orders(current_tick, branch_l, acc);

    int sum = acc.alpha + acc.beta + acc.gamma;
    acc.sigma = sum & 0xF;
    carry = (sum >> 4) & 1;

    execute_ik1302_085068_post_add_orders(current_tick, branch_l, acc);

    output = memory[current_tick];
    memory[current_tick] = input;
    ++tick_index;
    if (tick_index == 42) tick_index = 0;
  }


  void execute_ik1302_4344c0_pre_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 27:
        execute_order_list({0, 11}, acc);
        break;
      case 28:
        execute_order_list({5, 11}, acc);
        break;
      case 29:
        execute_order_list({5, 11}, acc);
        break;
      case 30:
        execute_order_list({10}, acc);
        break;
      case 31:
        execute_order_list({8}, acc);
        break;
      case 32:
        execute_order_list({5, 9}, acc);
        break;
      case 33:
        execute_order_list({5, 8}, acc);
        break;
      case 34:
        execute_order_list({8}, acc);
        break;
      case 35:
        execute_order_list({5, 11}, acc);
        break;
      case 36:
        if (branch_l) {
          execute_order_list({0, 7}, acc);
        } else {
          execute_order_list({0, 11}, acc);
        }
        break;
      case 39:
        execute_order_list({0, 11}, acc);
        break;
      case 40:
        execute_order_list({5}, acc);
        break;
      case 41:
        execute_order_list({5, 8}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void execute_ik1302_4344c0_gamma_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 36:
        if (branch_l) {
          execute_order_list({12, 13}, acc);
        }
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void execute_ik1302_4344c0_post_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 27:
        execute_order_list({27}, acc);
        break;
      case 28:
        execute_order_list({27}, acc);
        break;
      case 30:
        execute_order_list({27}, acc);
        break;
      case 31:
        execute_order_list({27}, acc);
        break;
      case 32:
        execute_order_list({25, 27}, acc);
        break;
      case 33:
        execute_order_list({27}, acc);
        break;
      case 34:
        execute_order_list({27}, acc);
        break;
      case 35:
        execute_order_list({27}, acc);
        break;
      case 36:
        execute_order_list({16}, acc);
        break;
      case 39:
        execute_order_list({27}, acc);
        break;
      case 40:
        execute_order_list({22}, acc);
        break;
      case 41:
        execute_order_list({27}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void tick_ik1302_4344c0_decoded() {
    static constexpr uint32_t kCommand = 0x4344c0;
    if (tick_index == 0) command = kCommand;

    const int current_tick = tick_index;
    const bool branch_l = l != 0;
    if (current_tick < 27) {
      sync_address = 64;
    } else if (current_tick < 36) {
      sync_address = 9;
    } else {
      sync_address = 13;
    }

    Accumulator acc;
    execute_ik1302_4344c0_pre_add_orders(current_tick, branch_l, acc);

    if (key_y == 0) t = 0;

    execute_ik1302_4344c0_gamma_orders(current_tick, branch_l, acc);

    int sum = acc.alpha + acc.beta + acc.gamma;
    acc.sigma = sum & 0xF;
    carry = (sum >> 4) & 1;

    execute_ik1302_4344c0_post_add_orders(current_tick, branch_l, acc);

    output = memory[current_tick];
    memory[current_tick] = input;
    ++tick_index;
    if (tick_index == 42) tick_index = 0;
  }


  void execute_ik1302_63e06f_pre_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 2:
        execute_order_list({0}, acc);
        break;
      case 24:
        execute_order_list({10}, acc);
        break;
      case 25:
        execute_order_list({5, 11}, acc);
        break;
      case 26:
        execute_order_list({9}, acc);
        break;
      case 36:
        execute_order_list({0, 11}, acc);
        break;
      case 37:
        if (branch_l) {
          execute_order_list({0, 11}, acc);
        } else {
          execute_order_list({5}, acc);
        }
        break;
      case 38:
        execute_order_list({5, 11}, acc);
        break;
      case 39:
        execute_order_list({0}, acc);
        break;
      case 40:
        if (branch_l) {
          execute_order_list({0, 11}, acc);
        } else {
          execute_order_list({5}, acc);
        }
        break;
      case 41:
        execute_order_list({0}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void execute_ik1302_63e06f_gamma_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      default:
        break;
    }
    (void)branch_l;
  }

  void execute_ik1302_63e06f_post_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 2:
        execute_order_list({29}, acc);
        break;
      case 24:
        execute_order_list({27}, acc);
        break;
      case 25:
        execute_order_list({27}, acc);
        break;
      case 36:
        execute_order_list({27, 32}, acc);
        break;
      case 37:
        execute_order_list({22, 27}, acc);
        break;
      case 38:
        execute_order_list({23}, acc);
        break;
      case 39:
        execute_order_list({27, 32}, acc);
        break;
      case 40:
        execute_order_list({22, 27}, acc);
        break;
      case 41:
        execute_order_list({27}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void tick_ik1302_63e06f_decoded() {
    static constexpr uint32_t kCommand = 0x63e06f;
    if (tick_index == 0) command = kCommand;

    const int current_tick = tick_index;
    const bool branch_l = l != 0;
    if (current_tick < 27) {
      sync_address = 111;
    } else if (current_tick < 36) {
      sync_address = 64;
    } else {
      if (current_tick == 36) {
        r[37] = 15 & 0xF;
        r[40] = 143 >> 4;
      }
      sync_address = 95;
    }

    Accumulator acc;
    execute_ik1302_63e06f_pre_add_orders(current_tick, branch_l, acc);

    if (key_y == 0) t = 0;

    execute_ik1302_63e06f_gamma_orders(current_tick, branch_l, acc);

    int sum = acc.alpha + acc.beta + acc.gamma;
    acc.sigma = sum & 0xF;
    carry = (sum >> 4) & 1;

    execute_ik1302_63e06f_post_add_orders(current_tick, branch_l, acc);

    output = memory[current_tick];
    memory[current_tick] = input;
    ++tick_index;
    if (tick_index == 42) tick_index = 0;
  }


  void execute_ik1302_5459cc_pre_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 5:
        execute_order_list({0, 7, 8}, acc);
        break;
      case 8:
        execute_order_list({0, 7, 8}, acc);
        break;
      case 11:
        execute_order_list({0, 7, 8}, acc);
        break;
      case 14:
        execute_order_list({0, 7, 8}, acc);
        break;
      case 17:
        execute_order_list({0, 7, 8}, acc);
        break;
      case 20:
        execute_order_list({0, 7, 8}, acc);
        break;
      case 23:
        execute_order_list({0, 7, 8}, acc);
        break;
      case 24:
        execute_order_list({5}, acc);
        break;
      case 26:
        execute_order_list({10}, acc);
        break;
      case 27:
        execute_order_list({0}, acc);
        break;
      case 30:
        execute_order_list({0}, acc);
        break;
      case 33:
        execute_order_list({0}, acc);
        break;
      case 36:
        execute_order_list({0, 11}, acc);
        break;
      case 37:
        if (branch_l) {
          execute_order_list({0, 11}, acc);
        } else {
          execute_order_list({5}, acc);
        }
        break;
      case 38:
        execute_order_list({5, 11}, acc);
        break;
      case 39:
        execute_order_list({0}, acc);
        break;
      case 40:
        if (branch_l) {
          execute_order_list({0, 11}, acc);
        } else {
          execute_order_list({5}, acc);
        }
        break;
      case 41:
        execute_order_list({0}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void execute_ik1302_5459cc_gamma_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      default:
        break;
    }
    (void)branch_l;
  }

  void execute_ik1302_5459cc_post_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 5:
        execute_order_list({25, 27}, acc);
        break;
      case 8:
        execute_order_list({25, 27}, acc);
        break;
      case 11:
        execute_order_list({25, 27}, acc);
        break;
      case 14:
        execute_order_list({25, 27}, acc);
        break;
      case 17:
        execute_order_list({25, 27}, acc);
        break;
      case 20:
        execute_order_list({25, 27}, acc);
        break;
      case 23:
        execute_order_list({25, 27}, acc);
        break;
      case 26:
        execute_order_list({27}, acc);
        break;
      case 27:
        execute_order_list({27, 32}, acc);
        break;
      case 30:
        execute_order_list({27, 32}, acc);
        break;
      case 33:
        execute_order_list({27, 32}, acc);
        break;
      case 36:
        execute_order_list({27, 32}, acc);
        break;
      case 37:
        execute_order_list({22, 27}, acc);
        break;
      case 38:
        execute_order_list({23}, acc);
        break;
      case 39:
        execute_order_list({27, 32}, acc);
        break;
      case 40:
        execute_order_list({22, 27}, acc);
        break;
      case 41:
        execute_order_list({27}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void tick_ik1302_5459cc_decoded() {
    static constexpr uint32_t kCommand = 0x5459cc;
    if (tick_index == 0) command = kCommand;

    const int current_tick = tick_index;
    const bool branch_l = l != 0;
    if (current_tick < 27) {
      sync_address = 76;
    } else if (current_tick < 36) {
      sync_address = 51;
    } else {
      if (current_tick == 36) {
        r[37] = 1 & 0xF;
        r[40] = 81 >> 4;
      }
      sync_address = 95;
    }

    Accumulator acc;
    execute_ik1302_5459cc_pre_add_orders(current_tick, branch_l, acc);

    if (key_y == 0) t = 0;

    execute_ik1302_5459cc_gamma_orders(current_tick, branch_l, acc);

    int sum = acc.alpha + acc.beta + acc.gamma;
    acc.sigma = sum & 0xF;
    carry = (sum >> 4) & 1;

    execute_ik1302_5459cc_post_add_orders(current_tick, branch_l, acc);

    output = memory[current_tick];
    memory[current_tick] = input;
    ++tick_index;
    if (tick_index == 42) tick_index = 0;
  }


  void execute_ik1302_061cb9_pre_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 1:
        execute_order_list({0}, acc);
        break;
      case 2:
        execute_order_list({1}, acc);
        break;
      case 4:
        execute_order_list({0}, acc);
        break;
      case 5:
        execute_order_list({1}, acc);
        break;
      case 7:
        execute_order_list({0}, acc);
        break;
      case 8:
        execute_order_list({1}, acc);
        break;
      case 10:
        execute_order_list({0}, acc);
        break;
      case 11:
        execute_order_list({1}, acc);
        break;
      case 13:
        execute_order_list({0}, acc);
        break;
      case 14:
        execute_order_list({1}, acc);
        break;
      case 16:
        execute_order_list({0}, acc);
        break;
      case 17:
        execute_order_list({1}, acc);
        break;
      case 19:
        execute_order_list({0}, acc);
        break;
      case 20:
        execute_order_list({1}, acc);
        break;
      case 22:
        execute_order_list({0}, acc);
        break;
      case 23:
        execute_order_list({1}, acc);
        break;
      case 25:
        execute_order_list({0}, acc);
        break;
      case 26:
        execute_order_list({1}, acc);
        break;
      case 28:
        execute_order_list({0}, acc);
        break;
      case 29:
        execute_order_list({1}, acc);
        break;
      case 31:
        execute_order_list({0}, acc);
        break;
      case 32:
        execute_order_list({1}, acc);
        break;
      case 34:
        execute_order_list({0}, acc);
        break;
      case 35:
        execute_order_list({1}, acc);
        break;
      case 36:
        execute_order_list({2}, acc);
        break;
      case 37:
        execute_order_list({0}, acc);
        break;
      case 38:
        execute_order_list({1}, acc);
        break;
      case 39:
        execute_order_list({2}, acc);
        break;
      case 40:
        execute_order_list({0}, acc);
        break;
      case 41:
        execute_order_list({1}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void execute_ik1302_061cb9_gamma_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      default:
        break;
    }
    (void)branch_l;
  }

  void execute_ik1302_061cb9_post_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 1:
        execute_order_list({27}, acc);
        break;
      case 2:
        execute_order_list({22, 24}, acc);
        break;
      case 4:
        execute_order_list({27}, acc);
        break;
      case 5:
        execute_order_list({22, 24}, acc);
        break;
      case 7:
        execute_order_list({27}, acc);
        break;
      case 8:
        execute_order_list({22, 24}, acc);
        break;
      case 10:
        execute_order_list({27}, acc);
        break;
      case 11:
        execute_order_list({22, 24}, acc);
        break;
      case 13:
        execute_order_list({27}, acc);
        break;
      case 14:
        execute_order_list({22, 24}, acc);
        break;
      case 16:
        execute_order_list({27}, acc);
        break;
      case 17:
        execute_order_list({22, 24}, acc);
        break;
      case 19:
        execute_order_list({27}, acc);
        break;
      case 20:
        execute_order_list({22, 24}, acc);
        break;
      case 22:
        execute_order_list({27}, acc);
        break;
      case 23:
        execute_order_list({22, 24}, acc);
        break;
      case 25:
        execute_order_list({27}, acc);
        break;
      case 26:
        execute_order_list({22, 24}, acc);
        break;
      case 28:
        execute_order_list({27}, acc);
        break;
      case 29:
        execute_order_list({22, 24}, acc);
        break;
      case 31:
        execute_order_list({27}, acc);
        break;
      case 32:
        execute_order_list({22, 24}, acc);
        break;
      case 34:
        execute_order_list({27}, acc);
        break;
      case 35:
        execute_order_list({22, 24}, acc);
        break;
      case 36:
        execute_order_list({16, 33}, acc);
        break;
      case 37:
        execute_order_list({27}, acc);
        break;
      case 38:
        execute_order_list({22, 24}, acc);
        break;
      case 39:
        execute_order_list({16, 33}, acc);
        break;
      case 40:
        execute_order_list({27}, acc);
        break;
      case 41:
        execute_order_list({22, 24}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void tick_ik1302_061cb9_decoded() {
    static constexpr uint32_t kCommand = 0x061cb9;
    if (tick_index == 0) command = kCommand;

    const int current_tick = tick_index;
    const bool branch_l = l != 0;
    if (current_tick < 27) {
      sync_address = 57;
    } else if (current_tick < 36) {
      sync_address = 57;
    } else {
      sync_address = 24;
    }

    Accumulator acc;
    execute_ik1302_061cb9_pre_add_orders(current_tick, branch_l, acc);

    if (key_y == 0) t = 0;

    execute_ik1302_061cb9_gamma_orders(current_tick, branch_l, acc);

    int sum = acc.alpha + acc.beta + acc.gamma;
    acc.sigma = sum & 0xF;
    carry = (sum >> 4) & 1;

    execute_ik1302_061cb9_post_add_orders(current_tick, branch_l, acc);

    output = memory[current_tick];
    memory[current_tick] = input;
    ++tick_index;
    if (tick_index == 42) tick_index = 0;
  }


  void execute_ik1302_438bc0_pre_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 27:
        if (branch_l) {
          execute_order_list({0, 11}, acc);
        } else {
          execute_order_list({5}, acc);
        }
        break;
      case 28:
        execute_order_list({5}, acc);
        break;
      case 30:
        execute_order_list({10}, acc);
        break;
      case 31:
        execute_order_list({5, 9}, acc);
        break;
      case 32:
        execute_order_list({5, 8}, acc);
        break;
      case 33:
        execute_order_list({8}, acc);
        break;
      case 34:
        execute_order_list({5, 11}, acc);
        break;
      case 35:
        execute_order_list({8}, acc);
        break;
      case 36:
        if (branch_l) {
          execute_order_list({0, 7}, acc);
        } else {
          execute_order_list({0, 11}, acc);
        }
        break;
      case 38:
        execute_order_list({5, 8}, acc);
        break;
      case 39:
        if (branch_l) {
          execute_order_list({0, 7}, acc);
        } else {
          execute_order_list({0}, acc);
        }
        break;
      case 40:
        execute_order_list({5}, acc);
        break;
      case 41:
        execute_order_list({0}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void execute_ik1302_438bc0_gamma_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 36:
        if (branch_l) {
          execute_order_list({12, 13}, acc);
        }
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void execute_ik1302_438bc0_post_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 27:
        execute_order_list({27}, acc);
        break;
      case 30:
        execute_order_list({27}, acc);
        break;
      case 31:
        execute_order_list({25, 27}, acc);
        break;
      case 32:
        execute_order_list({27}, acc);
        break;
      case 33:
        execute_order_list({27}, acc);
        break;
      case 34:
        execute_order_list({27}, acc);
        break;
      case 35:
        execute_order_list({27}, acc);
        break;
      case 36:
        execute_order_list({16}, acc);
        break;
      case 38:
        execute_order_list({27}, acc);
        break;
      case 39:
        if (branch_l) {
          execute_order_list({25, 27}, acc);
        } else {
          execute_order_list({27}, acc);
        }
        break;
      case 40:
        execute_order_list({22}, acc);
        break;
      case 41:
        execute_order_list({27}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void tick_ik1302_438bc0_decoded() {
    static constexpr uint32_t kCommand = 0x438bc0;
    if (tick_index == 0) command = kCommand;

    const int current_tick = tick_index;
    const bool branch_l = l != 0;
    if (current_tick < 27) {
      sync_address = 64;
    } else if (current_tick < 36) {
      sync_address = 23;
    } else {
      sync_address = 14;
    }

    Accumulator acc;
    execute_ik1302_438bc0_pre_add_orders(current_tick, branch_l, acc);

    if (key_y == 0) t = 0;

    execute_ik1302_438bc0_gamma_orders(current_tick, branch_l, acc);

    int sum = acc.alpha + acc.beta + acc.gamma;
    acc.sigma = sum & 0xF;
    carry = (sum >> 4) & 1;

    execute_ik1302_438bc0_post_add_orders(current_tick, branch_l, acc);

    output = memory[current_tick];
    memory[current_tick] = input;
    ++tick_index;
    if (tick_index == 42) tick_index = 0;
  }


  void execute_ik1302_0179c0_pre_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 34:
        execute_order_list({10}, acc);
        break;
      case 35:
        execute_order_list({5, 11}, acc);
        break;
      case 36:
        execute_order_list({9}, acc);
        break;
      case 40:
        execute_order_list({5, 11}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void execute_ik1302_0179c0_gamma_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      default:
        break;
    }
    (void)branch_l;
  }

  void execute_ik1302_0179c0_post_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 30:
        execute_order_list({22}, acc);
        break;
      case 34:
        execute_order_list({27}, acc);
        break;
      case 35:
        execute_order_list({27}, acc);
        break;
      case 36:
        execute_order_list({16}, acc);
        break;
      case 40:
        execute_order_list({23}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void tick_ik1302_0179c0_decoded() {
    static constexpr uint32_t kCommand = 0x0179c0;
    if (tick_index == 0) command = kCommand;

    const int current_tick = tick_index;
    const bool branch_l = l != 0;
    if (current_tick < 27) {
      sync_address = 64;
    } else if (current_tick < 36) {
      sync_address = 115;
    } else {
      sync_address = 5;
    }

    Accumulator acc;
    execute_ik1302_0179c0_pre_add_orders(current_tick, branch_l, acc);

    if (key_y == 0) t = 0;

    execute_ik1302_0179c0_gamma_orders(current_tick, branch_l, acc);

    int sum = acc.alpha + acc.beta + acc.gamma;
    acc.sigma = sum & 0xF;
    carry = (sum >> 4) & 1;

    execute_ik1302_0179c0_post_add_orders(current_tick, branch_l, acc);

    output = memory[current_tick];
    memory[current_tick] = input;
    ++tick_index;
    if (tick_index == 42) tick_index = 0;
  }


  void execute_ik1302_3854f3_pre_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 25:
        execute_order_list({10}, acc);
        break;
      case 26:
        execute_order_list({5, 11}, acc);
        break;
      case 27:
        execute_order_list({3, 11}, acc);
        break;
      case 28:
        execute_order_list({4, 7}, acc);
        break;
      case 30:
        execute_order_list({3}, acc);
        break;
      case 31:
        execute_order_list({4, 7}, acc);
        break;
      case 32:
        execute_order_list({11}, acc);
        break;
      case 33:
        execute_order_list({5, 10, 11}, acc);
        break;
      case 34:
        execute_order_list({5}, acc);
        break;
      case 35:
        execute_order_list({5, 8}, acc);
        break;
      case 36:
        execute_order_list({0, 11}, acc);
        break;
      case 37:
        if (branch_l) {
          execute_order_list({0, 11}, acc);
        } else {
          execute_order_list({5}, acc);
        }
        break;
      case 38:
        execute_order_list({5, 11}, acc);
        break;
      case 39:
        execute_order_list({0}, acc);
        break;
      case 40:
        if (branch_l) {
          execute_order_list({0, 11}, acc);
        } else {
          execute_order_list({5}, acc);
        }
        break;
      case 41:
        execute_order_list({0}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void execute_ik1302_3854f3_gamma_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 30:
        execute_order_list({12}, acc);
        break;
      case 32:
        execute_order_list({12, 13}, acc);
        break;
      case 33:
        execute_order_list({12, 13}, acc);
        break;
      case 35:
        execute_order_list({12, 13}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void execute_ik1302_3854f3_post_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 3:
        execute_order_list({22}, acc);
        break;
      case 6:
        execute_order_list({22}, acc);
        break;
      case 9:
        execute_order_list({22}, acc);
        break;
      case 12:
        execute_order_list({22}, acc);
        break;
      case 15:
        execute_order_list({22}, acc);
        break;
      case 18:
        execute_order_list({22}, acc);
        break;
      case 21:
        execute_order_list({22}, acc);
        break;
      case 25:
        execute_order_list({27}, acc);
        break;
      case 26:
        execute_order_list({27}, acc);
        break;
      case 27:
        execute_order_list({25, 27}, acc);
        break;
      case 28:
        execute_order_list({22}, acc);
        break;
      case 30:
        execute_order_list({25, 27}, acc);
        break;
      case 31:
        execute_order_list({22}, acc);
        break;
      case 32:
        execute_order_list({27}, acc);
        break;
      case 33:
        execute_order_list({25, 27}, acc);
        break;
      case 34:
        execute_order_list({22}, acc);
        break;
      case 35:
        execute_order_list({25, 27}, acc);
        break;
      case 36:
        execute_order_list({27, 32}, acc);
        break;
      case 37:
        execute_order_list({22, 27}, acc);
        break;
      case 38:
        execute_order_list({23}, acc);
        break;
      case 39:
        execute_order_list({27, 32}, acc);
        break;
      case 40:
        execute_order_list({22, 27}, acc);
        break;
      case 41:
        execute_order_list({27}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void tick_ik1302_3854f3_decoded() {
    static constexpr uint32_t kCommand = 0x3854f3;
    if (tick_index == 0) command = kCommand;

    const int current_tick = tick_index;
    const bool branch_l = l != 0;
    if (current_tick < 27) {
      sync_address = 115;
    } else if (current_tick < 36) {
      sync_address = 41;
    } else {
      if (current_tick == 36) {
        r[37] = 1 & 0xF;
        r[40] = 225 >> 4;
      }
      sync_address = 95;
    }

    Accumulator acc;
    execute_ik1302_3854f3_pre_add_orders(current_tick, branch_l, acc);

    if (key_y == 0) t = 0;

    execute_ik1302_3854f3_gamma_orders(current_tick, branch_l, acc);

    int sum = acc.alpha + acc.beta + acc.gamma;
    acc.sigma = sum & 0xF;
    carry = (sum >> 4) & 1;

    execute_ik1302_3854f3_post_add_orders(current_tick, branch_l, acc);

    output = memory[current_tick];
    memory[current_tick] = input;
    ++tick_index;
    if (tick_index == 42) tick_index = 0;
  }


  void execute_ik1302_01546c_pre_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 0:
        execute_order_list({5, 11}, acc);
        break;
      case 24:
        execute_order_list({10}, acc);
        break;
      case 25:
        execute_order_list({5, 11}, acc);
        break;
      case 26:
        execute_order_list({0}, acc);
        break;
      case 28:
        execute_order_list({0}, acc);
        break;
      case 29:
        execute_order_list({5, 8}, acc);
        break;
      case 30:
        execute_order_list({5}, acc);
        break;
      case 31:
        execute_order_list({0}, acc);
        break;
      case 32:
        execute_order_list({5, 8}, acc);
        break;
      case 33:
        execute_order_list({9}, acc);
        break;
      case 34:
        execute_order_list({0}, acc);
        break;
      case 35:
        execute_order_list({0, 3, 7}, acc);
        break;
      case 36:
        execute_order_list({9}, acc);
        break;
      case 40:
        execute_order_list({5, 11}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void execute_ik1302_01546c_gamma_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      default:
        break;
    }
    (void)branch_l;
  }

  void execute_ik1302_01546c_post_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 0:
        execute_order_list({25, 27}, acc);
        break;
      case 24:
        execute_order_list({27}, acc);
        break;
      case 25:
        execute_order_list({23}, acc);
        break;
      case 26:
        execute_order_list({27}, acc);
        break;
      case 27:
        execute_order_list({22}, acc);
        break;
      case 28:
        execute_order_list({22}, acc);
        break;
      case 29:
        execute_order_list({27}, acc);
        break;
      case 30:
        execute_order_list({26, 29}, acc);
        break;
      case 31:
        execute_order_list({22}, acc);
        break;
      case 32:
        execute_order_list({27}, acc);
        break;
      case 33:
        execute_order_list({16}, acc);
        break;
      case 34:
        execute_order_list({29}, acc);
        break;
      case 35:
        execute_order_list({25, 27}, acc);
        break;
      case 36:
        execute_order_list({16}, acc);
        break;
      case 40:
        execute_order_list({23}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void tick_ik1302_01546c_decoded() {
    static constexpr uint32_t kCommand = 0x01546c;
    if (tick_index == 0) command = kCommand;

    const int current_tick = tick_index;
    const bool branch_l = l != 0;
    if (current_tick < 27) {
      sync_address = 108;
    } else if (current_tick < 36) {
      sync_address = 40;
    } else {
      sync_address = 5;
    }

    Accumulator acc;
    execute_ik1302_01546c_pre_add_orders(current_tick, branch_l, acc);

    if (key_y == 0) t = 0;

    execute_ik1302_01546c_gamma_orders(current_tick, branch_l, acc);

    int sum = acc.alpha + acc.beta + acc.gamma;
    acc.sigma = sum & 0xF;
    carry = (sum >> 4) & 1;

    execute_ik1302_01546c_post_add_orders(current_tick, branch_l, acc);

    output = memory[current_tick];
    memory[current_tick] = input;
    ++tick_index;
    if (tick_index == 42) tick_index = 0;
  }


  void execute_ik1302_74e9c0_pre_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 28:
        execute_order_list({5, 8}, acc);
        break;
      case 29:
        execute_order_list({0}, acc);
        break;
      case 30:
        execute_order_list({5}, acc);
        break;
      case 31:
        execute_order_list({11}, acc);
        break;
      case 32:
        execute_order_list({8}, acc);
        break;
      case 33:
        execute_order_list({5}, acc);
        break;
      case 35:
        execute_order_list({0}, acc);
        break;
      case 36:
        execute_order_list({0, 11}, acc);
        break;
      case 37:
        if (branch_l) {
          execute_order_list({0, 11}, acc);
        } else {
          execute_order_list({5}, acc);
        }
        break;
      case 38:
        execute_order_list({5, 11}, acc);
        break;
      case 39:
        execute_order_list({0}, acc);
        break;
      case 40:
        if (branch_l) {
          execute_order_list({0, 11}, acc);
        } else {
          execute_order_list({5}, acc);
        }
        break;
      case 41:
        execute_order_list({0}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void execute_ik1302_74e9c0_gamma_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 28:
        execute_order_list({12, 13}, acc);
        break;
      case 31:
        execute_order_list({12, 13}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void execute_ik1302_74e9c0_post_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 28:
        execute_order_list({25, 27}, acc);
        break;
      case 29:
        execute_order_list({27}, acc);
        break;
      case 30:
        execute_order_list({26, 29}, acc);
        break;
      case 31:
        execute_order_list({27}, acc);
        break;
      case 32:
        execute_order_list({27}, acc);
        break;
      case 33:
        execute_order_list({26, 29}, acc);
        break;
      case 35:
        execute_order_list({27}, acc);
        break;
      case 36:
        execute_order_list({27, 32}, acc);
        break;
      case 37:
        execute_order_list({22, 27}, acc);
        break;
      case 38:
        execute_order_list({23}, acc);
        break;
      case 39:
        execute_order_list({27, 32}, acc);
        break;
      case 40:
        execute_order_list({22, 27}, acc);
        break;
      case 41:
        execute_order_list({27}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void tick_ik1302_74e9c0_decoded() {
    static constexpr uint32_t kCommand = 0x74e9c0;
    if (tick_index == 0) command = kCommand;

    const int current_tick = tick_index;
    const bool branch_l = l != 0;
    if (current_tick < 27) {
      sync_address = 64;
    } else if (current_tick < 36) {
      sync_address = 83;
    } else {
      if (current_tick == 36) {
        r[37] = 3 & 0xF;
        r[40] = 211 >> 4;
      }
      sync_address = 95;
    }

    Accumulator acc;
    execute_ik1302_74e9c0_pre_add_orders(current_tick, branch_l, acc);

    if (key_y == 0) t = 0;

    execute_ik1302_74e9c0_gamma_orders(current_tick, branch_l, acc);

    int sum = acc.alpha + acc.beta + acc.gamma;
    acc.sigma = sum & 0xF;
    carry = (sum >> 4) & 1;

    execute_ik1302_74e9c0_post_add_orders(current_tick, branch_l, acc);

    output = memory[current_tick];
    memory[current_tick] = input;
    ++tick_index;
    if (tick_index == 42) tick_index = 0;
  }


  void execute_ik1302_23d040_pre_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 35:
        execute_order_list({5, 8}, acc);
        break;
      case 36:
        execute_order_list({0, 11}, acc);
        break;
      case 37:
        if (branch_l) {
          execute_order_list({0, 11}, acc);
        } else {
          execute_order_list({5}, acc);
        }
        break;
      case 38:
        execute_order_list({5, 11}, acc);
        break;
      case 39:
        execute_order_list({0}, acc);
        break;
      case 40:
        if (branch_l) {
          execute_order_list({0, 11}, acc);
        } else {
          execute_order_list({5}, acc);
        }
        break;
      case 41:
        execute_order_list({0}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void execute_ik1302_23d040_gamma_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 35:
        execute_order_list({12, 13}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void execute_ik1302_23d040_post_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 35:
        execute_order_list({25, 27}, acc);
        break;
      case 36:
        execute_order_list({27, 32}, acc);
        break;
      case 37:
        execute_order_list({22, 27}, acc);
        break;
      case 38:
        execute_order_list({23}, acc);
        break;
      case 39:
        execute_order_list({27, 32}, acc);
        break;
      case 40:
        execute_order_list({22, 27}, acc);
        break;
      case 41:
        execute_order_list({27}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void tick_ik1302_23d040_decoded() {
    static constexpr uint32_t kCommand = 0x23d040;
    if (tick_index == 0) command = kCommand;

    const int current_tick = tick_index;
    const bool branch_l = l != 0;
    if (current_tick < 27) {
      sync_address = 64;
    } else if (current_tick < 36) {
      sync_address = 32;
    } else {
      if (current_tick == 36) {
        r[37] = 15 & 0xF;
        r[40] = 143 >> 4;
      }
      sync_address = 95;
    }

    Accumulator acc;
    execute_ik1302_23d040_pre_add_orders(current_tick, branch_l, acc);

    if (key_y == 0) t = 0;

    execute_ik1302_23d040_gamma_orders(current_tick, branch_l, acc);

    int sum = acc.alpha + acc.beta + acc.gamma;
    acc.sigma = sum & 0xF;
    carry = (sum >> 4) & 1;

    execute_ik1302_23d040_post_add_orders(current_tick, branch_l, acc);

    output = memory[current_tick];
    memory[current_tick] = input;
    ++tick_index;
    if (tick_index == 42) tick_index = 0;
  }


  void execute_ik1302_3327cf_pre_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 0:
        execute_order_list({0}, acc);
        break;
      case 1:
        execute_order_list({0}, acc);
        break;
      case 3:
        execute_order_list({0}, acc);
        break;
      case 4:
        execute_order_list({0}, acc);
        break;
      case 6:
        execute_order_list({0}, acc);
        break;
      case 7:
        execute_order_list({0}, acc);
        break;
      case 9:
        execute_order_list({0}, acc);
        break;
      case 10:
        execute_order_list({0}, acc);
        break;
      case 12:
        execute_order_list({0}, acc);
        break;
      case 13:
        execute_order_list({0}, acc);
        break;
      case 15:
        execute_order_list({0}, acc);
        break;
      case 16:
        execute_order_list({0}, acc);
        break;
      case 18:
        execute_order_list({0}, acc);
        break;
      case 19:
        execute_order_list({0}, acc);
        break;
      case 21:
        execute_order_list({0}, acc);
        break;
      case 22:
        execute_order_list({0}, acc);
        break;
      case 24:
        execute_order_list({0}, acc);
        break;
      case 25:
        execute_order_list({0}, acc);
        break;
      case 26:
        execute_order_list({5, 8}, acc);
        break;
      case 27:
        execute_order_list({0}, acc);
        break;
      case 28:
        execute_order_list({0}, acc);
        break;
      case 30:
        execute_order_list({0}, acc);
        break;
      case 31:
        execute_order_list({0}, acc);
        break;
      case 33:
        execute_order_list({0}, acc);
        break;
      case 34:
        execute_order_list({0}, acc);
        break;
      case 35:
        execute_order_list({5, 8}, acc);
        break;
      case 36:
        execute_order_list({0, 11}, acc);
        break;
      case 37:
        if (branch_l) {
          execute_order_list({0, 11}, acc);
        } else {
          execute_order_list({5}, acc);
        }
        break;
      case 38:
        execute_order_list({5, 11}, acc);
        break;
      case 39:
        execute_order_list({0}, acc);
        break;
      case 40:
        if (branch_l) {
          execute_order_list({0, 11}, acc);
        } else {
          execute_order_list({5}, acc);
        }
        break;
      case 41:
        execute_order_list({0}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void execute_ik1302_3327cf_gamma_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 26:
        execute_order_list({12, 13}, acc);
        break;
      case 35:
        execute_order_list({12, 13}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void execute_ik1302_3327cf_post_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 0:
        execute_order_list({27}, acc);
        break;
      case 1:
        execute_order_list({17, 27}, acc);
        break;
      case 3:
        execute_order_list({27}, acc);
        break;
      case 4:
        execute_order_list({17, 27}, acc);
        break;
      case 6:
        execute_order_list({27}, acc);
        break;
      case 7:
        execute_order_list({17, 27}, acc);
        break;
      case 9:
        execute_order_list({27}, acc);
        break;
      case 10:
        execute_order_list({17, 27}, acc);
        break;
      case 12:
        execute_order_list({27}, acc);
        break;
      case 13:
        execute_order_list({17, 27}, acc);
        break;
      case 15:
        execute_order_list({27}, acc);
        break;
      case 16:
        execute_order_list({17, 27}, acc);
        break;
      case 18:
        execute_order_list({27}, acc);
        break;
      case 19:
        execute_order_list({17, 27}, acc);
        break;
      case 21:
        execute_order_list({27}, acc);
        break;
      case 22:
        execute_order_list({17, 27}, acc);
        break;
      case 24:
        execute_order_list({27}, acc);
        break;
      case 25:
        execute_order_list({17, 27}, acc);
        break;
      case 26:
        execute_order_list({25, 27}, acc);
        break;
      case 27:
        execute_order_list({27}, acc);
        break;
      case 28:
        execute_order_list({17, 27}, acc);
        break;
      case 30:
        execute_order_list({27}, acc);
        break;
      case 31:
        execute_order_list({17, 27}, acc);
        break;
      case 33:
        execute_order_list({27}, acc);
        break;
      case 34:
        execute_order_list({17, 27}, acc);
        break;
      case 35:
        execute_order_list({25, 27}, acc);
        break;
      case 36:
        execute_order_list({27, 32}, acc);
        break;
      case 37:
        execute_order_list({22, 27}, acc);
        break;
      case 38:
        execute_order_list({23}, acc);
        break;
      case 39:
        execute_order_list({27, 32}, acc);
        break;
      case 40:
        execute_order_list({22, 27}, acc);
        break;
      case 41:
        execute_order_list({27}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void tick_ik1302_3327cf_decoded() {
    static constexpr uint32_t kCommand = 0x3327cf;
    if (tick_index == 0) command = kCommand;

    const int current_tick = tick_index;
    const bool branch_l = l != 0;
    if (current_tick < 27) {
      sync_address = 79;
    } else if (current_tick < 36) {
      sync_address = 79;
    } else {
      if (current_tick == 36) {
        r[37] = 12 & 0xF;
        r[40] = 204 >> 4;
      }
      sync_address = 95;
    }

    Accumulator acc;
    execute_ik1302_3327cf_pre_add_orders(current_tick, branch_l, acc);

    if (key_y == 0) t = 0;

    execute_ik1302_3327cf_gamma_orders(current_tick, branch_l, acc);

    int sum = acc.alpha + acc.beta + acc.gamma;
    acc.sigma = sum & 0xF;
    carry = (sum >> 4) & 1;

    execute_ik1302_3327cf_post_add_orders(current_tick, branch_l, acc);

    output = memory[current_tick];
    memory[current_tick] = input;
    ++tick_index;
    if (tick_index == 42) tick_index = 0;
  }


  void execute_ik1302_016040_pre_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 36:
        execute_order_list({9}, acc);
        break;
      case 40:
        execute_order_list({5, 11}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void execute_ik1302_016040_gamma_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      default:
        break;
    }
    (void)branch_l;
  }

  void execute_ik1302_016040_post_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 36:
        execute_order_list({16}, acc);
        break;
      case 40:
        execute_order_list({23}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void tick_ik1302_016040_decoded() {
    static constexpr uint32_t kCommand = 0x016040;
    if (tick_index == 0) command = kCommand;

    const int current_tick = tick_index;
    const bool branch_l = l != 0;
    if (current_tick < 27) {
      sync_address = 64;
    } else if (current_tick < 36) {
      sync_address = 64;
    } else {
      sync_address = 5;
    }

    Accumulator acc;
    execute_ik1302_016040_pre_add_orders(current_tick, branch_l, acc);

    if (key_y == 0) t = 0;

    execute_ik1302_016040_gamma_orders(current_tick, branch_l, acc);

    int sum = acc.alpha + acc.beta + acc.gamma;
    acc.sigma = sum & 0xF;
    carry = (sum >> 4) & 1;

    execute_ik1302_016040_post_add_orders(current_tick, branch_l, acc);

    output = memory[current_tick];
    memory[current_tick] = input;
    ++tick_index;
    if (tick_index == 42) tick_index = 0;
  }


  void execute_ik1302_079a34_pre_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 0:
        execute_order_list({0}, acc);
        break;
      case 1:
        execute_order_list({1}, acc);
        break;
      case 3:
        execute_order_list({0}, acc);
        break;
      case 4:
        execute_order_list({1}, acc);
        break;
      case 6:
        execute_order_list({0}, acc);
        break;
      case 7:
        execute_order_list({1}, acc);
        break;
      case 9:
        execute_order_list({0}, acc);
        break;
      case 10:
        execute_order_list({1}, acc);
        break;
      case 12:
        execute_order_list({0}, acc);
        break;
      case 13:
        execute_order_list({1}, acc);
        break;
      case 15:
        execute_order_list({0}, acc);
        break;
      case 16:
        execute_order_list({1}, acc);
        break;
      case 18:
        execute_order_list({0}, acc);
        break;
      case 19:
        execute_order_list({1}, acc);
        break;
      case 21:
        execute_order_list({0}, acc);
        break;
      case 22:
        execute_order_list({1}, acc);
        break;
      case 24:
        execute_order_list({0}, acc);
        break;
      case 25:
        execute_order_list({1}, acc);
        break;
      case 26:
        execute_order_list({9}, acc);
        break;
      case 27:
        execute_order_list({0}, acc);
        break;
      case 28:
        execute_order_list({1}, acc);
        break;
      case 30:
        execute_order_list({0}, acc);
        break;
      case 31:
        execute_order_list({1}, acc);
        break;
      case 33:
        execute_order_list({0}, acc);
        break;
      case 34:
        execute_order_list({1}, acc);
        break;
      case 35:
        execute_order_list({9}, acc);
        break;
      case 36:
        execute_order_list({2}, acc);
        break;
      case 39:
        execute_order_list({2}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void execute_ik1302_079a34_gamma_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 26:
        execute_order_list({12, 13}, acc);
        break;
      case 35:
        execute_order_list({12, 13}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void execute_ik1302_079a34_post_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 0:
        execute_order_list({27}, acc);
        break;
      case 1:
        execute_order_list({22, 24}, acc);
        break;
      case 3:
        execute_order_list({27}, acc);
        break;
      case 4:
        execute_order_list({22, 24}, acc);
        break;
      case 6:
        execute_order_list({27}, acc);
        break;
      case 7:
        execute_order_list({22, 24}, acc);
        break;
      case 9:
        execute_order_list({27}, acc);
        break;
      case 10:
        execute_order_list({22, 24}, acc);
        break;
      case 12:
        execute_order_list({27}, acc);
        break;
      case 13:
        execute_order_list({22, 24}, acc);
        break;
      case 15:
        execute_order_list({27}, acc);
        break;
      case 16:
        execute_order_list({22, 24}, acc);
        break;
      case 18:
        execute_order_list({27}, acc);
        break;
      case 19:
        execute_order_list({22, 24}, acc);
        break;
      case 21:
        execute_order_list({27}, acc);
        break;
      case 22:
        execute_order_list({22, 24}, acc);
        break;
      case 24:
        execute_order_list({27}, acc);
        break;
      case 25:
        execute_order_list({22, 24}, acc);
        break;
      case 26:
        execute_order_list({25, 29}, acc);
        break;
      case 27:
        execute_order_list({27}, acc);
        break;
      case 28:
        execute_order_list({22, 24}, acc);
        break;
      case 30:
        execute_order_list({27}, acc);
        break;
      case 31:
        execute_order_list({22, 24}, acc);
        break;
      case 33:
        execute_order_list({27}, acc);
        break;
      case 34:
        execute_order_list({22, 24}, acc);
        break;
      case 35:
        execute_order_list({25, 29}, acc);
        break;
      case 36:
        execute_order_list({16, 33}, acc);
        break;
      case 39:
        execute_order_list({16, 33}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void tick_ik1302_079a34_decoded() {
    static constexpr uint32_t kCommand = 0x079a34;
    if (tick_index == 0) command = kCommand;

    const int current_tick = tick_index;
    const bool branch_l = l != 0;
    if (current_tick < 27) {
      sync_address = 52;
    } else if (current_tick < 36) {
      sync_address = 52;
    } else {
      sync_address = 30;
    }

    Accumulator acc;
    execute_ik1302_079a34_pre_add_orders(current_tick, branch_l, acc);

    if (key_y == 0) t = 0;

    execute_ik1302_079a34_gamma_orders(current_tick, branch_l, acc);

    int sum = acc.alpha + acc.beta + acc.gamma;
    acc.sigma = sum & 0xF;
    carry = (sum >> 4) & 1;

    execute_ik1302_079a34_post_add_orders(current_tick, branch_l, acc);

    output = memory[current_tick];
    memory[current_tick] = input;
    ++tick_index;
    if (tick_index == 42) tick_index = 0;
  }


  void execute_ik1302_4139c0_pre_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 34:
        execute_order_list({10}, acc);
        break;
      case 35:
        execute_order_list({5, 11}, acc);
        break;
      case 37:
        execute_order_list({5}, acc);
        break;
      case 38:
        execute_order_list({5, 8}, acc);
        break;
      case 39:
        execute_order_list({8}, acc);
        break;
      case 40:
        execute_order_list({4, 7}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void execute_ik1302_4139c0_gamma_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      default:
        break;
    }
    (void)branch_l;
  }

  void execute_ik1302_4139c0_post_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 34:
        execute_order_list({27}, acc);
        break;
      case 35:
        execute_order_list({27}, acc);
        break;
      case 37:
        execute_order_list({22}, acc);
        break;
      case 38:
        execute_order_list({27}, acc);
        break;
      case 39:
        execute_order_list({27}, acc);
        break;
      case 40:
        execute_order_list({25, 27}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void tick_ik1302_4139c0_decoded() {
    static constexpr uint32_t kCommand = 0x4139c0;
    if (tick_index == 0) command = kCommand;

    const int current_tick = tick_index;
    const bool branch_l = l != 0;
    if (current_tick < 27) {
      sync_address = 64;
    } else if (current_tick < 36) {
      sync_address = 115;
    } else {
      sync_address = 4;
    }

    Accumulator acc;
    execute_ik1302_4139c0_pre_add_orders(current_tick, branch_l, acc);

    if (key_y == 0) t = 0;

    execute_ik1302_4139c0_gamma_orders(current_tick, branch_l, acc);

    int sum = acc.alpha + acc.beta + acc.gamma;
    acc.sigma = sum & 0xF;
    carry = (sum >> 4) & 1;

    execute_ik1302_4139c0_post_add_orders(current_tick, branch_l, acc);

    output = memory[current_tick];
    memory[current_tick] = input;
    ++tick_index;
    if (tick_index == 42) tick_index = 0;
  }


  void execute_ik1302_31962b_pre_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 0:
        execute_order_list({10}, acc);
        break;
      case 1:
        execute_order_list({5, 11}, acc);
        break;
      case 2:
        execute_order_list({0, 7}, acc);
        break;
      case 3:
        execute_order_list({5}, acc);
        break;
      case 6:
        execute_order_list({5}, acc);
        break;
      case 9:
        execute_order_list({5}, acc);
        break;
      case 12:
        execute_order_list({5}, acc);
        break;
      case 15:
        execute_order_list({5}, acc);
        break;
      case 18:
        execute_order_list({5}, acc);
        break;
      case 21:
        execute_order_list({5}, acc);
        break;
      case 24:
        execute_order_list({0, 7, 8}, acc);
        break;
      case 25:
        if (branch_l) {
          execute_order_list({0, 11}, acc);
        } else {
          execute_order_list({5}, acc);
        }
        break;
      case 28:
        execute_order_list({3, 11}, acc);
        break;
      case 31:
        execute_order_list({3}, acc);
        break;
      case 33:
        execute_order_list({10}, acc);
        break;
      case 34:
        execute_order_list({0, 7}, acc);
        break;
      case 36:
        execute_order_list({0, 11}, acc);
        break;
      case 37:
        if (branch_l) {
          execute_order_list({0, 11}, acc);
        } else {
          execute_order_list({5}, acc);
        }
        break;
      case 38:
        execute_order_list({5, 11}, acc);
        break;
      case 39:
        execute_order_list({0}, acc);
        break;
      case 40:
        if (branch_l) {
          execute_order_list({0, 11}, acc);
        } else {
          execute_order_list({5}, acc);
        }
        break;
      case 41:
        execute_order_list({0}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void execute_ik1302_31962b_gamma_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 2:
        execute_order_list({12, 13}, acc);
        break;
      case 31:
        execute_order_list({12}, acc);
        break;
      case 34:
        execute_order_list({12}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void execute_ik1302_31962b_post_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 0:
        execute_order_list({27}, acc);
        break;
      case 1:
        execute_order_list({27}, acc);
        break;
      case 2:
        execute_order_list({25, 27}, acc);
        break;
      case 3:
        execute_order_list({22}, acc);
        break;
      case 6:
        execute_order_list({22}, acc);
        break;
      case 9:
        execute_order_list({22}, acc);
        break;
      case 12:
        execute_order_list({22}, acc);
        break;
      case 15:
        execute_order_list({22}, acc);
        break;
      case 18:
        execute_order_list({22}, acc);
        break;
      case 21:
        execute_order_list({22}, acc);
        break;
      case 24:
        execute_order_list({25, 27}, acc);
        break;
      case 25:
        execute_order_list({22, 27}, acc);
        break;
      case 28:
        execute_order_list({25, 27}, acc);
        break;
      case 31:
        execute_order_list({25, 27}, acc);
        break;
      case 33:
        execute_order_list({27}, acc);
        break;
      case 34:
        execute_order_list({25, 27}, acc);
        break;
      case 36:
        execute_order_list({27, 32}, acc);
        break;
      case 37:
        execute_order_list({22, 27}, acc);
        break;
      case 38:
        execute_order_list({23}, acc);
        break;
      case 39:
        execute_order_list({27, 32}, acc);
        break;
      case 40:
        execute_order_list({22, 27}, acc);
        break;
      case 41:
        execute_order_list({27}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void tick_ik1302_31962b_decoded() {
    static constexpr uint32_t kCommand = 0x31962b;
    if (tick_index == 0) command = kCommand;

    const int current_tick = tick_index;
    const bool branch_l = l != 0;
    if (current_tick < 27) {
      sync_address = 43;
    } else if (current_tick < 36) {
      sync_address = 44;
    } else {
      if (current_tick == 36) {
        r[37] = 6 & 0xF;
        r[40] = 198 >> 4;
      }
      sync_address = 95;
    }

    Accumulator acc;
    execute_ik1302_31962b_pre_add_orders(current_tick, branch_l, acc);

    if (key_y == 0) t = 0;

    execute_ik1302_31962b_gamma_orders(current_tick, branch_l, acc);

    int sum = acc.alpha + acc.beta + acc.gamma;
    acc.sigma = sum & 0xF;
    carry = (sum >> 4) & 1;

    execute_ik1302_31962b_post_add_orders(current_tick, branch_l, acc);

    output = memory[current_tick];
    memory[current_tick] = input;
    ++tick_index;
    if (tick_index == 42) tick_index = 0;
  }


  void execute_ik1302_0f8e9d_pre_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 0:
        execute_order_list({0}, acc);
        break;
      case 1:
        execute_order_list({0}, acc);
        break;
      case 3:
        execute_order_list({0}, acc);
        break;
      case 4:
        execute_order_list({0}, acc);
        break;
      case 6:
        execute_order_list({0}, acc);
        break;
      case 7:
        execute_order_list({0}, acc);
        break;
      case 9:
        execute_order_list({0}, acc);
        break;
      case 10:
        execute_order_list({0}, acc);
        break;
      case 12:
        execute_order_list({0}, acc);
        break;
      case 13:
        execute_order_list({0}, acc);
        break;
      case 15:
        execute_order_list({0}, acc);
        break;
      case 16:
        execute_order_list({0}, acc);
        break;
      case 18:
        execute_order_list({0}, acc);
        break;
      case 19:
        execute_order_list({0}, acc);
        break;
      case 21:
        execute_order_list({0}, acc);
        break;
      case 22:
        execute_order_list({0}, acc);
        break;
      case 24:
        execute_order_list({0}, acc);
        break;
      case 25:
        execute_order_list({0}, acc);
        break;
      case 26:
        execute_order_list({5, 8}, acc);
        break;
      case 27:
        execute_order_list({0}, acc);
        break;
      case 28:
        execute_order_list({0}, acc);
        break;
      case 30:
        execute_order_list({0}, acc);
        break;
      case 31:
        execute_order_list({0}, acc);
        break;
      case 33:
        execute_order_list({0}, acc);
        break;
      case 34:
        execute_order_list({0}, acc);
        break;
      case 35:
        execute_order_list({5, 8}, acc);
        break;
      case 36:
        execute_order_list({0, 11}, acc);
        break;
      case 37:
        if (branch_l) {
          execute_order_list({0, 11}, acc);
        } else {
          execute_order_list({5}, acc);
        }
        break;
      case 38:
        execute_order_list({5, 11}, acc);
        break;
      case 39:
        execute_order_list({0}, acc);
        break;
      case 40:
        if (branch_l) {
          execute_order_list({0, 11}, acc);
        } else {
          execute_order_list({5}, acc);
        }
        break;
      case 41:
        execute_order_list({0}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void execute_ik1302_0f8e9d_gamma_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 26:
        execute_order_list({12, 13}, acc);
        break;
      case 35:
        execute_order_list({12, 13}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void execute_ik1302_0f8e9d_post_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 0:
        execute_order_list({27}, acc);
        break;
      case 1:
        execute_order_list({17, 22}, acc);
        break;
      case 3:
        execute_order_list({27}, acc);
        break;
      case 4:
        execute_order_list({17, 22}, acc);
        break;
      case 6:
        execute_order_list({27}, acc);
        break;
      case 7:
        execute_order_list({17, 22}, acc);
        break;
      case 9:
        execute_order_list({27}, acc);
        break;
      case 10:
        execute_order_list({17, 22}, acc);
        break;
      case 12:
        execute_order_list({27}, acc);
        break;
      case 13:
        execute_order_list({17, 22}, acc);
        break;
      case 15:
        execute_order_list({27}, acc);
        break;
      case 16:
        execute_order_list({17, 22}, acc);
        break;
      case 18:
        execute_order_list({27}, acc);
        break;
      case 19:
        execute_order_list({17, 22}, acc);
        break;
      case 21:
        execute_order_list({27}, acc);
        break;
      case 22:
        execute_order_list({17, 22}, acc);
        break;
      case 24:
        execute_order_list({27}, acc);
        break;
      case 25:
        execute_order_list({17, 22}, acc);
        break;
      case 26:
        execute_order_list({25, 27}, acc);
        break;
      case 27:
        execute_order_list({27}, acc);
        break;
      case 28:
        execute_order_list({17, 22}, acc);
        break;
      case 30:
        execute_order_list({27}, acc);
        break;
      case 31:
        execute_order_list({17, 22}, acc);
        break;
      case 33:
        execute_order_list({27}, acc);
        break;
      case 34:
        execute_order_list({17, 22}, acc);
        break;
      case 35:
        execute_order_list({25, 27}, acc);
        break;
      case 36:
        execute_order_list({27, 32}, acc);
        break;
      case 37:
        execute_order_list({22, 27}, acc);
        break;
      case 38:
        execute_order_list({23}, acc);
        break;
      case 39:
        execute_order_list({27, 32}, acc);
        break;
      case 40:
        execute_order_list({22, 27}, acc);
        break;
      case 41:
        execute_order_list({27}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void tick_ik1302_0f8e9d_decoded() {
    static constexpr uint32_t kCommand = 0x0f8e9d;
    if (tick_index == 0) command = kCommand;

    const int current_tick = tick_index;
    const bool branch_l = l != 0;
    if (current_tick < 27) {
      sync_address = 29;
    } else if (current_tick < 36) {
      sync_address = 29;
    } else {
      if (current_tick == 36) {
        r[37] = 14 & 0xF;
        r[40] = 62 >> 4;
      }
      sync_address = 95;
    }

    Accumulator acc;
    execute_ik1302_0f8e9d_pre_add_orders(current_tick, branch_l, acc);

    if (key_y == 0) t = 0;

    execute_ik1302_0f8e9d_gamma_orders(current_tick, branch_l, acc);

    int sum = acc.alpha + acc.beta + acc.gamma;
    acc.sigma = sum & 0xF;
    carry = (sum >> 4) & 1;

    execute_ik1302_0f8e9d_post_add_orders(current_tick, branch_l, acc);

    output = memory[current_tick];
    memory[current_tick] = input;
    ++tick_index;
    if (tick_index == 42) tick_index = 0;
  }


  void execute_ik1302_410bc0_pre_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 27:
        if (branch_l) {
          execute_order_list({0, 11}, acc);
        } else {
          execute_order_list({5}, acc);
        }
        break;
      case 28:
        execute_order_list({5}, acc);
        break;
      case 30:
        execute_order_list({10}, acc);
        break;
      case 31:
        execute_order_list({5, 9}, acc);
        break;
      case 32:
        execute_order_list({5, 8}, acc);
        break;
      case 33:
        execute_order_list({8}, acc);
        break;
      case 34:
        execute_order_list({5, 11}, acc);
        break;
      case 35:
        execute_order_list({8}, acc);
        break;
      case 37:
        execute_order_list({5}, acc);
        break;
      case 38:
        execute_order_list({5, 8}, acc);
        break;
      case 39:
        execute_order_list({8}, acc);
        break;
      case 40:
        execute_order_list({4, 7}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void execute_ik1302_410bc0_gamma_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      default:
        break;
    }
    (void)branch_l;
  }

  void execute_ik1302_410bc0_post_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 27:
        execute_order_list({27}, acc);
        break;
      case 30:
        execute_order_list({27}, acc);
        break;
      case 31:
        execute_order_list({25, 27}, acc);
        break;
      case 32:
        execute_order_list({27}, acc);
        break;
      case 33:
        execute_order_list({27}, acc);
        break;
      case 34:
        execute_order_list({27}, acc);
        break;
      case 35:
        execute_order_list({27}, acc);
        break;
      case 37:
        execute_order_list({22}, acc);
        break;
      case 38:
        execute_order_list({27}, acc);
        break;
      case 39:
        execute_order_list({27}, acc);
        break;
      case 40:
        execute_order_list({25, 27}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void tick_ik1302_410bc0_decoded() {
    static constexpr uint32_t kCommand = 0x410bc0;
    if (tick_index == 0) command = kCommand;

    const int current_tick = tick_index;
    const bool branch_l = l != 0;
    if (current_tick < 27) {
      sync_address = 64;
    } else if (current_tick < 36) {
      sync_address = 23;
    } else {
      sync_address = 4;
    }

    Accumulator acc;
    execute_ik1302_410bc0_pre_add_orders(current_tick, branch_l, acc);

    if (key_y == 0) t = 0;

    execute_ik1302_410bc0_gamma_orders(current_tick, branch_l, acc);

    int sum = acc.alpha + acc.beta + acc.gamma;
    acc.sigma = sum & 0xF;
    carry = (sum >> 4) & 1;

    execute_ik1302_410bc0_post_add_orders(current_tick, branch_l, acc);

    output = memory[current_tick];
    memory[current_tick] = input;
    ++tick_index;
    if (tick_index == 42) tick_index = 0;
  }


  void execute_ik1302_079ab5_pre_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 0:
        execute_order_list({0, 7}, acc);
        break;
      case 1:
        execute_order_list({1}, acc);
        break;
      case 2:
        execute_order_list({5}, acc);
        break;
      case 4:
        execute_order_list({1}, acc);
        break;
      case 5:
        execute_order_list({5}, acc);
        break;
      case 7:
        execute_order_list({1}, acc);
        break;
      case 8:
        execute_order_list({5}, acc);
        break;
      case 10:
        execute_order_list({1}, acc);
        break;
      case 11:
        execute_order_list({5}, acc);
        break;
      case 13:
        execute_order_list({1}, acc);
        break;
      case 14:
        execute_order_list({5}, acc);
        break;
      case 16:
        execute_order_list({1}, acc);
        break;
      case 17:
        execute_order_list({5}, acc);
        break;
      case 19:
        execute_order_list({1}, acc);
        break;
      case 20:
        execute_order_list({5}, acc);
        break;
      case 22:
        execute_order_list({1}, acc);
        break;
      case 23:
        execute_order_list({5}, acc);
        break;
      case 25:
        execute_order_list({1}, acc);
        break;
      case 26:
        execute_order_list({5}, acc);
        break;
      case 27:
        execute_order_list({0, 7}, acc);
        break;
      case 28:
        execute_order_list({1}, acc);
        break;
      case 29:
        execute_order_list({5}, acc);
        break;
      case 31:
        execute_order_list({1}, acc);
        break;
      case 32:
        execute_order_list({5}, acc);
        break;
      case 34:
        execute_order_list({1}, acc);
        break;
      case 35:
        execute_order_list({5}, acc);
        break;
      case 36:
        execute_order_list({2}, acc);
        break;
      case 39:
        execute_order_list({2}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void execute_ik1302_079ab5_gamma_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      default:
        break;
    }
    (void)branch_l;
  }

  void execute_ik1302_079ab5_post_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 0:
        execute_order_list({25, 27}, acc);
        break;
      case 1:
        execute_order_list({27}, acc);
        break;
      case 2:
        execute_order_list({23}, acc);
        break;
      case 4:
        execute_order_list({27}, acc);
        break;
      case 5:
        execute_order_list({23}, acc);
        break;
      case 7:
        execute_order_list({27}, acc);
        break;
      case 8:
        execute_order_list({23}, acc);
        break;
      case 10:
        execute_order_list({27}, acc);
        break;
      case 11:
        execute_order_list({23}, acc);
        break;
      case 13:
        execute_order_list({27}, acc);
        break;
      case 14:
        execute_order_list({23}, acc);
        break;
      case 16:
        execute_order_list({27}, acc);
        break;
      case 17:
        execute_order_list({23}, acc);
        break;
      case 19:
        execute_order_list({27}, acc);
        break;
      case 20:
        execute_order_list({23}, acc);
        break;
      case 22:
        execute_order_list({27}, acc);
        break;
      case 23:
        execute_order_list({23}, acc);
        break;
      case 25:
        execute_order_list({27}, acc);
        break;
      case 26:
        execute_order_list({23}, acc);
        break;
      case 27:
        execute_order_list({25, 27}, acc);
        break;
      case 28:
        execute_order_list({27}, acc);
        break;
      case 29:
        execute_order_list({23}, acc);
        break;
      case 31:
        execute_order_list({27}, acc);
        break;
      case 32:
        execute_order_list({23}, acc);
        break;
      case 34:
        execute_order_list({27}, acc);
        break;
      case 35:
        execute_order_list({23}, acc);
        break;
      case 36:
        execute_order_list({16, 33}, acc);
        break;
      case 39:
        execute_order_list({16, 33}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void tick_ik1302_079ab5_decoded() {
    static constexpr uint32_t kCommand = 0x079ab5;
    if (tick_index == 0) command = kCommand;

    const int current_tick = tick_index;
    const bool branch_l = l != 0;
    if (current_tick < 27) {
      sync_address = 53;
    } else if (current_tick < 36) {
      sync_address = 53;
    } else {
      sync_address = 30;
    }

    Accumulator acc;
    execute_ik1302_079ab5_pre_add_orders(current_tick, branch_l, acc);

    if (key_y == 0) t = 0;

    execute_ik1302_079ab5_gamma_orders(current_tick, branch_l, acc);

    int sum = acc.alpha + acc.beta + acc.gamma;
    acc.sigma = sum & 0xF;
    carry = (sum >> 4) & 1;

    execute_ik1302_079ab5_post_add_orders(current_tick, branch_l, acc);

    output = memory[current_tick];
    memory[current_tick] = input;
    ++tick_index;
    if (tick_index == 42) tick_index = 0;
  }


  void execute_ik1302_38a057_pre_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 2:
        execute_order_list({0}, acc);
        break;
      case 5:
        execute_order_list({0, 7}, acc);
        break;
      case 8:
        execute_order_list({0, 7}, acc);
        break;
      case 11:
        execute_order_list({0, 7}, acc);
        break;
      case 14:
        execute_order_list({0, 7}, acc);
        break;
      case 17:
        execute_order_list({0, 7}, acc);
        break;
      case 20:
        execute_order_list({0, 7}, acc);
        break;
      case 23:
        execute_order_list({0, 7}, acc);
        break;
      case 25:
        execute_order_list({0, 7, 8}, acc);
        break;
      case 26:
        execute_order_list({5}, acc);
        break;
      case 36:
        execute_order_list({0, 11}, acc);
        break;
      case 37:
        if (branch_l) {
          execute_order_list({0, 11}, acc);
        } else {
          execute_order_list({5}, acc);
        }
        break;
      case 38:
        execute_order_list({5, 11}, acc);
        break;
      case 39:
        execute_order_list({0}, acc);
        break;
      case 40:
        if (branch_l) {
          execute_order_list({0, 11}, acc);
        } else {
          execute_order_list({5}, acc);
        }
        break;
      case 41:
        execute_order_list({0}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void execute_ik1302_38a057_gamma_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 5:
        execute_order_list({12, 13}, acc);
        break;
      case 8:
        execute_order_list({12, 13}, acc);
        break;
      case 11:
        execute_order_list({12, 13}, acc);
        break;
      case 14:
        execute_order_list({12, 13}, acc);
        break;
      case 17:
        execute_order_list({12, 13}, acc);
        break;
      case 20:
        execute_order_list({12, 13}, acc);
        break;
      case 23:
        execute_order_list({12, 13}, acc);
        break;
      case 25:
        execute_order_list({12}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void execute_ik1302_38a057_post_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 2:
        execute_order_list({27}, acc);
        break;
      case 5:
        execute_order_list({25}, acc);
        break;
      case 8:
        execute_order_list({25}, acc);
        break;
      case 11:
        execute_order_list({25}, acc);
        break;
      case 14:
        execute_order_list({25}, acc);
        break;
      case 17:
        execute_order_list({25}, acc);
        break;
      case 20:
        execute_order_list({25}, acc);
        break;
      case 23:
        execute_order_list({25}, acc);
        break;
      case 25:
        execute_order_list({25, 27}, acc);
        break;
      case 26:
        execute_order_list({23}, acc);
        break;
      case 36:
        execute_order_list({27, 32}, acc);
        break;
      case 37:
        execute_order_list({22, 27}, acc);
        break;
      case 38:
        execute_order_list({23}, acc);
        break;
      case 39:
        execute_order_list({27, 32}, acc);
        break;
      case 40:
        execute_order_list({22, 27}, acc);
        break;
      case 41:
        execute_order_list({27}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void tick_ik1302_38a057_decoded() {
    static constexpr uint32_t kCommand = 0x38a057;
    if (tick_index == 0) command = kCommand;

    const int current_tick = tick_index;
    const bool branch_l = l != 0;
    if (current_tick < 27) {
      sync_address = 87;
    } else if (current_tick < 36) {
      sync_address = 64;
    } else {
      if (current_tick == 36) {
        r[37] = 2 & 0xF;
        r[40] = 226 >> 4;
      }
      sync_address = 95;
    }

    Accumulator acc;
    execute_ik1302_38a057_pre_add_orders(current_tick, branch_l, acc);

    if (key_y == 0) t = 0;

    execute_ik1302_38a057_gamma_orders(current_tick, branch_l, acc);

    int sum = acc.alpha + acc.beta + acc.gamma;
    acc.sigma = sum & 0xF;
    carry = (sum >> 4) & 1;

    execute_ik1302_38a057_post_add_orders(current_tick, branch_l, acc);

    output = memory[current_tick];
    memory[current_tick] = input;
    ++tick_index;
    if (tick_index == 42) tick_index = 0;
  }


  void execute_ik1302_795c12_pre_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 0:
        execute_order_list({0}, acc);
        break;
      case 1:
        execute_order_list({5, 11}, acc);
        break;
      case 2:
        execute_order_list({5, 11}, acc);
        break;
      case 3:
        execute_order_list({5, 8}, acc);
        break;
      case 4:
        execute_order_list({5}, acc);
        break;
      case 5:
        execute_order_list({0}, acc);
        break;
      case 6:
        execute_order_list({5, 8}, acc);
        break;
      case 7:
        execute_order_list({5}, acc);
        break;
      case 8:
        execute_order_list({0}, acc);
        break;
      case 9:
        execute_order_list({5, 8}, acc);
        break;
      case 10:
        execute_order_list({5}, acc);
        break;
      case 11:
        execute_order_list({0}, acc);
        break;
      case 12:
        execute_order_list({5, 8}, acc);
        break;
      case 13:
        execute_order_list({5}, acc);
        break;
      case 14:
        execute_order_list({0}, acc);
        break;
      case 15:
        execute_order_list({5, 8}, acc);
        break;
      case 16:
        execute_order_list({5}, acc);
        break;
      case 17:
        execute_order_list({0}, acc);
        break;
      case 18:
        execute_order_list({5, 8}, acc);
        break;
      case 19:
        execute_order_list({5}, acc);
        break;
      case 20:
        execute_order_list({0}, acc);
        break;
      case 21:
        execute_order_list({5, 8}, acc);
        break;
      case 22:
        execute_order_list({5}, acc);
        break;
      case 23:
        execute_order_list({0}, acc);
        break;
      case 26:
        execute_order_list({0, 7}, acc);
        break;
      case 27:
        execute_order_list({0, 3, 7}, acc);
        break;
      case 28:
        execute_order_list({1}, acc);
        break;
      case 29:
        execute_order_list({5}, acc);
        break;
      case 31:
        execute_order_list({1}, acc);
        break;
      case 32:
        execute_order_list({5}, acc);
        break;
      case 34:
        execute_order_list({1}, acc);
        break;
      case 35:
        execute_order_list({5}, acc);
        break;
      case 36:
        execute_order_list({0, 11}, acc);
        break;
      case 37:
        if (branch_l) {
          execute_order_list({0, 11}, acc);
        } else {
          execute_order_list({5}, acc);
        }
        break;
      case 38:
        execute_order_list({5, 11}, acc);
        break;
      case 39:
        execute_order_list({0}, acc);
        break;
      case 40:
        if (branch_l) {
          execute_order_list({0, 11}, acc);
        } else {
          execute_order_list({5}, acc);
        }
        break;
      case 41:
        execute_order_list({0}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void execute_ik1302_795c12_gamma_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 0:
        execute_order_list({12}, acc);
        break;
      case 26:
        execute_order_list({12, 13}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void execute_ik1302_795c12_post_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 0:
        execute_order_list({27}, acc);
        break;
      case 1:
        execute_order_list({27}, acc);
        break;
      case 3:
        execute_order_list({27}, acc);
        break;
      case 5:
        execute_order_list({27}, acc);
        break;
      case 6:
        execute_order_list({27}, acc);
        break;
      case 8:
        execute_order_list({27}, acc);
        break;
      case 9:
        execute_order_list({27}, acc);
        break;
      case 11:
        execute_order_list({27}, acc);
        break;
      case 12:
        execute_order_list({27}, acc);
        break;
      case 14:
        execute_order_list({27}, acc);
        break;
      case 15:
        execute_order_list({27}, acc);
        break;
      case 17:
        execute_order_list({27}, acc);
        break;
      case 18:
        execute_order_list({27}, acc);
        break;
      case 20:
        execute_order_list({27}, acc);
        break;
      case 21:
        execute_order_list({27}, acc);
        break;
      case 23:
        execute_order_list({27}, acc);
        break;
      case 26:
        execute_order_list({25, 27}, acc);
        break;
      case 27:
        execute_order_list({25, 27}, acc);
        break;
      case 28:
        execute_order_list({27}, acc);
        break;
      case 31:
        execute_order_list({27}, acc);
        break;
      case 34:
        execute_order_list({27}, acc);
        break;
      case 36:
        execute_order_list({27, 32}, acc);
        break;
      case 37:
        execute_order_list({22, 27}, acc);
        break;
      case 38:
        execute_order_list({23}, acc);
        break;
      case 39:
        execute_order_list({27, 32}, acc);
        break;
      case 40:
        execute_order_list({22, 27}, acc);
        break;
      case 41:
        execute_order_list({27}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void tick_ik1302_795c12_decoded() {
    static constexpr uint32_t kCommand = 0x795c12;
    if (tick_index == 0) command = kCommand;

    const int current_tick = tick_index;
    const bool branch_l = l != 0;
    if (current_tick < 27) {
      sync_address = 18;
    } else if (current_tick < 36) {
      sync_address = 56;
    } else {
      if (current_tick == 36) {
        r[37] = 5 & 0xF;
        r[40] = 229 >> 4;
      }
      sync_address = 95;
    }

    Accumulator acc;
    execute_ik1302_795c12_pre_add_orders(current_tick, branch_l, acc);

    if (key_y == 0) t = 0;

    execute_ik1302_795c12_gamma_orders(current_tick, branch_l, acc);

    int sum = acc.alpha + acc.beta + acc.gamma;
    acc.sigma = sum & 0xF;
    carry = (sum >> 4) & 1;

    execute_ik1302_795c12_post_add_orders(current_tick, branch_l, acc);

    output = memory[current_tick];
    memory[current_tick] = input;
    ++tick_index;
    if (tick_index == 42) tick_index = 0;
  }


  void execute_ik1302_362067_pre_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 5:
        execute_order_list({3, 11}, acc);
        break;
      case 8:
        execute_order_list({3, 11}, acc);
        break;
      case 11:
        execute_order_list({3, 11}, acc);
        break;
      case 14:
        execute_order_list({3, 11}, acc);
        break;
      case 17:
        execute_order_list({3, 11}, acc);
        break;
      case 20:
        execute_order_list({3, 11}, acc);
        break;
      case 23:
        execute_order_list({3, 11}, acc);
        break;
      case 25:
        execute_order_list({0}, acc);
        break;
      case 36:
        execute_order_list({0, 11}, acc);
        break;
      case 37:
        if (branch_l) {
          execute_order_list({0, 11}, acc);
        } else {
          execute_order_list({5}, acc);
        }
        break;
      case 38:
        execute_order_list({5, 11}, acc);
        break;
      case 39:
        execute_order_list({0}, acc);
        break;
      case 40:
        if (branch_l) {
          execute_order_list({0, 11}, acc);
        } else {
          execute_order_list({5}, acc);
        }
        break;
      case 41:
        execute_order_list({0}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void execute_ik1302_362067_gamma_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      default:
        break;
    }
    (void)branch_l;
  }

  void execute_ik1302_362067_post_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 5:
        execute_order_list({25, 27}, acc);
        break;
      case 8:
        execute_order_list({25, 27}, acc);
        break;
      case 11:
        execute_order_list({25, 27}, acc);
        break;
      case 14:
        execute_order_list({25, 27}, acc);
        break;
      case 17:
        execute_order_list({25, 27}, acc);
        break;
      case 20:
        execute_order_list({25, 27}, acc);
        break;
      case 23:
        execute_order_list({25, 27}, acc);
        break;
      case 25:
        execute_order_list({22}, acc);
        break;
      case 36:
        execute_order_list({27, 32}, acc);
        break;
      case 37:
        execute_order_list({22, 27}, acc);
        break;
      case 38:
        execute_order_list({23}, acc);
        break;
      case 39:
        execute_order_list({27, 32}, acc);
        break;
      case 40:
        execute_order_list({22, 27}, acc);
        break;
      case 41:
        execute_order_list({27}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void tick_ik1302_362067_decoded() {
    static constexpr uint32_t kCommand = 0x362067;
    if (tick_index == 0) command = kCommand;

    const int current_tick = tick_index;
    const bool branch_l = l != 0;
    if (current_tick < 27) {
      sync_address = 103;
    } else if (current_tick < 36) {
      sync_address = 64;
    } else {
      if (current_tick == 36) {
        r[37] = 8 & 0xF;
        r[40] = 216 >> 4;
      }
      sync_address = 95;
    }

    Accumulator acc;
    execute_ik1302_362067_pre_add_orders(current_tick, branch_l, acc);

    if (key_y == 0) t = 0;

    execute_ik1302_362067_gamma_orders(current_tick, branch_l, acc);

    int sum = acc.alpha + acc.beta + acc.gamma;
    acc.sigma = sum & 0xF;
    carry = (sum >> 4) & 1;

    execute_ik1302_362067_post_add_orders(current_tick, branch_l, acc);

    output = memory[current_tick];
    memory[current_tick] = input;
    ++tick_index;
    if (tick_index == 42) tick_index = 0;
  }


  void execute_ik1302_02a02a_pre_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 1:
        execute_order_list({0}, acc);
        break;
      case 5:
        execute_order_list({0, 11}, acc);
        break;
      case 8:
        execute_order_list({0, 11}, acc);
        break;
      case 11:
        execute_order_list({0, 11}, acc);
        break;
      case 14:
        execute_order_list({0, 11}, acc);
        break;
      case 17:
        execute_order_list({0, 11}, acc);
        break;
      case 20:
        execute_order_list({0, 11}, acc);
        break;
      case 23:
        execute_order_list({0, 11}, acc);
        break;
      case 25:
        execute_order_list({0}, acc);
        break;
      case 36:
        execute_order_list({0, 7, 8}, acc);
        break;
      case 37:
        execute_order_list({0, 3, 7}, acc);
        break;
      case 38:
        execute_order_list({5}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void execute_ik1302_02a02a_gamma_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      default:
        break;
    }
    (void)branch_l;
  }

  void execute_ik1302_02a02a_post_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 0:
        execute_order_list({15}, acc);
        break;
      case 1:
        execute_order_list({15, 27}, acc);
        break;
      case 3:
        execute_order_list({15}, acc);
        break;
      case 4:
        execute_order_list({15}, acc);
        break;
      case 5:
        execute_order_list({16, 25}, acc);
        break;
      case 6:
        execute_order_list({15}, acc);
        break;
      case 7:
        execute_order_list({15}, acc);
        break;
      case 8:
        execute_order_list({16, 25}, acc);
        break;
      case 9:
        execute_order_list({15}, acc);
        break;
      case 10:
        execute_order_list({15}, acc);
        break;
      case 11:
        execute_order_list({16, 25}, acc);
        break;
      case 12:
        execute_order_list({15}, acc);
        break;
      case 13:
        execute_order_list({15}, acc);
        break;
      case 14:
        execute_order_list({16, 25}, acc);
        break;
      case 15:
        execute_order_list({15}, acc);
        break;
      case 16:
        execute_order_list({15}, acc);
        break;
      case 17:
        execute_order_list({16, 25}, acc);
        break;
      case 18:
        execute_order_list({15}, acc);
        break;
      case 19:
        execute_order_list({15}, acc);
        break;
      case 20:
        execute_order_list({16, 25}, acc);
        break;
      case 21:
        execute_order_list({15}, acc);
        break;
      case 22:
        execute_order_list({15}, acc);
        break;
      case 23:
        execute_order_list({16, 25}, acc);
        break;
      case 25:
        execute_order_list({17, 27}, acc);
        break;
      case 36:
        execute_order_list({25, 27}, acc);
        break;
      case 37:
        execute_order_list({25, 27}, acc);
        break;
      case 38:
        execute_order_list({23}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void tick_ik1302_02a02a_decoded() {
    static constexpr uint32_t kCommand = 0x02a02a;
    if (tick_index == 0) command = kCommand;

    const int current_tick = tick_index;
    const bool branch_l = l != 0;
    if (current_tick < 27) {
      sync_address = 42;
    } else if (current_tick < 36) {
      sync_address = 64;
    } else {
      sync_address = 10;
    }

    Accumulator acc;
    execute_ik1302_02a02a_pre_add_orders(current_tick, branch_l, acc);

    if (key_y == 0) t = 0;

    execute_ik1302_02a02a_gamma_orders(current_tick, branch_l, acc);

    int sum = acc.alpha + acc.beta + acc.gamma;
    acc.sigma = sum & 0xF;
    carry = (sum >> 4) & 1;

    execute_ik1302_02a02a_post_add_orders(current_tick, branch_l, acc);

    output = memory[current_tick];
    memory[current_tick] = input;
    ++tick_index;
    if (tick_index == 42) tick_index = 0;
  }


  void execute_ik1302_3bd02a_pre_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 1:
        execute_order_list({0}, acc);
        break;
      case 5:
        execute_order_list({0, 11}, acc);
        break;
      case 8:
        execute_order_list({0, 11}, acc);
        break;
      case 11:
        execute_order_list({0, 11}, acc);
        break;
      case 14:
        execute_order_list({0, 11}, acc);
        break;
      case 17:
        execute_order_list({0, 11}, acc);
        break;
      case 20:
        execute_order_list({0, 11}, acc);
        break;
      case 23:
        execute_order_list({0, 11}, acc);
        break;
      case 25:
        execute_order_list({0}, acc);
        break;
      case 35:
        execute_order_list({5, 8}, acc);
        break;
      case 36:
        execute_order_list({0, 11}, acc);
        break;
      case 37:
        if (branch_l) {
          execute_order_list({0, 11}, acc);
        } else {
          execute_order_list({5}, acc);
        }
        break;
      case 38:
        execute_order_list({5, 11}, acc);
        break;
      case 39:
        execute_order_list({0}, acc);
        break;
      case 40:
        if (branch_l) {
          execute_order_list({0, 11}, acc);
        } else {
          execute_order_list({5}, acc);
        }
        break;
      case 41:
        execute_order_list({0}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void execute_ik1302_3bd02a_gamma_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 35:
        execute_order_list({12, 13}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void execute_ik1302_3bd02a_post_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 0:
        execute_order_list({15}, acc);
        break;
      case 1:
        execute_order_list({15, 27}, acc);
        break;
      case 3:
        execute_order_list({15}, acc);
        break;
      case 4:
        execute_order_list({15}, acc);
        break;
      case 5:
        execute_order_list({16, 25}, acc);
        break;
      case 6:
        execute_order_list({15}, acc);
        break;
      case 7:
        execute_order_list({15}, acc);
        break;
      case 8:
        execute_order_list({16, 25}, acc);
        break;
      case 9:
        execute_order_list({15}, acc);
        break;
      case 10:
        execute_order_list({15}, acc);
        break;
      case 11:
        execute_order_list({16, 25}, acc);
        break;
      case 12:
        execute_order_list({15}, acc);
        break;
      case 13:
        execute_order_list({15}, acc);
        break;
      case 14:
        execute_order_list({16, 25}, acc);
        break;
      case 15:
        execute_order_list({15}, acc);
        break;
      case 16:
        execute_order_list({15}, acc);
        break;
      case 17:
        execute_order_list({16, 25}, acc);
        break;
      case 18:
        execute_order_list({15}, acc);
        break;
      case 19:
        execute_order_list({15}, acc);
        break;
      case 20:
        execute_order_list({16, 25}, acc);
        break;
      case 21:
        execute_order_list({15}, acc);
        break;
      case 22:
        execute_order_list({15}, acc);
        break;
      case 23:
        execute_order_list({16, 25}, acc);
        break;
      case 25:
        execute_order_list({17, 27}, acc);
        break;
      case 35:
        execute_order_list({25, 27}, acc);
        break;
      case 36:
        execute_order_list({27, 32}, acc);
        break;
      case 37:
        execute_order_list({22, 27}, acc);
        break;
      case 38:
        execute_order_list({23}, acc);
        break;
      case 39:
        execute_order_list({27, 32}, acc);
        break;
      case 40:
        execute_order_list({22, 27}, acc);
        break;
      case 41:
        execute_order_list({27}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void tick_ik1302_3bd02a_decoded() {
    static constexpr uint32_t kCommand = 0x3bd02a;
    if (tick_index == 0) command = kCommand;

    const int current_tick = tick_index;
    const bool branch_l = l != 0;
    if (current_tick < 27) {
      sync_address = 42;
    } else if (current_tick < 36) {
      sync_address = 32;
    } else {
      if (current_tick == 36) {
        r[37] = 15 & 0xF;
        r[40] = 239 >> 4;
      }
      sync_address = 95;
    }

    Accumulator acc;
    execute_ik1302_3bd02a_pre_add_orders(current_tick, branch_l, acc);

    if (key_y == 0) t = 0;

    execute_ik1302_3bd02a_gamma_orders(current_tick, branch_l, acc);

    int sum = acc.alpha + acc.beta + acc.gamma;
    acc.sigma = sum & 0xF;
    carry = (sum >> 4) & 1;

    execute_ik1302_3bd02a_post_add_orders(current_tick, branch_l, acc);

    output = memory[current_tick];
    memory[current_tick] = input;
    ++tick_index;
    if (tick_index == 42) tick_index = 0;
  }


  void execute_ik1302_42db13_pre_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 0:
        execute_order_list({6}, acc);
        break;
      case 1:
        execute_order_list({8}, acc);
        break;
      case 2:
        execute_order_list({0, 7}, acc);
        break;
      case 3:
        execute_order_list({9}, acc);
        break;
      case 6:
        execute_order_list({9}, acc);
        break;
      case 9:
        execute_order_list({9}, acc);
        break;
      case 12:
        execute_order_list({9}, acc);
        break;
      case 15:
        execute_order_list({9}, acc);
        break;
      case 18:
        execute_order_list({9}, acc);
        break;
      case 21:
        execute_order_list({9}, acc);
        break;
      case 27:
        execute_order_list({1}, acc);
        break;
      case 28:
        execute_order_list({5}, acc);
        break;
      case 30:
        execute_order_list({1}, acc);
        break;
      case 31:
        execute_order_list({5}, acc);
        break;
      case 33:
        execute_order_list({1}, acc);
        break;
      case 34:
        execute_order_list({5}, acc);
        break;
      case 35:
        execute_order_list({0}, acc);
        break;
      case 36:
        execute_order_list({0, 11}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void execute_ik1302_42db13_gamma_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 36:
        execute_order_list({12}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void execute_ik1302_42db13_post_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 0:
        execute_order_list({27}, acc);
        break;
      case 1:
        execute_order_list({27}, acc);
        break;
      case 2:
        execute_order_list({25, 27}, acc);
        break;
      case 4:
        execute_order_list({29}, acc);
        break;
      case 7:
        execute_order_list({29}, acc);
        break;
      case 10:
        execute_order_list({29}, acc);
        break;
      case 13:
        execute_order_list({29}, acc);
        break;
      case 16:
        execute_order_list({29}, acc);
        break;
      case 19:
        execute_order_list({29}, acc);
        break;
      case 22:
        execute_order_list({29}, acc);
        break;
      case 27:
        execute_order_list({27}, acc);
        break;
      case 30:
        execute_order_list({27}, acc);
        break;
      case 33:
        execute_order_list({27}, acc);
        break;
      case 35:
        execute_order_list({29}, acc);
        break;
      case 36:
        execute_order_list({16}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void tick_ik1302_42db13_decoded() {
    static constexpr uint32_t kCommand = 0x42db13;
    if (tick_index == 0) command = kCommand;

    const int current_tick = tick_index;
    const bool branch_l = l != 0;
    if (current_tick < 27) {
      sync_address = 19;
    } else if (current_tick < 36) {
      sync_address = 54;
    } else {
      sync_address = 11;
    }

    Accumulator acc;
    execute_ik1302_42db13_pre_add_orders(current_tick, branch_l, acc);

    if (key_y == 0) t = 0;

    execute_ik1302_42db13_gamma_orders(current_tick, branch_l, acc);

    int sum = acc.alpha + acc.beta + acc.gamma;
    acc.sigma = sum & 0xF;
    carry = (sum >> 4) & 1;

    execute_ik1302_42db13_post_add_orders(current_tick, branch_l, acc);

    output = memory[current_tick];
    memory[current_tick] = input;
    ++tick_index;
    if (tick_index == 42) tick_index = 0;
  }


  void execute_ik1302_0523c0_pre_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 27:
        execute_order_list({5, 8}, acc);
        break;
      case 28:
        execute_order_list({5}, acc);
        break;
      case 29:
        execute_order_list({10}, acc);
        break;
      case 30:
        execute_order_list({5, 11}, acc);
        break;
      case 31:
        execute_order_list({0}, acc);
        break;
      case 32:
        execute_order_list({8}, acc);
        break;
      case 33:
        execute_order_list({5}, acc);
        break;
      case 34:
        execute_order_list({0}, acc);
        break;
      case 35:
        execute_order_list({0}, acc);
        break;
      case 36:
        execute_order_list({10}, acc);
        break;
      case 37:
        execute_order_list({5, 11}, acc);
        break;
      case 38:
        execute_order_list({5, 11}, acc);
        break;
      case 39:
        execute_order_list({10}, acc);
        break;
      case 40:
        execute_order_list({5}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void execute_ik1302_0523c0_gamma_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      default:
        break;
    }
    (void)branch_l;
  }

  void execute_ik1302_0523c0_post_add_orders(int tick, bool branch_l, Accumulator& acc) {
    switch (tick) {
      case 27:
        execute_order_list({27}, acc);
        break;
      case 28:
        execute_order_list({22}, acc);
        break;
      case 29:
        execute_order_list({27}, acc);
        break;
      case 30:
        execute_order_list({27}, acc);
        break;
      case 31:
        execute_order_list({22}, acc);
        break;
      case 32:
        execute_order_list({27}, acc);
        break;
      case 33:
        execute_order_list({26, 29}, acc);
        break;
      case 34:
        execute_order_list({22}, acc);
        break;
      case 35:
        execute_order_list({17, 27}, acc);
        break;
      case 36:
        execute_order_list({27}, acc);
        break;
      case 37:
        execute_order_list({27}, acc);
        break;
      case 38:
        execute_order_list({23}, acc);
        break;
      case 39:
        execute_order_list({27}, acc);
        break;
      case 40:
        execute_order_list({22}, acc);
        break;
      default:
        break;
    }
    (void)branch_l;
  }

  void tick_ik1302_0523c0_decoded() {
    static constexpr uint32_t kCommand = 0x0523c0;
    if (tick_index == 0) command = kCommand;

    const int current_tick = tick_index;
    const bool branch_l = l != 0;
    if (current_tick < 27) {
      sync_address = 64;
    } else if (current_tick < 36) {
      sync_address = 71;
    } else {
      sync_address = 20;
    }

    Accumulator acc;
    execute_ik1302_0523c0_pre_add_orders(current_tick, branch_l, acc);

    if (key_y == 0) t = 0;

    execute_ik1302_0523c0_gamma_orders(current_tick, branch_l, acc);

    int sum = acc.alpha + acc.beta + acc.gamma;
    acc.sigma = sum & 0xF;
    carry = (sum >> 4) & 1;

    execute_ik1302_0523c0_post_add_orders(current_tick, branch_l, acc);

    output = memory[current_tick];
    memory[current_tick] = input;
    ++tick_index;
    if (tick_index == 42) tick_index = 0;
  }


  bool tick_ik1303_generated_decoded(uint32_t decoded_command) {
    switch (decoded_command) {
      case 0x060204:
        tick_ik1303_060204_decoded();
        return true;
      case 0x701d50:
        tick_ik1303_701d50_decoded();
        return true;
      case 0x388221:
        tick_ik1303_388221_decoded();
        return true;
      case 0x0a3dd0:
        tick_ik1303_0a3dd0_decoded();
        return true;
      case 0x1cf7dc:
        tick_ik1303_1cf7dc_decoded();
        return true;
      case 0x29bae9:
        tick_ik1303_29bae9_decoded();
        return true;
      case 0x2b53a6:
        tick_ik1303_2b53a6_decoded();
        return true;
      case 0x0c5084:
        tick_ik1303_0c5084_decoded();
        return true;
      case 0x046d2f:
        tick_ik1303_046d2f_decoded();
        return true;
      case 0x691122:
        tick_ik1303_691122_decoded();
        return true;
      case 0x0b9020:
        tick_ik1303_0b9020_decoded();
        return true;
      case 0x482848:
        tick_ik1303_482848_decoded();
        return true;
      case 0x0b704c:
        tick_ik1303_0b704c_decoded();
        return true;
      case 0x43f7d0:
        tick_ik1303_43f7d0_decoded();
        return true;
      case 0x05e6d0:
        tick_ik1303_05e6d0_decoded();
        return true;
      case 0x0fe9cb:
        tick_ik1303_0fe9cb_decoded();
        return true;
      case 0x1e4285:
        tick_ik1303_1e4285_decoded();
        return true;
      case 0x041c2f:
        tick_ik1303_041c2f_decoded();
        return true;
      case 0x058285:
        tick_ik1303_058285_decoded();
        return true;
      case 0x7b5d50:
        tick_ik1303_7b5d50_decoded();
        return true;
      case 0x010285:
        tick_ik1303_010285_decoded();
        return true;
      case 0x0212a5:
        tick_ik1303_0212a5_decoded();
        return true;
      case 0x4207d0:
        tick_ik1303_4207d0_decoded();
        return true;
      case 0x6ce847:
        tick_ik1303_6ce847_decoded();
        return true;
      case 0x035ecc:
        tick_ik1303_035ecc_decoded();
        return true;
      case 0x2f7047:
        tick_ik1303_2f7047_decoded();
        return true;
      case 0x05e879:
        tick_ik1303_05e879_decoded();
        return true;
      case 0x17b035:
        tick_ik1303_17b035_decoded();
        return true;
      case 0x41bfc7:
        tick_ik1303_41bfc7_decoded();
        return true;
      case 0x232951:
        tick_ik1303_232951_decoded();
        return true;
      case 0x032951:
        tick_ik1303_032951_decoded();
        return true;
      case 0x1b94a6:
        tick_ik1303_1b94a6_decoded();
        return true;
      case 0x23f02f:
        tick_ik1303_23f02f_decoded();
        return true;
      case 0x23282a:
        tick_ik1303_23282a_decoded();
        return true;
      case 0x05d4a8:
        tick_ik1303_05d4a8_decoded();
        return true;
      case 0x033977:
        tick_ik1303_033977_decoded();
        return true;
      case 0x0d8204:
        tick_ik1303_0d8204_decoded();
        return true;
      case 0x011020:
        tick_ik1303_011020_decoded();
        return true;
      case 0x1d3950:
        tick_ik1303_1d3950_decoded();
        return true;
      case 0x1079f4:
        tick_ik1303_1079f4_decoded();
        return true;
      case 0x2f4358:
        tick_ik1303_2f4358_decoded();
        return true;
      case 0x3a9250:
        tick_ik1303_3a9250_decoded();
        return true;
      case 0x0238e6:
        tick_ik1303_0238e6_decoded();
        return true;
      case 0x6f5cd0:
        tick_ik1303_6f5cd0_decoded();
        return true;
      case 0x069750:
        tick_ik1303_069750_decoded();
        return true;
      case 0x6d2847:
        tick_ik1303_6d2847_decoded();
        return true;
      case 0x0f1020:
        tick_ik1303_0f1020_decoded();
        return true;
      case 0x0764b3:
        tick_ik1303_0764b3_decoded();
        return true;
      case 0x0152d6:
        tick_ik1303_0152d6_decoded();
        return true;
      case 0x1babd6:
        tick_ik1303_1babd6_decoded();
        return true;
      case 0x397ed8:
        tick_ik1303_397ed8_decoded();
        return true;
      case 0x032050:
        tick_ik1303_032050_decoded();
        return true;
      case 0x04ee5c:
        tick_ik1303_04ee5c_decoded();
        return true;
      case 0x01a1d0:
        tick_ik1303_01a1d0_decoded();
        return true;
      case 0x0c40fd:
        tick_ik1303_0c40fd_decoded();
        return true;
      case 0x0c46ae:
        tick_ik1303_0c46ae_decoded();
        return true;
      case 0x040735:
        tick_ik1303_040735_decoded();
        return true;
      case 0x014150:
        tick_ik1303_014150_decoded();
        return true;
      case 0x066850:
        tick_ik1303_066850_decoded();
        return true;
      case 0x0e162c:
        tick_ik1303_0e162c_decoded();
        return true;
      case 0x0bf5d6:
        tick_ik1303_0bf5d6_decoded();
        return true;
      case 0x025eec:
        tick_ik1303_025eec_decoded();
        return true;
      case 0x3c26d0:
        tick_ik1303_3c26d0_decoded();
        return true;
      case 0x030bd0:
        tick_ik1303_030bd0_decoded();
        return true;
      case 0x01e550:
        tick_ik1303_01e550_decoded();
        return true;
      case 0x03ecf0:
        tick_ik1303_03ecf0_decoded();
        return true;
      case 0x6ed122:
        tick_ik1303_6ed122_decoded();
        return true;
      case 0x43e947:
        tick_ik1303_43e947_decoded();
        return true;
      case 0x0612a5:
        tick_ik1303_0612a5_decoded();
        return true;
      case 0x03821e:
        tick_ik1303_03821e_decoded();
        return true;
      case 0x1ff225:
        tick_ik1303_1ff225_decoded();
        return true;
      case 0x45f2d3:
        tick_ik1303_45f2d3_decoded();
        return true;
      case 0x1f9232:
        tick_ik1303_1f9232_decoded();
        return true;
      case 0x031726:
        tick_ik1303_031726_decoded();
        return true;
      case 0x16dfbe:
        tick_ik1303_16dfbe_decoded();
        return true;
      case 0x027fa6:
        tick_ik1303_027fa6_decoded();
        return true;
      default:
        return false;
    }
  }


  bool tick_ik1306_generated_decoded(uint32_t decoded_command) {
    switch (decoded_command) {
      case 0x018040:
        tick_ik1306_018040_decoded();
        return true;
      case 0x01f52f:
        tick_ik1306_01f52f_decoded();
        return true;
      case 0x02e600:
        tick_ik1306_02e600_decoded();
        return true;
      case 0x024000:
        tick_ik1306_024000_decoded();
        return true;
      case 0x02e680:
        tick_ik1306_02e680_decoded();
        return true;
      default:
        return false;
    }
  }

  bool tick_ik1302_generated_decoded(uint32_t decoded_command) {
    switch (decoded_command) {
      case 0x0479e0:
        tick_ik1302_0479e0_decoded();
        return true;
      case 0x046440:
        tick_ik1302_046440_decoded();
        return true;
      case 0x410140:
        tick_ik1302_410140_decoded();
        return true;
      case 0x05a040:
        tick_ik1302_05a040_decoded();
        return true;
      case 0x06d940:
        tick_ik1302_06d940_decoded();
        return true;
      case 0x01a340:
        tick_ik1302_01a340_decoded();
        return true;
      case 0x56e013:
        tick_ik1302_56e013_decoded();
        return true;
      case 0x000840:
        tick_ik1302_000840_decoded();
        return true;
      case 0x3429ed:
        tick_ik1302_3429ed_decoded();
        return true;
      case 0x0176c0:
        tick_ik1302_0176c0_decoded();
        return true;
      case 0x406e5b:
        tick_ik1302_406e5b_decoded();
        return true;
      case 0x002f5d:
        tick_ik1302_002f5d_decoded();
        return true;
      case 0x0199b3:
        tick_ik1302_0199b3_decoded();
        return true;
      case 0x42e03b:
        tick_ik1302_42e03b_decoded();
        return true;
      case 0x4d2043:
        tick_ik1302_4d2043_decoded();
        return true;
      case 0x74d047:
        tick_ik1302_74d047_decoded();
        return true;
      case 0x020f1e:
        tick_ik1302_020f1e_decoded();
        return true;
      case 0x2ace9d:
        tick_ik1302_2ace9d_decoded();
        return true;
      case 0x025940:
        tick_ik1302_025940_decoded();
        return true;
      case 0x47c0c0:
        tick_ik1302_47c0c0_decoded();
        return true;
      case 0x056440:
        tick_ik1302_056440_decoded();
        return true;
      case 0x019223:
        tick_ik1302_019223_decoded();
        return true;
      case 0x4179c0:
        tick_ik1302_4179c0_decoded();
        return true;
      case 0x018e9d:
        tick_ik1302_018e9d_decoded();
        return true;
      case 0x2062c5:
        tick_ik1302_2062c5_decoded();
        return true;
      case 0x018f1e:
        tick_ik1302_018f1e_decoded();
        return true;
      case 0x024f1e:
        tick_ik1302_024f1e_decoded();
        return true;
      case 0x546078:
        tick_ik1302_546078_decoded();
        return true;
      case 0x020e9d:
        tick_ik1302_020e9d_decoded();
        return true;
      case 0x589106:
        tick_ik1302_589106_decoded();
        return true;
      case 0x07a2c5:
        tick_ik1302_07a2c5_decoded();
        return true;
      case 0x046060:
        tick_ik1302_046060_decoded();
        return true;
      case 0x02d740:
        tick_ik1302_02d740_decoded();
        return true;
      case 0x03d6c0:
        tick_ik1302_03d6c0_decoded();
        return true;
      case 0x438fc0:
        tick_ik1302_438fc0_decoded();
        return true;
      case 0x037b77:
        tick_ik1302_037b77_decoded();
        return true;
      case 0x01d940:
        tick_ik1302_01d940_decoded();
        return true;
      case 0x34de40:
        tick_ik1302_34de40_decoded();
        return true;
      case 0x24fac0:
        tick_ik1302_24fac0_decoded();
        return true;
      case 0x011977:
        tick_ik1302_011977_decoded();
        return true;
      case 0x085068:
        tick_ik1302_085068_decoded();
        return true;
      case 0x4344c0:
        tick_ik1302_4344c0_decoded();
        return true;
      case 0x63e06f:
        tick_ik1302_63e06f_decoded();
        return true;
      case 0x5459cc:
        tick_ik1302_5459cc_decoded();
        return true;
      case 0x061cb9:
        tick_ik1302_061cb9_decoded();
        return true;
      case 0x438bc0:
        tick_ik1302_438bc0_decoded();
        return true;
      case 0x0179c0:
        tick_ik1302_0179c0_decoded();
        return true;
      case 0x3854f3:
        tick_ik1302_3854f3_decoded();
        return true;
      case 0x01546c:
        tick_ik1302_01546c_decoded();
        return true;
      case 0x74e9c0:
        tick_ik1302_74e9c0_decoded();
        return true;
      case 0x23d040:
        tick_ik1302_23d040_decoded();
        return true;
      case 0x3327cf:
        tick_ik1302_3327cf_decoded();
        return true;
      case 0x016040:
        tick_ik1302_016040_decoded();
        return true;
      case 0x079a34:
        tick_ik1302_079a34_decoded();
        return true;
      case 0x4139c0:
        tick_ik1302_4139c0_decoded();
        return true;
      case 0x31962b:
        tick_ik1302_31962b_decoded();
        return true;
      case 0x0f8e9d:
        tick_ik1302_0f8e9d_decoded();
        return true;
      case 0x410bc0:
        tick_ik1302_410bc0_decoded();
        return true;
      case 0x079ab5:
        tick_ik1302_079ab5_decoded();
        return true;
      case 0x38a057:
        tick_ik1302_38a057_decoded();
        return true;
      case 0x795c12:
        tick_ik1302_795c12_decoded();
        return true;
      case 0x362067:
        tick_ik1302_362067_decoded();
        return true;
      case 0x02a02a:
        tick_ik1302_02a02a_decoded();
        return true;
      case 0x3bd02a:
        tick_ik1302_3bd02a_decoded();
        return true;
      case 0x42db13:
        tick_ik1302_42db13_decoded();
        return true;
      case 0x0523c0:
        tick_ik1302_0523c0_decoded();
        return true;
      default:
        return false;
    }
  }

  bool tick_ik1302_generated_command_dispatch(uint32_t decoded_command) {
    switch (decoded_command) {
      case 0x000840:
        tick_ik1302_000840_decoded();
        return true;
      case 0x002f5d:
        tick_ik1302_002f5d_decoded();
        return true;
      case 0x011977:
        tick_ik1302_011977_decoded();
        return true;
      case 0x01546c:
        tick_ik1302_01546c_decoded();
        return true;
      case 0x016040:
        tick_ik1302_016040_decoded();
        return true;
      case 0x0176c0:
        tick_ik1302_0176c0_decoded();
        return true;
      case 0x0179c0:
        tick_ik1302_0179c0_decoded();
        return true;
      case 0x018e9d:
        tick_ik1302_018e9d_decoded();
        return true;
      case 0x018f1e:
        tick_ik1302_018f1e_decoded();
        return true;
      case 0x019223:
        tick_ik1302_019223_decoded();
        return true;
      case 0x0199b3:
        tick_ik1302_0199b3_decoded();
        return true;
      case 0x01a340:
        tick_ik1302_01a340_decoded();
        return true;
      case 0x01d940:
        tick_ik1302_01d940_decoded();
        return true;
      case 0x020e9d:
        tick_ik1302_020e9d_decoded();
        return true;
      case 0x020f1e:
        tick_ik1302_020f1e_decoded();
        return true;
      case 0x024f1e:
        tick_ik1302_024f1e_decoded();
        return true;
      case 0x025940:
        tick_ik1302_025940_decoded();
        return true;
      case 0x02a02a:
        tick_ik1302_02a02a_decoded();
        return true;
      case 0x02d740:
        tick_ik1302_02d740_decoded();
        return true;
      case 0x037b77:
        tick_ik1302_037b77_decoded();
        return true;
      case 0x03d6c0:
        tick_ik1302_03d6c0_decoded();
        return true;
      case 0x046060:
        tick_ik1302_046060_decoded();
        return true;
      case 0x046440:
        tick_ik1302_046440_decoded();
        return true;
      case 0x0479e0:
        tick_ik1302_0479e0_decoded();
        return true;
      case 0x0523c0:
        tick_ik1302_0523c0_decoded();
        return true;
      case 0x056440:
        tick_ik1302_056440_decoded();
        return true;
      case 0x05a040:
        tick_ik1302_05a040_decoded();
        return true;
      case 0x061cb9:
        tick_ik1302_061cb9_decoded();
        return true;
      case 0x06d940:
        tick_ik1302_06d940_decoded();
        return true;
      case 0x079a34:
        tick_ik1302_079a34_decoded();
        return true;
      case 0x079ab5:
        tick_ik1302_079ab5_decoded();
        return true;
      case 0x07a2c5:
        tick_ik1302_07a2c5_decoded();
        return true;
      case 0x085068:
        tick_ik1302_085068_decoded();
        return true;
      case 0x0f8e9d:
        tick_ik1302_0f8e9d_decoded();
        return true;
      case 0x2062c5:
        tick_ik1302_2062c5_decoded();
        return true;
      case 0x23d040:
        tick_ik1302_23d040_decoded();
        return true;
      case 0x24fac0:
        tick_ik1302_24fac0_decoded();
        return true;
      case 0x2ace9d:
        tick_ik1302_2ace9d_decoded();
        return true;
      case 0x31962b:
        tick_ik1302_31962b_decoded();
        return true;
      case 0x3327cf:
        tick_ik1302_3327cf_decoded();
        return true;
      case 0x3429ed:
        tick_ik1302_3429ed_decoded();
        return true;
      case 0x34de40:
        tick_ik1302_34de40_decoded();
        return true;
      case 0x362067:
        tick_ik1302_362067_decoded();
        return true;
      case 0x3854f3:
        tick_ik1302_3854f3_decoded();
        return true;
      case 0x38a057:
        tick_ik1302_38a057_decoded();
        return true;
      case 0x3bd02a:
        tick_ik1302_3bd02a_decoded();
        return true;
      case 0x406e5b:
        tick_ik1302_406e5b_decoded();
        return true;
      case 0x410140:
        tick_ik1302_410140_decoded();
        return true;
      case 0x410bc0:
        tick_ik1302_410bc0_decoded();
        return true;
      case 0x4139c0:
        tick_ik1302_4139c0_decoded();
        return true;
      case 0x4179c0:
        tick_ik1302_4179c0_decoded();
        return true;
      case 0x42db13:
        tick_ik1302_42db13_decoded();
        return true;
      case 0x42e03b:
        tick_ik1302_42e03b_decoded();
        return true;
      case 0x4344c0:
        tick_ik1302_4344c0_decoded();
        return true;
      case 0x438bc0:
        tick_ik1302_438bc0_decoded();
        return true;
      case 0x438fc0:
        tick_ik1302_438fc0_decoded();
        return true;
      case 0x47c0c0:
        tick_ik1302_47c0c0_decoded();
        return true;
      case 0x4d2043:
        tick_ik1302_4d2043_decoded();
        return true;
      case 0x5459cc:
        tick_ik1302_5459cc_decoded();
        return true;
      case 0x546078:
        tick_ik1302_546078_decoded();
        return true;
      case 0x56e013:
        tick_ik1302_56e013_decoded();
        return true;
      case 0x589106:
        tick_ik1302_589106_decoded();
        return true;
      case 0x63e06f:
        tick_ik1302_63e06f_decoded();
        return true;
      case 0x74d047:
        tick_ik1302_74d047_decoded();
        return true;
      case 0x74e9c0:
        tick_ik1302_74e9c0_decoded();
        return true;
      case 0x795c12:
        tick_ik1302_795c12_decoded();
        return true;
      default:
        return false;
    }
  }

  bool tick_ik1302_generated_address_dispatch() {
    if (tick_index != 0) return tick_ik1302_generated_command_dispatch(command);
    switch (command_address_for_trace()) {
      case 0x01:
        tick_ik1302_0479e0_decoded();
        return true;
      case 0x02:
        tick_ik1302_046440_decoded();
        return true;
      case 0x03:
        tick_ik1302_410140_decoded();
        return true;
      case 0x04:
        tick_ik1302_05a040_decoded();
        return true;
      case 0x05:
        tick_ik1302_06d940_decoded();
        return true;
      case 0x06:
        tick_ik1302_01a340_decoded();
        return true;
      case 0x07:
        tick_ik1302_56e013_decoded();
        return true;
      case 0x09:
        tick_ik1302_000840_decoded();
        return true;
      case 0x10:
        tick_ik1302_3429ed_decoded();
        return true;
      case 0x1f:
        tick_ik1302_0176c0_decoded();
        return true;
      case 0x33:
        tick_ik1302_406e5b_decoded();
        return true;
      case 0x35:
        tick_ik1302_002f5d_decoded();
        return true;
      case 0x40:
        tick_ik1302_0199b3_decoded();
        return true;
      case 0x41:
        tick_ik1302_42e03b_decoded();
        return true;
      case 0x42:
        tick_ik1302_4d2043_decoded();
        return true;
      case 0x46:
        tick_ik1302_74d047_decoded();
        return true;
      case 0x47:
        tick_ik1302_020f1e_decoded();
        return true;
      case 0x49:
        tick_ik1302_2ace9d_decoded();
        return true;
      case 0x67:
        tick_ik1302_025940_decoded();
        return true;
      case 0x68:
        tick_ik1302_47c0c0_decoded();
        return true;
      case 0x69:
        tick_ik1302_056440_decoded();
        return true;
      case 0x6a:
        tick_ik1302_019223_decoded();
        return true;
      case 0x6b:
        tick_ik1302_019223_decoded();
        return true;
      case 0x6c:
        tick_ik1302_4179c0_decoded();
        return true;
      case 0x91:
        tick_ik1302_018e9d_decoded();
        return true;
      case 0x92:
        tick_ik1302_2062c5_decoded();
        return true;
      case 0x93:
        tick_ik1302_0199b3_decoded();
        return true;
      case 0x94:
        tick_ik1302_018f1e_decoded();
        return true;
      case 0x95:
        tick_ik1302_024f1e_decoded();
        return true;
      case 0x97:
        tick_ik1302_546078_decoded();
        return true;
      case 0x98:
        tick_ik1302_020e9d_decoded();
        return true;
      case 0x99:
        tick_ik1302_589106_decoded();
        return true;
      case 0x9a:
        tick_ik1302_07a2c5_decoded();
        return true;
      case 0x9b:
        tick_ik1302_046060_decoded();
        return true;
      case 0x9c:
        tick_ik1302_02d740_decoded();
        return true;
      case 0x9d:
        tick_ik1302_03d6c0_decoded();
        return true;
      case 0x9e:
        tick_ik1302_438fc0_decoded();
        return true;
      case 0x9f:
        tick_ik1302_037b77_decoded();
        return true;
      case 0xa5:
        tick_ik1302_01d940_decoded();
        return true;
      case 0xaa:
        tick_ik1302_34de40_decoded();
        return true;
      case 0xab:
        tick_ik1302_24fac0_decoded();
        return true;
      case 0xad:
        tick_ik1302_011977_decoded();
        return true;
      case 0xb0:
        tick_ik1302_085068_decoded();
        return true;
      case 0xba:
        tick_ik1302_4344c0_decoded();
        return true;
      case 0xbd:
        tick_ik1302_63e06f_decoded();
        return true;
      case 0xbe:
        tick_ik1302_5459cc_decoded();
        return true;
      case 0xc9:
        tick_ik1302_061cb9_decoded();
        return true;
      case 0xcc:
        tick_ik1302_438bc0_decoded();
        return true;
      case 0xcd:
        tick_ik1302_0179c0_decoded();
        return true;
      case 0xd9:
        tick_ik1302_3854f3_decoded();
        return true;
      case 0xde:
        tick_ik1302_01546c_decoded();
        return true;
      case 0xe2:
        tick_ik1302_74e9c0_decoded();
        return true;
      case 0xe3:
        tick_ik1302_23d040_decoded();
        return true;
      case 0xe4:
        tick_ik1302_3327cf_decoded();
        return true;
      case 0xe5:
        tick_ik1302_046060_decoded();
        return true;
      case 0xe6:
        tick_ik1302_016040_decoded();
        return true;
      case 0xe7:
        tick_ik1302_079a34_decoded();
        return true;
      case 0xe8:
        tick_ik1302_4139c0_decoded();
        return true;
      case 0xea:
        tick_ik1302_31962b_decoded();
        return true;
      case 0xec:
        tick_ik1302_0f8e9d_decoded();
        return true;
      case 0xed:
        tick_ik1302_410bc0_decoded();
        return true;
      case 0xee:
        tick_ik1302_079ab5_decoded();
        return true;
      case 0xf1:
        tick_ik1302_38a057_decoded();
        return true;
      case 0xf3:
        tick_ik1302_795c12_decoded();
        return true;
      case 0xf4:
        tick_ik1302_362067_decoded();
        return true;
      case 0xf6:
        tick_ik1302_02a02a_decoded();
        return true;
      case 0xf7:
        tick_ik1302_3bd02a_decoded();
        return true;
      case 0xfb:
        tick_ik1302_42db13_decoded();
        return true;
      case 0xfc:
        tick_ik1302_0523c0_decoded();
        return true;
      default:
        return false;
    }
  }

  bool tick_ik1303_generated_command_dispatch(uint32_t decoded_command) {
    switch (decoded_command) {
      case 0x010285:
        tick_ik1303_010285_decoded();
        return true;
      case 0x011020:
        tick_ik1303_011020_decoded();
        return true;
      case 0x01322b:
        tick_ik1303_01322b_decoded();
        return true;
      case 0x014150:
        tick_ik1303_014150_decoded();
        return true;
      case 0x0152d6:
        tick_ik1303_0152d6_decoded();
        return true;
      case 0x01a1d0:
        tick_ik1303_01a1d0_decoded();
        return true;
      case 0x01a7ce:
        tick_ik1303_01a7ce_decoded();
        return true;
      case 0x01e550:
        tick_ik1303_01e550_decoded();
        return true;
      case 0x020285:
        tick_ik1303_020285_decoded();
        return true;
      case 0x0212a5:
        tick_ik1303_0212a5_decoded();
        return true;
      case 0x0238e6:
        tick_ik1303_0238e6_decoded();
        return true;
      case 0x023e50:
        tick_ik1303_023e50_decoded();
        return true;
      case 0x025eec:
        tick_ik1303_025eec_decoded();
        return true;
      case 0x027fa6:
        tick_ik1303_027fa6_decoded();
        return true;
      case 0x029ba0:
        tick_ik1303_029ba0_decoded();
        return true;
      case 0x02af04:
        tick_ik1303_02af04_decoded();
        return true;
      case 0x02b0ae:
        tick_ik1303_02b0ae_decoded();
        return true;
      case 0x030bd0:
        tick_ik1303_030bd0_decoded();
        return true;
      case 0x031726:
        tick_ik1303_031726_decoded();
        return true;
      case 0x032050:
        tick_ik1303_032050_decoded();
        return true;
      case 0x032951:
        tick_ik1303_032951_decoded();
        return true;
      case 0x0331ab:
        tick_ik1303_0331ab_decoded();
        return true;
      case 0x033977:
        tick_ik1303_033977_decoded();
        return true;
      case 0x035ecc:
        tick_ik1303_035ecc_decoded();
        return true;
      case 0x03821e:
        tick_ik1303_03821e_decoded();
        return true;
      case 0x03ecf0:
        tick_ik1303_03ecf0_decoded();
        return true;
      case 0x04052e:
        tick_ik1303_04052e_decoded();
        return true;
      case 0x040735:
        tick_ik1303_040735_decoded();
        return true;
      case 0x041c2f:
        tick_ik1303_041c2f_decoded();
        return true;
      case 0x04582e:
        tick_ik1303_04582e_decoded();
        return true;
      case 0x0458af:
        tick_ik1303_0458af_decoded();
        return true;
      case 0x046d2f:
        tick_ik1303_046d2f_decoded();
        return true;
      case 0x049a32:
        tick_ik1303_049a32_decoded();
        return true;
      case 0x049a33:
        tick_ik1303_049a33_decoded();
        return true;
      case 0x04ee5c:
        tick_ik1303_04ee5c_decoded();
        return true;
      case 0x04f0ae:
        tick_ik1303_04f0ae_decoded();
        return true;
      case 0x058285:
        tick_ik1303_058285_decoded();
        return true;
      case 0x05d4a8:
        tick_ik1303_05d4a8_decoded();
        return true;
      case 0x05dd62:
        tick_ik1303_05dd62_decoded();
        return true;
      case 0x05e6d0:
        tick_ik1303_05e6d0_decoded();
        return true;
      case 0x05e879:
        tick_ik1303_05e879_decoded();
        return true;
      case 0x060204:
        tick_ik1303_060204_decoded();
        return true;
      case 0x0612a5:
        tick_ik1303_0612a5_decoded();
        return true;
      case 0x066850:
        tick_ik1303_066850_decoded();
        return true;
      case 0x069750:
        tick_ik1303_069750_decoded();
        return true;
      case 0x0764b3:
        tick_ik1303_0764b3_decoded();
        return true;
      case 0x0a3dd0:
        tick_ik1303_0a3dd0_decoded();
        return true;
      case 0x0b704c:
        tick_ik1303_0b704c_decoded();
        return true;
      case 0x0b9020:
        tick_ik1303_0b9020_decoded();
        return true;
      case 0x0bf5d6:
        tick_ik1303_0bf5d6_decoded();
        return true;
      case 0x0c40fd:
        tick_ik1303_0c40fd_decoded();
        return true;
      case 0x0c46ae:
        tick_ik1303_0c46ae_decoded();
        return true;
      case 0x0c5084:
        tick_ik1303_0c5084_decoded();
        return true;
      case 0x0d8204:
        tick_ik1303_0d8204_decoded();
        return true;
      case 0x0e162c:
        tick_ik1303_0e162c_decoded();
        return true;
      case 0x0e1dbb:
        tick_ik1303_0e1dbb_decoded();
        return true;
      case 0x0f1020:
        tick_ik1303_0f1020_decoded();
        return true;
      case 0x0fe9cb:
        tick_ik1303_0fe9cb_decoded();
        return true;
      case 0x1079f4:
        tick_ik1303_1079f4_decoded();
        return true;
      case 0x16dfbe:
        tick_ik1303_16dfbe_decoded();
        return true;
      case 0x17b035:
        tick_ik1303_17b035_decoded();
        return true;
      case 0x1b94a6:
        tick_ik1303_1b94a6_decoded();
        return true;
      case 0x1babd6:
        tick_ik1303_1babd6_decoded();
        return true;
      case 0x1cf7dc:
        tick_ik1303_1cf7dc_decoded();
        return true;
      case 0x1d3950:
        tick_ik1303_1d3950_decoded();
        return true;
      case 0x1e4285:
        tick_ik1303_1e4285_decoded();
        return true;
      case 0x1f9232:
        tick_ik1303_1f9232_decoded();
        return true;
      case 0x1ff225:
        tick_ik1303_1ff225_decoded();
        return true;
      case 0x203047:
        tick_ik1303_203047_decoded();
        return true;
      case 0x2067ce:
        tick_ik1303_2067ce_decoded();
        return true;
      case 0x21f02e:
        tick_ik1303_21f02e_decoded();
        return true;
      case 0x23282a:
        tick_ik1303_23282a_decoded();
        return true;
      case 0x232951:
        tick_ik1303_232951_decoded();
        return true;
      case 0x23f02f:
        tick_ik1303_23f02f_decoded();
        return true;
      case 0x29bae9:
        tick_ik1303_29bae9_decoded();
        return true;
      case 0x2b53a6:
        tick_ik1303_2b53a6_decoded();
        return true;
      case 0x2b73ab:
        tick_ik1303_2b73ab_decoded();
        return true;
      case 0x2f4358:
        tick_ik1303_2f4358_decoded();
        return true;
      case 0x2f7047:
        tick_ik1303_2f7047_decoded();
        return true;
      case 0x388221:
        tick_ik1303_388221_decoded();
        return true;
      case 0x397ed8:
        tick_ik1303_397ed8_decoded();
        return true;
      case 0x3a9250:
        tick_ik1303_3a9250_decoded();
        return true;
      case 0x3c26d0:
        tick_ik1303_3c26d0_decoded();
        return true;
      case 0x41bfc7:
        tick_ik1303_41bfc7_decoded();
        return true;
      case 0x4207d0:
        tick_ik1303_4207d0_decoded();
        return true;
      case 0x43e947:
        tick_ik1303_43e947_decoded();
        return true;
      case 0x43f7d0:
        tick_ik1303_43f7d0_decoded();
        return true;
      case 0x44ca8c:
        tick_ik1303_44ca8c_decoded();
        return true;
      case 0x44e80c:
        tick_ik1303_44e80c_decoded();
        return true;
      case 0x45f2d3:
        tick_ik1303_45f2d3_decoded();
        return true;
      case 0x482848:
        tick_ik1303_482848_decoded();
        return true;
      case 0x691122:
        tick_ik1303_691122_decoded();
        return true;
      case 0x6a95ea:
        tick_ik1303_6a95ea_decoded();
        return true;
      case 0x6ce847:
        tick_ik1303_6ce847_decoded();
        return true;
      case 0x6d2847:
        tick_ik1303_6d2847_decoded();
        return true;
      case 0x6e7a63:
        tick_ik1303_6e7a63_decoded();
        return true;
      case 0x6ed122:
        tick_ik1303_6ed122_decoded();
        return true;
      case 0x6f5cd0:
        tick_ik1303_6f5cd0_decoded();
        return true;
      case 0x6fa822:
        tick_ik1303_6fa822_decoded();
        return true;
      case 0x701d50:
        tick_ik1303_701d50_decoded();
        return true;
      case 0x7b5d50:
        tick_ik1303_7b5d50_decoded();
        return true;
      default:
        return false;
    }
  }

  bool tick_ik1303_generated_address_dispatch() {
    if (tick_index != 0) return tick_ik1303_generated_command_dispatch(command);
    switch (command_address_for_trace()) {
      case 0x01:
        tick_ik1303_16dfbe_decoded();
        return true;
      case 0x02:
        tick_ik1303_03ecf0_decoded();
        return true;
      case 0x05:
        tick_ik1303_0458af_decoded();
        return true;
      case 0x0f:
        tick_ik1303_6ed122_decoded();
        return true;
      case 0x11:
        tick_ik1303_43e947_decoded();
        return true;
      case 0x12:
        tick_ik1303_0612a5_decoded();
        return true;
      case 0x13:
        tick_ik1303_020285_decoded();
        return true;
      case 0x14:
        tick_ik1303_03821e_decoded();
        return true;
      case 0x15:
        tick_ik1303_049a33_decoded();
        return true;
      case 0x16:
        tick_ik1303_1ff225_decoded();
        return true;
      case 0x19:
        tick_ik1303_45f2d3_decoded();
        return true;
      case 0x1a:
        tick_ik1303_1f9232_decoded();
        return true;
      case 0x20:
        tick_ik1303_031726_decoded();
        return true;
      case 0x21:
        tick_ik1303_0c46ae_decoded();
        return true;
      case 0x22:
        tick_ik1303_040735_decoded();
        return true;
      case 0x25:
        tick_ik1303_04582e_decoded();
        return true;
      case 0x27:
        tick_ik1303_014150_decoded();
        return true;
      case 0x29:
        tick_ik1303_066850_decoded();
        return true;
      case 0x2a:
        tick_ik1303_0e162c_decoded();
        return true;
      case 0x2d:
        tick_ik1303_0bf5d6_decoded();
        return true;
      case 0x2e:
        tick_ik1303_025eec_decoded();
        return true;
      case 0x2f:
        tick_ik1303_3c26d0_decoded();
        return true;
      case 0x30:
        tick_ik1303_030bd0_decoded();
        return true;
      case 0x31:
        tick_ik1303_01e550_decoded();
        return true;
      case 0x32:
        tick_ik1303_6d2847_decoded();
        return true;
      case 0x33:
        tick_ik1303_0f1020_decoded();
        return true;
      case 0x34:
        tick_ik1303_6a95ea_decoded();
        return true;
      case 0x35:
        tick_ik1303_049a32_decoded();
        return true;
      case 0x36:
        tick_ik1303_0764b3_decoded();
        return true;
      case 0x37:
        tick_ik1303_44e80c_decoded();
        return true;
      case 0x38:
        tick_ik1303_0152d6_decoded();
        return true;
      case 0x39:
        tick_ik1303_023e50_decoded();
        return true;
      case 0x3d:
        tick_ik1303_1babd6_decoded();
        return true;
      case 0x3f:
        tick_ik1303_397ed8_decoded();
        return true;
      case 0x46:
        tick_ik1303_032050_decoded();
        return true;
      case 0x48:
        tick_ik1303_04ee5c_decoded();
        return true;
      case 0x4c:
        tick_ik1303_01a1d0_decoded();
        return true;
      case 0x51:
        tick_ik1303_0c40fd_decoded();
        return true;
      case 0x5c:
        tick_ik1303_04ee5c_decoded();
        return true;
      case 0x5e:
        tick_ik1303_0e1dbb_decoded();
        return true;
      case 0x5f:
        tick_ik1303_033977_decoded();
        return true;
      case 0x60:
        tick_ik1303_0d8204_decoded();
        return true;
      case 0x61:
        tick_ik1303_011020_decoded();
        return true;
      case 0x62:
        tick_ik1303_04052e_decoded();
        return true;
      case 0x65:
        tick_ik1303_01322b_decoded();
        return true;
      case 0x6e:
        tick_ik1303_44ca8c_decoded();
        return true;
      case 0x70:
        tick_ik1303_1d3950_decoded();
        return true;
      case 0x71:
        tick_ik1303_0331ab_decoded();
        return true;
      case 0x72:
        tick_ik1303_2b73ab_decoded();
        return true;
      case 0x73:
        tick_ik1303_02b0ae_decoded();
        return true;
      case 0x74:
        tick_ik1303_6e7a63_decoded();
        return true;
      case 0x75:
        tick_ik1303_1079f4_decoded();
        return true;
      case 0x76:
        tick_ik1303_2f4358_decoded();
        return true;
      case 0x77:
        tick_ik1303_3a9250_decoded();
        return true;
      case 0x78:
        tick_ik1303_0238e6_decoded();
        return true;
      case 0x79:
        tick_ik1303_6f5cd0_decoded();
        return true;
      case 0x7a:
        tick_ik1303_069750_decoded();
        return true;
      case 0x7b:
        tick_ik1303_2f7047_decoded();
        return true;
      case 0x7c:
        tick_ik1303_05e879_decoded();
        return true;
      case 0x7d:
        tick_ik1303_17b035_decoded();
        return true;
      case 0x7f:
        tick_ik1303_41bfc7_decoded();
        return true;
      case 0x80:
        tick_ik1303_232951_decoded();
        return true;
      case 0x81:
        tick_ik1303_04f0ae_decoded();
        return true;
      case 0x82:
        tick_ik1303_21f02e_decoded();
        return true;
      case 0x88:
        tick_ik1303_01a7ce_decoded();
        return true;
      case 0x89:
        tick_ik1303_032951_decoded();
        return true;
      case 0x8a:
        tick_ik1303_1b94a6_decoded();
        return true;
      case 0x8b:
        tick_ik1303_23f02f_decoded();
        return true;
      case 0x8c:
        tick_ik1303_23282a_decoded();
        return true;
      case 0x8d:
        tick_ik1303_05d4a8_decoded();
        return true;
      case 0x8e:
        tick_ik1303_2067ce_decoded();
        return true;
      case 0x8f:
        tick_ik1303_0fe9cb_decoded();
        return true;
      case 0x93:
        tick_ik1303_1e4285_decoded();
        return true;
      case 0xa0:
        tick_ik1303_203047_decoded();
        return true;
      case 0xa1:
        tick_ik1303_029ba0_decoded();
        return true;
      case 0xa2:
        tick_ik1303_041c2f_decoded();
        return true;
      case 0xa5:
        tick_ik1303_058285_decoded();
        return true;
      case 0xa6:
        tick_ik1303_7b5d50_decoded();
        return true;
      case 0xa7:
        tick_ik1303_010285_decoded();
        return true;
      case 0xa8:
        tick_ik1303_0212a5_decoded();
        return true;
      case 0xa9:
        tick_ik1303_4207d0_decoded();
        return true;
      case 0xaa:
        tick_ik1303_6ce847_decoded();
        return true;
      case 0xab:
        tick_ik1303_035ecc_decoded();
        return true;
      case 0xac:
        tick_ik1303_060204_decoded();
        return true;
      case 0xad:
        tick_ik1303_701d50_decoded();
        return true;
      case 0xae:
        tick_ik1303_388221_decoded();
        return true;
      case 0xaf:
        tick_ik1303_0a3dd0_decoded();
        return true;
      case 0xb0:
        tick_ik1303_027fa6_decoded();
        return true;
      case 0xb1:
        tick_ik1303_04f0ae_decoded();
        return true;
      case 0xba:
        tick_ik1303_1cf7dc_decoded();
        return true;
      case 0xbb:
        tick_ik1303_29bae9_decoded();
        return true;
      case 0xbc:
        tick_ik1303_2b53a6_decoded();
        return true;
      case 0xbd:
        tick_ik1303_6fa822_decoded();
        return true;
      case 0xbe:
        tick_ik1303_02af04_decoded();
        return true;
      case 0xbf:
        tick_ik1303_05dd62_decoded();
        return true;
      case 0xe2:
        tick_ik1303_040735_decoded();
        return true;
      case 0xe3:
        tick_ik1303_0c5084_decoded();
        return true;
      case 0xe5:
        tick_ik1303_046d2f_decoded();
        return true;
      case 0xeb:
        tick_ik1303_691122_decoded();
        return true;
      case 0xf0:
        tick_ik1303_0b9020_decoded();
        return true;
      case 0xf1:
        tick_ik1303_482848_decoded();
        return true;
      case 0xf3:
        tick_ik1303_0b704c_decoded();
        return true;
      case 0xfb:
        tick_ik1303_43f7d0_decoded();
        return true;
      case 0xff:
        tick_ik1303_05e6d0_decoded();
        return true;
      default:
        return false;
    }
  }

  bool tick_ik1306_generated_command_dispatch(uint32_t decoded_command) {
    switch (decoded_command) {
      case 0x018040:
        tick_ik1306_018040_decoded();
        return true;
      case 0x01f52f:
        tick_ik1306_01f52f_decoded();
        return true;
      case 0x024000:
        tick_ik1306_024000_decoded();
        return true;
      case 0x02e600:
        tick_ik1306_02e600_decoded();
        return true;
      case 0x02e680:
        tick_ik1306_02e680_decoded();
        return true;
      default:
        return false;
    }
  }

  bool tick_ik1306_generated_address_dispatch() {
    if (tick_index != 0) return tick_ik1306_generated_command_dispatch(command);
    switch (command_address_for_trace()) {
      case 0x01:
        tick_ik1306_018040_decoded();
        return true;
      case 0x02:
        tick_ik1306_01f52f_decoded();
        return true;
      case 0x03:
        tick_ik1306_02e600_decoded();
        return true;
      case 0x04:
        tick_ik1306_024000_decoded();
        return true;
      case 0x05:
        tick_ik1306_02e680_decoded();
        return true;
      case 0x06:
        tick_ik1306_024000_decoded();
        return true;
      default:
        return false;
    }
  }

  void tick() {
    if (tick_index == 0) {
      const int command_address = r[36] + 16 * r[39];
      if (last_command_address >= 0) {
        used_command_edges[static_cast<std::size_t>(last_command_address)]
                          [static_cast<std::size_t>(command_address)] = true;
      }
      last_command_address = command_address;
      used_command_addresses[command_address] = true;
      command = rom->command[command_address];
      if (((command >> 16) & 0x3F) == 0) t = 0;
    }

    int nine = tick_index / 9;
    int tick_in_nine = tick_index - nine * 9;
    if (tick_in_nine == 0 && !(nine > 0 && nine < 3)) {
      if (nine < 3) {
        sync_address = command & 0x7F;
      } else if (nine == 3) {
        sync_address = (command >> 7) & 0x7F;
      } else if (nine == 4) {
        sync_address = (command >> 14) & 0xFF;
        if (sync_address > 31) {
          if (tick_index == 36) {
            r[37] = sync_address & 0xF;
            r[40] = sync_address >> 4;
          }
          sync_address = 95;
        }
      }
    }

    int sync_microcommand = rom->sync[sync_address * 9 + j[tick_index]] & 0x3F;
    used_sync_addresses[static_cast<std::size_t>(sync_address)] = true;
    if (sync_microcommand > 59) {
      sync_microcommand = (sync_microcommand - 60) * 2;
      if (l == 0) ++sync_microcommand;
      sync_microcommand += 60;
    }
    used_microcommands[static_cast<std::size_t>(sync_microcommand)] = true;

    uint32_t microcommand = rom->microcommand[sync_microcommand];
    std::array<uint8_t, 28> micro_orders{};
    for (int i = 0; i < 28; ++i) {
      micro_orders[i] = microcommand & 1;
      microcommand >>= 1;
    }

    int trio = tick_index / 3;
    Accumulator acc;
    if (micro_orders[25] == 1 && trio != key_x - 1) s1 |= key_y;

    for (int i = 0; i < 12; ++i) if (micro_orders[i]) execute_micro_order(i, acc);

    if (((command >> 16) & 0x3F) > 0) {
      if (key_y == 0) t = 0;
    } else {
      if (trio == key_x - 1 && key_y > 0) {
        s1 = key_y;
        t = 1;
      }
      commas[trio] = l > 0;
    }

    for (int i = 12; i < 15; ++i) if (micro_orders[i]) execute_micro_order(i, acc);

    int sum = acc.alpha + acc.beta + acc.gamma;
    acc.sigma = sum & 0xF;
    carry = (sum >> 4) & 1;

    if (((command >> 22) & 1) == 0 || nine == 4) {
      int field = (micro_orders[17] << 2) | (micro_orders[16] << 1) | micro_orders[15];
      if (field > 0) execute_micro_order(field + 14, acc);
      for (int i = 18; i < 20; ++i) if (micro_orders[i]) execute_micro_order(i + 4, acc);
    }

    for (int i = 20; i < 22; ++i) if (micro_orders[i]) execute_micro_order(i + 4, acc);

    for (int i = 0; i < 3; ++i) {
      int field = (micro_orders[23 + i * 2] << 1) | micro_orders[22 + i * 2];
      if (field > 0) execute_micro_order(field + 25 + i * 3, acc);
    }

    output = memory[tick_index];
    memory[tick_index] = input;
    ++tick_index;
    if (tick_index == 42) tick_index = 0;
  }

  void close_ring(uint8_t value) { memory[(tick_index + 41) % 42] = value; }
};

struct IR2 {
  std::array<uint8_t, 252> memory{};
  uint8_t input = 0;
  uint8_t output = 0;
  int tick_index = 0;

  void reset() {
    memory.fill(0);
    input = output = 0;
    tick_index = 0;
  }

  void tick() {
    output = memory[tick_index];
    memory[tick_index] = input;
    ++tick_index;
    if (tick_index == 252) tick_index = 0;
  }
};

struct Mk61TrigRunner {
  IK13 ik1302{kIk1302Rom};
  IK13 ik1303{kIk1303Rom};
  IK13 ik1306{kIk1306Rom};
  IR2 ir2a;
  IR2 ir2b;
  bool powered = false;
  uint8_t angle_mode = 10;
  bool trace = false;
  bool trace_raw = false;
  bool trace_commands = false;
  bool trace_micro = false;
  bool trace_active = true;
  bool trace_run_only = false;
  bool generated_address_dispatch = false;
  bool specialize_049a32 = false;
  bool specialize_04582e = false;
  bool specialize_049a33 = false;
  bool specialize_44ca8c = false;
  bool specialize_023e50 = false;
  bool specialize_0e1dbb = false;
  bool specialize_04f0ae = false;
  bool specialize_0458af = false;
  bool specialize_21f02e = false;
  bool specialize_2067ce = false;
  bool specialize_01a7ce = false;
  bool specialize_6e7a63 = false;
  bool specialize_04052e = false;
  bool specialize_6a95ea = false;
  bool specialize_01322b = false;
  bool specialize_6fa822 = false;
  bool specialize_44e80c = false;
  bool specialize_2b73ab = false;
  bool specialize_203047 = false;
  bool specialize_05dd62 = false;
  bool specialize_0331ab = false;
  bool specialize_02b0ae = false;
  bool specialize_02af04 = false;
  bool specialize_029ba0 = false;
  bool specialize_027fa6 = false;
  std::vector<uint32_t> specialize_commands;
  std::vector<uint32_t> specialize_ik1302_commands;
  std::vector<uint32_t> specialize_ik1306_commands;
  std::vector<uint32_t> trace_raw_commands;
  std::vector<int> trace_micro_addresses;
  std::vector<uint32_t> trace_micro_commands;
  uint64_t cycle = 0;
  std::string last_trace_x;
  std::string last_trace_r;

  static std::string format_number_ring(const std::array<uint8_t, 42>& memory) {
    constexpr int address = 34;
    int exponent = memory[address - 3] * 10 + memory[address - 6];
    if (memory[address] == 9) exponent = -(100 - exponent);
    int index = 0;
    while (memory[address - 33 + index * 3] == 0) {
      if (exponent == 7 - index || index == 7) break;
      ++index;
    }
    std::vector<uint8_t> digits;
    while (index < 8) {
      digits.push_back(memory[address - 33 + index * 3]);
      ++index;
    }
    std::string mantissa = memory[address - 9] == 9 ? "-" : "";
    bool comma = false;
    for (int i = static_cast<int>(digits.size()) - 1, out_index = 0; i >= 0; --i, ++out_index) {
      static constexpr char kDigits[] = "0123456789ABCDEF";
      mantissa.push_back(kDigits[digits[i] & 0xF]);
      if ((out_index == 0 && (exponent < 0 || exponent > 7)) || out_index == exponent) {
        mantissa.push_back(',');
        comma = true;
      }
    }
    if (!comma) mantissa.push_back(',');
    if (exponent < 0 || exponent > 7) {
      int abs_exponent = exponent < 0 ? -exponent : exponent;
      mantissa.push_back(exponent < 0 ? '-' : ' ');
      mantissa.push_back(static_cast<char>('0' + (abs_exponent / 10) % 10));
      mantissa.push_back(static_cast<char>('0' + abs_exponent % 10));
    }
    return mantissa;
  }

  std::string current_command_trace() const {
    const uint8_t op = static_cast<uint8_t>(ik1303.r[36] + 16 * ik1303.r[39]);
    const uint32_t command = kIk1303Rom.command[op];
    std::ostringstream out;
    out << "C" << cycle << " a=" << std::hex << std::setw(2) << std::setfill('0')
        << static_cast<int>(op) << " cmd=" << std::setw(6) << command << std::dec
        << std::setfill(' ') << " r=" << format_number_ring(ik1303.r)
        << " x=" << read_x();
    return out.str();
  }

  static bool trace_command(uint32_t command) {
    switch (command) {
      case 0x01322b:
      case 0x611e47:
      case 0x68e847:
      case 0x5708af:
      case 0x020234:
      case 0x0557a3:
      case 0x020285:
      case 0x243047:
      case 0x44ca8c:
        return true;
      default:
        return false;
    }
  }

  static std::string raw_ring(const std::array<uint8_t, 42>& ring) {
    static constexpr char kDigits[] = "0123456789ABCDEF";
    std::string out;
    out.reserve(42);
    for (uint8_t cell : ring)
      out.push_back(kDigits[cell & 0xF]);
    return out;
  }

  bool should_specialize_command(uint32_t command) const {
    for (uint32_t specialized : specialize_commands) {
      if (specialized == command) return true;
    }
    return false;
  }

  bool should_specialize_ik1302_command(uint32_t command) const {
    for (uint32_t specialized : specialize_ik1302_commands) {
      if (specialized == command) return true;
    }
    return false;
  }

  bool should_specialize_ik1306_command(uint32_t command) const {
    for (uint32_t specialized : specialize_ik1306_commands) {
      if (specialized == command) return true;
    }
    return false;
  }

  void tick() {
    ik1302.input = ir2b.output;
    if (generated_address_dispatch && ik1302.tick_ik1302_generated_address_dispatch()) {
    } else {
      const uint32_t ik1302_command_for_tick = ik1302.command_for_trace();
      if (should_specialize_ik1302_command(ik1302_command_for_tick) &&
          ik1302.tick_ik1302_generated_decoded(ik1302_command_for_tick)) {
      } else {
        ik1302.tick();
      }
    }
    ik1303.input = ik1302.output;
    if (trace_active && trace_micro) {
      const int trace_address = ik1303.command_address_for_trace();
      const uint32_t trace_command = ik1303.command_for_trace();
      bool trace_this_address = trace_micro_addresses.empty() && trace_micro_commands.empty();
      for (int address : trace_micro_addresses) {
        if (address == trace_address) {
          trace_this_address = true;
          break;
        }
      }
      for (uint32_t command : trace_micro_commands) {
        if (command == trace_command) {
          trace_this_address = true;
          break;
        }
      }
      if (trace_this_address) {
        const int trace_sync = ik1303.sync_address_for_trace(trace_command);
        const int trace_microcommand = ik1303.sync_microcommand_for_trace(trace_sync);
        const uint32_t trace_micro_word = ik1303.microcommand_word_for_trace(trace_microcommand);
        std::cout << "MICRO C" << cycle
                  << " t=" << std::setw(2) << std::setfill('0')
                  << static_cast<int>(ik1303.tick_index)
                  << " a=" << std::dec << trace_address
                  << " cmd=" << std::hex << std::setw(6) << std::setfill('0') << trace_command
                  << " sync=" << std::dec << trace_sync
                  << " mc=" << trace_microcommand
                  << " word=" << std::hex << std::setw(7) << trace_micro_word << std::dec
                  << std::setfill(' ')
                  << " orders=" << IK13::micro_order_list(trace_micro_word)
                  << " exec=" << IK13::micro_execute_order_list(
                         trace_micro_word, trace_command, ik1303.tick_index)
                  << " s=" << std::hex << static_cast<int>(ik1303.s)
                  << " s1=" << static_cast<int>(ik1303.s1)
                  << " l=" << static_cast<int>(ik1303.l)
                  << " carry=" << static_cast<int>(ik1303.carry) << std::dec
                  << " rraw=" << raw_ring(ik1303.r)
                  << " mraw=" << raw_ring(ik1303.memory)
                  << " sraw=" << raw_ring(ik1303.stack) << '\n';
      }
    }
    if (generated_address_dispatch && ik1303.tick_ik1303_generated_address_dispatch()) {
    } else {
      const uint32_t command_for_tick = ik1303.command_for_trace();
      if (should_specialize_command(command_for_tick) &&
          ik1303.tick_ik1303_generated_decoded(command_for_tick)) {
      } else if (specialize_049a32 && command_for_tick == 0x049a32) {
        ik1303.tick_ik1303_049a32_decoded();
      } else if (specialize_04582e && command_for_tick == 0x04582e) {
        ik1303.tick_ik1303_04582e_decoded();
      } else if (specialize_049a33 && command_for_tick == 0x049a33) {
        ik1303.tick_ik1303_049a33_decoded();
      } else if (specialize_44ca8c && command_for_tick == 0x44ca8c) {
        ik1303.tick_ik1303_44ca8c_decoded();
      } else if (specialize_023e50 && command_for_tick == 0x023e50) {
        ik1303.tick_ik1303_023e50_decoded();
      } else if (specialize_0e1dbb && command_for_tick == 0x0e1dbb) {
        ik1303.tick_ik1303_0e1dbb_decoded();
      } else if (specialize_04f0ae && command_for_tick == 0x04f0ae) {
        ik1303.tick_ik1303_04f0ae_decoded();
      } else if (specialize_0458af && command_for_tick == 0x0458af) {
        ik1303.tick_ik1303_0458af_decoded();
      } else if (specialize_21f02e && command_for_tick == 0x21f02e) {
        ik1303.tick_ik1303_21f02e_decoded();
      } else if (specialize_2067ce && command_for_tick == 0x2067ce) {
        ik1303.tick_ik1303_2067ce_decoded();
      } else if (specialize_01a7ce && command_for_tick == 0x01a7ce) {
        ik1303.tick_ik1303_01a7ce_decoded();
      } else if (specialize_6e7a63 && command_for_tick == 0x6e7a63) {
        ik1303.tick_ik1303_6e7a63_decoded();
      } else if (specialize_04052e && command_for_tick == 0x04052e) {
        ik1303.tick_ik1303_04052e_decoded();
      } else if (specialize_6a95ea && command_for_tick == 0x6a95ea) {
        ik1303.tick_ik1303_6a95ea_decoded();
      } else if (specialize_01322b && command_for_tick == 0x01322b) {
        ik1303.tick_ik1303_01322b_decoded();
      } else if (specialize_6fa822 && command_for_tick == 0x6fa822) {
        ik1303.tick_ik1303_6fa822_decoded();
      } else if (specialize_44e80c && command_for_tick == 0x44e80c) {
        ik1303.tick_ik1303_44e80c_decoded();
      } else if (specialize_2b73ab && command_for_tick == 0x2b73ab) {
        ik1303.tick_ik1303_2b73ab_decoded();
      } else if (specialize_203047 && command_for_tick == 0x203047) {
        ik1303.tick_ik1303_203047_decoded();
      } else if (specialize_05dd62 && command_for_tick == 0x05dd62) {
        ik1303.tick_ik1303_05dd62_decoded();
      } else if (specialize_0331ab && command_for_tick == 0x0331ab) {
        ik1303.tick_ik1303_0331ab_decoded();
      } else if (specialize_02b0ae && command_for_tick == 0x02b0ae) {
        ik1303.tick_ik1303_02b0ae_decoded();
      } else if (specialize_02af04 && command_for_tick == 0x02af04) {
        ik1303.tick_ik1303_02af04_decoded();
      } else if (specialize_029ba0 && command_for_tick == 0x029ba0) {
        ik1303.tick_ik1303_029ba0_decoded();
      } else if (specialize_027fa6 && command_for_tick == 0x027fa6) {
        ik1303.tick_ik1303_027fa6_decoded();
      } else {
        ik1303.tick();
      }
    }
    bool trace_requested_raw_command = false;
    for (uint32_t command : trace_raw_commands) {
      if (ik1303.command == command) {
        trace_requested_raw_command = true;
        break;
      }
    }
    const bool trace_full_raw_command =
        ik1303.command == 0x611e47 || ik1303.command == 0x04582e ||
        ik1303.command == 0x049a32 || ik1303.command == 0x049a33 ||
        ik1303.command == 0x0458af || trace_requested_raw_command;
    const bool trace_sparse_raw_command =
        ik1303.command == 0x01322b &&
        (ik1303.tick_index == 1 || ik1303.tick_index == 24 ||
         ik1303.tick_index == 27 || ik1303.tick_index == 32);
    if (trace_active && trace_raw && (trace_full_raw_command || trace_sparse_raw_command)) {
      std::cout << "RAW C" << cycle << " t=" << std::setw(2) << std::setfill('0')
                << static_cast<int>(ik1303.tick_index) << std::dec << std::setfill(' ')
                << " cmd=" << std::hex << std::setw(6) << std::setfill('0')
                << ik1303.command << std::dec << std::setfill(' ')
                << " rnum=" << format_number_ring(ik1303.r)
                << " x=" << read_x()
                << " s=" << std::hex << static_cast<int>(ik1303.s)
                << " s1=" << static_cast<int>(ik1303.s1)
                << " l=" << static_cast<int>(ik1303.l)
                << " tflag=" << static_cast<int>(ik1303.t)
                << " carry=" << static_cast<int>(ik1303.carry)
                << " sync=" << static_cast<int>(ik1303.sync_address) << std::dec
                << " rraw=" << raw_ring(ik1303.r)
                << " mraw=" << raw_ring(ik1303.memory)
                << " sraw=" << raw_ring(ik1303.stack) << '\n';
    }
    if (trace_active && trace_commands && ik1303.tick_index == 1) {
      const uint8_t op = static_cast<uint8_t>(ik1303.r[36] + 16 * ik1303.r[39]);
      std::cout << "CMD C" << cycle << " cmd=" << std::hex << std::setw(6)
                << std::setfill('0') << ik1303.command << std::dec << std::setfill(' ')
                << " a=" << static_cast<int>(op)
                << " rnum=" << format_number_ring(ik1303.r)
                << " x=" << read_x() << '\n';
    }
    if (trace_active && trace && trace_command(ik1303.command)) {
      const std::string x = read_x();
      const std::string r = format_number_ring(ik1303.r);
      if (x != last_trace_x || r != last_trace_r) {
        const uint8_t op = static_cast<uint8_t>(ik1303.r[36] + 16 * ik1303.r[39]);
        std::cout << "C" << cycle << " t=" << std::setw(2) << std::setfill('0')
                  << static_cast<int>(ik1303.tick_index) << " a=" << std::hex
                  << std::setw(2) << static_cast<int>(op) << " cmd=" << std::setw(6)
                  << ik1303.command << std::dec << std::setfill(' ')
                  << " r=" << r << " x=" << x << '\n';
        last_trace_x = x;
        last_trace_r = r;
      }
    }
    ik1306.input = ik1303.output;
    if (generated_address_dispatch && ik1306.tick_ik1306_generated_address_dispatch()) {
    } else {
      const uint32_t ik1306_command_for_tick = ik1306.command_for_trace();
      if (should_specialize_ik1306_command(ik1306_command_for_tick) &&
          ik1306.tick_ik1306_generated_decoded(ik1306_command_for_tick)) {
      } else {
        ik1306.tick();
      }
    }
    ir2a.input = ik1306.output;
    ir2a.tick();
    ir2b.input = ir2a.output;
    ir2b.tick();
    ik1302.close_ring(ir2b.output);
    ++cycle;
  }

  void frame(bool press_start = false) {
    ik1303.key_y = 1;
    ik1303.key_x = angle_mode;
    if (press_start) {
      ik1302.key_x = 2;
      ik1302.key_y = 9;
    }
    for (int i = 0; i < 560; ++i) {
      for (int j = 0; j < 42; ++j) tick();
    }
    ik1302.key_x = 0;
    ik1302.key_y = 0;
  }

  void power_on() {
    if (powered) return;
    powered = true;
    frame(false);
  }

  void sync_memory_phase(int target = 1) {
    for (int i = 0; i < 3; ++i) {
      if (ir2a.tick_index == target * 84) return;
      frame(false);
    }
    throw std::runtime_error("cannot sync memory phase");
  }

  void write_number_ik1303_x(const ParsedNumber& number) {
    write_number_x_ring(ik1303.memory, number);
  }

  static void write_number_x_ring(std::array<uint8_t, 42>& memory, const ParsedNumber& number) {
    constexpr int address = 34;
    memory[address] = number.exponent_negative ? 9 : 0;
    memory[address - 3] = number.exponent / 10;
    memory[address - 6] = number.exponent % 10;
    memory[address - 9] = number.mantissa_negative ? 9 : 0;
    for (int i = 0; i < 8; ++i) memory[address - 3 * (i + 4)] = number.mantissa[i];
  }

  void set_x(std::string_view literal) {
    power_on();
    sync_memory_phase(1);
    write_number_ik1303_x(parse_mk_literal(std::string(literal)));
  }

  void load_program(uint8_t function_code) {
    power_on();
    sync_memory_phase(1);
    ir2b.memory[209] = function_code / 16;
    ir2b.memory[206] = function_code % 16;
    ir2b.memory[173] = 0x50 / 16;
    ir2b.memory[170] = 0x50 % 16;
  }

  std::string read_x() const {
    return format_number_ring(ik1303.memory);
  }

  void run(uint8_t function_code, std::string_view literal) {
    const bool saved_trace_active = trace_active;
    const bool saved_specialize_049a32 = specialize_049a32;
    const bool saved_specialize_04582e = specialize_04582e;
    const bool saved_specialize_049a33 = specialize_049a33;
    const bool saved_specialize_44ca8c = specialize_44ca8c;
    const bool saved_specialize_023e50 = specialize_023e50;
    const bool saved_specialize_0e1dbb = specialize_0e1dbb;
    if (trace_run_only) trace_active = false;
    specialize_049a32 = false;
    specialize_04582e = false;
    specialize_049a33 = false;
    specialize_44ca8c = false;
    specialize_023e50 = false;
    specialize_0e1dbb = false;
    set_x(literal);
    load_program(function_code);
    clear_coverage();
    specialize_049a32 = saved_specialize_049a32;
    specialize_04582e = saved_specialize_04582e;
    specialize_049a33 = saved_specialize_049a33;
    specialize_44ca8c = saved_specialize_44ca8c;
    specialize_023e50 = saved_specialize_023e50;
    specialize_0e1dbb = saved_specialize_0e1dbb;
    if (trace_run_only) trace_active = saved_trace_active;
    frame(true);
    for (int i = 0; i < 6; ++i) frame(false);
  }

  void initialize_after_common_trig_prolog(uint8_t function_code, std::string_view literal) {
    const ParsedNumber number = parse_mk_literal(std::string(literal));
    const uint8_t marker = function_code & 0xF;

    ik1302.reset();
    ik1303.reset();
    ik1306.reset();
    ir2a.reset();
    ir2b.reset();
    powered = true;
    cycle = 0;

    write_number_x_ring(ik1303.memory, number);
    ik1306.memory = ik1303.memory;

    ik1302.r.fill(0);
    for (int i = 1; i < 42; ++i) ik1302.r[i - 1] |= ik1303.memory[i];
    ik1302.r[5] |= 0x8;
    ik1302.r[8] |= 0x8;
    ik1302.r[11] |= 0x8;
    ik1302.r[14] |= 0x8;
    ik1302.r[17] |= 0x8;
    ik1302.r[20] |= 0x8;
    ik1302.r[23] |= 0xF;
    ik1302.r[30] |= marker;
    ik1302.r[31] |= 0x1;
    ik1302.r[32] |= marker;
    ik1302.r[33] |= 0x1;
    ik1302.r[35] |= 0x1;
    ik1302.r[36] |= 0x2;
    ik1302.r[37] |= 0x3;
    ik1302.r[38] |= marker;
    ik1302.r[39] |= 0xA;
    ik1302.r[40] |= 0xD;
    ik1302.r[41] |= 0xF;
    ik1302.stack[1] = 0x8;
    ik1302.stack[4] = 0x8;
    ik1302.stack[7] = 0x8;
    ik1302.stack[10] = 0x8;
    ik1302.stack[13] = 0x8;
    ik1302.stack[16] = 0x8;
    ik1302.stack[19] = 0x8;
    ik1302.stack[27] = 0xF;
    ik1302.stack[30] = 0xF;
    ik1302.stack[33] = 0xF;
    ik1302.stack[36] = 0xA;
    ik1302.stack[37] = 0x8;
    ik1302.stack[38] = 0x2;
    ik1302.stack[39] = 0x9;
    ik1302.stack[41] = 0xA;
    ik1302.s = 0xB;
    ik1302.s1 = 0xB;
    ik1302.l = 1;
    ik1302.carry = 1;

    ik1303.r[23] = 0x1;
    ik1303.r[35] = 0xF;
    ik1303.r[36] = 0xE;
    ik1303.r[37] = 0xF;
    ik1303.r[38] = 0x4;
    ik1303.r[39] = 0x6;
    ik1303.r[40] = 0x5;
    ik1303.stack[27] = 0x1;
    ik1303.stack[28] = 0x1;
    ik1303.stack[29] = 0x1;
    ik1303.stack[36] = 0xA;
    ik1303.stack[37] = 0xA;
    ik1303.stack[38] = 0xA;
    ik1303.stack[39] = 0x3;
    ik1303.stack[40] = 0x3;
    ik1303.stack[41] = 0x3;
    ik1303.s = 0x3;
    ik1303.s1 = 0xE;
    ik1303.l = 1;
    ik1303.carry = 1;
    ik1303.key_y = 1;
    ik1303.key_x = angle_mode;

    ik1306.r[27] = 0xF;
    ik1306.r[28] = 0xF;
    ik1306.r[30] = 0xF;
    ik1306.r[31] = 0xF;
    ik1306.r[33] = 0xF;
    ik1306.r[34] = 0xF;
    ik1306.r[36] = 0x3;

    ir2a.memory[127] = 0xF;
    ir2a.memory[130] = 0xF;
    ir2a.memory[133] = 0xF;
    ir2a.memory[136] = 0xF;
    ir2a.memory[139] = 0xF;
    ir2a.memory[142] = 0xF;
    ir2a.memory[145] = 0xF;
    ir2a.memory[148] = 0xF;
    ir2a.memory[151] = 0xF;
    ir2a.memory[196] = 0xF;
    ir2a.memory[199] = 0xF;
    ir2a.memory[202] = 0xF;
    ir2a.tick_index = 210;
    ir2b.memory[47] = 0x5;
    ir2b.memory[80] = marker;
    ir2b.memory[83] = 0x1;
    ir2b.tick_index = 210;
  }

  void run_skip_common_trig_prolog(uint8_t function_code, std::string_view literal) {
    initialize_after_common_trig_prolog(function_code, literal);
    clear_coverage();
    for (int command = 675; command < 7 * 560; ++command) {
      for (int tick_number = 0; tick_number < 42; ++tick_number) tick();
    }
  }

  void initialize_after_common_trig_entry_prefix(uint8_t function_code, std::string_view literal) {
    const ParsedNumber number = parse_mk_literal(std::string(literal));
    std::array<uint8_t, 42> x_ring{};
    write_number_x_ring(x_ring, number);
    const uint8_t marker = function_code & 0xF;
    const uint8_t result_marker = static_cast<uint8_t>((marker + 1) & 0xF);

    ik1302.reset();
    ik1303.reset();
    ik1306.reset();
    ir2a.reset();
    ir2b.reset();
    powered = true;
    cycle = 0;

    for (int i = 1; i < 42; ++i) ik1302.r[i - 1] |= x_ring[i];
    ik1302.r[5] |= 0x8;
    ik1302.r[8] |= 0x8;
    ik1302.r[11] |= 0x8;
    ik1302.r[14] |= 0x8;
    ik1302.r[17] |= 0x8;
    ik1302.r[20] |= 0x8;
    ik1302.r[23] |= 0xF;
    ik1302.r[30] |= marker;
    ik1302.r[31] |= 0x1;
    ik1302.r[33] |= 0x1;
    ik1302.r[35] |= 0x1;
    ik1302.r[36] |= 0x1;
    ik1302.r[37] |= 0x2;
    ik1302.r[38] |= marker;
    ik1302.r[40] |= 0x5;
    ik1302.r[41] |= 0xF;
    ik1302.stack[1] = 0x8;
    ik1302.stack[4] = 0x8;
    ik1302.stack[7] = 0x8;
    ik1302.stack[10] = 0x8;
    ik1302.stack[13] = 0x8;
    ik1302.stack[16] = 0x8;
    ik1302.stack[19] = 0x8;
    ik1302.stack[27] = 0xF;
    ik1302.stack[30] = 0xF;
    ik1302.stack[33] = 0xF;
    ik1302.stack[36] = 0x7;
    ik1302.stack[37] = 0x5;
    ik1302.stack[38] = 0x4;
    ik1302.stack[39] = 0x6;
    ik1302.stack[40] = 0x6;
    ik1302.stack[41] = 0xD;
    ik1302.s = 0xB;
    ik1302.s1 = 0x9;
    ik1302.l = 1;
    ik1302.carry = 1;

    ik1303.r = x_ring;
    ik1303.r[30] |= 0xF;
    ik1303.r[32] |= 0x1;
    if (angle_mode == 10) {
      ik1303.r[36] = 0x1;
    } else if (angle_mode == 11) {
      ik1303.r[36] = 0xE;
    } else if (angle_mode == 12) {
      ik1303.r[36] = 0xB;
    } else {
      throw std::runtime_error("bad angle mode");
    }
    ik1303.r[37] |= 0xE;
    ik1303.r[38] |= 0x4;
    ik1303.r[40] |= 0x6;
    ik1303.r[41] |= result_marker;
    ik1303.stack[27] = 0x1;
    ik1303.stack[28] = 0x1;
    ik1303.stack[29] = 0x1;
    ik1303.stack[36] = 0xB;
    ik1303.stack[37] = 0xA;
    ik1303.stack[38] = 0xA;
    ik1303.stack[39] = 0xF;
    ik1303.stack[40] = 0x3;
    ik1303.stack[41] = 0x3;
    ik1303.s = 0x3;
    ik1303.s1 = 0x1;
    ik1303.carry = 1;
    ik1303.key_y = 1;
    ik1303.key_x = angle_mode;

    if (angle_mode != 10) {
      ik1306.r[27] = 0x1;
      ik1306.r[28] = 0x1;
      ir2b.memory[70] = 0x1;
    }
    ik1306.r[30] = marker;
    ik1306.r[31] = marker;
    ik1306.r[33] = 0x1;
    ik1306.r[34] = 0x1;
    ik1306.r[36] = 0x1;

    ir2a.memory[47] = 0x5;
    ir2a.memory[80] = marker;
    ir2a.memory[83] = 0x1;
    ir2a.tick_index = 168;

    for (int i = 0; i < 9; ++i) ir2b.memory[1 + i * 3] = 0xF;
    ir2b.memory[73] = marker;
    ir2b.memory[76] = 0x1;
    for (int i = 0; i < 42; ++i) {
      if (i + 84 < 252) ir2b.memory[i + 84] = x_ring[i];
      if (i + 126 < 252) ir2b.memory[i + 126] = x_ring[i];
    }
    ir2b.tick_index = 168;
  }

  void run_skip_common_trig_entry_prefix(uint8_t function_code, std::string_view literal) {
    initialize_after_common_trig_entry_prefix(function_code, literal);
    clear_coverage();
    for (int command = 675 + 23; command < 7 * 560; ++command) {
      for (int tick_number = 0; tick_number < 42; ++tick_number) tick();
    }
  }

  void clear_coverage() {
    ik1302.clear_coverage();
    ik1303.clear_coverage();
    ik1306.clear_coverage();
  }

  template <std::size_t N>
  static int used_count(const std::array<bool, N>& used) {
    int count = 0;
    for (bool value : used) {
      if (value) ++count;
    }
    return count;
  }

  static int used_edge_count(const std::array<std::array<bool, 256>, 256>& used) {
    int count = 0;
    for (const auto& row : used) {
      for (bool value : row) {
        if (value) ++count;
      }
    }
    return count;
  }

  template <std::size_t N>
  static std::string used_indices(const std::array<bool, N>& used) {
    std::ostringstream out;
    bool first = true;
    for (std::size_t i = 0; i < used.size(); ++i) {
      if (!used[i]) continue;
      if (!first) out << ' ';
      first = false;
      out << i;
    }
    return out.str();
  }

  static std::string used_command_words(const IK13& chip) {
    std::ostringstream out;
    bool first = true;
    for (std::size_t i = 0; i < chip.used_command_addresses.size(); ++i) {
      if (!chip.used_command_addresses[i]) continue;
      if (!first) out << ' ';
      first = false;
      out << std::hex << std::setw(2) << std::setfill('0') << i << ':'
          << std::setw(6) << chip.rom->command[i] << std::dec << std::setfill(' ');
    }
    return out.str();
  }

  static std::string used_command_edges(const IK13& chip) {
    std::ostringstream out;
    bool first = true;
    for (std::size_t from = 0; from < chip.used_command_edges.size(); ++from) {
      for (std::size_t to = 0; to < chip.used_command_edges[from].size(); ++to) {
        if (!chip.used_command_edges[from][to]) continue;
        if (!first) out << ' ';
        first = false;
        out << from << "->" << to;
      }
    }
    return out.str();
  }

  static void print_chip_coverage(std::ostream& out,
                                  std::string_view name,
                                  const IK13& chip,
                                  bool print_edges) {
    out << name << " command_addresses(" << used_count(chip.used_command_addresses)
        << "): " << used_indices(chip.used_command_addresses) << '\n';
    out << name << " command_words(" << used_count(chip.used_command_addresses)
        << "): " << used_command_words(chip) << '\n';
    out << name << " command_edges(" << used_edge_count(chip.used_command_edges) << ")";
    if (print_edges) out << ": " << used_command_edges(chip);
    out << '\n';
    out << name << " sync_addresses(" << used_count(chip.used_sync_addresses)
        << "): " << used_indices(chip.used_sync_addresses) << '\n';
    out << name << " microcommands(" << used_count(chip.used_microcommands)
        << "): " << used_indices(chip.used_microcommands) << '\n';
  }

  void print_coverage(std::ostream& out, bool print_edges = false) const {
    print_chip_coverage(out, "IK1302", ik1302, print_edges);
    print_chip_coverage(out, "IK1303", ik1303, print_edges);
    print_chip_coverage(out, "IK1306", ik1306, print_edges);
  }
};

std::string mk61_trig_exact_display(uint8_t angle_mode_code,
                                    uint8_t function_code,
                                    std::string_view literal) {
  Mk61TrigRunner runner;
  runner.angle_mode = angle_mode_code;
  runner.run(function_code, literal);
  runner.sync_memory_phase(1);
  return runner.read_x();
}

std::string mk61_trig_skip_common_prolog_display(uint8_t angle_mode_code,
                                                 uint8_t function_code,
                                                 std::string_view literal) {
  Mk61TrigRunner runner;
  runner.angle_mode = angle_mode_code;
  runner.run_skip_common_trig_prolog(function_code, literal);
  runner.sync_memory_phase(1);
  return runner.read_x();
}

std::string mk61_trig_skip_common_entry_prefix_display(uint8_t angle_mode_code,
                                                       uint8_t function_code,
                                                       std::string_view literal) {
  Mk61TrigRunner runner;
  runner.angle_mode = angle_mode_code;
  runner.run_skip_common_trig_entry_prefix(function_code, literal);
  runner.sync_memory_phase(1);
  return runner.read_x();
}

std::string mk61_sin_exact_display(uint8_t angle_mode_code, std::string_view literal) {
  return mk61_trig_exact_display(angle_mode_code, 0x1c, literal);
}

std::string mk61_cos_exact_display(uint8_t angle_mode_code, std::string_view literal) {
  return mk61_trig_exact_display(angle_mode_code, 0x1d, literal);
}

std::string mk61_tg_exact_display(uint8_t angle_mode_code, std::string_view literal) {
  return mk61_trig_exact_display(angle_mode_code, 0x1e, literal);
}

uint8_t function_code(std::string_view name) {
  if (name == "sin" || name == "--sin") return 0x1c;
  if (name == "cos" || name == "--cos") return 0x1d;
  if (name == "tg" || name == "tan" || name == "--tg" || name == "--tan") return 0x1e;
  throw std::runtime_error("function must be sin, cos, or tg");
}

uint8_t angle_mode(std::string_view name) {
  if (name == "rad" || name == "--rad") return 10;
  if (name == "deg" || name == "--deg") return 11;
  if (name == "grad" || name == "grd" || name == "--grad" || name == "--grd") return 12;
  throw std::runtime_error("angle mode must be rad, deg, or grad");
}

void usage(const char* argv0) {
  std::cerr << "Usage: " << argv0
            << " [--trace] [--trace-raw] [--trace-raw-cmd HEX]"
            << " [--trace-run-commands]"
            << " [--trace-micro-address N] [--trace-micro-cmd HEX]"
            << " [--dump-specialization HEX] [--dump-chip-specialization CHIP HEX]"
            << " [--generated-address-dispatch]"
            << " [--specialize-command HEX]"
            << " [--specialize-ik1302-command HEX] [--specialize-ik1306-command HEX]"
            << " [--coverage] [--coverage-edges]"
            << " [--skip-common-prolog] [--skip-common-entry-prefix]"
            << " [--specialize-049a32] [--specialize-04582e] [--specialize-049a33]"
            << " [--specialize-44ca8c]"
            << " [--specialize-023e50]"
            << " [--specialize-0e1dbb]"
            << " [--specialize-04f0ae]"
            << " [--specialize-0458af]"
            << " [--specialize-21f02e]"
            << " [--specialize-2067ce]"
            << " [--specialize-01a7ce]"
            << " [--specialize-6e7a63]"
            << " [--specialize-04052e]"
            << " [--specialize-6a95ea]"
            << " [--specialize-01322b]"
            << " [--specialize-6fa822]"
            << " [--specialize-44e80c]"
            << " [--specialize-2b73ab]"
            << " [--specialize-203047]"
            << " [--specialize-05dd62]"
            << " [--specialize-0331ab]"
            << " [--specialize-02b0ae]"
            << " [--specialize-02af04]"
            << " [--specialize-029ba0]"
            << " [--specialize-027fa6]"
            << " [--rad|--deg|--grad] [--sin|--cos|--tg] <x>\n";
}

uint32_t parse_hex_command(std::string text) {
  if (text.starts_with("0x") || text.starts_with("0X")) text.erase(0, 2);
  std::size_t consumed = 0;
  const auto value = static_cast<uint32_t>(std::stoul(text, &consumed, 16));
  if (consumed != text.size()) throw std::runtime_error("bad hex command: " + text);
  return value;
}

std::string command_suffix(uint32_t command) {
  std::ostringstream out;
  out << std::hex << std::setw(6) << std::setfill('0') << std::nouppercase << command;
  return out.str();
}

int j_for_tick(int tick) {
  return tick < 6 ? tick : tick < 21 ? tick % 3 + 3 : tick % 9;
}

int command_sync_for_tick(uint32_t command, int tick) {
  if (tick < 27) return command & 0x7F;
  if (tick < 36) return (command >> 7) & 0x7F;
  int sync = (command >> 14) & 0xFF;
  return sync > 31 ? 95 : sync;
}

std::vector<int> execute_orders_vector(uint32_t microcommand_word,
                                       uint32_t command,
                                       int tick) {
  std::array<uint8_t, 28> micro_orders{};
  for (int i = 0; i < 28; ++i) {
    micro_orders[i] = microcommand_word & 1;
    microcommand_word >>= 1;
  }

  std::vector<int> out;
  for (int i = 0; i < 12; ++i) {
    if (micro_orders[i]) out.push_back(i);
  }
  for (int i = 12; i < 15; ++i) {
    if (micro_orders[i]) out.push_back(i);
  }

  const int nine = tick / 9;
  if (((command >> 22) & 1) == 0 || nine == 4) {
    int field = (micro_orders[17] << 2) | (micro_orders[16] << 1) | micro_orders[15];
    if (field > 0) out.push_back(field + 14);
    for (int i = 18; i < 20; ++i) {
      if (micro_orders[i]) out.push_back(i + 4);
    }
  }

  for (int i = 20; i < 22; ++i) {
    if (micro_orders[i]) out.push_back(i + 4);
  }

  for (int i = 0; i < 3; ++i) {
    int field = (micro_orders[23 + i * 2] << 1) | micro_orders[22 + i * 2];
    if (field > 0) out.push_back(field + 25 + i * 3);
  }
  return out;
}

std::vector<int> execute_orders_for_l(const Rom& rom,
                                      uint32_t command,
                                      int tick,
                                      bool branch_l) {
  const int sync = command_sync_for_tick(command, tick);
  int microcommand = rom.sync[sync * 9 + j_for_tick(tick)] & 0x3F;
  if (microcommand > 59) {
    microcommand = (microcommand - 60) * 2;
    if (!branch_l) ++microcommand;
    microcommand += 60;
  }
  return execute_orders_vector(rom.microcommand[microcommand], command, tick);
}

bool key_micro_order_25_for_l(const Rom& rom,
                              uint32_t command,
                              int tick,
                              bool branch_l) {
  const int sync = command_sync_for_tick(command, tick);
  int microcommand = rom.sync[sync * 9 + j_for_tick(tick)] & 0x3F;
  if (microcommand > 59) {
    microcommand = (microcommand - 60) * 2;
    if (!branch_l) ++microcommand;
    microcommand += 60;
  }
  return ((rom.microcommand[microcommand] >> 25) & 1) != 0;
}

std::vector<int> order_phase(const std::vector<int>& orders, int phase) {
  std::vector<int> out;
  for (int order : orders) {
    if ((phase == 0 && order < 12) ||
        (phase == 1 && order >= 12 && order < 15) ||
        (phase == 2 && order >= 15)) {
      out.push_back(order);
    }
  }
  return out;
}

std::string order_initializer(const std::vector<int>& orders) {
  std::ostringstream out;
  out << "{";
  for (std::size_t i = 0; i < orders.size(); ++i) {
    if (i != 0) out << ", ";
    out << orders[i];
  }
  out << "}";
  return out.str();
}

void dump_order_call(const std::vector<int>& orders, const std::string& indent) {
  if (!orders.empty()) {
    std::cout << indent << "execute_order_list(" << order_initializer(orders) << ", acc);\n";
  }
}

void dump_phase_function(const Rom& rom,
                         uint32_t command,
                         const std::string& suffix,
                         const std::string& phase_name,
                         int phase) {
  std::cout << "  void execute_" << suffix << "_" << phase_name
            << "_orders(int tick, bool branch_l, Accumulator& acc) {\n"
            << "    switch (tick) {\n";
  for (int tick = 0; tick < 42; ++tick) {
    const std::vector<int> if_l0 =
        order_phase(execute_orders_for_l(rom, command, tick, false), phase);
    const std::vector<int> if_l1 =
        order_phase(execute_orders_for_l(rom, command, tick, true), phase);
    if (if_l0.empty() && if_l1.empty()) continue;

    std::cout << "      case " << tick << ":\n";
    if (if_l0 == if_l1) {
      dump_order_call(if_l0, "        ");
    } else if (if_l0.empty()) {
      std::cout << "        if (branch_l) {\n";
      dump_order_call(if_l1, "          ");
      std::cout << "        }\n";
    } else if (if_l1.empty()) {
      std::cout << "        if (!branch_l) {\n";
      dump_order_call(if_l0, "          ");
      std::cout << "        }\n";
    } else {
      std::cout << "        if (branch_l) {\n";
      dump_order_call(if_l1, "          ");
      std::cout << "        } else {\n";
      dump_order_call(if_l0, "          ");
      std::cout << "        }\n";
    }
    std::cout << "        break;\n";
  }
  std::cout << "      default:\n"
            << "        break;\n"
            << "    }\n"
            << "    (void)branch_l;\n"
            << "  }\n\n";
}

void dump_key_micro_order_25_block(const Rom& rom, uint32_t command) {
  struct TickKey {
    int tick = 0;
    bool if_l0 = false;
    bool if_l1 = false;
  };
  std::vector<TickKey> ticks;
  for (int tick = 0; tick < 42; ++tick) {
    const bool if_l0 = key_micro_order_25_for_l(rom, command, tick, false);
    const bool if_l1 = key_micro_order_25_for_l(rom, command, tick, true);
    if (if_l0 || if_l1) ticks.push_back(TickKey{tick, if_l0, if_l1});
  }
  if (ticks.empty()) return;

  std::cout << "    if (key_y > 0 && current_tick / 3 != key_x - 1) {\n"
            << "      switch (current_tick) {\n";
  for (const TickKey& tick_key : ticks) {
    std::cout << "        case " << tick_key.tick << ":\n";
    if (tick_key.if_l0 && tick_key.if_l1) {
      std::cout << "          s1 |= key_y;\n";
    } else if (tick_key.if_l1) {
      std::cout << "          if (branch_l) s1 |= key_y;\n";
    } else {
      std::cout << "          if (!branch_l) s1 |= key_y;\n";
    }
    std::cout << "          break;\n";
  }
  std::cout << "        default:\n"
            << "          break;\n"
            << "      }\n"
            << "    }\n\n";
}

const Rom& rom_for_chip_name(const std::string& chip_name) {
  if (chip_name == "ik1302" || chip_name == "IK1302" || chip_name == "1302") return kIk1302Rom;
  if (chip_name == "ik1303" || chip_name == "IK1303" || chip_name == "1303") return kIk1303Rom;
  if (chip_name == "ik1306" || chip_name == "IK1306" || chip_name == "1306") return kIk1306Rom;
  throw std::runtime_error("chip must be ik1302, ik1303, or ik1306");
}

std::string chip_prefix_for_name(const std::string& chip_name) {
  if (chip_name == "ik1302" || chip_name == "IK1302" || chip_name == "1302") return "ik1302";
  if (chip_name == "ik1303" || chip_name == "IK1303" || chip_name == "1303") return "ik1303";
  if (chip_name == "ik1306" || chip_name == "IK1306" || chip_name == "1306") return "ik1306";
  throw std::runtime_error("chip must be ik1302, ik1303, or ik1306");
}

void dump_chip_specialization(const std::string& chip_prefix, const Rom& rom, uint32_t command) {
  const std::string command_id = command_suffix(command);
  const std::string symbol_suffix =
      chip_prefix == "ik1303" ? command_id : chip_prefix + "_" + command_id;
  dump_phase_function(rom, command, symbol_suffix, "pre_add", 0);
  dump_phase_function(rom, command, symbol_suffix, "gamma", 1);
  dump_phase_function(rom, command, symbol_suffix, "post_add", 2);

  const int sync0 = command_sync_for_tick(command, 0);
  const int sync1 = command_sync_for_tick(command, 27);
  const int raw_sync2 = (command >> 14) & 0xFF;
  const int sync2 = command_sync_for_tick(command, 36);
  std::cout << "  void tick_" << chip_prefix << "_" << command_id << "_decoded() {\n"
            << "    static constexpr uint32_t kCommand = 0x" << command_id << ";\n"
            << "    if (tick_index == 0) command = kCommand;\n\n"
            << "    const int current_tick = tick_index;\n"
            << "    const bool branch_l = l != 0;\n"
            << "    if (current_tick < 27) {\n"
            << "      sync_address = " << sync0 << ";\n"
            << "    } else if (current_tick < 36) {\n"
            << "      sync_address = " << sync1 << ";\n"
            << "    } else {\n";
  if (raw_sync2 > 31) {
    std::cout << "      if (current_tick == 36) {\n"
              << "        r[37] = " << (raw_sync2 & 0xF) << " & 0xF;\n"
              << "        r[40] = " << raw_sync2 << " >> 4;\n"
              << "      }\n";
  }
  std::cout << "      sync_address = " << sync2 << ";\n"
            << "    }\n\n";
  dump_key_micro_order_25_block(rom, command);
  std::cout << "    Accumulator acc;\n"
            << "    execute_" << symbol_suffix << "_pre_add_orders(current_tick, branch_l, acc);\n\n"
            << "    if (((kCommand >> 16) & 0x3F) > 0) {\n"
            << "      if (key_y == 0) t = 0;\n"
            << "    } else {\n"
            << "      const int trio = current_tick / 3;\n"
            << "      if (trio == key_x - 1 && key_y > 0) {\n"
            << "        s1 = key_y;\n"
            << "        t = 1;\n"
            << "      }\n"
            << "    }\n\n"
            << "    execute_" << symbol_suffix << "_gamma_orders(current_tick, branch_l, acc);\n\n"
            << "    int sum = acc.alpha + acc.beta + acc.gamma;\n"
            << "    acc.sigma = sum & 0xF;\n"
            << "    carry = (sum >> 4) & 1;\n\n"
            << "    execute_" << symbol_suffix << "_post_add_orders(current_tick, branch_l, acc);\n\n"
            << "    output = memory[current_tick];\n"
            << "    memory[current_tick] = input;\n"
            << "    ++tick_index;\n"
            << "    if (tick_index == 42) tick_index = 0;\n"
            << "  }\n";
}

void dump_ik1303_specialization(uint32_t command) {
  dump_chip_specialization("ik1303", kIk1303Rom, command);
}

}  // namespace

int main(int argc, char** argv) {
  try {
    std::string mode = "rad";
    std::string fn = "sin";
    std::string value;
    bool trace = false;
    bool trace_raw = false;
    bool trace_commands = false;
    bool trace_micro = false;
    bool trace_run_only = false;
    bool coverage = false;
    bool coverage_edges = false;
    bool generated_address_dispatch = false;
    bool dump_specialization = false;
    std::string dump_specialization_chip = "ik1303";
    uint32_t dump_specialization_command = 0;
    std::vector<uint32_t> specialize_commands;
    std::vector<uint32_t> specialize_ik1302_commands;
    std::vector<uint32_t> specialize_ik1306_commands;
    bool skip_common_prolog = false;
    bool skip_common_entry_prefix = false;
    bool specialize_049a32 = false;
    bool specialize_04582e = false;
    bool specialize_049a33 = false;
    bool specialize_44ca8c = false;
    bool specialize_023e50 = false;
    bool specialize_0e1dbb = false;
    bool specialize_04f0ae = false;
    bool specialize_0458af = false;
    bool specialize_21f02e = false;
    bool specialize_2067ce = false;
    bool specialize_01a7ce = false;
    bool specialize_6e7a63 = false;
    bool specialize_04052e = false;
    bool specialize_6a95ea = false;
    bool specialize_01322b = false;
    bool specialize_6fa822 = false;
    bool specialize_44e80c = false;
    bool specialize_2b73ab = false;
    bool specialize_203047 = false;
    bool specialize_05dd62 = false;
    bool specialize_0331ab = false;
    bool specialize_02b0ae = false;
    bool specialize_02af04 = false;
    bool specialize_029ba0 = false;
    bool specialize_027fa6 = false;
    std::vector<uint32_t> trace_raw_commands;
    std::vector<int> trace_micro_addresses;
    std::vector<uint32_t> trace_micro_commands;
    for (int i = 1; i < argc; ++i) {
      std::string arg = argv[i];
      if (arg == "--trace") trace = true;
      else if (arg == "--trace-raw") trace_raw = true;
      else if (arg == "--trace-raw-cmd") {
        if (++i >= argc) throw std::runtime_error("--trace-raw-cmd requires HEX");
        trace_raw = true;
        trace_raw_commands.push_back(parse_hex_command(argv[i]));
      } else if (arg.starts_with("--trace-raw-cmd=")) {
        trace_raw = true;
        trace_raw_commands.push_back(parse_hex_command(arg.substr(std::string("--trace-raw-cmd=").size())));
      }
      else if (arg == "--trace-commands") trace_commands = true;
      else if (arg == "--trace-run-commands") {
        trace_commands = true;
        trace_run_only = true;
      }
      else if (arg == "--trace-micro-address") {
        if (++i >= argc) throw std::runtime_error("--trace-micro-address requires address");
        trace_micro = true;
        trace_micro_addresses.push_back(std::stoi(argv[i], nullptr, 0));
      } else if (arg.starts_with("--trace-micro-address=")) {
        trace_micro = true;
        trace_micro_addresses.push_back(
            std::stoi(arg.substr(std::string("--trace-micro-address=").size()), nullptr, 0));
      }
      else if (arg == "--trace-micro-cmd") {
        if (++i >= argc) throw std::runtime_error("--trace-micro-cmd requires HEX");
        trace_micro = true;
        trace_micro_commands.push_back(parse_hex_command(argv[i]));
      } else if (arg.starts_with("--trace-micro-cmd=")) {
        trace_micro = true;
        trace_micro_commands.push_back(parse_hex_command(
            arg.substr(std::string("--trace-micro-cmd=").size())));
      }
      else if (arg == "--coverage") coverage = true;
      else if (arg == "--coverage-edges") {
        coverage = true;
        coverage_edges = true;
      }
      else if (arg == "--generated-address-dispatch") generated_address_dispatch = true;
      else if (arg == "--dump-specialization") {
        if (++i >= argc) throw std::runtime_error("--dump-specialization requires HEX");
        dump_specialization = true;
        dump_specialization_command = parse_hex_command(argv[i]);
      } else if (arg.starts_with("--dump-specialization=")) {
        dump_specialization = true;
        dump_specialization_command = parse_hex_command(
            arg.substr(std::string("--dump-specialization=").size()));
      }
      else if (arg == "--dump-chip-specialization") {
        if (i + 2 >= argc) throw std::runtime_error("--dump-chip-specialization requires CHIP HEX");
        dump_specialization = true;
        dump_specialization_chip = argv[++i];
        dump_specialization_command = parse_hex_command(argv[++i]);
      }
      else if (arg == "--specialize-command") {
        if (++i >= argc) throw std::runtime_error("--specialize-command requires HEX");
        specialize_commands.push_back(parse_hex_command(argv[i]));
      } else if (arg.starts_with("--specialize-command=")) {
        specialize_commands.push_back(parse_hex_command(
            arg.substr(std::string("--specialize-command=").size())));
      }
      else if (arg == "--specialize-ik1302-command") {
        if (++i >= argc) throw std::runtime_error("--specialize-ik1302-command requires HEX");
        specialize_ik1302_commands.push_back(parse_hex_command(argv[i]));
      } else if (arg.starts_with("--specialize-ik1302-command=")) {
        specialize_ik1302_commands.push_back(parse_hex_command(
            arg.substr(std::string("--specialize-ik1302-command=").size())));
      }
      else if (arg == "--specialize-ik1306-command") {
        if (++i >= argc) throw std::runtime_error("--specialize-ik1306-command requires HEX");
        specialize_ik1306_commands.push_back(parse_hex_command(argv[i]));
      } else if (arg.starts_with("--specialize-ik1306-command=")) {
        specialize_ik1306_commands.push_back(parse_hex_command(
            arg.substr(std::string("--specialize-ik1306-command=").size())));
      }
      else if (arg == "--skip-common-prolog") skip_common_prolog = true;
      else if (arg == "--skip-common-entry-prefix") skip_common_entry_prefix = true;
      else if (arg == "--specialize-049a32") specialize_049a32 = true;
      else if (arg == "--specialize-04582e") specialize_04582e = true;
      else if (arg == "--specialize-049a33") specialize_049a33 = true;
      else if (arg == "--specialize-44ca8c") specialize_44ca8c = true;
      else if (arg == "--specialize-023e50") specialize_023e50 = true;
      else if (arg == "--specialize-0e1dbb") specialize_0e1dbb = true;
      else if (arg == "--specialize-04f0ae") specialize_04f0ae = true;
      else if (arg == "--specialize-0458af") specialize_0458af = true;
      else if (arg == "--specialize-21f02e") specialize_21f02e = true;
      else if (arg == "--specialize-2067ce") specialize_2067ce = true;
      else if (arg == "--specialize-01a7ce") specialize_01a7ce = true;
      else if (arg == "--specialize-6e7a63") specialize_6e7a63 = true;
      else if (arg == "--specialize-04052e") specialize_04052e = true;
      else if (arg == "--specialize-6a95ea") specialize_6a95ea = true;
      else if (arg == "--specialize-01322b") specialize_01322b = true;
      else if (arg == "--specialize-6fa822") specialize_6fa822 = true;
      else if (arg == "--specialize-44e80c") specialize_44e80c = true;
      else if (arg == "--specialize-2b73ab") specialize_2b73ab = true;
      else if (arg == "--specialize-203047") specialize_203047 = true;
      else if (arg == "--specialize-05dd62") specialize_05dd62 = true;
      else if (arg == "--specialize-0331ab") specialize_0331ab = true;
      else if (arg == "--specialize-02b0ae") specialize_02b0ae = true;
      else if (arg == "--specialize-02af04") specialize_02af04 = true;
      else if (arg == "--specialize-029ba0") specialize_029ba0 = true;
      else if (arg == "--specialize-027fa6") specialize_027fa6 = true;
      else if (arg == "--rad" || arg == "--deg" || arg == "--grad" || arg == "--grd") mode = arg;
      else if (arg == "--sin" || arg == "--cos" || arg == "--tg" || arg == "--tan" || arg == "sin" || arg == "cos" || arg == "tg" || arg == "tan") fn = arg;
      else if (value.empty()) value = arg;
      else throw std::runtime_error("too many arguments");
    }
    if (dump_specialization) {
      dump_chip_specialization(chip_prefix_for_name(dump_specialization_chip),
                               rom_for_chip_name(dump_specialization_chip),
                               dump_specialization_command);
      return 0;
    }
    if (value.empty()) {
      usage(argv[0]);
      return 2;
    }
    if (!trace && !trace_raw && !trace_commands && !trace_micro && !coverage &&
        !generated_address_dispatch &&
        specialize_commands.empty() && specialize_ik1302_commands.empty() &&
        specialize_ik1306_commands.empty() &&
        !specialize_049a32 && !specialize_04582e && !specialize_049a33 &&
        !specialize_44ca8c && !specialize_023e50 && !specialize_0e1dbb &&
        !specialize_04f0ae && !specialize_0458af && !specialize_21f02e &&
        !specialize_2067ce && !specialize_01a7ce && !specialize_6e7a63 &&
        !specialize_04052e && !specialize_6a95ea && !specialize_01322b &&
        !specialize_6fa822 && !specialize_44e80c && !specialize_2b73ab &&
        !specialize_203047 && !specialize_05dd62 && !specialize_0331ab &&
        !specialize_02b0ae && !specialize_02af04 && !specialize_029ba0 &&
        !specialize_027fa6) {
      if (skip_common_prolog) {
        std::cout << mk61_trig_skip_common_prolog_display(angle_mode(mode), function_code(fn), value) << '\n';
      } else if (skip_common_entry_prefix) {
        std::cout << mk61_trig_skip_common_entry_prefix_display(angle_mode(mode), function_code(fn), value) << '\n';
      } else {
        std::cout << mk61_trig_exact_display(angle_mode(mode), function_code(fn), value) << '\n';
      }
      return 0;
    }
    Mk61TrigRunner runner;
    runner.angle_mode = angle_mode(mode);
    runner.trace = trace;
    runner.trace_raw = trace_raw;
    runner.trace_commands = trace_commands;
    runner.trace_micro = trace_micro;
    runner.trace_run_only = trace_run_only;
    runner.generated_address_dispatch = generated_address_dispatch;
    runner.specialize_049a32 = specialize_049a32;
    runner.specialize_04582e = specialize_04582e;
    runner.specialize_049a33 = specialize_049a33;
    runner.specialize_44ca8c = specialize_44ca8c;
    runner.specialize_023e50 = specialize_023e50;
    runner.specialize_0e1dbb = specialize_0e1dbb;
    runner.specialize_04f0ae = specialize_04f0ae;
    runner.specialize_0458af = specialize_0458af;
    runner.specialize_21f02e = specialize_21f02e;
    runner.specialize_2067ce = specialize_2067ce;
    runner.specialize_01a7ce = specialize_01a7ce;
    runner.specialize_6e7a63 = specialize_6e7a63;
    runner.specialize_04052e = specialize_04052e;
    runner.specialize_6a95ea = specialize_6a95ea;
    runner.specialize_01322b = specialize_01322b;
    runner.specialize_6fa822 = specialize_6fa822;
    runner.specialize_44e80c = specialize_44e80c;
    runner.specialize_2b73ab = specialize_2b73ab;
    runner.specialize_203047 = specialize_203047;
    runner.specialize_05dd62 = specialize_05dd62;
    runner.specialize_0331ab = specialize_0331ab;
    runner.specialize_02b0ae = specialize_02b0ae;
    runner.specialize_02af04 = specialize_02af04;
    runner.specialize_029ba0 = specialize_029ba0;
    runner.specialize_027fa6 = specialize_027fa6;
    runner.specialize_commands = std::move(specialize_commands);
    runner.specialize_ik1302_commands = std::move(specialize_ik1302_commands);
    runner.specialize_ik1306_commands = std::move(specialize_ik1306_commands);
    runner.trace_raw_commands = std::move(trace_raw_commands);
    runner.trace_micro_addresses = std::move(trace_micro_addresses);
    runner.trace_micro_commands = std::move(trace_micro_commands);
    if (skip_common_entry_prefix) {
      runner.run_skip_common_trig_entry_prefix(function_code(fn), value);
    } else if (skip_common_prolog) {
      runner.run_skip_common_trig_prolog(function_code(fn), value);
    } else {
      runner.run(function_code(fn), value);
    }
    runner.sync_memory_phase(1);
    std::cout << runner.read_x() << '\n';
    if (coverage) runner.print_coverage(std::cout, coverage_edges);
    return 0;
  } catch (const std::exception& e) {
    std::cerr << "error: " << e.what() << '\n';
    return 1;
  }
}
