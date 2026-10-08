/*
  HardwareTest -- bench test for the RPiPico-RA8876 library.

  Hardware: Raspberry Pi Pico / Pico 2 + ER-TFTM101-1 (or another 1024 x 600 RA8876 module).
  Wiring below is the library default (the LCC RPi Pico boards' IO1 header, SPI1).

  Open the Serial Monitor at 115200 baud, with its line ending set to "Newline" (or "Both NL & CR").
  Each test:
    - says on the screen and in the Serial Monitor what you should see;
    - reads pixels back from the display where it can and prints PASS or FAIL;
    - asks about the things only your eyes can judge (LOOK): type y or n, and a note after it
      if something looked wrong, then Enter. The test waits for your answer.
  After each test: Enter for the next one, r + Enter to repeat it. Nothing moves on by itself.
  The summary at the end lists every result, answer and timing: paste all of it into your report.
*/

#include <RPiPico-RA8876.h>

//                cs  rst mosi sclk miso port
RPiPico_RA8876 tft(9, 14, 11,  10,  8,   spi1);

// ---------------------------------------------------------------------------------------
// Result log
// ---------------------------------------------------------------------------------------
static String   summary;
static uint16_t passes = 0, fails = 0, seen = 0, notSeen = 0, unanswered = 0;
static const char *testId = "";

static void logLine(const char *kind, const String &text)
{
  String line = String("[") + testId + "] " + kind + " " + text;
  Serial.println(line);
  summary += line + "\n";
}

static bool check(bool ok, const String &what)
{
  logLine(ok ? "PASS" : "FAIL", what);
  if (ok) passes++; else fails++;
  return ok;
}

// One line typed in the Serial Monitor (waits as long as it takes)
static String readLine(void)
{
  String s;
  for (;;) {
    if (Serial.available()) {
      char c = Serial.read();
      if (c == '\n' || c == '\r') break;
      s += c;
    }
    else delay(2);
  }
  delay(20);                                   // "Both NL & CR": drop the second one
  while (Serial.available() && (Serial.peek() == '\n' || Serial.peek() == '\r')) Serial.read();
  s.trim();
  return s;
}

// Ask about something only your eyes can judge; the answer goes into the summary
static void look(const String &what)
{
  Serial.println(String("[") + testId + "] LOOK " + what);
  Serial.print("        y or n (a note after it is welcome), then Enter: ");
  String a = readLine();
  Serial.println(a);
  String remark = a.length() > 1 ? a.substring(1) : String("");
  remark.trim();
  char c = a.length() ? a[0] : ' ';
  const char *kind;
  if (c == 'y' || c == 'Y')      { kind = "SEEN";      seen++; }
  else if (c == 'n' || c == 'N') { kind = "NOT SEEN";  notSeen++; }
  else                           { kind = "NO ANSWER"; unanswered++; }
  String line = String("[") + testId + "] " + kind + " " + what;
  if (remark.length()) line += "  -- " + remark;
  summary += line + "\n";
}

static void note(const String &what)
{
  logLine("INFO", what);
}

static String hex4(uint16_t v)
{
  char b[8];
  snprintf(b, sizeof(b), "0x%04X", v);
  return String(b);
}

// Wait for Enter (next) or 'r' + Enter (repeat), as long as it takes
static bool waitNext(void)
{
  Serial.println("   ... Enter = next test, r + Enter = repeat this one");
  String a = readLine();
  return a.length() && (a[0] == 'r' || a[0] == 'R');
}

// Clear the screen and show the test title and what to look for
static void title(const char *id, const char *name, const char *expect)
{
  testId = id;
  Serial.println();
  Serial.printf("==== %s  %s ====\n", id, name);
  Serial.printf("     Expect: %s\n", expect);
  tft.resetViewport();
  tft.setTextDatum(TL_DATUM);
  tft.setTextPadding(0);
  tft.setTextSize(1);
  tft.fillScreen(TFT_BLACK);
  tft.fillRect(0, 0, tft.width(), 56, TFT_NAVY);
  tft.setTextColor(TFT_WHITE, TFT_NAVY);
  tft.drawString(String(id) + "  " + name, 10, 4, 4);
  tft.setTextColor(TFT_YELLOW, TFT_NAVY);
  tft.drawString(expect, 10, 34, 2);
}

// A deterministic "random" colour for each pixel
static inline uint16_t pattern(int32_t x, int32_t y)
{
  uint32_t v = (uint32_t)(x * 73856093u) ^ (uint32_t)(y * 19349663u);
  return (uint16_t)(v ^ (v >> 16));
}

static uint16_t swap16(uint16_t v) { return (uint16_t)(v >> 8 | v << 8); }

struct NamedColour { uint16_t c; const char *name; };
static const NamedColour bars[] = {
  { TFT_RED, "RED" }, { TFT_GREEN, "GREEN" }, { TFT_BLUE, "BLUE" }, { TFT_WHITE, "WHITE" },
  { TFT_YELLOW, "YELLOW" }, { TFT_CYAN, "CYAN" }, { TFT_MAGENTA, "MAGENTA" },
  { TFT_ORANGE, "ORANGE" }, { TFT_DARKGREY, "GREY" }, { TFT_PINK, "PINK" },
};
static const int NBARS = sizeof(bars) / sizeof(bars[0]);

