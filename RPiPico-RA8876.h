/***************************************************************************************
  RPiPico-RA8876 -- graphics library for an RA8876 display (EastRising ER-TFTM101-1 and
  similar 1024 x 600 panels) on a Raspberry Pi Pico or Pico 2 over hardware SPI.

  The drawing API is Bodmer's TFT_eSPI (v2.5.43, via the TFT_eSPI_RA8876 fork): every TFT_eSPI
  graphics, text, font, sprite and anti-aliasing function works as documented there. On top of
  that the RA8876's own hardware is used where it helps:
    - large fillRect()/fillScreen() calls run in the controller's fill engine;
    - the controller's 16 MB of display memory is split into pages: draw on one while another
      is shown, then switch in one register write (setDrawPage(), showPage());
    - block copies, transparent copies and alpha blends between pages (copyRect() and friends);
    - hardware scrolling of any rectangle (scrollRect());
    - flicker-free updates by composing off screen (beginComposite()/endComposite());
    - two picture-in-picture overlay windows (pipShow());
    - the controller's character ROM fonts, drawn by the controller itself (ROM_FONT_16/24/32).

  Pins, SPI port and SPI clock are run-time settings (constructor and setSPIFrequency()), so a
  sketch configures the library without editing it.

  TFT_eSPI is Copyright (c) Bodmer, FreeBSD licence (see license.txt). RA8876 register
  sequences follow RAiO's application notes as used in the RA8876_t3 / RA8876_RP2040 library
  (Warren Watson, mjs513, KurtE, MorganS; MIT licence).
 ***************************************************************************************/

#ifndef _RPIPICO_RA8876_H_
#define _RPIPICO_RA8876_H_

#define RPIPICO_RA8876_VERSION "1.0.0"
#define TFT_ESPI_VERSION       "2.5.43"   // the TFT_eSPI API level this library provides

// Bit level feature flags
// Bit 0 set: viewport capability
#define TFT_ESPI_FEATURES 1

/***************************************************************************************
**                         Section 1: Load required header files
***************************************************************************************/
#include <Arduino.h>
#include <Print.h>

#if !defined(ARDUINO_ARCH_RP2040) || defined(ARDUINO_ARCH_MBED)
  #error "RPiPico-RA8876 needs an RP2040/RP2350 with the Earle Philhower arduino-pico core"
#endif

#include "RPiPico-RA8876_Config.h"
#include "RA8876_Registers.h"

// Handle FLASH based storage e.g. PROGMEM
#undef pgm_read_byte
#define pgm_read_byte(addr)   (*(const unsigned char *)(addr))
#undef pgm_read_word
#define pgm_read_word(addr) ({ \
  typeof(addr) _addr = (addr); \
  *(const unsigned short *)(_addr); \
})
#undef pgm_read_dword
#define pgm_read_dword(addr) ({ \
  typeof(addr) _addr = (addr); \
  *(const unsigned long *)(_addr); \
})

// The RP2040/RP2350 hardware SPI driver
#include "Processors/RP2040_SPI.h"

/***************************************************************************************
**                         Section 2: Panel geometry and display memory
***************************************************************************************/
#ifndef RA8876_WIDTH
  #define RA8876_WIDTH   1024
#endif
#ifndef RA8876_HEIGHT
  #define RA8876_HEIGHT   600
#endif
// commandList() delay flag
#define TFT_INIT_DELAY 0x80

// For code written for TFT_eSPI
#ifndef TFT_WIDTH
  #define TFT_WIDTH  RA8876_WIDTH
#endif
#ifndef TFT_HEIGHT
  #define TFT_HEIGHT RA8876_HEIGHT
#endif

// Display memory: 16 MB of SDRAM (W9812G6), split into whole-screen 16 bpp pages
#define RA8876_SDRAM_BYTES   (16UL * 1024UL * 1024UL)
#define RA8876_PAGE_BYTES    ((uint32_t)RA8876_WIDTH * RA8876_HEIGHT * 2)
#define RA8876_PAGES         ((uint8_t)(RA8876_SDRAM_BYTES / RA8876_PAGE_BYTES))  // 13 at 1024 x 600
// The last two pages belong to the library: scrollRect() and beginComposite() work there.
#define RA8876_PAGE_SCRATCH  (RA8876_PAGES - 1)
#define RA8876_PAGE_COMPOSE  (RA8876_PAGES - 2)
#define RA8876_USER_PAGES    (RA8876_PAGES - 2)                                   // 0 .. 10

// Character ROM fonts: font numbers for setTextFont(), drawString(), drawNumber(), print() ...
// The controller draws these itself: 1-2 SPI bytes per character instead of every pixel.
// setTextSize(1..4) scales them in hardware. Characters: ISO 8859-1 (ASCII plus Latin-1).
#define ROM_FONT_16  16   //  8 x 16 pixels
#define ROM_FONT_24  24   // 12 x 24 pixels
#define ROM_FONT_32  32   // 16 x 32 pixels

/***************************************************************************************
**                         Section 3: Setup fonts
***************************************************************************************/
// Use GLCD font in error case where user requests a smooth font file
// that does not exist (this is a temporary fix to stop a reboot)
#ifdef SMOOTH_FONT
  #ifndef LOAD_GLCD
    #define LOAD_GLCD
  #endif
#endif

// Only load the fonts defined in RPiPico-RA8876_Config.h
// Set flag so RLE rendering code is optionally compiled
#ifdef LOAD_GLCD
  #include "Fonts/glcdfont.c"
#endif

#ifdef LOAD_FONT2
  #include "Fonts/Font16.h"
#endif

#ifdef LOAD_FONT4
  #include "Fonts/Font32rle.h"
  #define LOAD_RLE
#endif

#ifdef LOAD_FONT6
  #include "Fonts/Font64rle.h"
  #ifndef LOAD_RLE
    #define LOAD_RLE
  #endif
#endif

#ifdef LOAD_FONT7
  #include "Fonts/Font7srle.h"
  #ifndef LOAD_RLE
    #define LOAD_RLE
  #endif
#endif

#ifdef LOAD_FONT8
  #include "Fonts/Font72rle.h"
  #ifndef LOAD_RLE
    #define LOAD_RLE
  #endif
#elif defined LOAD_FONT8N // Optional narrower version
  #define LOAD_FONT8
  #include "Fonts/Font72x53rle.h"
  #ifndef LOAD_RLE
    #define LOAD_RLE
  #endif
#endif

#ifdef LOAD_GFXFF
  // We can include all the free fonts and they will only be built into
  // the sketch if they are used
  #include "Fonts/GFXFF/gfxfont.h"
  // Custom fonts
  #include "Fonts/Custom/Custom_Fonts.h"
#endif // #ifdef LOAD_GFXFF

// Create a null default font in case some fonts not used (to prevent crash)
const  uint8_t widtbl_null[1] = {0};
PROGMEM const uint8_t chr_null[1] = {0};
PROGMEM const uint8_t* const chrtbl_null[1] = {chr_null};

