#define SERIAL_RX 17
#define I2C Wire
#define I2C_SDA 21
#define I2C_SCL 22
#include <Arduino.h>
#include <Wire.h>
#include "driver/i2c.h"
#define LCD_IMPLEMENTATION
#include <atomic>
#include <button.hpp>
#include <lcd_miser.hpp>
#include <thread.hpp>
#include <uix.hpp>

#include "lcd_init.h"
#include "ui.hpp"
using namespace arduino;
using namespace gfx;
using namespace uix;
using namespace freertos;

static void uix_on_flush(point16 location, bitmap<rgb_pixel<16>>& bmp, void* state); 
static bool lcd_flush_ready(esp_lcd_panel_io_handle_t panel_io,
                            esp_lcd_panel_io_event_data_t* edata,
                            void* user_ctx);
static void lcd_sleep();
static void lcd_wake();
static bool refresh_i2c();
static void draw_probe();
static void button_a_on_click(int clicks, void* state);
static void button_b_on_click(int clicks, void* state);
static void update_task(void* state);
using dimmer_t = lcd_miser<4>;
using color16_t = color<rgb_pixel<16>>;
using color32_t = color<rgba_pixel<32>>;
using button_a_raw_t = int_button<35, 10, true>;
using button_b_raw_t = int_button<0, 10, true>;
using button_t = multi_button;
using screen_t = screen<LCD_HRES, LCD_VRES, rgb_pixel<16>>;

static thread updater;
static SemaphoreHandle_t update_sync;
static volatile std::atomic_bool updater_ran;
struct i2c_data {
    uint32_t banks[4];
};
static i2c_data i2c_addresses;
static i2c_data i2c_addresses_old;

static char display_text[8 * 1024];

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
    lcd_buffer1 = (uint8_t*)malloc(lcd_buffer_size);
    if (lcd_buffer1 == nullptr) {
        Serial.println("Error: Out of memory allocating lcd_buffer1");
        while (1)
            ;
    }
    lcd_dimmer.initialize();
    memset(&i2c_addresses_old,0,sizeof(i2c_addresses_old));
    memset(&i2c_addresses,0,sizeof(i2c_addresses));
    updater_ran = false;
    update_sync = xSemaphoreCreateMutex();
    updater = thread::create_affinity(1-thread::current().affinity(),update_task,nullptr,10,2000);
    updater.start();
    button_a.initialize();
    button_b.initialize();
    button_a.on_click(button_a_on_click);
    button_b.on_click(button_b_on_click);
    lcd_panel_init(lcd_buffer_size, lcd_flush_ready);
    if (lcd_handle == nullptr) {
        Serial.println("Could not init the display");
        while (1)
            ;
    }
    main_screen = screen_t(lcd_buffer_size, lcd_buffer1, lcd_buffer2);
    main_screen.on_flush_callback(uix_on_flush);
    ui_init();
    lcd_buffer2 = (uint8_t*)malloc(lcd_buffer_size);
    if (lcd_buffer2 == nullptr) {
        Serial.println("Warning: Out of memory allocating lcd_buffer2. Performance may be degraded. Try a smaller lcd_buffer_size");
    }
    
    *display_text = '\0';
    Serial.printf("SRAM free: %0.1fKB\n",(float)ESP.getFreeHeap()/1024.0);
    Serial.printf("SRAM largest free block: %0.1fKB\n",(float)ESP.getMaxAllocHeap()/1024.0);
}

void loop() {
    lcd_dimmer.update();
    button_a.update();
    button_b.update();
    if(refresh_i2c()) {
        lcd_wake();
        lcd_dimmer.wake();
        Serial.println("I2C changed");
        probe_label.text(display_text);
        probe_label.visible(true);
    }
    if (lcd_dimmer.faded()) {
        if (!lcd_sleeping) {
            lcd_sleep();
        }
    } else {
        lcd_wake();
        main_screen.update();
    }
}
static void uix_on_flush(point16 location, bitmap<rgb_pixel<16>>& bmp, void* state) {
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
        esp_lcd_panel_io_tx_param(lcd_io_handle, 0x10, params, sizeof(params));
        delay(5);
        lcd_sleeping = true;
    }
}
static void lcd_wake() {
    if (lcd_sleeping) {
        uint8_t params[] = {};
        esp_lcd_panel_io_tx_param(lcd_io_handle, 0x11, params, sizeof(params));
        delay(120);
        lcd_sleeping = false;
    }
}

static void button_a_on_click(int clicks, void* state) {
    if (lcd_sleeping) {
        lcd_wake();
    }
    lcd_dimmer.wake();
}
static void button_b_on_click(int clicks, void* state) {
    if (lcd_sleeping) {
        lcd_wake();
    }
    lcd_dimmer.wake();
}
void update_task(void* state) {
    while (true) {
        I2C.begin(I2C_SDA, I2C_SCL);  // sda= GPIO_21 /scl= GPIO_22
        i2c_set_pin(0, I2C_SDA, I2C_SCL, true, true, I2C_MODE_MASTER);
        I2C.setTimeOut(uint16_t(-1));
        uint32_t banks[4];
        memset(banks,0,sizeof(banks));
        for (byte i = 0; i < 127; i++) {
            I2C.beginTransmission(i);        // Begin I2C transmission Address (i)
            if (I2C.endTransmission() == 0)  // Receive 0 = success (ACK response)
            {
                banks[i/32]|=(1<<(i%32));
            }
        }
        I2C.end();
        xSemaphoreTake(update_sync,portMAX_DELAY);
        memcpy(i2c_addresses.banks,banks,sizeof(banks));
        xSemaphoreGive(update_sync);
        updater_ran = true;
        delay(1000);
    }
}
static bool refresh_i2c() {
    uint32_t banks[4];
    if (updater_ran) {
        xSemaphoreTake(update_sync, portMAX_DELAY);
        memcpy(banks, i2c_addresses.banks, sizeof(banks));
        xSemaphoreGive(update_sync);

        if (memcmp(banks, i2c_addresses_old.banks, sizeof(banks))) {
            char buf[32];
            *display_text = '\0';
            bool found = false;
            for (int i = 0; i < 128; ++i) {
                int mask = 1 << (i % 32);
                int bank = i / 32;
                if (banks[bank] & mask) {
                    if (found) {
                        strcat(display_text, "\n");
                    }
                    found = true;
                    snprintf(buf, sizeof(buf), "0x%02X (%d)", i, i);
                    strncat(display_text, buf, sizeof(buf));
                }
            }
            if (!found) {
                strncpy(display_text, "<none>", sizeof(display_text));
            }
            memcpy(i2c_addresses_old.banks, banks, sizeof(banks));
            Serial.println(display_text);
            return true;
        }
    }
    return false;
}