// ---------------------------------------------------------------------------------------
// T1  Start-up
// ---------------------------------------------------------------------------------------
static uint32_t initMs = 0;

static void t1_init(void)
{
  title("T1", "Start-up", "Panel found, register read-back, status");
  check(tft.panelFound(), "panelFound() after init(), took " + String(initMs) + " ms");
  uint8_t hdwr = tft.readRegister(RA8876_HDWR);
  check(hdwr == RA8876_WIDTH / 8 - 1, "register 0x14 reads back " + String(hdwr, HEX) + " (expect 7f)");
  uint8_t st = tft.readStatus();
  check((st & RA8876_STSR_SDRAM_READY) && !(st & RA8876_STSR_CORE_BUSY),
        "status " + String(st, HEX) + ": SDRAM ready, engine idle");
  setup_t s;
  tft.getSetup(s);
  note("SPI " + String(s.tft_spi_freq / 10.0, 1) + " MHz, spi" + String(s.port) +
       ", pins MOSI " + String(s.pin_tft_mosi) + " SCK " + String(s.pin_tft_clk) +
       " MISO " + String(s.pin_tft_miso) + " CS " + String(s.pin_tft_cs) + " RST " + String(s.pin_tft_rst));
}

// ---------------------------------------------------------------------------------------
// T2  Colours and fill speed: pixel fills (top row) against hardware fills (bottom row)
// ---------------------------------------------------------------------------------------
static uint32_t fillSwUs = 0, fillHwUs = 0;

static void t2_colours(void)
{
  title("T2", "Colours, pixel fill vs hardware fill", "Two identical rows of colour bars, names under each");

  uint32_t t0;
  tft.setHardwareFill(false);
  t0 = micros(); tft.fillRect(0, 60, tft.width(), tft.height() - 60, TFT_BLACK); fillSwUs = micros() - t0;
  tft.setHardwareFill(true);
  t0 = micros(); tft.fillRect(0, 60, tft.width(), tft.height() - 60, TFT_BLACK); fillHwUs = micros() - t0;
  note("fill 1024 x 540: pixels " + String(fillSwUs) + " us, hardware " + String(fillHwUs) + " us");

  int bw = tft.width() / NBARS;
  bool allOk = true;
  for (int i = 0; i < NBARS; i++) {
    int x = i * bw;
    tft.setHardwareFill(false);
    tft.fillRect(x + 4, 80, bw - 8, 200, bars[i].c);
    tft.setHardwareFill(true);
    tft.fillRect(x + 4, 320, bw - 8, 200, bars[i].c);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setTextDatum(TC_DATUM);
    tft.drawString(bars[i].name, x + bw / 2, 290, 2);
    tft.drawString(bars[i].name, x + bw / 2, 530, 2);
    uint16_t a = tft.readPixel(x + bw / 2, 180), b = tft.readPixel(x + bw / 2, 420);
    if (a != bars[i].c || b != bars[i].c) {
      allOk = false;
      note(String(bars[i].name) + ": pixel fill reads " + hex4(a) + ", hardware fill " + hex4(b) + ", expect " + hex4(bars[i].c));
    }
  }
  tft.setTextDatum(TL_DATUM);
  check(allOk, "every bar reads back its exact colour (pixel and hardware fill)");
  look("the two rows look the same, and each bar is the colour named under it");
}

// ---------------------------------------------------------------------------------------
// T3  Pixel streaming: drawPixel with and without write-position reuse, read back
// ---------------------------------------------------------------------------------------
static uint32_t pixOptUs = 0, pixPlainUs = 0;

static uint32_t drawPattern(int x0, int y0, int w, int h, bool optimise)
{
  tft.setStreamOptimisation(optimise);
  tft.fillRect(x0, y0, w, h, TFT_BLACK);
  uint32_t t0 = micros();
  for (int y = y0; y < y0 + h; y++) {
    for (int x = x0; x < x0 + w; x++) {
      // gaps of different lengths, like glyph rows: every pixel except a few
      if (((x + y) % 5) != 0 && ((x * 3 + y) % 11) != 0) tft.drawPixel(x, y, pattern(x, y));
    }
  }
  uint32_t us = micros() - t0;
  tft.setStreamOptimisation(true);
  return us;
}

static uint32_t comparePattern(int x0, int y0, int w, int h)
{
  static uint16_t line[400];
  uint32_t bad = 0;
  for (int y = y0; y < y0 + h; y++) {
    tft.readRect(x0, y, w, 1, line);
    for (int x = x0; x < x0 + w; x++) {
      uint16_t want = (((x + y) % 5) != 0 && ((x * 3 + y) % 11) != 0) ? pattern(x, y) : TFT_BLACK;
      if (swap16(line[x - x0]) != want) {
        if (bad < 3) note("pixel " + String(x) + "," + String(y) + " reads " + hex4(swap16(line[x - x0])) + ", expect " + hex4(want));
        bad++;
      }
    }
  }
  return bad;
}

