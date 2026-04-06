#include "gf3_packed.hpp"

GF3Arena* gf3_arena_init(size_t qutrit_count) {
    GF3Arena* arena = (GF3Arena*)malloc(sizeof(GF3Arena));
    if (!arena) return NULL;
    
    arena->qutrit_count = qutrit_count;
    arena->capacity_bytes = (qutrit_count + 3) / 4; // Round up to full bytes
    
    arena->data = (uint8_t*)calloc(arena->capacity_bytes, sizeof(uint8_t));
    if (!arena->data) {
        free(arena);
        return NULL;
    }
    
    return arena;
}

void gf3_arena_free(GF3Arena* arena) {
    if (!arena) return;
    if (arena->data) free(arena->data);
    free(arena);
}

uint8_t gf3_arena_get(const GF3Arena* arena, size_t index) {
    const size_t byte_index = index / 4;
    const uint8_t position = index % 4;
    return gf3_unpack(arena->data[byte_index], position);
}

void gf3_arena_set(GF3Arena* arena, size_t index, uint8_t value) {
    const size_t byte_index = index / 4;
    const uint8_t position = index % 4;
    
    uint8_t byte = arena->data[byte_index];
    const uint8_t old_value = gf3_unpack(byte, position);
    
    // Subtract old value, add new value
    byte = byte - old_value * gf3_pow4[position] + value * gf3_pow4[position];
    arena->data[byte_index] = byte;
}