#define I2C Wire
#define I2C_SDA 21
#define I2C_SCL 22
#define SER Serial1
#define SER_RX 17
#include <Arduino.h>
#include <Wire.h>

#include <atomic>
#include <button.hpp>
#include <htcw_data.hpp>
#include <lcd_miser.hpp>
#include <thread.hpp>
#include <uix.hpp>
#define LCD_IMPLEMENTATION
#include "driver/i2c.h"
#include "lcd_init.h"
#include "ui.hpp"
using namespace arduino;
using namespace gfx;
using namespace uix;
using namespace freertos;
using namespace data;
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
// click handler for button a
static void button_a_on_click(int clicks, void* state);
// long click handler for button a
static void button_a_on_long_click(void* state);
// click handler for button b (not necessary, but
// for future proofing in case the buttons get used
// later)
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

static thread i2c_updater;
static SemaphoreHandle_t i2c_update_sync;
static volatile std::atomic_bool i2c_updater_ran;

struct i2c_data {
    uint32_t banks[4];
};

static i2c_data i2c_addresses;
static i2c_data i2c_addresses_old;

static const int serial_bauds[] = {
    115200,
    19200,
    9600,
    2400
};
static const size_t serial_bauds_size = sizeof(serial_bauds)/sizeof(int);
static size_t serial_baud = 0;
static bool serial_bin = false;
static uint32_t serial_msg_ts = 0;

static char* display_text = nullptr;
static size_t display_text_size = 0;

static constexpr const size_t lcd_buffer_size = 64 * 1024;
static uint8_t* lcd_buffer1 = nullptr;
static uint8_t* lcd_buffer2 = nullptr;
static bool lcd_sleeping = false;
static dimmer_t lcd_dimmer;

static button_a_raw_t button_a_raw;
static button_b_raw_t button_b_raw;
static button_t button_a(button_a_raw);
static button_t button_b(button_b_raw);

void setup() {
    Serial.begin(115200);
    SER.begin(115200, SERIAL_8N1, SER_RX, -1);
    lcd_buffer1 = (uint8_t*)malloc(lcd_buffer_size);
    if (lcd_buffer1 == nullptr) {
        Serial.println("Error: Out of memory allocating lcd_buffer1");
        while (1)
            ;
    }
    lcd_dimmer.initialize();
    memset(&i2c_addresses_old, 0, sizeof(i2c_addresses_old));
    memset(&i2c_addresses, 0, sizeof(i2c_addresses));
    i2c_updater_ran = false;
    i2c_update_sync = xSemaphoreCreateMutex();
    if (i2c_update_sync == nullptr) {
        Serial.println("Could not allocate I2C updater semaphore");
        while (1)
            ;
    }
    i2c_updater = thread::create_affinity(1 - thread::current().affinity(),
                                          i2c_update_task,
                                          nullptr,
                                          10,
                                          2000);
    if (i2c_updater.handle() == nullptr) {
        Serial.println("Could not allocate I2C updater thread");
        while (1)
            ;
    }
    i2c_updater.start();
    button_a.initialize();
    button_b.initialize();
    button_a.on_click(button_a_on_click);
    button_a.on_long_click(button_a_on_long_click);
    button_b.on_click(button_b_on_click);
    lcd_panel_init(lcd_buffer_size, lcd_flush_ready);
    if (lcd_handle == nullptr) {
        Serial.println("Could not initialize the display");
        while (1)
            ;
    }
    lcd_buffer2 = (uint8_t*)malloc(lcd_buffer_size);
    if (lcd_buffer2 == nullptr) {
        Serial.println("Warning: Out of memory allocating lcd_buffer2.");
        Serial.println("Performance may be degraded. Try a smaller lcd_buffer_size");
    }
    main_screen = screen_t(lcd_buffer_size, lcd_buffer1, lcd_buffer2);
    main_screen.on_flush_callback(uix_on_flush);
    ui_init();
    display_text_size = probe_cols * (probe_rows + 1) + 1;
    display_text = (char*)malloc(display_text_size);
    if (display_text == nullptr) {
        Serial.println("Could not allocate display text");
        while (1)
            ;
    }
    *display_text = '\0';

    Serial.printf("SRAM free: %0.1fKB\n",
                  (float)ESP.getFreeHeap() / 1024.0);
    Serial.printf("SRAM largest free block: %0.1fKB\n",
                  (float)ESP.getMaxAllocHeap() / 1024.0);
}

void loop() {
    if(serial_msg_ts && millis()>serial_msg_ts+1000) {
        probe_msg_label1.visible(false);
        probe_msg_label2.visible(false);
        serial_msg_ts = 0;
    }
    while (button_a.pressed() || button_b.pressed()) {
        button_a.update();
        button_b.update();
    }
    lcd_dimmer.update();
    button_a.update();
    button_b.update();
    if (refresh_i2c()) {
        probe_label.text_color(color32_t::light_blue);
        probe_label.text(display_text);
        probe_label.visible(true);
        lcd_wake();
        lcd_dimmer.wake();

    } else if (refresh_serial()) {
        probe_label.text_color(color32_t::yellow);
        probe_label.text(display_text);
        probe_label.visible(true);
        lcd_wake();
        lcd_dimmer.wake();
    }
    if (lcd_dimmer.faded()) {
        lcd_sleep();
    } else {
        lcd_wake();
        main_screen.update();
    }
}
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

static bool lcd_flush_ready(esp_lcd_panel_io_handle_t panel_io,
                            esp_lcd_panel_io_event_data_t* edata,
                            void* user_ctx) {
    main_screen.set_flush_complete();
    return true;
}
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

