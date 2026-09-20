#pragma once
// Pancake C5 display: a 3.5" ST7796 320x480 panel driven from the same 240x320
// UI back buffer the ILI9341 Touch build uses.
//
// The entire AxD UI is laid out in a fixed 240x320 coordinate space (draw calls
// and touch zones alike). Rather than reflow every view for the larger panel,
// the Pancake build keeps that logical 240x320 canvas in a PSRAM back buffer --
// exactly like touch_display.h -- and, once per frame, upscale-blits it to fill
// the 320x480 ST7796 (nearest-neighbour, x4/3 across and x3/2 down). Touch is
// mapped back through the same scale in input.ino, so every existing view and
// hit box works unchanged. See input.ino readTouch() for the inverse mapping.
//
// There is no Adafruit ST7796 library on the Arduino registry, so the panel
// driver is a small Adafruit_SPITFT subclass here -- the same base class
// Adafruit_ILI9341 is built on. Only an ST7796 init sequence and setAddrWindow
// differ; the SPI plumbing, writePixels and colour handling are inherited.
#include <Adafruit_GFX.h>
#include <Adafruit_SPITFT.h>
#include <SPI.h>
#include <esp_heap_caps.h>

// ---- Minimal ST7796 320x480 panel driver (Adafruit_SPITFT subclass) --------
class AwokST7796 : public Adafruit_SPITFT {
 public:
  AwokST7796(SPIClass* spi, int8_t dc, int8_t cs, int8_t rst)
      : Adafruit_SPITFT(kPanelW, kPanelH, spi, cs, dc, rst) {}

  void begin(uint32_t freq) {
    if (freq == 0) freq = 27000000;  // matches the ILI9341 Touch build
    initSPI(freq, SPI_MODE0);
    // ST7796S bring-up (extension command set, gamma, power); values are the
    // widely-used ST7796 init shared by TFT_eSPI/LovyanGFX for this panel.
    static const uint8_t kGammaPlus[] = {0xF0, 0x09, 0x0B, 0x06, 0x04, 0x15,
                                         0x2F, 0x54, 0x42, 0x3C, 0x17, 0x14,
                                         0x18, 0x1B};
    static const uint8_t kGammaMinus[] = {0xE0, 0x09, 0x0B, 0x06, 0x04, 0x03,
                                          0x2B, 0x43, 0x42, 0x3B, 0x16, 0x14,
                                          0x17, 0x1B};
    static const uint8_t kOutputAdjust[] = {0x40, 0x8A, 0x00, 0x00, 0x29,
                                            0x19, 0xA5, 0x33};
    static const uint8_t kDispFnCtrl[] = {0x80, 0x02, 0x3B};
    // Each sendCommand() opens and closes its own SPI transaction, so the init
    // sequence is not wrapped in an outer startWrite()/endWrite() (that would
    // nest transactions). present() wraps its raw writeCommand/writePixels.
    sendCommand(0x01);  // Software reset
    delay(120);
    sendCommand(0x11);  // Sleep out
    delay(120);
    cmd(0xF0, 0xC3);            // Enable extension command 2, part I
    cmd(0xF0, 0x96);            // Enable extension command 2, part II
    cmd(0x36, kMadctlPortrait); // MADCTL
    cmd(0x3A, 0x55);            // COLMOD: 16-bit/pixel
    cmd(0xB4, 0x01);            // Column inversion
    sendCommand(0xB6, kDispFnCtrl, sizeof(kDispFnCtrl));  // Display fn control
    sendCommand(0xE8, kOutputAdjust, sizeof(kOutputAdjust));
    cmd(0xC1, 0x06);            // Power control 2
    cmd(0xC2, 0xA7);            // Power control 3
    cmd(0xC5, 0x18);            // VCOM control
    delay(120);
    sendCommand(0xE0, kGammaPlus, sizeof(kGammaPlus));
    sendCommand(0xE1, kGammaMinus, sizeof(kGammaMinus));
    delay(120);
    cmd(0xF0, 0x3C);  // Disable extension command 2, part I
    cmd(0xF0, 0x69);  // Disable extension command 2, part II
    delay(120);
    sendCommand(0x21);  // Inversion on (Pancake panel: TFT_INVERSION_ON)
    sendCommand(0x29);  // Display on
    _width = kPanelW;
    _height = kPanelH;
  }

