#pragma once
#include <TFT_eSPI.h>
#include <SPI.h>
#include "Kandinsky/tjpgd/TJpg_Decoder.h"
#include "gen.h"


/*   Подключение Wemos MINI R1 к TFT iLi9488
 * 
 * ESP8266(Wemos MINI) |      iLi9488
 * -------------------   --------------------   
 *  3.3V  --------------------->  VCC
          GND   --------------------->  GND
          GPIO23 --------------------->  SDI / MOSI
          GPIO19 --------------------->  SDO / MISO    (если чтение с дисплея требуется)
          GPIO18 --------------------->  SCLK
          GPIO5  --------------------->  CS    (чип-селект дисплея)
          GPIO2  --------------------->  DC    (Data/Command)
          GPIO4  --------------------->  RST   (Reset, или подключите к 3.3V, если не используется)
          GPIO15  --------------------->  BL    (подсветка дисплея)

                                  Для сенсорного контроллера (XPT2046):
          GPIO21 --------------------->  TCS   (Chip Select для тача)
          GPIO18 (общий с дисплеем) -->  TCK   (тактовая линия, общая с SCLK)
          GPIO23 (общий с дисплеем) -->  TDI   (данные от ESP32 к тачу, общий с MOSI)
          GPIO19 (общий с дисплеем) -->  TDO   (данные от тача к ESP32, общий с MISO)
 * -------------------   --------------------  
*/


#define TFT_MISO 19
#define TFT_MOSI 23
#define TFT_SCLK 18
#define TFT_CS    5 
#define TFT_DC    2
#define TFT_RST   4
#define TOUCH_CS  21 
#define TFT_BL    15 

TFT_eSPI tft = TFT_eSPI();

#define TFT_PURPLE 0xA81F

bool tft_output(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t *bitmap) {
    tft.pushImage(x, y, w, h, bitmap);
    return true;
}

void tft_render(int x, int y, int w, int h, uint8_t* buf) {
    tft.pushImage(x, y, w, h, (uint16_t*)buf);
}

void setBrightness(uint8_t brightness) {
    ledcWrite(0, brightness); // Канал 0, значение от 0 до 255
}

void tft_init() {
    // Настройка ШИМ для подсветки
    ledcSetup(0, 5000, 8); // Канал 0, частота 5 кГц, 8 бит
    ledcAttachPin(TFT_BL, 0); // Привязываем пин подсветки к каналу 0
    setBrightness(db[kk::brightness].toInt()); // Устанавливаем начальную яркость

    SPI.setFrequency(40000000ul);
    SPI.begin(TFT_SCLK, TFT_MISO, TFT_MOSI, TFT_CS);
    tft.begin();
    uint16_t calData[5] = { 205, 3755, 185, 3651, 7 };
    tft.setTouch(calData);
    tft.setRotation(2);
    tft.setSwapBytes(true);
    tft.fillScreen(0x0000);
    tft.setTextColor(0x07E0);
    tft.setTextSize(2);

    gen.onRender(tft_render);
    TJpgDec.setCallback(tft_output);
}