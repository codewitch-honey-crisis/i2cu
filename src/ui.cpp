#include "lcd_config.h"
#include <ui.hpp>
#include <uix.hpp>
#include "probe.hpp"
#include <fonts/OpenSans_Regular.hpp>
#include <fonts/Telegrama.hpp>
const gfx::open_font& title_font = OpenSans_Regular;
const gfx::open_font& probe_font = Telegrama;

using namespace gfx;
using namespace uix;
using scr_color_t = color<typename ui_screen_t::pixel_type>;
using ctl_color_t = color<rgba_pixel<32>>;

svg_doc title_doc;
ui_screen_t main_screen(0,nullptr,nullptr);
ui_screen_t probe_screen(0,nullptr,nullptr);

// main screen
ui_label_t title_label(main_screen);
ui_svg_box_t title_svg(main_screen);
// probe screen
ui_label_t probe_label(probe_screen);
//ui_label_t probe_msg_label1(probe_screen);
//ui_label_t probe_msg_label2(probe_screen);
uint16_t probe_cols = 0;
uint16_t probe_rows = 0;
static void ui_init_main_screen() {
    rgba_pixel<32> trans;
    trans.channel<channel_name::A>(0);
    
    title_label.background_color(trans);
    title_label.border_color(trans);
    title_label.text_color(ctl_color_t::black);
    title_label.text("i2cu");
    title_label.text_open_font(&title_font);
    title_label.text_line_height(40);
    title_label.text_justify(uix_justify::bottom_middle);
    title_label.bounds(main_screen.bounds());
    main_screen.register_control(title_label);
    gfx_result res = svg_doc::read(&probe,&title_doc);
    if(res!=gfx_result::success) {
        Serial.println("Could not load title svg");
    } else {
        title_svg.doc(&title_doc);
        title_svg.bounds(main_screen.bounds().offset(main_screen.dimensions().height/16,main_screen.dimensions().height/4));
        main_screen.register_control(title_svg);
    }
    main_screen.background_color(scr_color_t::white);
}
static void ui_init_probe_screen() {
    rgba_pixel<32> trans;
    trans.channel<channel_name::A>(0);
    
    probe_label.background_color(trans);
    probe_label.border_color(trans);
    probe_label.text_color(ctl_color_t::white);
    probe_label.text_open_font(&probe_font);
    probe_label.text_line_height(20);
    probe_label.text_justify(uix_justify::top_left);
    probe_label.bounds(probe_screen.bounds());
    probe_screen.register_control(probe_label);

    probe_screen.background_color(scr_color_t::black);
    probe_rows = (probe_screen.dimensions().height-
        probe_label.padding().height*2)/
        probe_label.text_line_height();
    int probe_m;
    ssize16 tsz = probe_font.measure_text(ssize16::max(),
                            spoint16::zero(),
                            "M",
                            probe_font.scale(
                                probe_label.text_line_height()));
    probe_cols = (probe_screen.dimensions().width-
        probe_label.padding().width*2)/
        tsz.width;
    
}
void ui_init() {
    ui_init_main_screen();
    ui_init_probe_screen();
}