// This is a structure to conveniently hold information on the default fonts
// Stores pointer to font character image address table, width table and height
typedef struct {
    const uint8_t *chartbl;
    const uint8_t *widthtbl;
    uint8_t height;
    uint8_t baseline;
    } fontinfo;

// Now fill the structure
const PROGMEM fontinfo fontdata [] = {
  #ifdef LOAD_GLCD
   { (const uint8_t *)font, widtbl_null, 0, 0 },
  #else
   { (const uint8_t *)chrtbl_null, widtbl_null, 0, 0 },
  #endif
   // GLCD font (Font 1) does not have all parameters
   { (const uint8_t *)chrtbl_null, widtbl_null, 8, 7 },

  #ifdef LOAD_FONT2
   { (const uint8_t *)chrtbl_f16, widtbl_f16, chr_hgt_f16, baseline_f16},
  #else
   { (const uint8_t *)chrtbl_null, widtbl_null, 0, 0 },
  #endif

   // Font 3 current unused
   { (const uint8_t *)chrtbl_null, widtbl_null, 0, 0 },

  #ifdef LOAD_FONT4
   { (const uint8_t *)chrtbl_f32, widtbl_f32, chr_hgt_f32, baseline_f32},
  #else
   { (const uint8_t *)chrtbl_null, widtbl_null, 0, 0 },
  #endif

   // Font 5 current unused
   { (const uint8_t *)chrtbl_null, widtbl_null, 0, 0 },

  #ifdef LOAD_FONT6
   { (const uint8_t *)chrtbl_f64, widtbl_f64, chr_hgt_f64, baseline_f64},
  #else
   { (const uint8_t *)chrtbl_null, widtbl_null, 0, 0 },
  #endif

  #ifdef LOAD_FONT7
   { (const uint8_t *)chrtbl_f7s, widtbl_f7s, chr_hgt_f7s, baseline_f7s},
  #else
   { (const uint8_t *)chrtbl_null, widtbl_null, 0, 0 },
  #endif

  #ifdef LOAD_FONT8
   { (const uint8_t *)chrtbl_f72, widtbl_f72, chr_hgt_f72, baseline_f72}
  #else
   { (const uint8_t *)chrtbl_null, widtbl_null, 0, 0 }
  #endif
};

/***************************************************************************************
**                         Section 4: Font datum enumeration
***************************************************************************************/
//These enumerate the text plotting alignment (reference datum point)
#define TL_DATUM 0 // Top left (default)
#define TC_DATUM 1 // Top centre
#define TR_DATUM 2 // Top right
#define ML_DATUM 3 // Middle left
#define CL_DATUM 3 // Centre left, same as above
#define MC_DATUM 4 // Middle centre
#define CC_DATUM 4 // Centre centre, same as above
#define MR_DATUM 5 // Middle right
#define CR_DATUM 5 // Centre right, same as above
#define BL_DATUM 6 // Bottom left
#define BC_DATUM 7 // Bottom centre
#define BR_DATUM 8 // Bottom right
#define L_BASELINE  9 // Left character baseline (Line the 'A' character would sit on)
#define C_BASELINE 10 // Centre character baseline
#define R_BASELINE 11 // Right character baseline

/***************************************************************************************
**                         Section 5: Colour enumeration
***************************************************************************************/
// Default color definitions
#define TFT_BLACK       0x0000      /*   0,   0,   0 */
#define TFT_NAVY        0x000F      /*   0,   0, 128 */
#define TFT_DARKGREEN   0x03E0      /*   0, 128,   0 */
#define TFT_DARKCYAN    0x03EF      /*   0, 128, 128 */
#define TFT_MAROON      0x7800      /* 128,   0,   0 */
#define TFT_PURPLE      0x780F      /* 128,   0, 128 */
#define TFT_OLIVE       0x7BE0      /* 128, 128,   0 */
#define TFT_LIGHTGREY   0xD69A      /* 211, 211, 211 */
#define TFT_DARKGREY    0x7BEF      /* 128, 128, 128 */
#define TFT_BLUE        0x001F      /*   0,   0, 255 */
#define TFT_GREEN       0x07E0      /*   0, 255,   0 */
#define TFT_CYAN        0x07FF      /*   0, 255, 255 */
#define TFT_RED         0xF800      /* 255,   0,   0 */
#define TFT_MAGENTA     0xF81F      /* 255,   0, 255 */
#define TFT_YELLOW      0xFFE0      /* 255, 255,   0 */
#define TFT_WHITE       0xFFFF      /* 255, 255, 255 */
#define TFT_ORANGE      0xFDA0      /* 255, 180,   0 */
#define TFT_GREENYELLOW 0xB7E0      /* 180, 255,   0 */
#define TFT_PINK        0xFE19      /* 255, 192, 203 */ //Lighter pink, was 0xFC9F
#define TFT_BROWN       0x9A60      /* 150,  75,   0 */
#define TFT_GOLD        0xFEA0      /* 255, 215,   0 */
#define TFT_SILVER      0xC618      /* 192, 192, 192 */
#define TFT_SKYBLUE     0x867D      /* 135, 206, 235 */
#define TFT_VIOLET      0x915C      /* 180,  46, 226 */

// Next is a special 16-bit colour value that encodes to 8 bits
// and will then decode back to the same 16-bit value.
// Convenient for 8-bit and 16-bit transparent sprites.
#define TFT_TRANSPARENT 0x0120 // This is actually a dark green

// Default palette for 4-bit colour sprites
static const uint16_t default_4bit_palette[] PROGMEM = {
  TFT_BLACK,    //  0  ^
  TFT_BROWN,    //  1  |
  TFT_RED,      //  2  |
  TFT_ORANGE,   //  3  |
  TFT_YELLOW,   //  4  Colours 0-9 follow the resistor colour code!
  TFT_GREEN,    //  5  |
  TFT_BLUE,     //  6  |
  TFT_PURPLE,   //  7  |
  TFT_DARKGREY, //  8  |
  TFT_WHITE,    //  9  v
  TFT_CYAN,     // 10  Blue+green mix
  TFT_MAGENTA,  // 11  Blue+red mix
  TFT_MAROON,   // 12  Darker red colour
  TFT_DARKGREEN,// 13  Darker green colour
  TFT_NAVY,     // 14  Darker blue colour
  TFT_PINK      // 15
};

