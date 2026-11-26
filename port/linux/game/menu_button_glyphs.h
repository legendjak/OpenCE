#ifndef MENU_BUTTON_GLYPHS_H
#define MENU_BUTTON_GLYPHS_H
#include "math/integer_math.h"

/* Recognized stock face-button legends only. Returning false preserves native art. */
boolean menu_button_glyph_draw(short button, short port, rectangle2d const *bounds,
    rectangle2d const *clip, pixel32 color);
boolean menu_button_glyph_inline(short icon, short port, rectangle2d *cursor);

/* The carnage report's prompt, its A and B at the menus' size, and Halo PC's
 * maps' key names as buttons for a controller's player; kept left of
 * right_edge (-1: 48 short of the bounds' right, where draw_string cuts it
 * off; 0: anywhere). */
void menu_button_glyph_postgame_prompt(rectangle2d *bounds, wchar_t const *prompt, short right_edge);

/* Keep independent join instructions; suppress only duplicated legend actions. */
wchar_t const *menu_button_glyph_join_hint(boolean can_join, short port);

boolean menu_button_glyph_join_draw(wchar_t const *text, short port, rectangle2d const *bounds);

boolean menu_button_glyph_keyboard_active(short port);

short menu_button_glyph_key_width(short button);
short menu_button_glyph_compact_key_width(short button);

/* The virtual keyboard's background's painted B and A buttons, drawn over
 * from the button keys' bitmaps (their redraws with the high-res text). */
struct bitmap_data;
void menu_button_glyph_keyboard_legend(long background_index, struct bitmap_data const *background);

#endif
