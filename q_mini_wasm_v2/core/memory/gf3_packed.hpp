/**
 * GF(3) Dense Bit Packing for WASM Linear Memory
 * 
 * 4 qutrits per 8-bit byte: 3^4 = 81 < 256
 * Zero overhead memory representation
 * Strict Galois Field arithmetic only
 * 
 * Memory footprint reduction: 75%
 * Cache utilization improvement: 4x
 */

#ifndef GF3_PACKED_HPP
#define GF3_PACKED_HPP

#include <stdint.h>
#include <stdlib.h>

// GF(3) field operations - strictly modulo 3
#define GF3_ADD(a, b) ((a + b) % 3)
#define GF3_MUL(a, b) ((a * b) % 3)
#define GF3_SUB(a, b) ((a - b + 3) % 3)

// Packing lookup tables - precomputed at compile time
static const uint8_t gf3_pow4[4] = { 1, 3, 9, 27 };

/**
 * Pack 4 individual qutrits into a single 8-bit byte
 * Values must be in {0, 1, 2}
 */
static inline uint8_t gf3_pack_4(const uint8_t t0, const uint8_t t1, const uint8_t t2, const uint8_t t3) {
    return t0 +
           t1 * gf3_pow4[1] +
           t2 * gf3_pow4[2] +
           t3 * gf3_pow4[3];
}

/**
 * Unpack a single qutrit at position 0-3 from packed byte
 */
static inline uint8_t gf3_unpack(const uint8_t packed, const uint8_t position) {
    return (packed / gf3_pow4[position]) % 3;
}

/**
 * GF(3) Memory Arena
 * Optimized for WASM 64KB page alignment
 * 4 qutrits per byte storage
 */
typedef struct {
    uint8_t* data;
    size_t capacity_bytes;
    size_t qutrit_count;
} GF3Arena;

/**
 * Initialize packed GF(3) memory arena
 */
GF3Arena* gf3_arena_init(size_t qutrit_count);

/**
 * Free packed memory arena
 */
void gf3_arena_free(GF3Arena* arena);

/**
 * Read single qutrit from packed memory
 */
uint8_t gf3_arena_get(const GF3Arena* arena, size_t index);

/**
 * Write single qutrit to packed memory
 */
void gf3_arena_set(GF3Arena* arena, size_t index, uint8_t value);

#endif // GF3_PACKED_HPP