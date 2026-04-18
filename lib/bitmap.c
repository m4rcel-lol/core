/* CORE Kernel — (c) CORE Project, MIT License */
#include <core/types.h>

void bitmap_set(uint8_t *bitmap, size_t bit) {
    bitmap[bit / 8] |= (uint8_t)(1u << (bit % 8));
}

void bitmap_clear(uint8_t *bitmap, size_t bit) {
    bitmap[bit / 8] &= (uint8_t)~(1u << (bit % 8));
}

int bitmap_test(const uint8_t *bitmap, size_t bit) {
    return (bitmap[bit / 8] >> (bit % 8)) & 1;
}

ssize_t bitmap_find_first_free(const uint8_t *bitmap, size_t nbits) {
    for (size_t i = 0; i < nbits; i++) {
        if (!bitmap_test(bitmap, i)) return (ssize_t)i;
    }
    return -1;
}

ssize_t bitmap_find_first_set(const uint8_t *bitmap, size_t nbits) {
    for (size_t i = 0; i < nbits; i++) {
        if (bitmap_test(bitmap, i)) return (ssize_t)i;
    }
    return -1;
}