/***************************************************************************************
**                         Section 6: Diagnostic support
***************************************************************************************/
// This structure allows sketches to retrieve the setup parameters at runtime
// by calling getSetup(), zero impact on code size unless used, mainly for diagnostics
typedef struct setup_t
{
String  version = RPIPICO_RA8876_VERSION;
String  setup_info;  // Setup reference name
uint32_t setup_id;   // ID available to use in a user setup
int32_t esp;         // Processor code
uint8_t trans;       // SPI transaction support
uint8_t serial;      // Serial (SPI) or parallel
uint8_t  port;       // SPI port
uint8_t overlap;     // ESP8266 overlap mode
uint8_t interface;   // Interface type

uint16_t tft_driver; // Hexadecimal code
uint16_t tft_width;  // Rotation 0 width and height
uint16_t tft_height;

uint8_t r0_x_offset; // Display offsets, not all used yet
uint8_t r0_y_offset;
uint8_t r1_x_offset;
uint8_t r1_y_offset;
uint8_t r2_x_offset;
uint8_t r2_y_offset;
uint8_t r3_x_offset;
uint8_t r3_y_offset;

int8_t pin_tft_mosi; // SPI pins
int8_t pin_tft_miso;
int8_t pin_tft_clk;
int8_t pin_tft_cs;

int8_t pin_tft_dc;   // Control pins
int8_t pin_tft_rd;
int8_t pin_tft_wr;
int8_t pin_tft_rst;

int8_t pin_tft_d0;   // Parallel port pins
int8_t pin_tft_d1;
int8_t pin_tft_d2;
int8_t pin_tft_d3;
int8_t pin_tft_d4;
int8_t pin_tft_d5;
int8_t pin_tft_d6;
int8_t pin_tft_d7;

int8_t pin_tft_led;
int8_t pin_tft_led_on;

int8_t pin_tch_cs;   // Touch chip select pin

int16_t tft_spi_freq;// TFT write SPI frequency (units of 100 kHz)
int16_t tft_rd_freq; // TFT read  SPI frequency (units of 100 kHz)
int16_t tch_spi_freq;// Touch controller read/write SPI frequency
} setup_t;

/***************************************************************************************
**                         Section 7: Class member and support functions
***************************************************************************************/

// Callback prototype for smooth font pixel colour read
typedef uint16_t (*getColorCallback)(uint16_t x, uint16_t y);

// Class functions and variables
class RPiPico_RA8876 : public Print { friend class RPiPico_RA8876_Sprite; // Sprite class has access to protected members

 //--------------------------------------- public ------------------------------------//
 public:

  // Pins are GPIO numbers. mosi/sclk/miso must belong to the SPI port given (on the Pico:
  // spi1 TX = 11/15, SCK = 10/14, RX = 8/12). cs can be any GPIO, rst can be -1 if the reset
  // line is not driven by the Pico.
  RPiPico_RA8876(int8_t cs   = RA8876_DEFAULT_CS,   int8_t rst  = RA8876_DEFAULT_RST,
                 int8_t mosi = RA8876_DEFAULT_MOSI, int8_t sclk = RA8876_DEFAULT_SCLK,
                 int8_t miso = RA8876_DEFAULT_MISO, spi_inst_t *port = RA8876_DEFAULT_SPI);

  // SPI clock for writes and for display memory reads (call before init()). 0 keeps a value.
  void     setSPIFrequency(uint32_t write_hz, uint32_t read_hz = 0);
  void     setSPIMode(uint8_t mode);   // 0 or 3, before init()

  // init() and begin() are equivalent, begin() included for backwards compatibility.
  // The argument is ignored (TFT_eSPI uses it for ST7735 panels).
  void     init(uint8_t tc = 0), begin(uint8_t tc = 0);

  // false if the last init() found no panel -- the controller never reported its PLL locked
  // or its SDRAM ready within RA8876_INIT_TIMEOUT_MS. init() then returns instead of waiting
  // forever, so a sketch can run without its display (check this after init()). Nothing is
  // sent to the display while this is false.
  bool     panelFound(void) { return _panelFound; }

  // These are virtual so the RPiPico_RA8876_Sprite class can override them with sprite specific functions
  virtual void     drawPixel(int32_t x, int32_t y, uint32_t color),
                   drawChar(int32_t x, int32_t y, uint16_t c, uint32_t color, uint32_t bg, uint8_t size),
                   drawLine(int32_t xs, int32_t ys, int32_t xe, int32_t ye, uint32_t color),
                   drawFastVLine(int32_t x, int32_t y, int32_t h, uint32_t color),
                   drawFastHLine(int32_t x, int32_t y, int32_t w, uint32_t color),
                   fillRect(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t color);

  virtual int16_t  drawChar(uint16_t uniCode, int32_t x, int32_t y, uint8_t font),
                   drawChar(uint16_t uniCode, int32_t x, int32_t y),
                   height(void),
                   width(void);

                   // Read the colour of a pixel at x,y and return value in 565 format
  virtual uint16_t readPixel(int32_t x, int32_t y);

  virtual void     setWindow(int32_t xs, int32_t ys, int32_t xe, int32_t ye);   // Note: start + end coordinates

                   // Push (aka write pixel) colours to the set window
  virtual void     pushColor(uint16_t color);

                   // These are non-inlined to enable override
  virtual void     begin_nin_write();
  virtual void     end_nin_write();

  // RA8876: landscape only. The controller can neither swap rows and columns (portrait) nor
  // scan right to left; it can only flip its scan top to bottom, and its text engine and
  // picture-in-picture windows cannot follow a mirror done in software. So setRotation() keeps
  // rotation 0 whatever it is given (and says so once through setLogOutput()).
  void     setRotation(uint8_t r); // Kept for TFT_eSPI code: the rotation stays 0
  uint8_t  getRotation(void);      // Always 0

  // Change the origin position from the default top left
  // Note: setRotation, setViewport and resetViewport will revert origin to top left corner of screen/sprite
  void     setOrigin(int32_t x, int32_t y);
  int32_t  getOriginX(void);
  int32_t  getOriginY(void);

  void     invertDisplay(bool i);  // Not supported by the RA8876: does nothing

  // The RPiPico_RA8876_Sprite class inherits the following functions (not all are useful to Sprite class
  void     setAddrWindow(int32_t xs, int32_t ys, int32_t w, int32_t h); // Note: start coordinates + width and height

  // Viewport commands, see "Viewport_Demo" sketch
  void     setViewport(int32_t x, int32_t y, int32_t w, int32_t h, bool vpDatum = true);
  bool     checkViewport(int32_t x, int32_t y, int32_t w, int32_t h);
  int32_t  getViewportX(void);
  int32_t  getViewportY(void);
  int32_t  getViewportWidth(void);
  int32_t  getViewportHeight(void);
  bool     getViewportDatum(void);
  void     frameViewport(uint16_t color, int32_t w);
  void     resetViewport(void);

           // Clip input window to viewport bounds, return false if whole area is out of bounds
  bool     clipAddrWindow(int32_t* x, int32_t* y, int32_t* w, int32_t* h);
           // Clip input window area to viewport bounds, return false if whole area is out of bounds
  bool     clipWindow(int32_t* xs, int32_t* ys, int32_t* xe, int32_t* ye);

           // Push (aka write pixel) colours to the TFT (use setAddrWindow() first)
  void     pushColor(uint16_t color, uint32_t len),  // Deprecated, use pushBlock()
           pushColors(uint16_t  *data, uint32_t len, bool swap = true), // With byte swap option
           pushColors(uint8_t  *data, uint32_t len); // Deprecated, use pushPixels()

