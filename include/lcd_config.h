#ifndef LCD_CONFIG_H
#define LCD_CONFIG_H

#ifdef TTGO_T1
#define LCD_SPI_HOST    SPI3_HOST
#define LCD_BCKL_ON_LEVEL 1
#define LCD_BCKL_OFF_LEVEL !LCD_BCKL_ON_LEVEL
#define PIN_NUM_MOSI 19
#define PIN_NUM_CLK 18
#define PIN_NUM_CS 5
#define PIN_NUM_DC 16
#define PIN_NUM_RST 23
//#define PIN_NUM_BCKL 4
#define LCD_PANEL esp_lcd_new_panel_st7789
#define LCD_HRES 135
#define LCD_VRES 240
#define LCD_COLOR_SPACE ESP_LCD_COLOR_SPACE_RGB
#define LCD_PIXEL_CLOCK_HZ (40 * 1000 * 1000)
#define LCD_GAP_X 52
#define LCD_GAP_Y 40
#define LCD_MIRROR_X false
#define LCD_MIRROR_Y false
#define LCD_INVERT_COLOR true
#define LCD_SWAP_XY false
#endif // TTGO_T1

#endif // LCD_CONFIG_H