static void t3_pixels(void)
{
  title("T3", "Pixel streaming", "Two identical blocks of coloured noise with a fine diagonal grid of black gaps");
  const int W = 400, H = 200;
  pixOptUs   = drawPattern(40,  100, W, H, true);
  pixPlainUs = drawPattern(560, 100, W, H, false);
  note("drawPixel x " + String(W * H) + " area: reusing write position " + String(pixOptUs / 1000) +
       " ms, without " + String(pixPlainUs / 1000) + " ms");
  uint32_t badA = comparePattern(40, 100, W, H);
  uint32_t badB = comparePattern(560, 100, W, H);
  check(badA == 0, "left block (write position reused) reads back exactly (" + String(badA) + " wrong pixels)");
  check(badB == 0, "right block (position set for every pixel) reads back exactly (" + String(badB) + " wrong pixels)");

  // Lines and rectangles through the same paths
  tft.drawRect(40, 340, 400, 200, TFT_WHITE);
  for (int i = 0; i < 20; i++) tft.drawLine(40, 340 + i * 10, 439, 539 - i * 10, TFT_GREEN);
  tft.drawFastVLine(240, 340, 200, TFT_RED);
  tft.drawFastHLine(40, 440, 400, TFT_RED);
  check(tft.readPixel(240, 360) == TFT_RED && tft.readPixel(60, 440) == TFT_RED, "vertical and horizontal lines read back red");
  look("lower left: white frame, green fan of lines, red cross through the middle");
}

// ---------------------------------------------------------------------------------------
// T4  TFT_eSPI fonts: opaque background colour (the old font 2 bug) and transparency
// ---------------------------------------------------------------------------------------
static uint32_t font2Us = 0;

static void t4_fonts(void)
{
  title("T4", "TFT_eSPI fonts", "Text on matching coloured panels; no different-coloured box behind font 2");

  const uint16_t panels[] = { TFT_NAVY, TFT_DARKGREEN, TFT_MAROON };
  for (int i = 0; i < 3; i++) {
    int y = 70 + i * 60;
    tft.fillRect(20, y, 600, 50, panels[i]);
    tft.setTextColor(TFT_WHITE, panels[i]);
    uint32_t t0 = micros();
    tft.drawString(" Font 2 opaque 0123456789", 30, y + 4, 2);
    if (i == 0) font2Us = micros() - t0;
    tft.drawString(" Font 4 opaque", 330, y + 4, 4);
    // the leading space: its top-left pixel is background
    check(tft.readPixel(31, y + 5) == panels[i], "font 2 background matches the " + hex4(panels[i]) + " panel");
    check(tft.readPixel(331, y + 5) == panels[i], "font 4 background matches the " + hex4(panels[i]) + " panel");
  }
  note("font 2, 25 characters opaque: " + String(font2Us) + " us");

  // Transparent text over stripes
  for (int x = 640; x < 1010; x += 20) tft.fillRect(x, 70, 10, 170, TFT_DARKGREY);
  tft.setTextColor(TFT_YELLOW);
  tft.drawString("transparent 2", 650, 80, 2);
  tft.drawString("transparent 4", 650, 110, 4);
  tft.drawString("1:23", 650, 150, 7);

  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawString("Font 1 GLCD", 20, 260, 1);
  tft.setTextSize(2);
  tft.drawString("Font 1 size 2", 20, 275, 1);
  tft.setTextSize(1);
  tft.drawString("12:34", 20, 310, 6);
  tft.drawString("56.7", 220, 310, 7);
  tft.drawString("89", 420, 300, 8);
  tft.setFreeFont(&FreeSansBold18pt7b);
  tft.drawString("FreeSansBold18pt", 600, 300);
  tft.setTextFont(1);

  // Anti-aliased graphics (background given, so nothing is read back)
  tft.drawSmoothArc(120, 500, 70, 55, 30, 330, TFT_ORANGE, TFT_BLACK, true);
  tft.fillSmoothCircle(300, 500, 50, TFT_CYAN, TFT_BLACK);
  tft.drawWideLine(400, 450, 600, 560, 9, TFT_GREEN, TFT_BLACK);
  tft.drawWedgeLine(650, 560, 850, 450, 2, 16, TFT_MAGENTA, TFT_BLACK);
  // ... and with the background read back from the display
  tft.fillSmoothCircle(940, 500, 40, TFT_YELLOW);

  look("each line of text sits on its panel with no box of another colour behind the characters");
  look("yellow text over the grey stripes shows the stripes between the letters");
  look("fonts 1/6/7/8 and FreeSansBold read cleanly; arc, circles and lines have smooth edges");
}

// ---------------------------------------------------------------------------------------
// T5  Character ROM fonts
// ---------------------------------------------------------------------------------------
static uint32_t rom16Us = 0;

