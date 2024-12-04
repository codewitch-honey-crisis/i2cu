#include <lcd_config.h>
#include <ui.hpp>
#include <uix.hpp>
#define PROBE_IMPLEMENTATION
#include <assets/probe.h>
#define OPENSANS_REGULAR_IMPLEMENTATION
#include <assets/OpenSans_Regular.h>
#define TELEGRAMA_IMPLEMENTATION
#include <assets/Telegrama.h>
gfx::const_buffer_stream opensans_regular_stm(OpenSans_Regular,sizeof(OpenSans_Regular));
gfx::tt_font title_font = gfx::tt_font(opensans_regular_stm,40,gfx::font_size_units::px);
gfx::const_buffer_stream telegrama_stm(telegrama,sizeof(telegrama));
gfx::tt_font probe_font = gfx::tt_font(telegrama_stm,20,gfx::font_size_units::px);
gfx::const_buffer_stream probe_stm(probe,sizeof(probe));
gfx::tt_font probe_msg_font = gfx::tt_font(opensans_regular_stm,25,gfx::font_size_units::px);
using namespace gfx;
using namespace uix;
// declare native pixel type color enum
// for the screen
using scr_color_t = color<typename ui_screen_t::pixel_type>;
// declare 32-bit pixel color enum
// for controls
using ctl_color_t = color<rgba_pixel<32>>;

// the screen
ui_screen_t main_screen;

// main screen controls
ui_label_t title_label;
ui_svg_box_t title_svg;
ui_painter_t probe_painter;
ui_label_t probe_label;
ui_painter_t msg_painter;
ui_label_t probe_msg_label1;
ui_label_t probe_msg_label2;
// holds how many cols and rows
// are available
uint16_t probe_cols = 0;
uint16_t probe_rows = 0;
static void probe_painter_on_paint(typename ui_painter_t::control_surface_type& destination, const srect16& clip, void* state) {
    draw::filled_rectangle(destination,destination.bounds(),ctl_color_t::black.opacity(.85));
}
static void msg_painter_on_paint(typename ui_painter_t::control_surface_type& destination, const srect16& clip, void* state) {
    draw::filled_rectangle(destination,destination.bounds(),ctl_color_t::silver.opacity(.87));
}

// set up all the main screen
// controls
static void ui_init_main_screen() {
    // create a transparent color
    rgba_pixel<32> trans(0,true);
    title_label.color(ctl_color_t::black);
    title_label.text("i2cu");
    title_label.font(title_font);
    
    title_label.text_justify(uix_justify::bottom_middle);
    title_label.bounds(main_screen.bounds());
    
    main_screen.register_control(title_label);
    title_svg.stream(probe_stm);
    title_svg.bounds(srect16(0,0,200,110).offset(10,0));
    main_screen.register_control(title_svg);
    probe_painter.bounds(main_screen.bounds());
    probe_painter.on_paint_callback(probe_painter_on_paint);
    probe_painter.visible(false);
    main_screen.register_control(probe_painter);
    probe_label.color(ctl_color_t::white);
    probe_label.font(probe_font);
    probe_label.text_justify(uix_justify::center_left);
    probe_label.bounds(main_screen.bounds());
    probe_label.visible(false);

    main_screen.register_control(probe_label);

    // compute the probe columns and rows
    probe_rows = (main_screen.dimensions().height-
        probe_label.padding().height*2)/
        probe_font.line_height();
    int probe_m;
    // we use the standard method of measuring M
    // to determine the font width. This really
    // should be used with monospace fonts though
    size16 tsz;
    text_info ti;
    ti.text_font = &probe_font;
    ti.text_sz("M");
    probe_font.measure(-1,ti,&tsz);
    probe_cols = (main_screen.dimensions().width-
        probe_label.padding().width*2)/
        tsz.width;
    // now compute where our probe
    // configuration message labels
    // go
    srect16 b = main_screen.bounds();
    b=srect16(b.x1,
            b.y1,
            b.x2,
            b.y1+probe_msg_font.line_height()+
                probe_msg_label1.padding().height*2).
                    center_vertical(main_screen.bounds());
    b.offset_inplace(0,-(b.height()/2));
    rgba_pixel<32> mbg = ctl_color_t::silver;
    mbg.channelr<channel_name::A>(.87);
    msg_painter.bounds(b);
    msg_painter.on_paint_callback(msg_painter_on_paint);
    msg_painter.visible(false);
    main_screen.register_control(msg_painter);
    probe_msg_label1.color(ctl_color_t::black);
    probe_msg_label1.font(probe_msg_font);
    probe_msg_label1.text_justify(uix_justify::center);
    probe_msg_label1.bounds(b);
    probe_msg_label1.visible(false);

    main_screen.register_control(probe_msg_label1);

    probe_msg_label2.color(ctl_color_t::black);
    probe_msg_label2.font(probe_font);
    probe_msg_label2.text_justify(uix_justify::center);
    b.offset_inplace(0,probe_msg_label1.bounds().height());
    probe_msg_label2.bounds(b);
    probe_msg_label2.visible(false);

    main_screen.register_control(probe_msg_label2);

    main_screen.background_color(scr_color_t::white);
}
void ui_init() {
    title_font.initialize();
    probe_font.initialize();
    probe_msg_font.initialize();
    ui_init_main_screen();
}