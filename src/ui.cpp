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
// declare native pixel type color enum
// for the screen
using scr_color_t = color<typename ui_screen_t::pixel_type>;
// declare 32-bit pixel color enum
// for controls
using ctl_color_t = color<rgba_pixel<32>>;

// our title SVG
svg_doc title_doc;

// the screen
ui_screen_t main_screen(0,nullptr,nullptr);

// main screen controls
ui_label_t title_label(main_screen);
ui_svg_box_t title_svg(main_screen);
ui_label_t probe_label(main_screen);
ui_label_t probe_msg_label1(main_screen);
ui_label_t probe_msg_label2(main_screen);
// holds how many cols and rows
// are available
uint16_t probe_cols = 0;
uint16_t probe_rows = 0;

// set up all the main screen
// controls
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
        title_svg.bounds(main_screen.bounds()
                            .offset(main_screen.dimensions().height/16,
                                    main_screen.dimensions().height/4));
        main_screen.register_control(title_svg);
    }
    rgba_pixel<32> bg = ctl_color_t::black;
    bg.channelr<channel_name::A>(.85);
    probe_label.background_color(bg);
    probe_label.border_color(bg);
    probe_label.text_color(ctl_color_t::white);
    probe_label.text_open_font(&probe_font);
    probe_label.text_line_height(20);
    probe_label.text_justify(uix_justify::center_left);
    probe_label.bounds(main_screen.bounds());
    probe_label.visible(false);
    main_screen.register_control(probe_label);

    probe_rows = (main_screen.dimensions().height-
        probe_label.padding().height*2)/
        probe_label.text_line_height();
    int probe_m;
    ssize16 tsz = probe_font.measure_text(ssize16::max(),
                            spoint16::zero(),
                            "M",
                            probe_font.scale(
                                probe_label.text_line_height()));
    probe_cols = (main_screen.dimensions().width-
        probe_label.padding().width*2)/
        tsz.width;
    probe_rows = (main_screen.dimensions().height-
        probe_label.padding().height*2)/
        tsz.height;
    srect16 b = main_screen.bounds();
    b=srect16(b.x1,
            b.y1,
            b.x2,
            b.y1+probe_msg_label1.text_line_height()+
                probe_msg_label1.padding().height*2).
                    center_vertical(main_screen.bounds());
    b.offset_inplace(0,-(b.height()/2));
    rgba_pixel<32> mbg = ctl_color_t::silver;
    mbg.channelr<channel_name::A>(.87);
    probe_msg_label1.background_color(mbg);
    probe_msg_label1.border_color(mbg);
    probe_msg_label1.text_color(ctl_color_t::black);
    probe_msg_label1.text_open_font(&title_font);
    probe_msg_label1.text_line_height(25);
    probe_msg_label1.text_justify(uix_justify::center);
    
    probe_msg_label1.bounds(b);
    probe_msg_label1.visible(false);
    main_screen.register_control(probe_msg_label1);

    probe_msg_label2.background_color(mbg);
    probe_msg_label2.border_color(mbg);
    probe_msg_label2.text_color(ctl_color_t::black);
    probe_msg_label2.text_open_font(&probe_font);
    probe_msg_label2.text_line_height(25);
    probe_msg_label2.text_justify(uix_justify::center);
    b.offset_inplace(0,probe_msg_label1.bounds().height());
    probe_msg_label2.bounds(b);
    probe_msg_label2.visible(false);
    main_screen.register_control(probe_msg_label2);

    main_screen.background_color(scr_color_t::white);
}
void ui_init() {
    ui_init_main_screen();
}