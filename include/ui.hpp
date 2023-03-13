#pragma once
#include "lcd_config.h"

#include <uix.hpp>
using ui_screen_t = uix::screen<LCD_HRES,LCD_VRES,gfx::rgb_pixel<16>>;
using ui_label_t = uix::label<typename ui_screen_t::pixel_type,typename ui_screen_t::palette_type>;
using ui_svg_box_t = uix::svg_box<typename ui_screen_t::pixel_type,typename ui_screen_t::palette_type>;
extern const gfx::open_font& title_font;
extern const gfx::open_font& probe_font;
extern ui_screen_t main_screen;
extern uint16_t probe_cols;
extern uint16_t probe_rows;
// main screen
extern ui_label_t title_label;
extern ui_svg_box_t title_svg;
// probe screen
extern ui_label_t probe_label;
//extern ui_label_t probe_msg_label1;
//extern ui_label_t probe_msg_label2;
extern uint16_t probe_cols;
extern uint16_t probe_rows;
void ui_init();