           // Write a solid block of a single colour
  void     pushBlock(uint16_t color, uint32_t len);

           // Write a set of pixels stored in memory, use setSwapBytes(true/false) function to correct endianess
  void     pushPixels(const void * data_in, uint32_t len);

  // Graphics drawing
  void     fillScreen(uint32_t color),
           drawRect(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t color),
           drawRoundRect(int32_t x, int32_t y, int32_t w, int32_t h, int32_t radius, uint32_t color),
           fillRoundRect(int32_t x, int32_t y, int32_t w, int32_t h, int32_t radius, uint32_t color);

  void     fillRectVGradient(int16_t x, int16_t y, int16_t w, int16_t h, uint32_t color1, uint32_t color2);
  void     fillRectHGradient(int16_t x, int16_t y, int16_t w, int16_t h, uint32_t color1, uint32_t color2);

  void     drawCircle(int32_t x, int32_t y, int32_t r, uint32_t color),
           drawCircleHelper(int32_t x, int32_t y, int32_t r, uint8_t cornername, uint32_t color),
           fillCircle(int32_t x, int32_t y, int32_t r, uint32_t color),
           fillCircleHelper(int32_t x, int32_t y, int32_t r, uint8_t cornername, int32_t delta, uint32_t color),

           drawEllipse(int16_t x, int16_t y, int32_t rx, int32_t ry, uint16_t color),
           fillEllipse(int16_t x, int16_t y, int32_t rx, int32_t ry, uint16_t color),

           //                 Corner 1               Corner 2               Corner 3
           drawTriangle(int32_t x1,int32_t y1, int32_t x2,int32_t y2, int32_t x3,int32_t y3, uint32_t color),
           fillTriangle(int32_t x1,int32_t y1, int32_t x2,int32_t y2, int32_t x3,int32_t y3, uint32_t color);


  // Smooth (anti-aliased) graphics drawing
           // Draw a pixel blended with the background pixel colour (bg_color) specified,  return blended colour
           // If the bg_color is not specified, the background pixel colour will be read from the display or sprite
  uint16_t drawPixel(int32_t x, int32_t y, uint32_t color, uint8_t alpha, uint32_t bg_color = 0x00FFFFFF);

           // Draw an anti-aliased (smooth) arc between start and end angles. Arc ends are anti-aliased.
           // By default the arc is drawn with square ends unless the "roundEnds" parameter is included and set true
           // Angle = 0 is at 6 o'clock position, 90 at 9 o'clock etc. The angles must be in range 0-360 or they will be clipped to these limits
           // The start angle may be larger than the end angle. Arcs are always drawn clockwise from the start angle.
  void     drawSmoothArc(int32_t x, int32_t y, int32_t r, int32_t ir, uint32_t startAngle, uint32_t endAngle, uint32_t fg_color, uint32_t bg_color, bool roundEnds = false);

           // As per "drawSmoothArc" except the ends of the arc are NOT anti-aliased, this facilitates dynamic arc length changes with
           // arc segments and ensures clean segment joints.
           // The sides of the arc are anti-aliased by default. If smoothArc is false sides will NOT be anti-aliased
  void     drawArc(int32_t x, int32_t y, int32_t r, int32_t ir, uint32_t startAngle, uint32_t endAngle, uint32_t fg_color, uint32_t bg_color, bool smoothArc = true);

           // Draw an anti-aliased filled circle at x, y with radius r
           // Note: The thickness of line is 3 pixels to reduce the visible "braiding" effect of anti-aliasing narrow lines
           //       this means the inner anti-alias zone is always at r-1 and the outer zone at r+1
  void     drawSmoothCircle(int32_t x, int32_t y, int32_t r, uint32_t fg_color, uint32_t bg_color);

           // Draw an anti-aliased filled circle at x, y with radius r
           // If bg_color is not included the background pixel colour will be read from the display or sprite
  void     fillSmoothCircle(int32_t x, int32_t y, int32_t r, uint32_t color, uint32_t bg_color = 0x00FFFFFF);

           // Draw a rounded rectangle that has a line thickness of r-ir+1 and bounding box defined by x,y and w,h
           // The outer corner radius is r, inner corner radius is ir
           // The inside and outside of the border are anti-aliased
  void     drawSmoothRoundRect(int32_t x, int32_t y, int32_t r, int32_t ir, int32_t w, int32_t h, uint32_t fg_color, uint32_t bg_color = 0x00FFFFFF, uint8_t quadrants = 0xF);

           // Draw a filled rounded rectangle , corner radius r and bounding box defined by x,y and w,h
  void     fillSmoothRoundRect(int32_t x, int32_t y, int32_t w, int32_t h, int32_t radius, uint32_t color, uint32_t bg_color = 0x00FFFFFF);

           // Draw a small anti-aliased filled circle at ax,ay with radius r (uses drawWideLine)
           // If bg_color is not included the background pixel colour will be read from the display or sprite
  void     drawSpot(float ax, float ay, float r, uint32_t fg_color, uint32_t bg_color = 0x00FFFFFF);

           // Draw an anti-aliased wide line from ax,ay to bx,by width wd with radiused ends (radius is wd/2)
           // If bg_color is not included the background pixel colour will be read from the display or sprite
  void     drawWideLine(float ax, float ay, float bx, float by, float wd, uint32_t fg_color, uint32_t bg_color = 0x00FFFFFF);

           // Draw an anti-aliased wide line from ax,ay to bx,by with different width at each end aw, bw and with radiused ends
           // If bg_color is not included the background pixel colour will be read from the display or sprite
  void     drawWedgeLine(float ax, float ay, float bx, float by, float aw, float bw, uint32_t fg_color, uint32_t bg_color = 0x00FFFFFF);


  // Image rendering
           // Swap the byte order for pushImage() and pushPixels() - corrects endianness.
           // As in TFT_eSPI: true for arrays of ordinary RGB565 values (e.g. 0xF800 = red),
           // false (the default) for arrays already stored byte-swapped (sprite buffers, readRect()).
  void     setSwapBytes(bool swap);
  bool     getSwapBytes(void);

           // Draw bitmap
  void     drawBitmap( int16_t x, int16_t y, const uint8_t *bitmap, int16_t w, int16_t h, uint16_t fgcolor),
           drawBitmap( int16_t x, int16_t y, const uint8_t *bitmap, int16_t w, int16_t h, uint16_t fgcolor, uint16_t bgcolor),
           drawXBitmap(int16_t x, int16_t y, const uint8_t *bitmap, int16_t w, int16_t h, uint16_t fgcolor),
           drawXBitmap(int16_t x, int16_t y, const uint8_t *bitmap, int16_t w, int16_t h, uint16_t fgcolor, uint16_t bgcolor),
           setBitmapColor(uint16_t fgcolor, uint16_t bgcolor); // Define the 2 colours for 1bpp sprites

