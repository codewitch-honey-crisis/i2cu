// where the serial monitor output
// goes
#define MONITOR Serial
// the I2C probe connections
#define I2C Wire
#define I2C_SDA 21
#define I2C_SCL 22
// the serial probe connections
#define SER Serial1
#define SER_RX 17
#include <Arduino.h>
#include <Wire.h>
#include <SPIFFS.h>
#include <atomic>
#include <button.hpp>
#include <htcw_data.hpp>
#include <lcd_miser.hpp>
#include <thread.hpp>
#include <uix.hpp>
#include "driver/i2c.h"
#define LCD_IMPLEMENTATION
#include "lcd_init.h"
#include "ui.hpp"
using namespace arduino;
using namespace gfx;
using namespace uix;
using namespace freertos;

// htcw_uix calls this to send a bitmap to the LCD Panel API
static void uix_on_flush(point16 location,
                         bitmap<rgb_pixel<16>>& bmp,
                         void* state);
// the ESP Panel API calls this when the bitmap has been sent
static bool lcd_flush_ready(esp_lcd_panel_io_handle_t panel_io,
                            esp_lcd_panel_io_event_data_t* edata,
                            void* user_ctx);
// put the display controller and panel to sleep
static void lcd_sleep();
// wake up the display controller and panel
static void lcd_wake();
// check if the i2c address list has changed and
// rebuild the list if it has
static bool refresh_i2c();
// check if there is serial data incoming
// rebuild the display if it has
static bool refresh_serial();
// saves the settings
static void save_settings();
// click handler for button a
static void button_a_on_click(int clicks, void* state);
// long click handler for button a
static void button_a_on_long_click(void* state);
// click handler for button b
static void button_b_on_click(int clicks, void* state);
// thread routine that scans the bus and
// updates the i2c address list
static void i2c_update_task(void* state);

using dimmer_t = lcd_miser<4>;
using color16_t = color<rgb_pixel<16>>;
using color32_t = color<rgba_pixel<32>>;
using button_a_raw_t = int_button<35, 10, true>;
using button_b_raw_t = int_button<0, 10, true>;
using button_t = multi_button;
using screen_t = screen<LCD_HRES, LCD_VRES, rgb_pixel<16>>;

// i2c update thread data
static thread i2c_updater;
static SemaphoreHandle_t i2c_update_sync;
static volatile std::atomic_bool i2c_updater_ran;

// i2c address data
static uint32_t i2c_addresses[4];
static uint32_t i2c_addresses_old[4];

// serial data
static const int serial_bauds[] = {
    115200,
    19200,
    9600,
    2400
};
static const size_t serial_bauds_size 
    = sizeof(serial_bauds)/sizeof(int);
static size_t serial_baud_index = 0;
static bool serial_bin = false;
static uint32_t serial_msg_ts = 0;
static bool is_serial = false;
static uint8_t* serial_data = nullptr;
static size_t serial_data_capacity = 0;
static size_t serial_data_size = 0;

// probe display data
static char* display_text = nullptr;
static size_t display_text_size = 0;

// lcd panel ops and dimmer data
static constexpr const size_t lcd_buffer_size = 64 * 1024;
static uint8_t* lcd_buffer1 = nullptr;
static uint8_t* lcd_buffer2 = nullptr;
static bool lcd_sleeping = false;
static dimmer_t lcd_dimmer;

// button data
static button_a_raw_t button_a_raw; // right
static button_b_raw_t button_b_raw; // left
static button_t button_a(button_a_raw); // right
static button_t button_b(button_b_raw); // left