  // Standard MIPI DCS window commands (identical bytes to ILI9341/ST7796).
  void setAddrWindow(uint16_t x, uint16_t y, uint16_t w, uint16_t h) override {
    const uint16_t x2 = x + w - 1, y2 = y + h - 1;
    writeCommand(0x2A);  // Column address set
    SPI_WRITE16(x);
    SPI_WRITE16(x2);
    writeCommand(0x2B);  // Page address set
    SPI_WRITE16(y);
    SPI_WRITE16(y2);
    writeCommand(0x2C);  // Memory write
  }

  static constexpr int16_t kPanelW = 320;
  static constexpr int16_t kPanelH = 480;
  // MADCTL for the native portrait orientation: MX (0x40) | BGR (0x08).
  // If the image is mirrored/rotated or colours are swapped on real hardware,
  // this is the byte to adjust.
  static constexpr uint8_t kMadctlPortrait = 0x48;

 private:
  // Command followed by a single data byte (the common case in the init table).
  void cmd(uint8_t command, uint8_t data) { sendCommand(command, &data, 1); }
};

// ---- 240x320 logical canvas that scales to fill the ST7796 -----------------
// Drop-in for the AwokTouchDisplay used by the ILI9341 Touch build: same 240x320
// Adafruit_GFX surface and begin()/present() contract, so the sketch is
// unchanged. The difference is only in present(), which upscales to 320x480.
class AwokPancakeDisplay : public Adafruit_GFX {
 public:
  AwokPancakeDisplay(int8_t dc, int8_t cs, int8_t rst)
      : Adafruit_GFX(kLogicalW, kLogicalH), panel_(&SPI, dc, cs, rst) {}

