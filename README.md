# RPiPico-RA8876

Graphics for an **RA8876** display, such as the EastRising **ER-TFTM101-1** or the BuyDisplay 7" module (1024 × 600), on a **Raspberry Pi Pico or Pico 2** over SPI.

It combines the two earlier libraries:

- the **drawing API of TFT_eSPI**, via [TFT_eSPI_RA8876](https://github.com/bocabob/TFT_eSPI_RA8876): shapes, anti-aliased graphics, fonts 1–8, FreeFonts, smooth fonts, sprites, datums, padding;
- the **RA8876's own hardware**, as used by [RA8876_RP2040](https://github.com/bocabob/RA8876_RP2040): hardware fills, display pages, block copies, scrolling, picture-in-picture and the character ROM fonts.

## Documentation

- [Usage guide](docs/RPiPico-RA8876_Guide.md): setup, the hardware functions, ROM fonts, and moving from TFT_eSPI_RA8876.
- [examples/HardwareTest](examples/HardwareTest/HardwareTest.ino): a bench test that checks every feature and prints a report.

## What the hardware is used for

| Goal | How |
|------|-----|
| Fast clears and big fills | `fillRect()`/`fillScreen()` hand large rectangles to the controller's fill engine automatically. |
| Instant page changes | Draw on a hidden page, then `showPage()`: one register write. |
| Smooth scrolling | `scrollRect()` moves any rectangle inside the controller and fills the gap. |
| No blinking on updates | Draw between `beginComposite()` and `endComposite()`: the result appears in one step. |
| Overlays | `pipShow()` puts part of another page on top of the screen, with no redrawing. |
| Fast text | `ROM_FONT_16/24/32`: the controller draws the characters (one SPI command each). |

## Quick start

```cpp
#include <RPiPico-RA8876.h>

//                cs  rst mosi sclk miso port
RPiPico_RA8876 tft(9, 14, 11,  10,  8,   spi1);   // the defaults, shown for clarity

void setup() {
  tft.init();
  if (!tft.panelFound()) return;          // runs without a display

  tft.fillScreen(TFT_BLACK);              // hardware fill
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawString("Hello", 20, 20, 4);     // TFT_eSPI font 4
  tft.drawString("ROM font", 20, 60, ROM_FONT_24);

  tft.setDrawPage(1);                     // draw out of sight...
  tft.fillScreen(TFT_NAVY);
  tft.drawString("Page 1", 20, 20, ROM_FONT_32);
  tft.showPage(1);                        // ...and switch to it at once
}

void loop() {}
```

## Installation

1. Install the [Earle Philhower arduino-pico](https://github.com/earlephilhower/arduino-pico) board package.
2. Copy this folder into your sketchbook's `libraries/` folder and restart the Arduino IDE.
3. Wire the display to SPI1 (defaults below) or pass your pins to the constructor.

| Display | Pico GPIO (default) |
|---------|---------------------|
| CS   | 9 |
| RESET | 14 |
| MOSI (SDI) | 11 (SPI1 TX) |
| SCK | 10 (SPI1 SCK) |
| MISO (SDO) | 8 (SPI1 RX) |

The library drives the SPI port directly, so nothing else should share it.

## Status

Version 1.0.0 compiles cleanly for the Pico and Pico 2. On the bench (2026-10-08, ER-TFTM101-1, 20 MHz SPI) the HardwareTest sketch passed every read-back check (40 PASS, 0 FAIL), and everything it asks you to look at was as described: colours, pixel streaming, all fonts, ROM fonts, fills, pages and page switching, block copies, scrolling, flicker-free updates, picture-in-picture, sprites, read-back and viewports.

Measured there: a 1024 × 540 fill takes 8.6 ms in hardware (516 ms sent as pixels); ROM text about 9 µs a character (font 2 about 137 µs); a page switch 15 ms, timed to the next frame; scrolling a 984 × 468 area by one line 20 ms.

The display is landscape only: the RA8876 cannot rotate the picture (see the guide, §3).

## Credits

- TFT_eSPI by Bodmer (FreeBSD licence), via the TFT_eSPI_RA8876 fork.
- RA8876 register sequences after the RA8876_t3 / RA8876_RP2040 library by Warren Watson, mjs513, KurtE and MorganS (MIT licence).

See [license.txt](license.txt).