static void t5_rom(void)
{
  title("T5", "Character ROM fonts", "8x16, 12x24, 16x32, scaled, opaque and transparent, aligned on the crosshairs");

  tft.setTextColor(TFT_BLACK, TFT_YELLOW);
  uint32_t t0 = micros();
  tft.drawString(" ROM_FONT_16 opaque ABCDEFGHIJKLMNOPQRSTUVWXYZ 0123456789", 20, 70, ROM_FONT_16);
  rom16Us = micros() - t0;
  check(tft.readPixel(21, 71) == TFT_YELLOW, "ROM 16 background reads yellow");
  check(tft.textWidth("ABCD", ROM_FONT_16) == 32 && tft.fontHeight(ROM_FONT_16) == 16, "ROM 16 is 8 x 16 per character");

  tft.setTextColor(TFT_WHITE, TFT_BLUE);
  tft.drawString(" ROM_FONT_24 opaque", 20, 95, ROM_FONT_24);
  tft.drawString(" ROM_FONT_32 opaque", 20, 125, ROM_FONT_32);
  check(tft.readPixel(21, 96) == TFT_BLUE, "ROM 24 background reads blue");

  for (int x = 500; x < 1010; x += 16) tft.fillRect(x, 95, 8, 80, TFT_DARKGREEN);
  tft.setTextColor(TFT_WHITE);
  // The leading space's cell (500..511) covers a stripe (500..507) and part of a gap (508..515)
  tft.drawString(" transparent 24", 500, 100, ROM_FONT_24);
  tft.drawString(" transparent 32", 500, 135, ROM_FONT_32);
  uint16_t onStripe = tft.readPixel(503, 101), inGap = tft.readPixel(510, 101);
  check(onStripe == TFT_DARKGREEN && inGap == TFT_BLACK,
        "transparent ROM text leaves the background (stripe " + hex4(onStripe) + ", gap " + hex4(inGap) + ")");

  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  for (uint8_t s = 1; s <= 4; s++) {
    tft.setTextSize(s);
    tft.drawString("x" + String(s), 20 + (s - 1) * (s + 2) * 30, 180, ROM_FONT_16);
  }
  tft.setTextSize(1);

  // Datums: the crosshair marks the reference point
  const uint8_t datums[] = { TL_DATUM, MC_DATUM, BR_DATUM };
  const char *names[] = { "TL_DATUM", "MC_DATUM", "BR_DATUM" };
  for (int i = 0; i < 3; i++) {
    int cx = 180 + i * 330, cy = 330;
    tft.drawFastHLine(cx - 60, cy, 120, TFT_RED);
    tft.drawFastVLine(cx, cy - 30, 60, TFT_RED);
    tft.setTextDatum(datums[i]);
    tft.setTextColor(TFT_WHITE, TFT_DARKGREY);
    tft.drawString(names[i], cx, cy, ROM_FONT_24);
  }
  tft.setTextDatum(TL_DATUM);

  // print() and wrapping
  tft.setViewport(20, 400, 600, 190);
  tft.fillScreen(TFT_NAVY);
  tft.setTextColor(TFT_WHITE, TFT_NAVY);
  tft.setTextFont(ROM_FONT_16);
  tft.setCursor(0, 0);
  tft.println("print() with ROM_FONT_16 in a viewport. A long line wraps at the edge of the viewport when it runs out of room.");
  tft.printf("printf: %d %s %.2f\n", 42, "Latin-1: \xC3\xA9 \xC3\xBC \xC3\xB1", 3.14159);
  tft.setTextFont(ROM_FONT_24);
  tft.println("Next line ROM_FONT_24");
  tft.resetViewport();
  tft.setTextFont(1);

  note("ROM 16, 57 characters opaque: " + String(rom16Us) + " us (font 2, 25 characters: " + String(font2Us) + " us)");
  look("ROM text is crisp in all three sizes; opaque text has a solid box, transparent text shows the stripes");
  look("x1..x4 grow in steps; each datum label is placed on its crosshair as its name says");
  look("blue viewport: text wraps inside it; the printf line shows e-acute, u-umlaut, n-tilde");
}