           // Set TFT pivot point (use when rendering rotated sprites)
  void     setPivot(int16_t x, int16_t y);
  int16_t  getPivotX(void), // Get pivot x
           getPivotY(void); // Get pivot y

           // The next functions can be used as a pair to copy screen blocks (or horizontal/vertical lines) to another location
           // Read a block of pixels to a data buffer, buffer is 16-bit and the size must be at least w * h
           // (colours are byte-swapped, ready for pushRect(); readPixel() returns a plain RGB565 value)
  void     readRect(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t *data);
           // Write a block of pixels to the screen which have been read by readRect()
  void     pushRect(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t *data);

           // These are used to render images or sprites stored in RAM arrays (used by Sprite class for 16bpp Sprites)
  void     pushImage(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t *data);
  void     pushImage(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t *data, uint16_t transparent);

           // These are used to render images stored in FLASH (PROGMEM)
  void     pushImage(int32_t x, int32_t y, int32_t w, int32_t h, const uint16_t *data, uint16_t transparent);
  void     pushImage(int32_t x, int32_t y, int32_t w, int32_t h, const uint16_t *data);

           // These are used by Sprite class pushSprite() member function for 1, 4 and 8 bits per pixel (bpp) colours
           // They are not intended to be used with user sketches (but could be)
           // Set bpp8 true for 8bpp sprites, false otherwise. The cmap pointer must be specified for 4bpp
  void     pushImage(int32_t x, int32_t y, int32_t w, int32_t h, uint8_t  *data, bool bpp8 = true, uint16_t *cmap = nullptr);
  void     pushImage(int32_t x, int32_t y, int32_t w, int32_t h, uint8_t  *data, uint8_t  transparent, bool bpp8 = true, uint16_t *cmap = nullptr);
           // FLASH version
  void     pushImage(int32_t x, int32_t y, int32_t w, int32_t h, const uint8_t *data, bool bpp8,  uint16_t *cmap = nullptr);

           // Render a 16-bit colour image with a 1bpp mask
  void     pushMaskedImage(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t *img, uint8_t *mask);

           // Reads a screen area and returns the 3 RGB 8-bit colour values of each pixel in the buffer
           // Set w and h to 1 to read 1 pixel's colour. The data buffer must be at least w * h * 3 bytes
  void     readRectRGB(int32_t x, int32_t y, int32_t w, int32_t h, uint8_t *data);


  // Text rendering - value returned is the pixel width of the rendered text
  int16_t  drawNumber(long intNumber, int32_t x, int32_t y, uint8_t font), // Draw integer using specified font number
           drawNumber(long intNumber, int32_t x, int32_t y),               // Draw integer using current font

           // Decimal is the number of decimal places to render
           // Use with setTextDatum() to position values on TFT, and setTextPadding() to blank old displayed values
           drawFloat(float floatNumber, uint8_t decimal, int32_t x, int32_t y, uint8_t font), // Draw float using specified font number
           drawFloat(float floatNumber, uint8_t decimal, int32_t x, int32_t y),               // Draw float using current font

           // Handle char arrays
           // Use with setTextDatum() to position string on TFT, and setTextPadding() to blank old displayed strings
           drawString(const char *string, int32_t x, int32_t y, uint8_t font),  // Draw string using specified font number
           drawString(const char *string, int32_t x, int32_t y),                // Draw string using current font
           drawString(const String& string, int32_t x, int32_t y, uint8_t font),// Draw string using specified font number
           drawString(const String& string, int32_t x, int32_t y),              // Draw string using current font

           drawCentreString(const char *string, int32_t x, int32_t y, uint8_t font),  // Deprecated, use setTextDatum() and drawString()
           drawRightString(const char *string, int32_t x, int32_t y, uint8_t font),   // Deprecated, use setTextDatum() and drawString()
           drawCentreString(const String& string, int32_t x, int32_t y, uint8_t font),// Deprecated, use setTextDatum() and drawString()
           drawRightString(const String& string, int32_t x, int32_t y, uint8_t font); // Deprecated, use setTextDatum() and drawString()


  // Text rendering and font handling support functions
  void     setCursor(int16_t x, int16_t y),                 // Set cursor for tft.print()
           setCursor(int16_t x, int16_t y, uint8_t font);   // Set cursor and font number for tft.print()

  int16_t  getCursorX(void),                                // Read current cursor x position (moves with tft.print())
           getCursorY(void);                                // Read current cursor y position

  void     setTextColor(uint16_t color),                    // Set character (glyph) color only (background not over-written)
           setTextColor(uint16_t fgcolor, uint16_t bgcolor, bool bgfill = false),  // Set character (glyph) foreground and background colour, optional background fill for smooth fonts
           setTextSize(uint8_t size);                       // Set character size multiplier (this increases pixel size)

  void     setTextWrap(bool wrapX, bool wrapY = false);     // Turn on/off wrapping of text in TFT width and/or height

  void     setTextDatum(uint8_t datum);                     // Set text datum position (default is top left), see Section 4 above
  uint8_t  getTextDatum(void);

  void     setTextPadding(uint16_t x_width);                // Set text padding (background blanking/over-write) width in pixels
  uint16_t getTextPadding(void);                            // Get text padding

#ifdef LOAD_GFXFF
  void     setFreeFont(const GFXfont *f = NULL),            // Select the GFX Free Font
           setTextFont(uint8_t font);                       // Set the font number to use in future
#else
  void     setFreeFont(uint8_t font),                       // Not used, historical fix to prevent an error
           setTextFont(uint8_t font);                       // Set the font number to use in future
#endif

  int16_t  textWidth(const char *string, uint8_t font),     // Returns pixel width of string in specified font
           textWidth(const char *string),                   // Returns pixel width of string in current font
           textWidth(const String& string, uint8_t font),   // As above for String types
           textWidth(const String& string),
           fontHeight(uint8_t font),                        // Returns pixel height of specified font
           fontHeight(void);                                // Returns pixel height of current font

           // Used by library and Smooth font class to extract Unicode point codes from a UTF8 encoded string
  uint16_t decodeUTF8(uint8_t *buf, uint16_t *index, uint16_t remaining),
           decodeUTF8(uint8_t c);

           // Support function to UTF8 decode and draw characters piped through print stream
  size_t   write(uint8_t) override;
  size_t   write(const uint8_t *buf, size_t len) override;  // ROM fonts: one text write per run
  using    Print::write;

           // Used by Smooth font class to fetch a pixel colour for the anti-aliasing
  void     setCallback(getColorCallback getCol);

  uint16_t fontsLoaded(void); // Each bit in returned value represents a font type that is loaded - used for debug/error handling only


  // Low level read/write
  void     spiwrite(uint8_t);        // legacy: selects RA8876 register c (same as writecommand)
  void     writecommand(uint8_t c);  // Select RA8876 register c
  void     writedata(uint8_t d);     // Write d to the selected RA8876 register

  void     commandList(const uint8_t *addr); // Send an initialisation sequence stored in FLASH