static void button_a_on_click(int clicks, void* state) {
    if(lcd_dimmer.dimmed()) {
        lcd_wake();
        lcd_dimmer.wake();
        --clicks;
    }
    serial_bin = (serial_bin+(clicks&1))&1;
    probe_msg_label1.text("[ mode ]");
    probe_msg_label2.text(serial_bin?"bin":"txt");
    probe_msg_label1.visible(true);
    probe_msg_label2.visible(true);
    serial_msg_ts = millis();
    main_screen.update();
    
}
static void button_a_on_long_click(void* state) {
    if(lcd_dimmer.dimmed()) {
        lcd_wake();
        lcd_dimmer.wake();
        return;
    }
    // change baud rate
    if(++serial_baud==serial_bauds_size) {
        serial_baud = 0;
    }
    probe_msg_label1.text("[ baud ]");
    char buf[16];
    int baud = (int)serial_bauds[serial_baud];
    itoa((int)baud,buf,10);
    probe_msg_label2.text(buf);
    probe_msg_label1.visible(true);
    probe_msg_label2.visible(true);
    serial_msg_ts = millis();
    SER.updateBaudRate(baud);
    main_screen.update();

}
static void button_b_on_click(int clicks, void* state) {
    lcd_wake();
    lcd_dimmer.wake();
}
void i2c_update_task(void* state) {
    while (true) {
        I2C.begin(I2C_SDA, I2C_SCL);
        i2c_set_pin(0, I2C_SDA, I2C_SCL, true, true, I2C_MODE_MASTER);
        I2C.setTimeOut(uint16_t(-1));
        uint32_t banks[4];
        memset(banks, 0, sizeof(banks));
        for (byte i = 0; i < 127; i++) {
            I2C.beginTransmission(i);
            if (I2C.endTransmission() == 0) {
                banks[i / 32] |= (1 << (i % 32));
            }
        }
        I2C.end();
        xSemaphoreTake(i2c_update_sync, portMAX_DELAY);
        memcpy(i2c_addresses.banks, banks, sizeof(banks));
        xSemaphoreGive(i2c_update_sync);
        i2c_updater_ran = true;
        delay(1000);
    }
}
static bool refresh_i2c() {
    uint32_t banks[4];
    if (i2c_updater_ran) {
        xSemaphoreTake(i2c_update_sync, portMAX_DELAY);
        memcpy(banks, i2c_addresses.banks, sizeof(banks));
        xSemaphoreGive(i2c_update_sync);
        if (memcmp(banks, i2c_addresses_old.banks, sizeof(banks))) {
            char buf[32];
            *display_text = '\0';
            int count = 0;
            for (int i = 0; i < 128; ++i) {
                int mask = 1 << (i % 32);
                int bank = i / 32;
                if (banks[bank] & mask) {
                    if (count < probe_rows - 1) {
                        if (count) {
                            strcat(display_text, "\n");
                        }
                        ++count;
                        snprintf(buf, sizeof(buf), "0x%02X:%d", i, i);
                        strncat(display_text, buf, sizeof(buf));
                    }
                    Serial.printf("0x%02X:%d\n", i, i);
                }
            }
            if (!count) {
                strncpy(display_text, "<none>", sizeof(display_text));
                Serial.println("<none>");
            }
            Serial.println();
            memcpy(i2c_addresses_old.banks, banks, sizeof(banks));
            return true;
        }
    }
    return false;
}
static bool refresh_serial() {
    size_t available = (size_t)SER.available();
    size_t advanced = 0;
    if (available > 0) {
        if (!serial_bin) {  // text
            while (available > (display_text_size - probe_rows - 1)) {
                int i = SER.read();
                if (i != -1) {
                    uint8_t b = (uint8_t)i;
                    if (b == '\r' || b == '\n' || b == '\t' || b == ' ' || isprint(b)) {
                        Serial.print((char)b);
                    } else {
                        Serial.print('.');
                    }
                }

                --available;
            }
            char* sz = display_text;
            *sz = '\0';
            int cols = 0;
            do {
                int i = SER.read();
                if (i == -1) {
                    break;
                }
                uint8_t b = (uint8_t)i;
                if (b == ' ' || isprint(b)) {
                    *sz++ = (char)b;
                    Serial.print((char)b);
                } else {
                    *sz = '.';
                    if (b == '\n' || b == '\r' || b == '\t') {
                        Serial.print((char)b);
                    } else {
                        Serial.print('.');
                    }
                }
                if (++cols == probe_cols) {
                    cols = 0;
                    *sz++ = '\n';
                }
                ++advanced;
            } while (--available);
            *sz = '\0';
        } else { // binary
            int bin_cols = probe_cols/3;
            int count_bin = (bin_cols) * probe_rows;
            int mon_cols = 0;
            while (available > count_bin) {
                int i = SER.read();
                if (i != -1) {
                    uint8_t b = (uint8_t)i;
                    Serial.printf("%02X ",i);
                    if(++mon_cols==10) {
                        Serial.println();
                        mon_cols = 0;
                    }
                }
                --available;
            }
            char* sz = display_text;
            *sz = '\0';
            int cols = 0;
            do {
                int i = SER.read();
                if (i == -1) {
                    break;
                }
                uint8_t b = (uint8_t)i;
                char buf[4];
                if(bin_cols-1==cols) {
                    snprintf(buf,sizeof(buf),"%02X",i);
                    strcpy(sz,buf);
                    sz+=2;
                } else {
                    snprintf(buf,sizeof(buf),"%02X ",i);
                    strcpy(sz,buf);
                    sz+=3;
                }
                if (++cols == bin_cols) {
                    cols = 0;
                    *sz++ = '\n';
                }
                Serial.printf("%02X ");
                if(++mon_cols == 10) {
                    Serial.println();
                    mon_cols =0;
                }
                ++advanced;
            } while (--available);
            *sz = '\0';
            Serial.println();
        }
        return true;
    }
    return false;
}