void setup() {
    MONITOR.begin(115200);
    // load our previous settings
    SPIFFS.begin(true,"/spiffs",1);
    if(SPIFFS.exists("/settings")) {
        File file = SPIFFS.open("/settings");
        file.read((uint8_t*)&serial_baud_index,sizeof(serial_baud_index));
        file.read((uint8_t*)&serial_bin,sizeof(serial_bin));
        file.close();
        MONITOR.println("Loaded settings");    
    }
    // begin serial probe
    SER.begin(serial_bauds[serial_baud_index], SERIAL_8N1, SER_RX, -1);

    // allocate the primary display buffer
    lcd_buffer1 = (uint8_t*)malloc(lcd_buffer_size);
    if (lcd_buffer1 == nullptr) {
        MONITOR.println("Error: Out of memory allocating lcd_buffer1");
        while (1)
            ;
    }
    lcd_dimmer.initialize();
    // clear the i2c data
    memset(&i2c_addresses_old, 0, sizeof(i2c_addresses_old));
    memset(&i2c_addresses, 0, sizeof(i2c_addresses));
    // start up the i2c updater
    i2c_updater_ran = false;
    i2c_update_sync = xSemaphoreCreateMutex();
    if (i2c_update_sync == nullptr) {
        MONITOR.println("Could not allocate I2C updater semaphore");
        while (1)
            ;
    }
    // 1-affinity = use the core that isn't this one:
    i2c_updater = thread::create_affinity(1 - thread::current().affinity(),
                                          i2c_update_task,
                                          nullptr,
                                          10,
                                          2000);
    if (i2c_updater.handle() == nullptr) {
        MONITOR.println("Could not allocate I2C updater thread");
        while (1)
            ;
    }
    i2c_updater.start();
    // hook up the buttons
    button_a.initialize();
    button_b.initialize();
    button_a.on_click(button_a_on_click);
    button_a.on_long_click(button_a_on_long_click);
    button_b.on_click(button_b_on_click);
    lcd_panel_init(lcd_buffer_size, lcd_flush_ready);
    if (lcd_handle == nullptr) {
        MONITOR.println("Could not initialize the display");
        while (1)
            ;
    }
    // allocate the second display buffer (optional)
    lcd_buffer2 = (uint8_t*)malloc(lcd_buffer_size);
    if (lcd_buffer2 == nullptr) {
        MONITOR.println("Warning: Out of memory allocating lcd_buffer2.");
        MONITOR.println("Performance may be degraded. Try a smaller lcd_buffer_size");
    }
    // reinitialize the screen with valid pointers
    main_screen = screen_t(lcd_buffer_size, lcd_buffer1, lcd_buffer2);
    main_screen.on_flush_callback(uix_on_flush);
    // initialize the UI components
    ui_init();
    // compute the amount of string we need to fill the display
    display_text_size = probe_cols * (probe_rows + 1) + 1;
    // and allocate it (shouldn't be much)
    display_text = (char*)malloc(display_text_size);
    if (display_text == nullptr) {
        MONITOR.println("Could not allocate display text");
        while (1)
            ;
    }
    *display_text = '\0';
    serial_data_capacity = probe_cols*probe_rows;
    serial_data = (uint8_t*)malloc(serial_data_capacity);
    if(serial_data==nullptr) {
        MONITOR.println("Could not allocate serial data");
        while (1)
            ;
    }
    
    // report the memory vitals
    MONITOR.printf("SRAM free: %0.1fKB\n",
                  (float)ESP.getFreeHeap() / 1024.0);
    MONITOR.printf("SRAM largest free block: %0.1fKB\n",
                  (float)ESP.getMaxAllocHeap() / 1024.0);
    MONITOR.println();
}