// ---------------------------------------------------------------------------------------
// T6  Hardware fill edge cases
// ---------------------------------------------------------------------------------------
static void t6_fillEdges(void)
{
  title("T6", "Fill sizes and edges", "Small white shapes and a frame at the screen edges; nothing hangs");

  struct R { int x, y, w, h; };
  const R rects[] = {
    { 100, 100, 1, 1 }, { 120, 100, 1, 40 }, { 140, 100, 40, 1 }, { 200, 100, 2, 2 },
    { 220, 100, 3, 3 }, { 240, 100, 9, 7 }, { 270, 100, 64, 1 }, { 350, 100, 1, 64 },
    { 0, 60, 1024, 2 }, { 0, 598, 1024, 2 }, { 0, 60, 2, 540 }, { 1022, 60, 2, 540 },
    { 1000, 580, 100, 100 },        // runs off the right and bottom: clipped
    { -50, 300, 100, 50 },          // starts off the left: clipped
  };
  uint32_t errorsBefore = tft.engineErrors();
  bool ok = true;
  for (const R &r : rects) {
    tft.fillRect(r.x, r.y, r.w, r.h, TFT_WHITE);
    int cx = constrain(r.x + r.w / 2, 0, 1023), cy = constrain(r.y + r.h / 2, 0, 599);
    if (tft.readPixel(cx, cy) != TFT_WHITE) {
      ok = false;
      note("fill " + String(r.w) + " x " + String(r.h) + " at " + String(r.x) + "," + String(r.y) + " missing");
    }
  }
  check(ok, "every rectangle reads back white at its centre");
  check(tft.readPixel(99, 99) == TFT_BLACK && tft.readPixel(102, 102) == TFT_BLACK, "a 1 x 1 fill touches nothing around it");
  check(tft.engineErrors() == errorsBefore, "no engine time-outs (" + String(tft.engineErrors() - errorsBefore) + ")");

  uint32_t t0 = micros();
  for (int i = 0; i < 100; i++) tft.fillRect(450 + (i % 10) * 50, 150 + (i / 10) * 40, 40, 30, (uint16_t)(i * 655));
  note("100 fills of 40 x 30: " + String((micros() - t0) / 100) + " us each");
  look("a 2-pixel white frame round the area below the title; near its top left a row of tiny white "
       "marks (one pixel, thin lines, small squares); a white block in the bottom right corner and one "
       "on the left edge (rectangles cut off at the screen edge); a grid of coloured blocks");
}

// ---------------------------------------------------------------------------------------
// T7  Pages
// ---------------------------------------------------------------------------------------
static uint32_t flipUs = 0;
static bool vsyncSeen = false;

static void drawPageCard(uint8_t page, uint16_t bg, const char *label)
{
  tft.setDrawPage(page);
  tft.fillScreen(bg);
  tft.setTextColor(TFT_WHITE, bg);
  tft.setTextDatum(MC_DATUM);
  tft.setTextSize(4);
  tft.drawString(label, tft.width() / 2, tft.height() / 2, ROM_FONT_32);
  tft.setTextSize(1);
  tft.setTextDatum(TL_DATUM);
  for (int i = 0; i < 12; i++) tft.fillSmoothCircle(60 + i * 82, 520, 30, TFT_WHITE, bg);
}

static void t7_pages(void)
{
  title("T7", "Display pages", "This page stays still while pages 1 and 2 are drawn, then the screen flips between them");

  drawPageCard(1, TFT_BLUE,  "PAGE 1");
  drawPageCard(2, TFT_DARKGREEN, "PAGE 2");
  tft.setDrawPage(0);
  look("nothing changed on screen while pages 1 and 2 were drawn");
  delay(1500);

  vsyncSeen = tft.waitForVSync();
  note(vsyncSeen ? "VSYNC flag seen: page switches wait for it" : "no VSYNC flag: page switches do not wait");

  uint32_t t0 = micros();
  tft.showPage(1);
  flipUs = micros() - t0;
  check(tft.getShowPage() == 1, "showPage(1)");
  note("page switch: " + String(flipUs) + " us (includes waiting for VSYNC)");
  delay(1000);
  for (int i = 0; i < 10; i++) { tft.showPage(i & 1 ? 1 : 2); delay(300); }

  tft.setDrawPage(1);
  check(tft.readPixel(5, 5) == TFT_BLUE, "page 1 reads back blue while page 1 is drawn on");
  tft.setDrawPage(0);
  check(tft.readPixel(5, 100) == TFT_BLACK, "page 0 still reads back black");

  tft.copyPage(1, 3);
  tft.setDrawPage(3);
  check(tft.readPixel(5, 5) == TFT_BLUE, "copyPage(1, 3): page 3 reads back blue");
  tft.fillPage(4, TFT_RED);
  tft.setDrawPage(4);
  check(tft.readPixel(512, 300) == TFT_RED, "fillPage(4, RED)");
  tft.setDrawPage(0);

  tft.showPage(0);
  look("the switches between the blue and green pages were instant and clean (no tearing or partial redraws)");
}

// ---------------------------------------------------------------------------------------
// T8  Block transfers
// ---------------------------------------------------------------------------------------
static uint32_t copyUs = 0;

static void t8_bte(void)
{
  title("T8", "Block copies", "Magenta square, yellow disc without its square, then a fade from 1/4 to 3/4");

  for (int y = 70; y < 600; y += 20)
    for (int x = 0; x < 1024; x += 20)
      tft.fillRect(x, y, 20, 20, ((x + y) / 20) & 1 ? TFT_DARKGREY : TFT_LIGHTGREY);

  // A "sprite" on page 5: yellow disc on magenta
  tft.setDrawPage(5);
  tft.fillRect(0, 0, 160, 160, TFT_MAGENTA);
  tft.fillCircle(80, 80, 70, TFT_YELLOW);
  tft.setDrawPage(0);

  uint32_t t0 = micros();
  tft.copyRect(5, 0, 0, 0, 40, 100, 160, 160);
  copyUs = micros() - t0;
  check(tft.readPixel(40 + 80, 100 + 80) == TFT_YELLOW && tft.readPixel(42, 102) == TFT_MAGENTA, "copyRect: disc and corner read back");

  tft.copyRectTransparent(5, 0, 0, 0, 240, 100, 160, 160, TFT_MAGENTA);
  uint16_t corner = tft.readPixel(242, 102);
  check(tft.readPixel(320, 180) == TFT_YELLOW && corner != TFT_MAGENTA, "copyRectTransparent: magenta left out (corner " + hex4(corner) + ")");

  const uint8_t alphas[] = { 8, 16, 24 };
  for (int i = 0; i < 3; i++) {
    tft.blendRect(5, 0, 0, 0, 440 + i * 180, 100, 0, 440 + i * 180, 100, 160, 160, alphas[i]);
  }
  note("copyRect 160 x 160: " + String(copyUs) + " us");
  look("left: disc on a magenta square; next: disc straight on the checkerboard");
  look("right three: the square blended over the checkerboard, faint to strong");
}

