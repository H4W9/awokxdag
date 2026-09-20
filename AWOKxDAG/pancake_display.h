#pragma once
// Pancake C5 display: a 3.5" ST7796 320x480 panel.
//
// The AxD UI is authored in a fixed 240x320 coordinate space (draw calls and
// touch zones alike). The Pancake build renders it at the panel's *native*
// 320x480 resolution instead of stretching a finished 240x320 raster -- an
// earlier scaled-buffer approach made font strokes fade or vary in thickness,
// because scaling a bitmapped image by a non-integer ratio can't stay both
// crisp and uniform.
//
// AwokPancakeDisplay keeps the 240x320 logical Adafruit_GFX interface (so the
// sketch and touch mapping are untouched) but paints into a real 320x480
// buffer: geometry is scaled up to fill the panel, while *text glyphs are drawn
// at native resolution* (crisp, uniform, no resampling) at the scaled cursor
// position. Text rendering reuses the base Adafruit_GFX::drawChar via a 1:1
// "passthrough" pixel mode; geometry uses scaled pixel/rect fills.
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

// ---- 240x320 logical canvas rendered natively onto the 320x480 panel -------
// Drop-in for AwokTouchDisplay: presents a 240x320 Adafruit_GFX surface, so the
// sketch and the touch mapping (input.ino, panel->240x320) are unchanged. Draws
// into a real 320x480 buffer -- geometry scaled to fill, text drawn crisp at
// native resolution -- then blits 1:1 to the panel (no resampling).
class AwokPancakeDisplay : public Adafruit_GFX {
 public:
  AwokPancakeDisplay(int8_t dc, int8_t cs, int8_t rst)
      : Adafruit_GFX(kLogicalW, kLogicalH), panel_(&SPI, dc, cs, rst) {}

