#ifndef TRIPLE_VFO_U8G2_H
#define TRIPLE_VFO_U8G2_H

#include <stdbool.h>
#include <stdint.h>

uint8_t TripleVfoU8g2_GetSmallTextWidth(const char *text);
void TripleVfoU8g2_DrawSmallText(const char *text, uint8_t x_left, uint8_t y_top, bool set_black);
void TripleVfoU8g2_DrawSmallTextStatus(const char *text, uint8_t x_left, uint8_t y_top, bool set_black);

#endif