// ---------------------------------------------------------------------------------------
// T9  Hardware scrolling
// ---------------------------------------------------------------------------------------
static uint32_t scrollUs = 0;

static void t9_scroll(void)
{
  title("T9", "Hardware scrolling", "A console scrolls smoothly upward; then the box slides down, left and right");

  const int X = 20, Y = 70, W = 984, H = 18 * 26, LH = 18;
  tft.drawRect(X - 2, Y - 2, W + 4, H + 4, TFT_WHITE);
  tft.fillRect(X, Y, W, H, TFT_BLACK);
  tft.setTextColor(TFT_GREEN, TFT_BLACK);

  uint32_t total = 0;
  for (int n = 1; n <= 80; n++) {
    uint32_t t0 = micros();
    tft.scrollRect(X, Y, W, H, 0, -LH, TFT_BLACK);
    total += micros() - t0;
    char line[96];
    snprintf(line, sizeof(line), "%4d  The quick brown fox jumps over the lazy dog  millis %lu", n, (unsigned long)millis());
    tft.drawString(line, X + 4, Y + H - LH + 1, ROM_FONT_16);
    delay(15);
  }
  scrollUs = total / 80;
  note("scroll of " + String(W) + " x " + String(H) + " by one line: " + String(scrollUs) + " us");

  // Marker check: a block moved up by exactly 16 pixels
  tft.fillRect(X, Y, W, H, TFT_BLACK);
  tft.fillRect(X + 500, Y + 200, 20, 20, TFT_RED);
  tft.scrollRect(X, Y, W, H, 0, -16, TFT_BLACK);
  check(tft.readPixel(X + 510, Y + 190) == TFT_RED && tft.readPixel(X + 510, Y + 210) == TFT_BLACK,
        "a red marker moved up exactly 16 pixels");

  for (int i = 0; i < 20; i++) { tft.scrollRect(X, Y, W, H, 0, 8, TFT_NAVY); delay(20); }
  for (int i = 0; i < 20; i++) { tft.scrollRect(X, Y, W, H, -8, 0, TFT_MAROON); delay(20); }
  for (int i = 0; i < 20; i++) { tft.scrollRect(X, Y, W, H, 8, 0, TFT_DARKGREEN); delay(20); }
  look("lines scrolled up smoothly with no flicker; the box then slid down, left and right, filling the gap");
}

// ---------------------------------------------------------------------------------------
// T10  Flicker-free updates
// ---------------------------------------------------------------------------------------
static uint32_t compUs = 0, directUs = 0;

static void updateBox(int x, int n)
{
  tft.fillRect(x, 150, 400, 200, TFT_NAVY);               // the old way: clear, then draw
  tft.fillRoundRect(x + 20, 170, 360, 160, 20, TFT_DARKCYAN);
  tft.setTextColor(TFT_WHITE);
  tft.setTextDatum(MC_DATUM);
  tft.drawNumber(n, x + 200, 250, 7);
  tft.setTextDatum(TL_DATUM);
}

static void t10_composite(void)
{
  title("T10", "Flicker-free updates", "Left counter (clear then draw) may blink; right counter (composite) must not");

  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawString("direct", 60, 370, 4);
  tft.drawString("beginComposite / endComposite", 560, 370, 4);
  uint32_t td = 0, tc = 0;
  for (int n = 0; n < 150; n++) {
    uint32_t t0 = micros();
    updateBox(40, n);
    td += micros() - t0;
    t0 = micros();
    tft.beginComposite(560, 150, 400, 200, false);
    updateBox(560, n);
    tft.endComposite();
    tc += micros() - t0;
  }
  directUs = td / 150; compUs = tc / 150;
  check(tft.readPixel(580, 160) == TFT_NAVY, "composite result reached the screen");
  check(!tft.inComposite(), "composite closed");
  note("update: direct " + String(directUs) + " us, composite " + String(compUs) + " us");
  look("right counter never blinked or showed a blank box; compare with the left one");
}