  uint8_t  readcommand8( uint8_t cmd_function, uint8_t index = 0); // RA8876: read register cmd_function + index
  uint16_t readcommand16(uint8_t cmd_function, uint8_t index = 0); // two registers, cmd_function+index first (high byte)
  uint32_t readcommand32(uint8_t cmd_function, uint8_t index = 0); // four registers


  // Colour conversion
           // Convert 8-bit red, green and blue to 16 bits
  uint16_t color565(uint8_t red, uint8_t green, uint8_t blue);

           // Convert 8-bit colour to 16 bits
  uint16_t color8to16(uint8_t color332);
           // Convert 16-bit colour to 8 bits
  uint8_t  color16to8(uint16_t color565);

           // Convert 16-bit colour to/from 24-bit, R+G+B concatenated into LS 24 bits
  uint32_t color16to24(uint16_t color565);
  uint32_t color24to16(uint32_t color888);

           // Alpha blend 2 colours, see generic "alphaBlend_Test" example
           // alpha =   0 = 100% background colour
           // alpha = 255 = 100% foreground colour
  uint16_t alphaBlend(uint8_t alpha, uint16_t fgc, uint16_t bgc);

           // 16-bit colour alphaBlend with alpha dither (dither reduces colour banding)
  uint16_t alphaBlend(uint8_t alpha, uint16_t fgc, uint16_t bgc, uint8_t dither);
           // 24-bit colour alphaBlend with optional alpha dither
  uint32_t alphaBlend24(uint8_t alpha, uint32_t fgc, uint32_t bgc, uint8_t dither = 0);

  // Direct Memory Access (DMA) support functions
           // DMA transfer to the display is done while the processor moves on to other tasks.
           // Do not modify or free a buffer while it is being sent; use dmaBusy() to check. Use
           // startWrite() before invoking DMA; endWrite() waits for the DMA to complete.
  bool     initDMA(bool ctrl_cs = false);  // Claim a DMA channel for the SPI port - typically used in setup()
  void     deInitDMA(void);   // Release the DMA channel - typically not used

           // Push an image to the display using DMA, buffer is optional and grabs (double buffers) a copy of the image
           // Use the buffer if the image data will get over-written or destroyed while DMA is in progress
           // Note 1: If swapping colour bytes is set, and the double buffer option is NOT used, then the bytes
           // in the original image buffer content will be byte swapped by the function before DMA is initiated.
           // Note 2: If part of the image will be off screen or outside of a set viewport, then the the original
           // image buffer content will be altered to a correctly clipped image before DMA is initiated.
  void     pushImageDMA(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t* data, uint16_t* buffer = nullptr);

           // Push a block of pixels into a window set up using setAddrWindow()
  void     pushPixelsDMA(uint16_t* image, uint32_t len);

           // Check if the DMA is complete - use while(tft.dmaBusy); for a blocking wait
  bool     dmaBusy(void); // returns true if DMA is still in progress
  void     dmaWait(void); // wait until DMA is complete

  bool     DMA_Enabled = false;   // Flag for DMA enabled state
  uint8_t  spiBusyCheck = 0;      // Not used on the RP2040, kept for compatibility

  // Bare metal functions
  void     startWrite(void);                         // Begin SPI transaction
  void     writeColor(uint16_t color, uint32_t len); // Deprecated, use pushBlock()
  void     endWrite(void);                           // End SPI transaction

  // Set/get an arbitrary library configuration attribute or option
  //       Use to switch ON/OFF capabilities such as UTF8 decoding - each attribute has a unique ID
  //       id = 0: reserved - may be used in future to reset all attributes to a default state
  //       id = 1: Turn on (a=true) or off (a=false) GLCD cp437 font character error correction
  //       id = 2: Turn on (a=true) or off (a=false) UTF8 decoding
  //       id = 3: Enable or disable use of PSRAM (if available)
           #define CP437_SWITCH 1
           #define UTF8_SWITCH  2
           #define PSRAM_ENABLE 3
  void     setAttribute(uint8_t id = 0, uint8_t a = 0); // Set attribute value
  uint8_t  getAttribute(uint8_t id = 0);                // Get attribute value

           // Used for diagnostic sketch to see library setup
  void     getSetup(setup_t& tft_settings); // Sketch provides the instance to populate
  bool     verifySetupID(uint32_t id);

  spi_inst_t *getSPIport(void) { return _spi; }    // The pico-sdk SPI port the display is on

           // Where the library's few messages go (init result, problems). Default Serial;
           // nullptr for none. Call before init().
  void     setLogOutput(Print *out) { _log = out; }

  //=================================== RA8876 hardware ===================================//
  // All coordinates below are display memory pixels: they ignore viewports and setOrigin().
  // Every function waits for the controller to finish before it returns, so ordinary drawing
  // can follow at once. If the controller does not finish within RA8876_ENGINE_TIMEOUT_MS the
  // operation is abandoned and engineErrors() counts it.

  // ---- Hardware fill -----------------------------------------------------------------------
  // fillRect()/fillScreen() use the controller's fill engine for rectangles of at least
  // minPixels pixels (2 x 2 or larger). enable = false sends every pixel (as TFT_eSPI does).
  void     setHardwareFill(bool enable, uint32_t minPixels = RA8876_HW_FILL_MIN_PIXELS);
  bool     getHardwareFill(void) { return _hwFillOn; }
           // Fill a rectangle of the drawing page in hardware (w and h at least 2)
  bool     hwFillRect(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t color);

  // ---- Pages -------------------------------------------------------------------------------
  // Display memory holds RA8876_PAGES whole screens. Pages 0 .. RA8876_USER_PAGES-1 are free
  // for the sketch; the last two are used by scrollRect() and beginComposite().
  // At start-up page 0 is both shown and drawn on.
  uint32_t pageAddress(uint8_t page);         // byte address in display memory
  void     setDrawPage(uint8_t page);         // all drawing (TFT_eSPI and hardware) goes here
  uint8_t  getDrawPage(void) { return _drawPage; }
  void     showPage(uint8_t page, bool vsync = true);  // what the panel shows; vsync: switch between frames
  uint8_t  getShowPage(void) { return _showPage; }
  bool     waitForVSync(uint32_t timeout_ms = 40);     // false if no vertical sync was seen
  void     fillPage(uint8_t page, uint16_t color);     // whole page, in hardware
  void     copyPage(uint8_t srcPage, uint8_t dstPage); // whole page, in hardware