void loop() {
    // timeout the serial settings display
    // if it's showing
    if(serial_msg_ts && millis()>serial_msg_ts+1000) {
        probe_msg_label1.visible(false);
        probe_msg_label2.visible(false);
        serial_msg_ts = 0;
    }
    // pause the app while the buttons are pressed
    while (button_a.pressed() || button_b.pressed()) {
        button_a.update();
        button_b.update();
    }
    // give everything a chance to update
    lcd_dimmer.update();
    button_a.update();
    button_b.update();
    // if the i2c has changed, update the display
    if (refresh_i2c()) {
        is_serial = false;
        probe_label.text_color(color32_t::green);
        probe_label.text(display_text);
        probe_label.visible(true);
        lcd_wake();
        lcd_dimmer.wake();
    // otherwise if the serial has changed,
    // update the display
    } else if (refresh_serial()) {
        is_serial = true;
        probe_label.text_color(color32_t::yellow);
        probe_label.text(display_text);
        probe_label.visible(true);
        lcd_wake();
        lcd_dimmer.wake();
    }
    // if we're dimmed all the way, just
    // sleep, and stop updating the 
    // screen. Otherwise ensure
    // the display controller 
    // is awake and update
    if (lcd_dimmer.faded()) {
        lcd_sleep();
    } else {
        lcd_wake();
        main_screen.update();
    }
}
// writes bitmap data to the lcd panel api
static void uix_on_flush(point16 location,
                         bitmap<rgb_pixel<16>>& bmp,
                         void* state) {
    int x1 = location.x;
    int y1 = location.y;
    int x2 = x1 + bmp.dimensions().width;
    int y2 = y1 + bmp.dimensions().height;
    esp_lcd_panel_draw_bitmap(lcd_handle,
                              x1,
                              y1,
                              x2,
                              y2,
                              bmp.begin());
}
// informs UIX that a previous flush was complete
static bool lcd_flush_ready(esp_lcd_panel_io_handle_t panel_io,
                            esp_lcd_panel_io_event_data_t* edata,
                            void* user_ctx) {
    main_screen.set_flush_complete();
    return true;
}
// puts the ST7789 to sleep
static void lcd_sleep() {
    if (!lcd_sleeping) {
        uint8_t params[] = {};
        esp_lcd_panel_io_tx_param(lcd_io_handle,
                                  0x10,
                                  params,
                                  sizeof(params));
        delay(5);
        lcd_sleeping = true;
    }
}
// wakes the ST7789
static void lcd_wake() {
    if (lcd_sleeping) {
        uint8_t params[] = {};
        esp_lcd_panel_io_tx_param(lcd_io_handle,
                                  0x11,
                                  params,
                                  sizeof(params));
        delay(120);
        lcd_sleeping = false;
    }
}
// saves the current configuration to flash
static void save_settings() {
    File file;
    if(!SPIFFS.exists("/settings")) {
        file = SPIFFS.open("/settings","wb",true);
    } else {
        file = SPIFFS.open("/settings","wb");
        file.seek(0);
    }
    file.write((uint8_t*)&serial_baud_index,sizeof(serial_baud_index));
    file.write((uint8_t*)&serial_bin,sizeof(serial_bin));
    file.close();
}
// right button on click
static void button_a_on_click(int clicks, void* state) {
    // if it's dimmed, wake it and eat one
    // click
    if(lcd_dimmer.dimmed()) {
        lcd_wake();
        lcd_dimmer.wake();
        --clicks;
    }
    // eat all the clicks, setting serial_bin
    // accordingly
    serial_bin = (serial_bin+(clicks&1))&1;
    // update the message controls
    probe_msg_label1.text("[ mode ]");
    probe_msg_label2.text(serial_bin?"bin":"txt");
    probe_msg_label1.visible(true);
    probe_msg_label2.visible(true);
    // start the message timeout
    serial_msg_ts = millis();
    // ensure the screen is up to date
    main_screen.update();
    // save the config
    save_settings();
}
// right button on long click
static void button_a_on_long_click(void* state) {
    // wake if necessary, and return if
    // that's the case
    if(lcd_dimmer.dimmed()) {
        lcd_wake();
        lcd_dimmer.wake();
        return;
    }
    // otherwise, change baud rate
    if(++serial_baud_index==serial_bauds_size) {
        serial_baud_index = 0;
    }
    // update the message controls
    probe_msg_label1.text("[ baud ]");
    char buf[16];
    int baud = (int)serial_bauds[serial_baud_index];
    itoa((int)baud,buf,10);
    probe_msg_label2.text(buf);
    probe_msg_label1.visible(true);
    probe_msg_label2.visible(true);
    // start the message timeout
    serial_msg_ts = millis();
    // update the baud rate
    SER.updateBaudRate(baud);
    // update the main screen
    main_screen.update();
    // save the config
    save_settings();
}
// left button on click
static void button_b_on_click(int clicks, void* state) {
    // just wake the display
    lcd_wake();
    lcd_dimmer.wake();
}
// scan the i2c bus periodically
// (runs on alternative core)
void i2c_update_task(void* state) {
    while (true) {
        I2C.begin(I2C_SDA, I2C_SCL);
        // ensure pullups
        i2c_set_pin(0, I2C_SDA, I2C_SCL, true, true, I2C_MODE_MASTER);
        // catch slow devices
        I2C.setTimeOut(uint16_t(-1));
        // clear the banks
        uint32_t banks[4];
        memset(banks, 0, sizeof(banks));
        // for every address
        for (byte i = 0; i < 127; i++) {
            // start a transmission, and see 
            // if it's successful
            I2C.beginTransmission(i);
            if (I2C.endTransmission() == 0) {
                // if so, set the corresponding bit
                banks[i / 32] |= (1 << (i % 32));
            }
        }
        I2C.end();
        // safely update the main address list
        xSemaphoreTake(i2c_update_sync, portMAX_DELAY);
        memcpy(i2c_addresses, banks, sizeof(banks));
        xSemaphoreGive(i2c_update_sync);
        // say we ran
        i2c_updater_ran = true;
        delay(1000);
    }
}
// refresh the i2c display if it has changed,
// reporting true if so
static bool refresh_i2c() {
    uint32_t banks[4];
    // don't try anything until we've run once
    if (i2c_updater_ran) {
        // safely copy out the share address list
        xSemaphoreTake(i2c_update_sync, portMAX_DELAY);
        memcpy(banks, i2c_addresses, sizeof(banks));
        xSemaphoreGive(i2c_update_sync);
        // if our addresses have changed
        if (memcmp(banks, i2c_addresses_old, sizeof(banks))) {
            char buf[32];
            *display_text = '\0';
            int count = 0;
            // for each address
            for (int i = 0; i < 128; ++i) {
                int mask = 1 << (i % 32);
                int bank = i / 32;
                // if its bit is set
                if (banks[bank] & mask) {
                    // if we still have room
                    if (count < probe_rows - 1) {
                        // insert newlines at the end of the 
                        // previous row, if there was one
                        if (count) {
                            strcat(display_text, "\n");
                        }
                        ++count;
                        // display an address
                        snprintf(buf, sizeof(buf), "0x%02X:%d", i, i);
                        strncat(display_text, buf, sizeof(buf));
                    }
                    MONITOR.printf("0x%02X:%d\n", i, i);
                }
            }
            if (!count) {
                // display none if there weren't any
                memcpy(display_text, "<none>\0", 7);
                MONITOR.println("<none>");
            }
            MONITOR.println();
            // set the old addresses to the latest
            memcpy(i2c_addresses_old, banks, sizeof(banks));
            // return true, indicating a change
            return true;
        }
    }
    // no change
    return false;
}
// refresh the serial display if it has changed
// reporting true if so
static bool refresh_serial() {
    // get the available data count
    size_t available = (size_t)SER.available();
    size_t advanced = 0;
    // if we have incoming data
    if (available > 0) {
        // start over if we're just switching to serial
        if(!is_serial) {
            serial_data_size = 0;
        }
        size_t serial_remaining = serial_data_capacity - serial_data_size;
        uint8_t* p;
        if(serial_remaining<available) {
            size_t to_scroll = available - serial_remaining;
            // scroll the serial buffer
            memmove(serial_data,serial_data+to_scroll,serial_data_size-to_scroll);
            serial_data_size-=to_scroll;
        }
        p = serial_data+serial_data_size;
        serial_data_size+=SER.read(p,available);
        if (!serial_bin) {  // text
            // pointer to our display text
            char* sz = display_text;
            uint8_t* pb = serial_data;
            size_t pbc = serial_data_size;
            // null terminate it
            *sz = '\0';
            int cols = 0, rows=0;
            do {
                // get the next serial
                if(pbc==0) {
                    break;
                }
                uint8_t b = *pb++;
                --pbc;
                // if it's printable, print it
                // otherwise, print '.'
                if (b == ' ' || isprint(b)) {
                    *sz++ = (char)b;
                    MONITOR.print((char)b);
                } else {
                    // monitor follows slightly different rules
                    *sz = '.';
                    if (b == '\n' || b == '\r' || b == '\t') {
                        MONITOR.print((char)b);
                    } else {
                        MONITOR.print('.');
                    }
                }
                // insert newlines as necessary
                if(rows<probe_rows-1 && ++cols == probe_cols) {
                    cols = 0;
                    *sz++ = '\n';
                    ++rows;
                }
                ++advanced;
            } while (pbc);
            *sz = '\0';
        } else { // binary
            int bin_cols = probe_cols/3, rows = 0;
            int count_bin = (bin_cols) * probe_rows;
            int mon_cols = 0;
            uint8_t* pb = serial_data;
            size_t pbc = serial_data_size;
            // our display pointer
            char* sz = display_text;
            // null terminate it
            *sz = '\0';
            int cols = 0;
            do {
                if(pbc==0) {
                    break;
                }
                uint8_t b = *pb++;
                --pbc;
                char buf[4];
                // format the binary column
                // inserting spaces as necessary
                if(bin_cols-1==cols) {
                    snprintf(buf,sizeof(buf),"%02X",b);
                    strcpy(sz,buf);
                    sz+=2;
                } else {
                    snprintf(buf,sizeof(buf),"%02X ",b);
                    strcpy(sz,buf);
                    sz+=3;
                }
                // insert newlines as necessary
                if(rows<probe_rows-1 && ++cols == bin_cols) {
                    cols = 0;
                    *sz++ = '\n';
                    ++rows;
                }
                // dump to the monitor
                MONITOR.printf("%02X ",b);
                if(++mon_cols == 10) {
                    MONITOR.println();
                    mon_cols =0;
                }
                ++advanced;
            } while (--count_bin);
            *sz = '\0';
            MONITOR.println();
        }
        // report a change
        return true;
    }
    // no change
    return false;
}