// ---------------------------------------------------------------------------------------
// T11  Picture-in-picture
// ---------------------------------------------------------------------------------------
static void t11_pip(void)
{
  title("T11", "Picture-in-picture", "An orange window glides across, a purple one joins below it, then both vanish");

  tft.setDrawPage(6);
  tft.fillRect(0, 0, 320, 160, TFT_ORANGE);
  tft.drawRect(0, 0, 320, 160, TFT_WHITE);
  tft.setTextColor(TFT_BLACK, TFT_ORANGE);
  tft.drawString("PIP 1 (on top)", 20, 60, ROM_FONT_32);
  tft.setDrawPage(7);
  tft.fillRect(0, 0, 320, 160, TFT_PURPLE);
  tft.drawRect(0, 0, 320, 160, TFT_WHITE);
  tft.setTextColor(TFT_WHITE, TFT_PURPLE);
  tft.drawString("PIP 2", 20, 60, ROM_FONT_32);
  tft.setDrawPage(0);

  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  for (int y = 80; y < 600; y += 40) tft.drawString("main page text, not redrawn while the windows move", 20, y, ROM_FONT_24);

  check(tft.pipShow(1, 6, 0, 0, 0, 100, 320, 160), "pipShow(1)");
  for (int x = 0; x <= 680; x += 8) { tft.pipMove(1, x, 100 + x / 4); delay(15); }
  check(tft.pipShow(2, 7, 0, 0, 500, 200, 320, 160), "pipShow(2)");
  delay(1500);
  for (int x = 680; x >= 300; x -= 8) { tft.pipMove(1, x, 270); delay(15); }
  delay(1000);
  tft.pipHide(1);
  delay(700);
  tft.pipHide(2);
  look("windows moved smoothly over the text without disturbing it; orange stayed above purple; both disappeared");
}

// ---------------------------------------------------------------------------------------
// T12  Sprites and images (byte order)
// ---------------------------------------------------------------------------------------
static const uint16_t rgbImage[3] = { TFT_RED, TFT_GREEN, TFT_BLUE };

static void t12_sprites(void)
{
  title("T12", "Sprites and images", "A sprite with red, green, blue thirds; a red-green-blue strip; texts in the sprite");

  RPiPico_RA8876_Sprite spr(&tft);
  if (!check(spr.createSprite(300, 120) != nullptr, "createSprite 300 x 120")) return;
  spr.fillRect(0, 0, 100, 120, TFT_RED);
  spr.fillRect(100, 0, 100, 120, TFT_GREEN);
  spr.fillRect(200, 0, 100, 120, TFT_BLUE);
  spr.setTextColor(TFT_WHITE);
  spr.drawString("Font 4", 10, 10, 4);
  spr.drawString("ROM16 > font 2", 110, 60, ROM_FONT_16);
  spr.fillSmoothCircle(250, 90, 20, TFT_YELLOW, TFT_BLUE);
  spr.pushSprite(40, 100);
  check(tft.readPixel(60, 200) == TFT_RED && tft.readPixel(160, 200) == TFT_GREEN && tft.readPixel(260, 210) == TFT_BLUE,
        "sprite thirds read back red, green, blue");
  spr.pushSprite(400, 100, TFT_GREEN);   // green is transparent
  check(tft.readPixel(520, 200) == TFT_BLACK, "pushSprite with a transparent colour leaves the background");
  spr.deleteSprite();

  // A plain RGB565 array needs setSwapBytes(true), as in TFT_eSPI
  uint16_t strip[90];
  for (int i = 0; i < 90; i++) strip[i] = rgbImage[i / 30];
  tft.setSwapBytes(true);
  for (int y = 300; y < 340; y++) tft.pushImage(40, y, 90, 1, strip);
  tft.setSwapBytes(false);
  check(tft.readPixel(50, 320) == TFT_RED && tft.readPixel(80, 320) == TFT_GREEN && tft.readPixel(110, 320) == TFT_BLUE,
        "pushImage of an RGB565 array with setSwapBytes(true)");
  look("sprite: red/green/blue thirds, white text, yellow dot; second copy without its green third");
}

// ---------------------------------------------------------------------------------------
// T13  Reading back
// ---------------------------------------------------------------------------------------
static void t13_readback(void)
{
  title("T13", "Read back", "Two identical copies of a coloured block");

  for (int y = 0; y < 100; y++) for (int x = 0; x < 200; x += 10) tft.fillRect(40 + x, 100 + y, 10, 1, pattern(x, y));
  static uint16_t buf[200 * 100];
  uint32_t t0 = micros();
  tft.readRect(40, 100, 200, 100, buf);
  uint32_t readUs = micros() - t0;
  tft.pushRect(300, 100, 200, 100, buf);
  uint32_t bad = 0;
  for (int y = 0; y < 100; y += 7) for (int x = 0; x < 200; x += 13)
    if (tft.readPixel(300 + x, 100 + y) != tft.readPixel(40 + x, 100 + y)) bad++;
  check(bad == 0, "readRect then pushRect copies exactly (" + String(bad) + " differences)");
  note("readRect 200 x 100: " + String(readUs / 1000) + " ms");

  tft.fillRect(600, 100, 10, 10, TFT_RED);
  uint8_t rgb[3] = { 0, 0, 0 };
  tft.readRectRGB(605, 105, 1, 1, rgb);
  check(rgb[0] == 0xF8 && rgb[1] == 0 && rgb[2] == 0, "readRectRGB of red: " + String(rgb[0]) + "," + String(rgb[1]) + "," + String(rgb[2]));
}

