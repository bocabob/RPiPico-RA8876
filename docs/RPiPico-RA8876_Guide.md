# RPiPico-RA8876 — Usage Guide

RPiPico-RA8876 drives an RA8876 display (ER-TFTM101-1, BuyDisplay 7", 1024 × 600) from a Raspberry Pi Pico or Pico 2 over hardware SPI. Its drawing functions are those of **TFT_eSPI**; on top of them it uses the controller's own hardware for fills, pages, copies, scrolling, overlays and text.

---

## Contents

1. [Setup](#1-setup)
2. [How drawing reaches the display](#2-how-drawing-reaches-the-display)
3. [The TFT_eSPI drawing API](#3-the-tft_espi-drawing-api)
4. [Character ROM fonts](#4-character-rom-fonts)
5. [Hardware fill](#5-hardware-fill)
6. [Display pages](#6-display-pages)
7. [Block copies](#7-block-copies)
8. [Scrolling](#8-scrolling)
9. [Flicker-free updates](#9-flicker-free-updates)
10. [Picture-in-picture](#10-picture-in-picture)
11. [Images, sprites and byte order](#11-images-sprites-and-byte-order)
12. [Diagnostics and raw registers](#12-diagnostics-and-raw-registers)
13. [Moving from TFT_eSPI_RA8876](#13-moving-from-tft_espi_ra8876)
14. [Limits](#14-limits)

---

## 1. Setup

```cpp
#include <RPiPico-RA8876.h>

RPiPico_RA8876 tft(cs, rst, mosi, sclk, miso, spi_port);
```

| Argument | Default | Notes |
|----------|---------|-------|
| `cs` | 9 | Any GPIO. |
| `rst` | 14 | Any GPIO, or -1 if the Pico does not drive reset. |
| `mosi` | 11 | Must be a TX pin of `spi_port` (SPI1: 11, 15; SPI0: 3, 7, 19). |
| `sclk` | 10 | Must be an SCK pin of `spi_port` (SPI1: 10, 14; SPI0: 2, 6, 18). |
| `miso` | 8 | Must be an RX pin of `spi_port` (SPI1: 8, 12; SPI0: 0, 4, 16), or -1. |
| `spi_port` | `spi1` | `spi0` or `spi1` (pico-sdk names). |

Pins are checked at `init()`; wrong ones print a message and the display is not started.

| Method | Description |
|--------|-------------|
| `void setSPIFrequency(uint32_t write_hz, uint32_t read_hz = 0)` | Before `init()`. Default 20 MHz for both. `read_hz` is used for display memory reads only. |
| `void setSPIMode(uint8_t mode)` | Before `init()`. 0 (default) or 3. |
| `void init()` / `begin()` | Resets the panel, starts its PLLs and SDRAM, programs the panel timing, turns the display and backlight on. |
| `bool panelFound()` | `false` if nothing answered within `RA8876_INIT_TIMEOUT_MS` (3 s). Every drawing call is then ignored, so a sketch keeps running without its display. |

The SPI port is driven directly (pico-sdk), not through `SPIClass`, so it should not be shared with another device.

Compile-time options are in [RPiPico-RA8876_Config.h](../RPiPico-RA8876_Config.h); each can also be set with a build flag (for example `-D RA8876_NO_FONT8` to drop font 8).

---

## 2. How drawing reaches the display

The RA8876 has no D/C line: every byte on the bus is preceded by a prefix byte (register address, data write, status read or data read), with chip select toggled around each pair. Pixels are sent as a stream into the controller's memory port, which writes each one at its current write position and steps along.

The library keeps that cheap:

- **Register shadows.** Registers the controller never changes on its own (windows, colours, modes) are only written when their value changes.
- **Write-position reuse.** The library counts the pixels it sends. When the next pixel or run starts exactly where the controller already is (the next pixel of a glyph row, the next row of a block), the stream simply continues; when it starts nearby, only the changed position bytes are written. `setStreamOptimisation(false)` turns this off for comparison.
- **Hardware engines.** Large fills, copies, scrolls and ROM text are done by the controller from a handful of register writes.

Every hardware operation waits for the controller to finish before returning, so ordinary drawing can follow straight away. If the controller does not finish within `RA8876_ENGINE_TIMEOUT_MS` (100 ms) the operation is abandoned, `engineErrors()` counts it, and the sketch carries on.

---

## 3. The TFT_eSPI drawing API

All of TFT_eSPI 2.5.43 is here, with the same names and behaviour: see the [TFT_eSPI_RA8876 guide](https://github.com/bocabob/TFT_eSPI_RA8876/blob/main/docs/TFT_eSPI_RA8876_Guide.md) or Bodmer's TFT_eSPI documentation. In short:

| Area | Functions |
|------|-----------|
| Shapes | `drawPixel`, `drawLine`, `drawFastHLine/VLine`, `drawRect`/`fillRect`, `drawRoundRect`/`fillRoundRect`, `drawCircle`/`fillCircle`, `drawEllipse`/`fillEllipse`, `drawTriangle`/`fillTriangle`, `fillRectHGradient`/`VGradient`, `fillScreen` |
| Anti-aliased | `drawSmoothArc`, `drawArc`, `drawSmoothCircle`, `fillSmoothCircle`, `drawSmoothRoundRect`, `fillSmoothRoundRect`, `drawSpot`, `drawWideLine`, `drawWedgeLine`, `drawPixel(x, y, c, alpha, bg)` |
| Text | `drawString`, `drawNumber`, `drawFloat`, `drawChar`, `print`/`println`/`printf`, `setTextFont`, `setFreeFont`, `loadFont`, `setTextColor`, `setTextSize`, `setTextDatum`, `setTextPadding`, `setTextWrap`, `setCursor`, `textWidth`, `fontHeight` |
| Images | `pushImage`, `pushMaskedImage`, `drawBitmap`, `drawXBitmap`, `pushRect`, `readRect`, `readPixel`, `readRectRGB` |
| Areas | `setViewport`, `resetViewport`, `frameViewport`, `setOrigin` |
| Low level | `startWrite`/`endWrite`, `setAddrWindow`, `pushBlock`, `pushPixels`, `pushColor`, DMA (`initDMA`, `pushImageDMA`, `pushPixelsDMA`, `dmaBusy`, `dmaWait`) |
| Classes | `RPiPico_RA8876_Sprite` (was `TFT_eSprite`), `RPiPico_RA8876_Button` (was `TFT_eSPI_Button`) |

Colour names (`TFT_BLACK` …) and datums (`TL_DATUM`, `MC_DATUM` …) are the TFT_eSPI ones.

**RA8876 specifics:**

- Landscape only: `setRotation()` keeps rotation 0. The RA8876 cannot swap rows and columns (portrait) or scan right to left, only top to bottom; its text engine and picture-in-picture windows cannot follow a mirror done in software. Mount the panel the right way up. (TFT_eSPI_RA8876's rotations 1 and 2 set a reserved bit that blanks the panel.)
- `invertDisplay()` does nothing: the RA8876 cannot invert colours.
- Reading pixels works (`readPixel`, `readRect`, `readRectRGB`), so the anti-aliased functions may leave out their background colour.
- No touch support: the ER-TFTM101-1's GT9271 needs a separate library such as bb_captouch.

---

## 4. Character ROM fonts

The RA8876 contains three fixed-width fonts covering ISO 8859-1 (ASCII plus Latin-1). The controller draws them itself, so a character costs one SPI command instead of every pixel.

| Font number | Cell |
|-------------|------|
| `ROM_FONT_16` | 8 × 16 |
| `ROM_FONT_24` | 12 × 24 |
| `ROM_FONT_32` | 16 × 32 |

They work wherever a font number does:

```cpp
tft.setTextColor(TFT_WHITE, TFT_BLACK);     // opaque: background drawn with the text
tft.drawString("Status: OK", 20, 20, ROM_FONT_24);

tft.setTextDatum(MC_DATUM);
tft.drawNumber(speed, 512, 300, ROM_FONT_32);

tft.setTextFont(ROM_FONT_16);
tft.setCursor(0, 100);
tft.printf("Loco %d at speed %d\n", addr, speed);   // each run of characters is one text write
```

- `setTextSize(1..4)` enlarges them in hardware (independently in x and y is not exposed).
- `setTextColor(fg)` makes the background transparent; `setTextColor(fg, bg)` draws it.
- `textWidth()` and `fontHeight()` are exact (fixed width); the `*_BASELINE` datums use an approximate baseline.
- UTF-8 is decoded; characters outside ISO 8859-1 show as `?`.
- Characters are drawn whole: in a viewport, a character not wholly inside is left out (other fonts are cut at the edge).
- In a sprite, ROM fonts are replaced by font 2 (`ROM_FONT_16`) or font 4 (`ROM_FONT_24`/`32`): only the controller can draw them.
- Opaque ROM text with `setTextPadding()` updates a value with no separate clear, so it never blinks.

---

## 5. Hardware fill

`fillRect()` and `fillScreen()` hand any rectangle of at least `RA8876_HW_FILL_MIN_PIXELS` (64) pixels, and at least 2 × 2, to the controller's fill engine. Smaller ones are sent as pixels. Clipping, viewports and the origin apply as usual.

| Method | Description |
|--------|-------------|
| `void setHardwareFill(bool enable, uint32_t minPixels = 64)` | Turn the automatic hardware path on or off, or change the threshold. |
| `bool hwFillRect(x, y, w, h, color)` | Fill in hardware directly, in page coordinates (no viewport). `false` if w or h is under 2. |

The fill engine hangs when a rectangle's start and end coordinates are equal, so 1-pixel-wide fills are never given to it.

---

## 6. Display pages

The controller's 16 MB of display memory holds `RA8876_PAGES` (13) whole screens. Pages `0` to `RA8876_USER_PAGES - 1` (0–10) are for the sketch; the last two are used by `scrollRect()` and `beginComposite()`.

| Method | Description |
|--------|-------------|
| `void setDrawPage(uint8_t page)` | Where all drawing goes (TFT_eSPI functions and hardware ones). |
| `uint8_t getDrawPage()` | |
| `void showPage(uint8_t page, bool vsync = true)` | What the panel shows. With `vsync`, the switch waits for the start of a frame. |
| `uint8_t getShowPage()` | |
| `bool waitForVSync(uint32_t timeout_ms = 40)` | Wait for the next frame. `false` if the controller gives no VSYNC flag; page switches then stop waiting for it. |
| `void fillPage(uint8_t page, uint16_t color)` | Clear a whole page in hardware. |
| `void copyPage(uint8_t src, uint8_t dst)` | Copy a whole page in hardware. |
| `uint32_t pageAddress(uint8_t page)` | Byte address in display memory. |

At start-up page 0 is both shown and drawn on.

**Page change without a visible redraw:**

```cpp
uint8_t front = 0, back = 1;

void changeScreen() {
  tft.setDrawPage(back);      // out of sight
  drawWholeScreen();
  tft.showPage(back);         // appears complete, in one frame
  front = back;               // later updates keep going to the page now shown
  back = 1 - back;
}
```

To keep a fixed part (a tab bar, say) without redrawing it on every page, draw it once and `copyRect()` it onto the hidden page before `showPage()`.

---

## 7. Block copies

The block transfer engine copies rectangles anywhere in display memory. Coordinates are page pixels (no viewport or origin).

| Method | Description |
|--------|-------------|
| `bool copyRect(srcPage, sx, sy, dstPage, dx, dy, w, h)` | Copy a block. Same page is fine if source and destination do not overlap. |
| `bool copyRectTransparent(srcPage, sx, sy, dstPage, dx, dy, w, h, transparentColor)` | Pixels of `transparentColor` are not copied (sprites with a key colour). |
| `bool blendRect(page0, x0, y0, page1, x1, y1, dstPage, dx, dy, w, h, alpha)` | Mix two blocks: `alpha` 0–32 is the share of `page0`. |

Blocks are clipped to the page. Typical use: keep icons, button faces or a background on a spare page and stamp them with `copyRect()` instead of redrawing.

---

## 8. Scrolling

```cpp
bool scrollRect(x, y, w, h, dx, dy, fillColor = -1);
```

Moves the contents of a rectangle of the drawing page by `dx`, `dy` (positive = right, down) inside the controller and fills the uncovered strip with `fillColor` (`-1` leaves it). Page coordinates.

A console that scrolls by one text line:

```cpp
const int X = 8, Y = 64, W = 960, H = 26 * 18, LINE = 18;

void addLine(const char *text) {
  tft.scrollRect(X, Y, W, H, 0, -LINE, TFT_BLACK);
  tft.setTextColor(TFT_GREEN, TFT_BLACK);
  tft.drawString(text, X, Y + H - LINE, ROM_FONT_16);
}
```

---

## 9. Flicker-free updates

Clearing an area and then drawing into it shows the blank area for a moment. Two ways to avoid it:

**Opaque text with padding** (values and labels): the text and its background are drawn in one pass.

```cpp
tft.setTextColor(TFT_WHITE, TFT_NAVY);
tft.setTextDatum(MR_DATUM);
tft.setTextPadding(tft.textWidth("88888", 4));
tft.drawNumber(value, 500, 100, 4);
```

**Composition off screen** (anything else):

```cpp
tft.beginComposite(x, y, w, h);        // drawing now goes to an off-screen copy
tft.fillRect(x, y, w, h, TFT_NAVY);    // clear-then-draw as usual
drawGauge(value);
tft.endComposite();                    // the finished block is copied on in one step
```

| Method | Description |
|--------|-------------|
| `void beginComposite(x, y, w, h, bool copyBackground = true)` | Start. `copyBackground` copies the current contents first; pass `false` when the drawing covers the whole rectangle. Use ordinary coordinates. |
| `void endComposite()` | Copy the block onto the drawing page. |
| `bool inComposite()` | |

Calls do not nest, and the drawing page must not be changed in between.

---

## 10. Picture-in-picture

Two overlay windows show part of any page on top of the shown page, without touching it. PIP 1 is drawn above PIP 2.

| Method | Description |
|--------|-------------|
| `bool pipShow(pip, srcPage, sx, sy, dx, dy, w, h)` | Show the `w × h` block at `sx, sy` of `srcPage` at `dx, dy` on screen. |
| `void pipMove(pip, dx, dy)` | Move it. |
| `void pipHide(pip)` | Remove it. |

x positions and widths are rounded down to multiples of 4 (a controller limit). Change the overlay's content by drawing on its source page. Good for dialogs and pop-ups: hiding one restores the page underneath with no redraw.

---

## 11. Images, sprites and byte order

The RA8876 takes the low byte of each pixel first. The library hides this and follows TFT_eSPI's convention for `setSwapBytes()`:

| Data | `setSwapBytes` |
|------|----------------|
| Arrays of ordinary RGB565 values (`0xF800` = red), e.g. from most image converters | `true` |
| Byte-swapped data: sprite buffers, `readRect()` output | `false` (the default) |

Sprites, `readRect()`/`pushRect()` and DMA follow the same rule, so code written for TFT_eSPI works unchanged.

---

## 12. Diagnostics and raw registers

| Method | Description |
|--------|-------------|
| `uint8_t readStatus()` | The RA8876 status register (`RA8876_STSR_*` bits in `RA8876_Registers.h`). |
| `void writeRegister(uint8_t reg, uint8_t value)` | Write any register (the register shadows are then discarded). |
| `uint8_t readRegister(uint8_t reg)` | Read any register. |
| `uint32_t engineErrors()` | Hardware operations abandoned on time-out. |
| `void setStreamOptimisation(bool)` | Write-position reuse on/off (see §2). |
| `spi_inst_t *getSPIport()` | The pico-sdk SPI port. |
| `void getSetup(setup_t &s)` | Pins, port, SPI clocks, panel size. |
| `void setLogOutput(Print *out)` | Where the library's messages go (default `Serial`; `nullptr` for none). Call before `init()`. |

`init()` prints one line saying whether the display answered; the library also reports engine time-outs and a missing VSYNC flag, once each.

---

## 13. Moving from TFT_eSPI_RA8876

| TFT_eSPI_RA8876 | RPiPico-RA8876 |
|-----------------|----------------|
| `#include <TFT_eSPI_RA8876.h>` | `#include <RPiPico-RA8876.h>` |
| `TFT_eSPI_RA8876 tft;` + pins in `User_Setup` / `tft_RA8876_setup.h` | `RPiPico_RA8876 tft(cs, rst, mosi, sclk, miso, spi1);` |
| `SPI1.setRX(...)` … `SPI1.begin()` before `tft.init()` | Not needed: `init()` sets up the port and pins. |
| `TFT_eSprite_RA8876` | `RPiPico_RA8876_Sprite` |
| `TFT_eSPI_RA8876_Button` | `RPiPico_RA8876_Button` |
| Native RGB565 arrays pushed with `setSwapBytes(false)` | `setSwapBytes(true)`, as in TFT_eSPI |
| `getSPIinstance()` | `getSPIport()` |
| XPT2046 touch functions | Removed (use a touch library) |

Everything else (drawing, fonts, datums, colours, `panelFound()`) is unchanged.

---

## 14. Limits

- One panel geometry: 1024 × 600 (`RA8876_WIDTH`/`RA8876_HEIGHT`) with the ER-TFTM101-1 timing.
- RP2040/RP2350 with the arduino-pico core only.
- Landscape only, no rotation (see §3).
- ROM fonts: ISO 8859-1 only; whole characters only.
- The library owns its SPI port.