  // ---- Block copies (block transfer engine) ------------------------------------------------
  // Copy a w x h block. Source and destination may be the same page but must not overlap
  // (scrollRect() handles overlapping moves).
  bool     copyRect(uint8_t srcPage, int32_t sx, int32_t sy,
                    uint8_t dstPage, int32_t dx, int32_t dy, int32_t w, int32_t h);
           // As copyRect(), but source pixels of colour transparentColor are not copied
  bool     copyRectTransparent(uint8_t srcPage, int32_t sx, int32_t sy,
                               uint8_t dstPage, int32_t dx, int32_t dy, int32_t w, int32_t h,
                               uint16_t transparentColor);
           // dst = (src0 * alpha + src1 * (32 - alpha)) / 32, alpha 0..32
  bool     blendRect(uint8_t page0, int32_t x0, int32_t y0,
                     uint8_t page1, int32_t x1, int32_t y1,
                     uint8_t dstPage, int32_t dx, int32_t dy, int32_t w, int32_t h, uint8_t alpha);

  // ---- Scrolling ---------------------------------------------------------------------------
  // Move the contents of a rectangle of the drawing page by dx, dy pixels (positive = right,
  // down) in hardware; the strip uncovered is filled with fillColor (pass -1 to leave it).
  // Straight up is one block copy; other directions go through the scratch page (two).
  bool     scrollRect(int32_t x, int32_t y, int32_t w, int32_t h, int32_t dx, int32_t dy, int32_t fillColor = -1);

  // ---- Flicker-free updates ----------------------------------------------------------------
  // Draw between these two calls and the result appears in one step: drawing goes to an
  // off-screen copy of the rectangle, and endComposite() copies it onto the drawing page in
  // hardware. copyBackground = true starts the copy from what is there now; false when the
  // drawing covers the whole rectangle. Use ordinary coordinates; calls do not nest.
  void     beginComposite(int32_t x, int32_t y, int32_t w, int32_t h, bool copyBackground = true);
  void     endComposite(void);
  bool     inComposite(void) { return _composite; }

  // ---- Picture-in-picture ------------------------------------------------------------------
  // Show a w x h block of srcPage at dx, dy on top of the shown page (PIP 1 is above PIP 2).
  // The overlay costs no drawing: change it by drawing on srcPage, move it with pipMove().
  // x positions and widths are rounded down to multiples of 4 (a controller limit).
  bool     pipShow(uint8_t pip, uint8_t srcPage, int32_t sx, int32_t sy,
                   int32_t dx, int32_t dy, int32_t w, int32_t h);
  void     pipMove(uint8_t pip, int32_t dx, int32_t dy);
  void     pipHide(uint8_t pip);

  // ---- Diagnostics and raw access ----------------------------------------------------------
  uint8_t  readStatus(void);                    // the RA8876 status register
  void     writeRegister(uint8_t reg, uint8_t value);
  uint8_t  readRegister(uint8_t reg);
  uint32_t engineErrors(void) { return _engineErrors; }  // operations abandoned on timeout
  void     setStreamOptimisation(bool enable);  // reuse the controller's write position between
                                                // pixels (default on); off for testing

  // Global variables
  uint32_t textcolor, textbgcolor;         // Text foreground and background colours

  uint32_t bitmap_fg, bitmap_bg;           // Bitmap foreground (bit=1) and background (bit=0) colours

  uint8_t  textfont,  // Current selected font number
           textsize,  // Current font size multiplier
           textdatum, // Text reference datum
           rotation;  // Display rotation (always 0 on the RA8876)

  uint8_t  decoderState = 0;   // UTF8 decoder state        - not for user access
  uint16_t decoderBuffer;      // Unicode code-point buffer - not for user access

 //--------------------------------------- private ------------------------------------//
 private:
           // Legacy begin and end prototypes - deprecated TODO: delete
  void     spi_begin();
  void     spi_end();

  void     spi_begin_read();
  void     spi_end_read();

           // begin/end a write transaction. The RA8876 needs chip select toggled around every
           // command, so CS is driven by the command and stream functions, not here: a
           // transaction only lets a pixel stream stay open from one call to the next.
  inline void begin_tft_write() __attribute__((always_inline)) {
    if (locked) locked = false;
  }
  inline void end_tft_write()   __attribute__((always_inline)) {
    if (!inTransaction && !locked) {   // inTransaction: keep the stream open across calls
      locked = true;
      _streamEnd();
    }
  }

           // begin/end a read transaction: display memory reads may use a slower SPI clock
  inline void begin_tft_read()  __attribute__((always_inline)) {
    _streamEnd();                      // also waits for a DMA transfer to finish
    if (locked) locked = false;
    if (_spiReadHz != _spiWriteHz) spi_set_baudrate(_spi, _spiReadHz);
  }
  inline void end_tft_read()    __attribute__((always_inline)) {
    if (_spiReadHz != _spiWriteHz) spi_set_baudrate(_spi, _spiWriteHz);
    if (!inTransaction && !locked) locked = true;
  }

           // Initialise the data bus GPIO and hardware interfaces
  bool     initBus(void);

           // Smooth graphics helper
  uint8_t  sqrt_fraction(uint32_t num);

           // Helper function: calculate distance of a point from a finite length line between two points
  float    wedgeLineDistance(float pax, float pay, float bax, float bay, float dr);

  getColorCallback getColor = nullptr; // Smooth font callback function pointer

  bool     locked, inTransaction, lockTransaction; // SPI transaction and mutex lock flags

  //------------------------------- RA8876 bus and engine ---------------------------------//
  // One RA8876 command: CS low, a 16-bit frame (prefix byte + payload byte), CS high.
  // Returns the byte clocked in during the payload (the answer to a read).
  uint8_t  _xfer(uint16_t frame);
  void     _selectReg(uint8_t reg);             // skipped when reg is already selected
  void     _regWrite(uint8_t reg, uint8_t val);
  void     _regWriteCached(uint8_t reg, uint8_t val);  // skipped when the shadow copy matches
  uint8_t  _regRead(uint8_t reg);
  void     _reg16(uint8_t reg, uint16_t val);   // reg = low byte, reg + 1 = high byte (cached)
  void     _reg32(uint8_t reg, uint32_t val);   // four bytes from reg (cached)
  void     _streamBegin(void);                  // CS low + data-write prefix: pixels may follow
  void     _streamEnd(void);                    // wait for the SPI to drain, CS high
  void     _invalidateCaches(void);
  bool     _waitStatus(uint8_t mask, uint8_t want, uint32_t timeout_ms);
  bool     _waitEngine(void);                   // geometry engine / BTE / text write finished
  void     _graphicMode(void);
  void     _textMode(void);
  void     _setColorRegs(uint8_t reg, uint16_t color); // 0xD2 foreground or 0xD5 background
  void     _activeWindow(int32_t x, int32_t y, int32_t w, int32_t h);
  void     _activeWindowFull(void);
  void     _setCursor(int32_t x, int32_t y);
  bool     _predictedPos(int32_t &x, int32_t &y);   // where the write position is now, if known
  bool     _cursorPredicted(int32_t x, int32_t y);
  void     _hwPrepare(void);                    // before any engine operation
  bool     _bteRun(uint8_t ctrl1, uint32_t s0, int32_t s0x, int32_t s0y,
                   uint32_t s1, int32_t s1x, int32_t s1y,
                   uint32_t dst, int32_t dx, int32_t dy, int32_t w, int32_t h);
  bool     _hwFill(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t color);
  void     _ra8876Init(void);                   // the register sequence, called by init()
  void     _ra8876Rotation(uint8_t m);
  uint16_t _readMemPixel(void);                 // next pixel from the memory read port

