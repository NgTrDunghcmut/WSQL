#ifndef FONT_H
#define FONT_H

#include <stdint.h>

// Returns the 5-byte column-major bitmap for ch (5 columns x 7 rows;
// within each byte, bit0 = top row .. bit6 = bottom row), or NULL if
// ch isn't in the font.
const uint8_t *font_lookup(char ch);

#endif
