#pragma once
#include "lcd_config.h"
#include <gfx.hpp>
#include <uix.hpp>
// user interface controls
// and screen declarations
using ui_screen_t = uix::screen<gfx::rgb_pixel<LCD_BIT_DEPTH>>;
using ui_label_t = uix::label<typename ui_screen_t::control_surface_type>;
using ui_painter_t = uix::painter<typename ui_screen_t::control_surface_type>;

template<typename ControlSurfaceType>
class svg_box : public uix::canvas_control<ControlSurfaceType> {
    using base_type = uix::canvas_control<ControlSurfaceType>;
    gfx::stream* m_stream;
    gfx::matrix m_transform;
    bool m_dirty;
public:
    using control_surface_type = ControlSurfaceType;
    svg_box() : base_type(), m_stream(nullptr),m_dirty(true) {

    }
    gfx::stream& stream() const {
        return *m_stream;
    }
    void stream(gfx::stream& stream) {
        m_stream = &stream;
        m_dirty = true;
        this->invalidate();
    }
protected:
    virtual void on_before_paint() override {
        if(m_stream!=nullptr && m_dirty) {
            m_stream->seek(0);
            gfx::sizef dim;
            if(gfx::gfx_result::success == gfx::canvas::svg_dimensions(*m_stream,&dim)) {
                m_transform = gfx::matrix::create_fit_to(dim,(gfx::rectf)this->bounds());
                m_dirty = false;
            }
        
        }
    }
    virtual void on_paint(gfx::canvas& destination, const uix::srect16& clip) override {
        if(m_stream!=nullptr) {
            m_stream->seek(0);
            destination.render_svg(*m_stream,m_transform);
        }
    }
};

using ui_svg_box_t = svg_box<typename ui_screen_t::control_surface_type>;
extern gfx::tt_font title_font;
extern gfx::tt_font probe_font;
extern ui_screen_t main_screen;
extern uint16_t probe_cols;
extern uint16_t probe_rows;
// main screen
extern ui_label_t title_label;
extern ui_svg_box_t title_svg;
// probe screen
extern ui_painter_t probe_painter;
extern ui_label_t probe_label;
extern ui_painter_t msg_painter;
extern ui_label_t probe_msg_label1;
extern ui_label_t probe_msg_label2;
extern uint16_t probe_cols;
extern uint16_t probe_rows;
void ui_init();