  // ROM fonts
  static bool isRomFont(uint8_t f) { return f == ROM_FONT_16 || f == ROM_FONT_24 || f == ROM_FONT_32; }
  // Only the controller can draw ROM fonts, so sprites use the nearest built-in font instead
  uint8_t  _mapFont(uint8_t f) { return (isRomFont(f) && !_drawsOnDisplay()) ? (f == ROM_FONT_16 ? 2 : 4) : f; }
  int16_t  _romCharWidth(uint8_t f)  { return (f == ROM_FONT_16 ? 8 : f == ROM_FONT_24 ? 12 : 16); }
  uint8_t  _romScale(void)           { return textsize > 4 ? 4 : (textsize ? textsize : 1); }
  int16_t  _romText(int32_t x, int32_t y, const char *s, uint16_t len, uint8_t f); // UTF-8; returns width
  int16_t  _romTextRaw(int32_t x, int32_t y, const uint8_t *codes, uint16_t n, uint8_t f); // ISO 8859-1 codes
  uint8_t  _romCode(uint16_t uniCode) { return (uniCode >= 0x20 && uniCode <= 0xFF && (uniCode < 0x7F || uniCode >= 0xA0)) ? (uint8_t)uniCode : '?'; }

  // Pins and SPI
  int8_t       _pin_cs, _pin_rst, _pin_mosi, _pin_sclk, _pin_miso;
  spi_inst_t  *_spi;
  spi_hw_t    *_spiHw;
  uint32_t     _spiWriteHz, _spiReadHz;
  uint8_t      _spiMode;

  // Register shadows: 0 .. 255, valid when the bit in _regValid is set
  uint8_t      _regShadow[256];
  uint32_t     _regValid[8];
  int16_t      _selectedReg;      // register the next data byte goes to, -1 unknown
  bool         _streamOpen;       // CS low with the data-write prefix sent
  bool         _textModeOn;

  // Write position prediction (see setWindow())
  bool         _streamOpt;
  bool         _curValid;
  int16_t      _curRegXHi, _curRegYHi;      // high bytes last written to the position registers, -1 unknown
  int32_t      _curX, _curY;                 // where the stream started
  int32_t      _winX, _winY, _winW, _winH;   // active window the stream runs in
  uint32_t     _pixCount;                    // pixels sent since the stream started

  // Hardware state
  bool         _hwFillOn;
  uint32_t     _hwFillMin;
  uint8_t      _drawPage, _showPage;
  uint8_t      _mpwctr;                      // shadow of RA8876_MPWCTR (PIP enables)
  bool         _composite;
  int32_t      _compX, _compY, _compW, _compH;
  uint8_t      _compPrevPage;
  uint32_t     _engineErrors;
  bool         _vsyncWorks;
  uint8_t      _vsyncEnable;   // 0 untried, 1 flag works with the interrupt disabled, 2 needs it enabled

 //-------------------------------------- protected ----------------------------------//
 protected:

  int32_t  _init_width, _init_height; // Display w/h as input, used by setRotation()
  int32_t  _width, _height;           // Display w/h as modified by current rotation
  int32_t  addr_row, addr_col;        // Window position - used to minimise window commands

  int16_t  _xPivot;   // TFT x pivot point coordinate for rotated Sprites
  int16_t  _yPivot;   // TFT x pivot point coordinate for rotated Sprites

  // Viewport variables
  int32_t  _vpX, _vpY, _vpW, _vpH;    // Note: x start, y start, x end + 1, y end + 1
  int32_t  _xDatum;
  int32_t  _yDatum;
  int32_t  _xWidth;
  int32_t  _yHeight;
  bool     _vpDatum;
  bool     _vpOoB;

  int32_t  cursor_x, cursor_y, padX;       // Text cursor x,y and padding setting
  int32_t  bg_cursor_x;                    // Background fill cursor
  int32_t  last_cursor_x;                  // Previous text cursor position when fill used

  uint32_t fontsloaded;               // Bit field of fonts loaded

  uint8_t  glyph_ab,   // Smooth font glyph delta Y (height) above baseline
           glyph_bb;   // Smooth font glyph delta Y (height) below baseline

  bool     isDigits;   // adjust bounding box for numbers to reduce visual jiggling
  bool     textwrapX, textwrapY;  // If set, 'wrap' text at right and optionally bottom edge of display
  bool     _swapBytes; // Swap the byte order for TFT pushImage()

  bool     _booted;    // init() or begin() has already run once
  bool     _panelFound = false;  // the last init() got answers from the controller

                       // User sketch manages these via set/getAttribute()
  bool     _cp437;        // If set, use correct CP437 charset (default is OFF)
  bool     _utf8;         // If set, use UTF-8 decoder in print stream 'write()' function (default ON)
  bool     _psram_enable; // Enable PSRAM use for library functions (TBD) and Sprites

  uint32_t _lastColor; // Buffered value of last colour used

  bool     _fillbg;    // Fill background flag (just for for smooth fonts at the moment)

  Print   *_log = &Serial;   // setLogOutput()

  // false in RPiPico_RA8876_Sprite: drawing goes to RAM, not to the controller
  virtual bool _drawsOnDisplay(void) { return true; }

  // DMA
  int32_t            dma_tx_channel;
  dma_channel_config dma_tx_config;

#ifdef LOAD_GFXFF
  GFXfont  *gfxFont;
#endif

/***************************************************************************************
**                         Section 8: Class conditional extensions
***************************************************************************************/
// Load the Anti-aliased font extension
#ifdef SMOOTH_FONT
  #include "Extensions/Smooth_font.h"  // Loaded if SMOOTH_FONT is defined by user
#endif

}; // End of class RPiPico_RA8876

// Swap any type
template <typename T> static inline void
transpose(T& a, T& b) { T t = a; a = b; b = t; }

// Fast alphaBlend
template <typename A, typename F, typename B> static inline uint16_t
fastBlend(A alpha, F fgc, B bgc)
{
  // Split out and blend 5-bit red and blue channels
  uint32_t rxb = bgc & 0xF81F;
  rxb += ((fgc & 0xF81F) - rxb) * (alpha >> 2) >> 6;
  // Split out and blend 6-bit green channel
  uint32_t xgx = bgc & 0x07E0;
  xgx += ((fgc & 0x07E0) - xgx) * alpha >> 8;
  // Recombine channels
  return (rxb & 0xF81F) | (xgx & 0x07E0);
}

/***************************************************************************************
**                         Section 9: Additional extension classes
***************************************************************************************/
// Load the Button Class
#include "Extensions/Button.h"

// Load the Sprite Class
#include "Extensions/Sprite.h"

#endif // ends #ifndef _RPIPICO_RA8876_H_