// ---------------------------------------------------------------------------------------
// T14  Viewport clipping
// ---------------------------------------------------------------------------------------
static void t14_viewport(void)
{
  title("T14", "Viewport", "A navy box with a yellow frame; the text inside is cut at its edges");

  tft.fillRect(80, 80, 640, 400, TFT_DARKGREY);
  tft.setViewport(100, 100, 400, 200);
  tft.fillScreen(TFT_NAVY);
  tft.frameViewport(TFT_YELLOW, 2);
  tft.setTextColor(TFT_WHITE, TFT_NAVY);
  tft.drawString("ROM text running past the right-hand edge of this viewport", 10, 20, ROM_FONT_24);
  tft.drawString("Font 4 text running past the edge as well", 10, 80, 4);
  tft.drawString("half off the top", 10, -8, ROM_FONT_16);
  tft.resetViewport();
  check(tft.readPixel(90, 90) == TFT_DARKGREY && tft.readPixel(510, 150) == TFT_DARKGREY, "nothing drawn outside the viewport");
  check(tft.readPixel(300, 280) == TFT_NAVY, "fillScreen() filled the viewport");
  look("ROM text stops at the last whole character inside the box; font 4 is cut mid-letter; the half-off-the-top line is not drawn");
}

// ---------------------------------------------------------------------------------------
// T15  Rotation
// ---------------------------------------------------------------------------------------
// The RA8876 cannot rotate (its scan only flips top to bottom), so the library stays landscape.
static void t15_rotation(void)
{
  title("T15", "Rotation (landscape only)", "setRotation(2) changes nothing: the screen stays on and the right way up");
  tft.setRotation(2);
  check(tft.getRotation() == 0, "setRotation(2) keeps rotation 0");
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawString("Drawn after setRotation(2): still the right way up, top left", 20, 80, ROM_FONT_24);
  tft.fillTriangle(20, 120, 70, 120, 20, 170, TFT_RED);
  check(tft.readPixel(25, 125) == TFT_RED, "drawing still lands where it is drawn");
  delay(3000);
  look("the screen stayed on, and the text and red corner are at the top left, the right way up");
}

// ---------------------------------------------------------------------------------------
// Summary
// ---------------------------------------------------------------------------------------
static void finish(void)
{
  testId = "END";
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor((fails || notSeen) ? TFT_RED : TFT_GREEN, TFT_BLACK);
  tft.setTextDatum(MC_DATUM);
  tft.setTextSize(2);
  tft.drawString(String(passes) + " PASS  " + String(fails) + " FAIL  " + String(notSeen) + " NOT SEEN",
                 tft.width() / 2, tft.height() / 2, ROM_FONT_32);
  tft.setTextSize(1);
  tft.setTextDatum(TL_DATUM);

  Serial.println();
  Serial.println("================ SUMMARY (paste this) ================");
  Serial.print(summary);
  Serial.printf("Timing: fill 1024x540 pixels %lu us / hardware %lu us; drawPixel block %lu / %lu ms;\n",
                (unsigned long)fillSwUs, (unsigned long)fillHwUs, (unsigned long)(pixOptUs / 1000), (unsigned long)(pixPlainUs / 1000));
  Serial.printf("        font2 25 chars %lu us; ROM16 57 chars %lu us; page switch %lu us (VSYNC %s);\n",
                (unsigned long)font2Us, (unsigned long)rom16Us, (unsigned long)flipUs, vsyncSeen ? "yes" : "no");
  Serial.printf("        copyRect 160x160 %lu us; scroll line %lu us; box update direct %lu / composite %lu us\n",
                (unsigned long)copyUs, (unsigned long)scrollUs, (unsigned long)directUs, (unsigned long)compUs);
  Serial.printf("Engine time-outs: %lu\n", (unsigned long)tft.engineErrors());
  Serial.printf("%u PASS, %u FAIL; looked at: %u seen, %u not seen, %u not answered\n",
                passes, fails, seen, notSeen, unanswered);
  Serial.println("======================================================");
}

typedef void (*TestFn)(void);
static const TestFn tests[] = {
  t1_init, t2_colours, t3_pixels, t4_fonts, t5_rom, t6_fillEdges, t7_pages, t8_bte,
  t9_scroll, t10_composite, t11_pip, t12_sprites, t13_readback, t14_viewport, t15_rotation,
};

void setup()
{
  Serial.begin(115200);
  uint32_t t0 = millis();
  while (!Serial && millis() - t0 < 3000) {}
  Serial.println("\nRPiPico-RA8876 HardwareTest, library " RPIPICO_RA8876_VERSION);

  t0 = millis();
  tft.init();
  initMs = millis() - t0;
  if (!tft.panelFound()) {
    Serial.println("No RA8876 answered: check wiring and power. Stopping.");
    return;
  }

  for (TestFn t : tests) {
    do { t(); } while (waitNext());
  }
  finish();
}

void loop() {}
