/***************************************************************************************
  RPiPico-RA8876_Config.h -- compile-time options for RPiPico-RA8876.

  Nothing here needs editing for the ER-TFTM101-1 (or another 1024 x 600 RA8876 panel) on a
  Raspberry Pi Pico / Pico 2. Pins, SPI port and SPI clock are set at run time: see the
  RPiPico_RA8876 constructor and setSPIFrequency() in RPiPico-RA8876.h.

  Every option below can also be set from the build (PlatformIO build_flags, or
  arduino-cli --build-property), because each one is wrapped in #ifndef.
 ***************************************************************************************/
#ifndef _RPIPICO_RA8876_CONFIG_H_
#define _RPIPICO_RA8876_CONFIG_H_

// ---- Fonts compiled in ---------------------------------------------------------------------
// All on by default: the RP2040/RP2350 has flash to spare, and a font only costs flash when a
// sketch uses it (font tables are referenced from the font table either way, so define
// RA8876_NO_FONTx to drop one you never use).
#ifndef RA8876_NO_GLCD
  #define LOAD_GLCD    // Font 1: Adafruit 8 pixel, ~1820 bytes
#endif
#ifndef RA8876_NO_FONT2
  #define LOAD_FONT2   // Font 2: small 16 pixel high, ~3534 bytes
#endif
#ifndef RA8876_NO_FONT4
  #define LOAD_FONT4   // Font 4: medium 26 pixel high, ~5848 bytes
#endif
#ifndef RA8876_NO_FONT6
  #define LOAD_FONT6   // Font 6: large 48 pixel digits 1234567890:-.apm, ~2666 bytes
#endif
#ifndef RA8876_NO_FONT7
  #define LOAD_FONT7   // Font 7: 7-segment 48 pixel digits 1234567890:-., ~2438 bytes
#endif
#ifndef RA8876_NO_FONT8
  #define LOAD_FONT8   // Font 8: large 75 pixel digits 1234567890:-., ~3256 bytes
#endif
#ifndef RA8876_NO_GFXFF
  #define LOAD_GFXFF   // Adafruit GFX free fonts FF1 to FF48 and custom fonts
#endif
#ifndef RA8876_NO_SMOOTH_FONT
  #define SMOOTH_FONT  // Anti-aliased .vlw fonts from flash arrays or LittleFS
#endif

// The RA8876 character ROM fonts (8x16, 12x24, 16x32, ISO 8859-1) are in the controller and
// always available: see ROM_FONT_16 / ROM_FONT_24 / ROM_FONT_32 in RPiPico-RA8876.h.

// ---- Default wiring (the LCC RPi Pico boards: IO1 header on SPI1) --------------------------
#ifndef RA8876_DEFAULT_CS
  #define RA8876_DEFAULT_CS     9
#endif
#ifndef RA8876_DEFAULT_RST
  #define RA8876_DEFAULT_RST   14
#endif
#ifndef RA8876_DEFAULT_MOSI
  #define RA8876_DEFAULT_MOSI  11   // SPI1 TX
#endif
#ifndef RA8876_DEFAULT_SCLK
  #define RA8876_DEFAULT_SCLK  10   // SPI1 SCK
#endif
#ifndef RA8876_DEFAULT_MISO
  #define RA8876_DEFAULT_MISO   8   // SPI1 RX
#endif
#ifndef RA8876_DEFAULT_SPI
  #define RA8876_DEFAULT_SPI   spi1
#endif

// ---- SPI ------------------------------------------------------------------------------------
#ifndef RA8876_SPI_FREQUENCY
  #define RA8876_SPI_FREQUENCY       20000000  // bench-proven with TFT_eSPI_RA8876
#endif
#ifndef RA8876_SPI_READ_FREQUENCY
  #define RA8876_SPI_READ_FREQUENCY  20000000  // used for display memory reads only
#endif
#ifndef RA8876_SPI_MODE
  #define RA8876_SPI_MODE  0                   // 0 or 3; TFT_eSPI_RA8876 ran mode 0
#endif

// ---- Panel ----------------------------------------------------------------------------------
// ER-TFTM101-1 and the BuyDisplay 7" RA8876 module: 1024 x 600. Other panels: pass an
// RA8876_Panel to setPanel() before init().
#ifndef RA8876_INIT_TIMEOUT_MS
  #define RA8876_INIT_TIMEOUT_MS   3000   // init() gives up after this with no controller
#endif
#ifndef RA8876_ENGINE_TIMEOUT_MS
  #define RA8876_ENGINE_TIMEOUT_MS  100   // a fill/copy/text write that has not finished by
                                          // then is abandoned (see engineErrors())
#endif

// ---- Hardware acceleration ------------------------------------------------------------------
// fillRect() hands rectangles of at least this many pixels (and at least 2 x 2) to the RA8876's
// own fill engine instead of sending every pixel. Change at run time with setHardwareFill().
#ifndef RA8876_HW_FILL_MIN_PIXELS
  #define RA8876_HW_FILL_MIN_PIXELS  64
#endif

#endif // _RPIPICO_RA8876_CONFIG_H_