  bool begin(uint32_t freq) {
    panel_.begin(freq);
    if (!buffer_) {
      buffer_ = static_cast<uint16_t*>(heap_caps_malloc(
          size_t(kPanelW) * kPanelH * sizeof(uint16_t),
          MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    }
    buffered_ = buffer_ != nullptr;
    // Logical->physical edge maps: pixel column/row i spans [lut[i], lut[i+1]).
    for (int i = 0; i <= kLogicalW; ++i) sxLut_[i] = i * kPanelW / kLogicalW;
    for (int i = 0; i <= kLogicalH; ++i) syLut_[i] = i * kPanelH / kLogicalH;
    if (buffered_) {
      fillScreen(0);
    } else {
      Serial.println("[display] no PSRAM back buffer; direct-to-panel");
      panel_.fillScreen(0);
    }
    return true;
  }

  // Blit the native 320x480 buffer straight to the panel -- no scaling here, so
  // text stays exactly as rendered. Dirty-gated like the ILI9341 build.
  void present(bool = true) {
    if (!buffered_ || !dirty_) return;
    panel_.startWrite();
    panel_.setAddrWindow(0, 0, kPanelW, kPanelH);
    for (int y = 0; y < kPanelH; ++y) {
      panel_.writePixels(buffer_ + size_t(y) * kPanelW, kPanelW, true, false);
    }
    panel_.endWrite();
    dirty_ = false;
  }

  void setRotation(uint8_t r) {
    Adafruit_GFX::setRotation(r);  // UI uses rotation 0; scaling assumes it
  }

  // --- Adafruit_GFX primitive overrides ---
  // In passthrough mode (set only while drawChar renders a glyph) coordinates
  // are already physical and written 1:1 -> crisp native text. Otherwise the
  // logical coordinate/rect is scaled up to its physical footprint.
  void drawPixel(int16_t x, int16_t y, uint16_t color) override {
    if (passthrough_) {
      rawPixel(x, y, color);
      return;
    }
    if (!buffered_) {
      panel_.drawPixel(x, y, color);
      return;
    }
    if (x < 0 || y < 0 || x >= _width || y >= _height) return;
    fillNative(sxLut_[x], syLut_[y], sxLut_[x + 1], syLut_[y + 1], color);
  }

  void fillRect(int16_t x, int16_t y, int16_t w, int16_t h,
                uint16_t color) override {
    if (passthrough_) {  // physical coords from drawChar; write 1:1
      for (int16_t j = 0; j < h; ++j)
        for (int16_t i = 0; i < w; ++i) rawPixel(x + i, y + j, color);
      return;
    }
    if (!buffered_) {
      panel_.fillRect(x, y, w, h, color);
      return;
    }
    int x0 = x, y0 = y, x1 = x + w, y1 = y + h;
    if (!clampLogicalRect(x0, y0, x1, y1)) return;
    fillNative(sxLut_[x0], syLut_[y0], sxLut_[x1], syLut_[y1], color);
  }

  void drawFastHLine(int16_t x, int16_t y, int16_t w, uint16_t color) override {
    if (!buffered_) {
      panel_.drawFastHLine(x, y, w, color);
      return;
    }
    int x0 = x, y0 = y, x1 = x + w, y1 = y + 1;
    if (!clampLogicalRect(x0, y0, x1, y1)) return;
    fillNative(sxLut_[x0], syLut_[y0], sxLut_[x1], syLut_[y1], color);
  }

  void drawFastVLine(int16_t x, int16_t y, int16_t h, uint16_t color) override {
    if (!buffered_) {
      panel_.drawFastVLine(x, y, h, color);
      return;
    }
    int x0 = x, y0 = y, x1 = x + 1, y1 = y + h;
    if (!clampLogicalRect(x0, y0, x1, y1)) return;
    fillNative(sxLut_[x0], syLut_[y0], sxLut_[x1], syLut_[y1], color);
  }

  void fillScreen(uint16_t color) override {
    if (!buffered_) {
      panel_.fillScreen(color);
      return;
    }
    const uint32_t n = uint32_t(kPanelW) * kPanelH;
    for (uint32_t i = 0; i < n; ++i) buffer_[i] = color;
    dirty_ = true;
  }

  // Render text natively: draw each glyph 1:1 at the scaled cursor position so
  // strokes are always crisp and uniform, regardless of the fractional layout
  // scale. Mirrors Adafruit_GFX::write() (classic font) for cursor/wrap; AxD
  // never sets a custom GFX font.
  size_t write(uint8_t c) override {
    if (c == '\n') {
      cursor_x = 0;
      cursor_y += textsize_y * 8;
    } else if (c != '\r') {
      if (wrap && (cursor_x + textsize_x * 6) > _width) {
        cursor_x = 0;
        cursor_y += textsize_y * 8;
      }
      // Opaque background: fill the whole scaled character cell first (so there
      // are no gaps between the natively-sized glyphs), then draw the glyph
      // transparently on top.
      if (textbgcolor != textcolor) {
        fillRect(cursor_x, cursor_y, textsize_x * 6, textsize_y * 8,
                 textbgcolor);
      }
      // drawChar() clips against _width/_height, but we hand it *physical*
      // coordinates (up to 320x480) while the logical surface is 240x320 -- so
      // widen the clip to the panel for the glyph, else right/bottom text is
      // dropped. passthrough_ makes drawPixel/fillRect write 1:1 (crisp glyph).
      const int16_t savedW = _width, savedH = _height;
      _width = kPanelW;
      _height = kPanelH;
      passthrough_ = true;
      Adafruit_GFX::drawChar(physX(cursor_x), physY(cursor_y), c, textcolor,
                             textcolor, textsize_x, textsize_y);
      passthrough_ = false;
      _width = savedW;
      _height = savedH;
      cursor_x += textsize_x * 6;
    }
    return 1;
  }

 private:
  static constexpr int16_t kLogicalW = 240;
  static constexpr int16_t kLogicalH = 320;

  int physX(int lx) const {
    if (lx < 0) lx = 0;
    if (lx > kLogicalW) lx = kLogicalW;
    return sxLut_[lx];
  }
  int physY(int ly) const {
    if (ly < 0) ly = 0;
    if (ly > kLogicalH) ly = kLogicalH;
    return syLut_[ly];
  }

  // Clamp a logical rect to [0,kLogicalW] x [0,kLogicalH]; false if empty.
  bool clampLogicalRect(int& x0, int& y0, int& x1, int& y1) const {
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > kLogicalW) x1 = kLogicalW;
    if (y1 > kLogicalH) y1 = kLogicalH;
    return x1 > x0 && y1 > y0;
  }

  // Fill a physical (native-pixel) rect [x0,x1) x [y0,y1) in the back buffer.
  void fillNative(int x0, int y0, int x1, int y1, uint16_t color) {
    for (int py = y0; py < y1; ++py) {
      uint16_t* row = buffer_ + size_t(py) * kPanelW;
      for (int px = x0; px < x1; ++px) row[px] = color;
    }
    dirty_ = true;
  }

  void rawPixel(int px, int py, uint16_t color) {
    if (px < 0 || py < 0 || px >= AwokST7796::kPanelW ||
        py >= AwokST7796::kPanelH)
      return;
    buffer_[size_t(py) * AwokST7796::kPanelW + px] = color;
    dirty_ = true;
  }

  static constexpr int16_t kPanelW = AwokST7796::kPanelW;
  static constexpr int16_t kPanelH = AwokST7796::kPanelH;
  AwokST7796 panel_;
  uint16_t* buffer_ = nullptr;
  uint16_t sxLut_[kLogicalW + 1] = {0};  // logical column -> physical x edge
  uint16_t syLut_[kLogicalH + 1] = {0};  // logical row    -> physical y edge
  bool passthrough_ = false;             // true only while a glyph is drawn 1:1
  bool buffered_ = false;
  bool dirty_ = true;
};