  bool begin(uint32_t freq) {
    panel_.begin(freq);
    if (!buffer_) {
      buffer_ = static_cast<uint16_t*>(heap_caps_malloc(
          size_t(kLogicalW) * kLogicalH * sizeof(uint16_t),
          MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    }
    buffered_ = buffer_ != nullptr;
    if (buffered_) {
      // Precompute the bilinear source column + weight for each output column.
      // Q8 fixed point: 240*256/320 == 192 exactly, so no per-frame division.
      for (int outX = 0; outX < AwokST7796::kPanelW; ++outX) {
        const uint32_t sx = uint32_t(outX) * kLogicalW * 256 / AwokST7796::kPanelW;
        colX0_[outX] = sx >> 8;               // integer source column (0..239)
        colWx_[outX] = sx & 0xFF;             // weight of the next column (0..255)
      }
      fillScreen(0);
    } else {
      Serial.println("[display] no PSRAM back buffer; direct-to-panel 240x320");
      panel_.fillScreen(0);
    }
    return true;
  }

  // Upscale-blit the 240x320 buffer to the full 320x480 panel with bilinear
  // filtering. Point sampling at these non-integer ratios (x4/3, x3/2) makes 1px
  // font strokes land as 1px or 2px unpredictably -- uneven, hard-to-read text.
  // Bilinear renders every stroke at a consistent (anti-aliased) weight instead.
  // Dirty-gated so idle frames cost nothing, matching the buffered ILI9341 path.
  void present(bool = true) {
    if (!buffered_ || !dirty_) return;
    static uint16_t line[AwokST7796::kPanelW];
    panel_.startWrite();
    panel_.setAddrWindow(0, 0, AwokST7796::kPanelW, AwokST7796::kPanelH);
    for (int outY = 0; outY < AwokST7796::kPanelH; ++outY) {
      const uint32_t sy = uint32_t(outY) * kLogicalH * 256 / AwokST7796::kPanelH;
      const int y0 = sy >> 8;
      const int y1 = (y0 + 1 < kLogicalH) ? y0 + 1 : y0;
      const uint8_t wy = sy & 0xFF;
      const uint16_t* row0 = buffer_ + size_t(y0) * kLogicalW;
      const uint16_t* row1 = buffer_ + size_t(y1) * kLogicalW;
      for (int outX = 0; outX < AwokST7796::kPanelW; ++outX) {
        const int x0 = colX0_[outX];
        const int x1 = (x0 + 1 < kLogicalW) ? x0 + 1 : x0;
        const uint8_t wx = colWx_[outX];
        const uint16_t top = blend565(row0[x0], row0[x1], wx);
        const uint16_t bot = blend565(row1[x0], row1[x1], wx);
        line[outX] = blend565(top, bot, wy);
      }
      panel_.writePixels(line, AwokST7796::kPanelW, true, false);
    }
    panel_.endWrite();
    dirty_ = false;
  }

  void setRotation(uint8_t r) {
    Adafruit_GFX::setRotation(r);  // logical buffer is the canvas; panel is fixed
  }

  void drawPixel(int16_t x, int16_t y, uint16_t color) override {
    if (!buffered_) {
      panel_.drawPixel(x, y, color);
      return;
    }
    if (x < 0 || y < 0 || x >= _width || y >= _height) return;
    int16_t t;
    switch (rotation) {
      case 1: t = x; x = kLogicalW - 1 - y; y = t; break;
      case 2: x = kLogicalW - 1 - x; y = kLogicalH - 1 - y; break;
      case 3: t = x; x = y; y = kLogicalH - 1 - t; break;
    }
    buffer_[int32_t(y) * kLogicalW + x] = color;
    dirty_ = true;
  }

  void fillScreen(uint16_t color) override {
    if (!buffered_) {
      panel_.fillScreen(color);
      return;
    }
    const uint32_t n = uint32_t(kLogicalW) * kLogicalH;
    for (uint32_t i = 0; i < n; ++i) buffer_[i] = color;
    dirty_ = true;
  }

  void drawFastHLine(int16_t x, int16_t y, int16_t w, uint16_t color) override {
    if (!buffered_) {
      panel_.drawFastHLine(x, y, w, color);
      return;
    }
    for (int16_t i = 0; i < w; ++i) drawPixel(x + i, y, color);
  }

  void drawFastVLine(int16_t x, int16_t y, int16_t h, uint16_t color) override {
    if (!buffered_) {
      panel_.drawFastVLine(x, y, h, color);
      return;
    }
    for (int16_t i = 0; i < h; ++i) drawPixel(x, y + i, color);
  }

  void fillRect(int16_t x, int16_t y, int16_t w, int16_t h,
                uint16_t color) override {
    if (!buffered_) {
      panel_.fillRect(x, y, w, h, color);
      return;
    }
    for (int16_t j = 0; j < h; ++j) drawFastHLine(x, y + j, w, color);
  }

 private:
  static constexpr int16_t kLogicalW = 240;
  static constexpr int16_t kLogicalH = 320;

  // Blend two RGB565 pixels: result = a*(1-w/256) + b*(w/256), per channel.
  static inline uint16_t blend565(uint16_t a, uint16_t b, uint8_t w) {
    if (w == 0) return a;
    const uint16_t iw = 256 - w;
    const uint16_t r = ((a >> 11) * iw + (b >> 11) * w) >> 8;
    const uint16_t g = (((a >> 5) & 0x3F) * iw + ((b >> 5) & 0x3F) * w) >> 8;
    const uint16_t bl = ((a & 0x1F) * iw + (b & 0x1F) * w) >> 8;
    return (r << 11) | (g << 5) | bl;
  }

  AwokST7796 panel_;
  uint16_t* buffer_ = nullptr;
  uint8_t colX0_[AwokST7796::kPanelW] = {0};  // bilinear source column per output col
  uint8_t colWx_[AwokST7796::kPanelW] = {0};  // and its Q8 blend weight
  bool buffered_ = false;
  bool dirty_ = true;
};
