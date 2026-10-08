/***************************************************************************************
  RPiPico-RA8876_hw.cpp -- the RA8876 itself: SPI command protocol, start-up, and the
  hardware features (fill engine, display pages, block transfers, scrolling, off-screen
  composition, picture-in-picture, character ROM text).

  Start-up register values are those of the TFT_eSPI_RA8876 RA8876 driver, bench-proven on the
  ER-TFTM101-1 and the BuyDisplay 7" module. Engine sequences follow RAiO's application code as
  used by the RA8876_t3 / RA8876_RP2040 library.
 ***************************************************************************************/

#include "RPiPico-RA8876.h"

// Wait until the SPI port has sent everything (transmit FIFO empty and not busy)
#define RA8876_SPI_IDLE_WAIT  while (!(_spiHw->sr & SPI_SSPSR_TFE_BITS) || (_spiHw->sr & SPI_SSPSR_BSY_BITS)) {}

// Keep chip select high for a moment between two commands (about 50 ns)
#define RA8876_CS_GAP  __asm volatile ("nop\n nop\n nop\n nop\n nop\n nop\n nop\n nop")

/***************************************************************************************
**                         SPI command protocol
***************************************************************************************/

// Close a pixel stream: let the SPI port drain, empty the receive FIFO, raise CS
void RPiPico_RA8876::_streamEnd(void)
{
  if (!_streamOpen) return;
  if (DMA_Enabled) dmaWait();
  RA8876_SPI_IDLE_WAIT;
  while (_spiHw->sr & SPI_SSPSR_RNE_BITS) (void)_spiHw->dr;
  _spiHw->icr = SPI_SSPICR_RORIC_BITS;
  gpio_put(_pin_cs, 1);
  _streamOpen = false;
}

// Open a pixel stream into the memory data port: CS low, then the data-write prefix as one
// 8-bit frame. Pixels follow as 16-bit frames until _streamEnd().
void RPiPico_RA8876::_streamBegin(void)
{
  if (_streamOpen) return;
  _selectReg(RA8876_MRWDP);
  RA8876_CS_GAP;
  gpio_put(_pin_cs, 0);
  hw_write_masked(&_spiHw->cr0, (8 - 1) << SPI_SSPCR0_DSS_LSB, SPI_SSPCR0_DSS_BITS);
  _spiHw->dr = RA8876_SPI_DATAWRITE;
  RA8876_SPI_IDLE_WAIT;
  while (_spiHw->sr & SPI_SSPSR_RNE_BITS) (void)_spiHw->dr;
  hw_write_masked(&_spiHw->cr0, (16 - 1) << SPI_SSPCR0_DSS_LSB, SPI_SSPCR0_DSS_BITS);
  _streamOpen = true;
}

// One command: CS low, prefix byte + payload byte as one 16-bit frame, CS high.
// Returns the byte received during the payload: the answer to a status or data read.
uint8_t RPiPico_RA8876::_xfer(uint16_t frame)
{
  _streamEnd();   // a command ends any pixel burst
  while (_spiHw->sr & SPI_SSPSR_RNE_BITS) (void)_spiHw->dr;
  RA8876_CS_GAP;
  gpio_put(_pin_cs, 0);
  _spiHw->dr = frame;
  while (!(_spiHw->sr & SPI_SSPSR_RNE_BITS)) {}   // the reply arrives when the frame is done
  uint16_t r = _spiHw->dr;
  RA8876_SPI_IDLE_WAIT;
  gpio_put(_pin_cs, 1);
  return (uint8_t)r;
}

// The register a data byte goes to stays selected until another one is: skip re-selecting it
void RPiPico_RA8876::_selectReg(uint8_t reg)
{
  if (_selectedReg == reg) return;
  _xfer(RA8876_SPI_CMDWRITE << 8 | reg);
  _selectedReg = reg;
}

void RPiPico_RA8876::_regWrite(uint8_t reg, uint8_t val)
{
  _selectReg(reg);
  _xfer(RA8876_SPI_DATAWRITE << 8 | val);
}

// For registers the controller never changes by itself (windows, colours, modes): a write
// that would not change the register is skipped.
void RPiPico_RA8876::_regWriteCached(uint8_t reg, uint8_t val)
{
  uint32_t bit = 1UL << (reg & 31);
  if ((_regValid[reg >> 5] & bit) && _regShadow[reg] == val) return;
  _regWrite(reg, val);
  _regShadow[reg] = val;
  _regValid[reg >> 5] |= bit;
}

uint8_t RPiPico_RA8876::_regRead(uint8_t reg)
{
  _selectReg(reg);
  return _xfer(RA8876_SPI_DATAREAD << 8);
}

void RPiPico_RA8876::_reg16(uint8_t reg, uint16_t val)
{
  _regWriteCached(reg,     val & 0xFF);
  _regWriteCached(reg + 1, val >> 8);
}

void RPiPico_RA8876::_reg32(uint8_t reg, uint32_t val)
{
  _regWriteCached(reg,     val & 0xFF);
  _regWriteCached(reg + 1, (val >> 8) & 0xFF);
  _regWriteCached(reg + 2, (val >> 16) & 0xFF);
  _regWriteCached(reg + 3, val >> 24);
}

void RPiPico_RA8876::_invalidateCaches(void)
{
  memset(_regValid, 0, sizeof(_regValid));
  _selectedReg = -1;
  _curValid = false;
  _curRegXHi = _curRegYHi = -1;
  _winX = _winY = _winW = _winH = -1;
  _pixCount = 0;
}

bool RPiPico_RA8876::_waitStatus(uint8_t mask, uint8_t want, uint32_t timeout_ms)
{
  uint32_t t0 = millis();
  do {
    if ((_xfer(RA8876_SPI_STATUSREAD << 8) & mask) == want) return true;
  } while (millis() - t0 < timeout_ms);
  return false;
}

bool RPiPico_RA8876::_waitEngine(void)
{
  if (_waitStatus(RA8876_STSR_CORE_BUSY, 0, RA8876_ENGINE_TIMEOUT_MS)) return true;
  if (_engineErrors++ == 0) {
    if (_log) _log->println("RPiPico-RA8876: the RA8876 did not finish an operation in time (engineErrors() counts these)");
  }
  return false;
}

void RPiPico_RA8876::_graphicMode(void)
{
  _regWriteCached(RA8876_ICR, RA8876_ICR_GRAPHIC);
}

void RPiPico_RA8876::_textMode(void)
{
  _regWriteCached(RA8876_ICR, RA8876_ICR_TEXT);
}

// 16 bpp colour into the red/green/blue registers at reg (foreground 0xD2, background 0xD5).
// Each register uses its top bits: red 7:3, green 7:2, blue 7:3.
void RPiPico_RA8876::_setColorRegs(uint8_t reg, uint16_t color)
{
  _regWriteCached(reg,     (uint8_t)(color >> 8));
  _regWriteCached(reg + 1, (uint8_t)(color >> 3));
  _regWriteCached(reg + 2, (uint8_t)(color << 3));
}

void RPiPico_RA8876::_activeWindow(int32_t x, int32_t y, int32_t w, int32_t h)
{
  _reg16(RA8876_AWUL_X0,     x);
  _reg16(RA8876_AWUL_X0 + 2, y);
  _reg16(RA8876_AW_WTH0,     w);
  _reg16(RA8876_AW_HT0,      h);
  _winX = x; _winY = y; _winW = w; _winH = h;
}

void RPiPico_RA8876::_activeWindowFull(void)
{
  _activeWindow(0, 0, _init_width, _init_height);
}

// The read/write position. Writing one byte of a coordinate moves the position at once,
// combined with the other byte AS LAST WRITTEN -- not with where the position has stepped to
// since (bench, 2026-10-08: rewriting only the low byte of x after x had passed 255 put pixels
// 256 to the left). setWindow() relies on this when it writes only what changed.
void RPiPico_RA8876::_setCursor(int32_t x, int32_t y)
{
  _regWrite(RA8876_CURH0,     x & 0xFF);
  _regWrite(RA8876_CURH0 + 1, (x >> 8) & 0x1F);
  _regWrite(RA8876_CURH0 + 2, y & 0xFF);
  _regWrite(RA8876_CURH0 + 3, (y >> 8) & 0x1F);
  _curRegXHi = (x >> 8) & 0x1F;
  _curRegYHi = (y >> 8) & 0x1F;
}

// Is the controller's write position at x, y? It started at _curX, _curY and has stepped one
// pixel for each of the _pixCount pixels sent since, left to right then down through the
// active window.
bool RPiPico_RA8876::_predictedPos(int32_t &x, int32_t &y)
{
  if (!_curValid || _winW <= 0 || _winH <= 0) return false;
  int32_t i = (_curY - _winY) * _winW + (_curX - _winX) + (int32_t)_pixCount;
  if (i < 0 || i >= _winW * _winH) return false;
  x = _winX + i % _winW;
  y = _winY + i / _winW;
  return true;
}

bool RPiPico_RA8876::_cursorPredicted(int32_t x, int32_t y)
{
  int32_t px, py;
  return _predictedPos(px, py) && px == x && py == y;
}

// Before the fill engine, block transfer engine or text engine runs: close the pixel stream
// and let pixels still queued inside the controller land first.
void RPiPico_RA8876::_hwPrepare(void)
{
  _streamEnd();
  _curValid = false;
  _waitStatus(RA8876_STSR_WFIFO_EMPTY, RA8876_STSR_WFIFO_EMPTY, 10);
  _graphicMode();
}

uint16_t RPiPico_RA8876::_readMemPixel(void)
{
  uint8_t lo = _xfer(RA8876_SPI_DATAREAD << 8);
  uint8_t hi = _xfer(RA8876_SPI_DATAREAD << 8);
  return (uint16_t)(hi << 8 | lo);
}

/***************************************************************************************
**                         Start-up
***************************************************************************************/
void RPiPico_RA8876::_ra8876Init(void)
{
  _panelFound = false;
  uint32_t t0 = millis();

  // ---- PLLs: Fout = 10 MHz x N / R / OD (see the TFT_eSPI_RA8876 RA8876_Init.h notes) ----
  _regWrite(RA8876_PPLLC1, 0x06);   // pixel clock 50 MHz: OD 8
  _regWrite(RA8876_PPLLC2, 39);
  _regWrite(RA8876_MPLLC1, 0x04);   // SDRAM clock 120 MHz: OD 4
  _regWrite(RA8876_MPLLC2, 47);
  _regWrite(RA8876_SPLLC1, 0x04);   // core clock 120 MHz: OD 4
  _regWrite(RA8876_SPLLC2, 47);
  _regWrite(RA8876_CCR, 0x80);      // start the PLLs
  delay(2);

  // Wait for the core to come out of reset and the PLLs to lock (CCR bit 7 reads back 1)
  uint8_t attempt = 0;
  for (;;) {
    uint8_t status = _xfer(RA8876_SPI_STATUSREAD << 8);
    if ((status & RA8876_STSR_INHIBIT) == 0) {
      uint8_t ccr = _regRead(RA8876_CCR);
      if (ccr & 0x80) { _panelFound = true; break; }
      delay(2);
      _regWrite(RA8876_CCR, 0x80);
    }
    else if (++attempt >= 5) {
      attempt = 0;
      if (_pin_rst >= 0) {          // no answer yet: another hardware reset
        digitalWrite(_pin_rst, LOW);  delay(500);
        digitalWrite(_pin_rst, HIGH); delay(500);
        _selectedReg = -1;
      }
    }
    if (millis() - t0 > RA8876_INIT_TIMEOUT_MS) return;   // no panel fitted, or not powered
    delay(2);
  }

  // ---- SDRAM: W9812G6 (2M x 16 x 4 banks), CAS latency 3, refresh 1873 at 120 MHz ----
  _regWrite(RA8876_SDRAR, 0x29);
  _regWrite(RA8876_SDRMD, 0x03);
  _regWrite(RA8876_SDR_REF0, 0x51);
  _regWrite(RA8876_SDR_REF1, 0x07);
  _regWrite(RA8876_SDRCR, 0x01);
  delay(2);
  uint32_t left = (millis() - t0 < RA8876_INIT_TIMEOUT_MS) ? RA8876_INIT_TIMEOUT_MS - (millis() - t0) : 1;
  if (!_waitStatus(RA8876_STSR_SDRAM_READY, RA8876_STSR_SDRAM_READY, left)) {
    if (_log) _log->println("RPiPico-RA8876: the RA8876 SDRAM never reported ready");
    _panelFound = false;
    return;
  }

  // ---- Host interface, memory access, panel ----
  _regWrite(RA8876_CCR,  0x82);     // PLLs on, serial (SPI) host interface
  _regWrite(RA8876_MACR, 0x00);     // read and write left to right, top to bottom
  _regWrite(RA8876_ICR,  RA8876_ICR_GRAPHIC);
  _regWrite(RA8876_DPCR, RA8876_DPCR_PCLK_FALLING);   // display off while configuring
  _regWrite(RA8876_PCSR, 0xC0);     // HSYNC and VSYNC active low

  // Panel timing: 1024 x 600, HND 160, HST 160, HPW 70, VND 23, VST 12, VPW 10
  _regWrite(RA8876_HDWR,   RA8876_WIDTH / 8 - 1);
  _regWrite(RA8876_HDWFTR, RA8876_WIDTH % 8);
  _regWrite(RA8876_VDHR0,  (RA8876_HEIGHT - 1) & 0xFF);
  _regWrite(RA8876_VDHR1,  (RA8876_HEIGHT - 1) >> 8);
  _regWrite(RA8876_HNDR,   160 / 8 - 1);
  _regWrite(RA8876_HNDFTR, 160 % 8);
  _regWrite(RA8876_HSTR,   160 / 8 - 1);
  _regWrite(RA8876_HPWR,   70 / 8 - 1);
  _regWrite(RA8876_VNDR0,  23 - 1);
  _regWrite(RA8876_VNDR1,  0);
  _regWrite(RA8876_VSTR,   12 - 1);
  _regWrite(RA8876_VPWR,   10 - 1);

  _regWrite(RA8876_MPWCTR, RA8876_MPWCTR_16BPP);   // main image 16 bpp, PIPs off, sync mode
  _regWrite(RA8876_PIPCDEP, 0x05);                 // both PIP windows 16 bpp
  _regWrite(RA8876_AW_COLOR, 0x01);                // canvas: X/Y addressing, 16 bpp

  // Shown image and canvas: page 0, full panel width
  for (uint8_t i = 0; i < 4; i++) _regWrite(RA8876_MISA0 + i, 0);
  _regWrite(RA8876_MIW0,     RA8876_WIDTH & 0xFF);
  _regWrite(RA8876_MIW0 + 1, RA8876_WIDTH >> 8);
  for (uint8_t i = 0; i < 4; i++) _regWrite(RA8876_MWULX0 + i, 0);
  for (uint8_t i = 0; i < 4; i++) _regWrite(RA8876_CVSSA0 + i, 0);
  _regWrite(RA8876_CVS_IMWTH0,     RA8876_WIDTH & 0xFF);
  _regWrite(RA8876_CVS_IMWTH0 + 1, RA8876_WIDTH >> 8);

  // Text engine: no extra spacing
  _regWrite(RA8876_FLDR, 0);
  _regWrite(RA8876_F2FSSR, 0);

  // Backlight: PWM1 at 100% (as the TFT_eSPI_RA8876 driver set it)
  _regWrite(RA8876_PSCLR, 0x00);
  _regWrite(RA8876_PMUXR, 0x0A);
  _regWrite(RA8876_TCMPB0L, 0x00);
  _regWrite(RA8876_TCNTB0L, 0x00);
  _regWrite(RA8876_TCMPB1L, 0x64);
  _regWrite(RA8876_TCNTB1L, 0x64);
  _regWrite(RA8876_PCFGR, 0xD0);

  _regWrite(RA8876_DPCR, RA8876_DPCR_PCLK_FALLING | RA8876_DPCR_DISPLAY_ON);
  delay(5);

  // Check one register reads back
  uint8_t hdwr = _regRead(RA8876_HDWR);
  if (hdwr != RA8876_WIDTH / 8 - 1) {
    if (_log) _log->printf("RPiPico-RA8876: register read-back 0x%02X, expected 0x%02X -- check MISO\n",
                  hdwr, RA8876_WIDTH / 8 - 1);
  }

  _invalidateCaches();
  begin_tft_write();
  _activeWindowFull();
  _setCursor(0, 0);
  end_tft_write();
}

// Normal scan (the only orientation supported, see setRotation())
void RPiPico_RA8876::_ra8876Rotation(uint8_t m)
{
  (void)m;
  _regWrite(RA8876_DPCR, RA8876_DPCR_PCLK_FALLING | RA8876_DPCR_DISPLAY_ON);
}

/***************************************************************************************
**                         Diagnostics and raw access
***************************************************************************************/
uint8_t RPiPico_RA8876::readStatus(void)
{
  if (!_panelFound) return 0;
  begin_tft_read();
  uint8_t s = _xfer(RA8876_SPI_STATUSREAD << 8);
  end_tft_read();
  return s;
}

void RPiPico_RA8876::writeRegister(uint8_t reg, uint8_t value)
{
  if (!_panelFound) return;
  begin_tft_write();
  _regWrite(reg, value);
  _invalidateCaches();      // whatever this register was, trust nothing cached
  end_tft_write();
}

uint8_t RPiPico_RA8876::readRegister(uint8_t reg)
{
  if (!_panelFound) return 0;
  begin_tft_read();
  uint8_t v = _regRead(reg);
  end_tft_read();
  return v;
}

void RPiPico_RA8876::setStreamOptimisation(bool enable)
{
  _streamOpt = enable;
  _curValid = false;
}

/***************************************************************************************
**                         Hardware fill
***************************************************************************************/
void RPiPico_RA8876::setHardwareFill(bool enable, uint32_t minPixels)
{
  _hwFillOn  = enable;
  _hwFillMin = minPixels < 4 ? 4 : minPixels;
}

// Rectangle fill in the geometry engine, page coordinates. The engine hangs when the start
// and end coordinates are equal or reversed, so w and h must be at least 2.
bool RPiPico_RA8876::_hwFill(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t color)
{
  if (!_panelFound || w < 2 || h < 2) return false;
  _hwPrepare();
  _activeWindowFull();                 // the engine draws inside the active window only
  _setColorRegs(RA8876_FGCR, color);
  int32_t x1 = x + w - 1, y1 = y + h - 1;
  _regWrite(RA8876_DLHSR0,     x  & 0xFF);
  _regWrite(RA8876_DLHSR0 + 1, x  >> 8);
  _regWrite(RA8876_DLHSR0 + 2, y  & 0xFF);
  _regWrite(RA8876_DLHSR0 + 3, y  >> 8);
  _regWrite(RA8876_DLHER0,     x1 & 0xFF);
  _regWrite(RA8876_DLHER0 + 1, x1 >> 8);
  _regWrite(RA8876_DLHER0 + 2, y1 & 0xFF);
  _regWrite(RA8876_DLHER0 + 3, y1 >> 8);
  _regWrite(RA8876_DCR1, RA8876_DRAW_SQUARE_FILL);
  return _waitEngine();
}

bool RPiPico_RA8876::hwFillRect(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t color)
{
  if (x < 0) { w += x; x = 0; }
  if (y < 0) { h += y; y = 0; }
  if (x + w > _init_width)  w = _init_width - x;
  if (y + h > _init_height) h = _init_height - y;
  if (w < 2 || h < 2) return false;
  begin_tft_write();
  bool ok = _hwFill(x, y, w, h, color);
  end_tft_write();
  return ok;
}

// Fill in page coordinates, whatever the size (thin strips are sent as pixels)
static void _fillAny(RPiPico_RA8876 *tft, int32_t x, int32_t y, int32_t w, int32_t h, uint16_t color);

/***************************************************************************************
**                         Pages
***************************************************************************************/
uint32_t RPiPico_RA8876::pageAddress(uint8_t page)
{
  if (page >= RA8876_PAGES) page = RA8876_PAGES - 1;
  return (uint32_t)page * RA8876_PAGE_BYTES;
}

void RPiPico_RA8876::setDrawPage(uint8_t page)
{
  if (page >= RA8876_PAGES) return;
  _drawPage = page;
  if (!_panelFound) return;
  begin_tft_write();
  _streamEnd();
  _reg32(RA8876_CVSSA0, pageAddress(page));
  _reg16(RA8876_CVS_IMWTH0, _init_width);
  _curValid = false;
  end_tft_write();
}

// Waits for the VSYNC flag in the interrupt flag register. The first call finds out whether
// the flag is set with the VSYNC interrupt disabled (preferred: the RA8876's INT output then
// never changes) or only with it enabled; if neither, page switches stop waiting for it.
bool RPiPico_RA8876::waitForVSync(uint32_t timeout_ms)
{
  if (!_panelFound || !_vsyncWorks) return false;
  begin_tft_write();
  _streamEnd();
  bool seen = false;
  for (uint8_t mode = (_vsyncEnable ? _vsyncEnable : 1); mode <= 2 && !seen; mode++) {
    if (mode == 2) _regWrite(RA8876_INTEN, RA8876_INT_VSYNC);
    _regWrite(RA8876_INTF, RA8876_INT_VSYNC);        // clear the flag (write 1)
    uint32_t t0 = millis();
    do {
      if (_regRead(RA8876_INTF) & RA8876_INT_VSYNC) { seen = true; break; }
    } while (millis() - t0 < timeout_ms);
    _regWrite(RA8876_INTF, RA8876_INT_VSYNC);
    if (mode == 2) _regWrite(RA8876_INTEN, 0);
    if (seen) _vsyncEnable = mode;
    if (_vsyncEnable) break;                         // known mode: no fallback
  }
  end_tft_write();
  if (!seen && !_vsyncEnable) {
    _vsyncWorks = false;
    if (_log) _log->println("RPiPico-RA8876: no VSYNC flag from the RA8876 -- page switches will not wait for it");
  }
  return seen;
}

void RPiPico_RA8876::showPage(uint8_t page, bool vsync)
{
  if (page >= RA8876_PAGES) return;
  _showPage = page;
  if (!_panelFound) return;
  if (vsync) waitForVSync();
  begin_tft_write();
  _streamEnd();
  uint32_t a = pageAddress(page);
  _regWrite(RA8876_MISA0,     a & 0xFF);
  _regWrite(RA8876_MISA0 + 1, (a >> 8) & 0xFF);
  _regWrite(RA8876_MISA0 + 2, (a >> 16) & 0xFF);
  _regWrite(RA8876_MISA0 + 3, a >> 24);
  end_tft_write();
}

void RPiPico_RA8876::fillPage(uint8_t page, uint16_t color)
{
  if (page >= RA8876_PAGES || !_panelFound) return;
  uint8_t was = _drawPage;
  if (was != page) setDrawPage(page);
  begin_tft_write();
  _hwFill(0, 0, _init_width, _init_height, color);
  end_tft_write();
  if (was != page) setDrawPage(was);
}

void RPiPico_RA8876::copyPage(uint8_t srcPage, uint8_t dstPage)
{
  copyRect(srcPage, 0, 0, dstPage, 0, 0, _init_width, _init_height);
}

/***************************************************************************************
**                         Block transfer engine
***************************************************************************************/
bool RPiPico_RA8876::_bteRun(uint8_t ctrl1, uint32_t s0, int32_t s0x, int32_t s0y,
                            uint32_t s1, int32_t s1x, int32_t s1y,
                            uint32_t dst, int32_t dx, int32_t dy, int32_t w, int32_t h)
{
  if (!_panelFound) return false;
  _hwPrepare();
  const uint8_t regs[] = { RA8876_S0_STR0, RA8876_S1_STR0, RA8876_DT_STR0 };
  const uint32_t addr[] = { s0, s1, dst };
  const int32_t xs[] = { s0x, s1x, dx }, ys[] = { s0y, s1y, dy };
  for (uint8_t i = 0; i < 3; i++) {
    uint8_t r = regs[i];
    _regWrite(r,      addr[i] & 0xFF);          // start address
    _regWrite(r + 1, (addr[i] >> 8) & 0xFF);
    _regWrite(r + 2, (addr[i] >> 16) & 0xFF);
    _regWrite(r + 3,  addr[i] >> 24);
    _regWrite(r + 4,  _init_width & 0xFF);      // image width
    _regWrite(r + 5,  _init_width >> 8);
    _regWrite(r + 6,  xs[i] & 0xFF);            // window x, y
    _regWrite(r + 7,  xs[i] >> 8);
    _regWrite(r + 8,  ys[i] & 0xFF);
    _regWrite(r + 9,  ys[i] >> 8);
  }
  _regWrite(RA8876_BTE_WTH0,     w & 0xFF);
  _regWrite(RA8876_BTE_WTH0 + 1, w >> 8);
  _regWrite(RA8876_BTE_WTH0 + 2, h & 0xFF);
  _regWrite(RA8876_BTE_WTH0 + 3, h >> 8);
  _regWrite(RA8876_BTE_CTRL1, ctrl1);
  _regWrite(RA8876_BTE_COLR, RA8876_BTE_COLR_16BPP);
  _regWrite(RA8876_BTE_CTRL0, RA8876_BTE_START);
  return _waitEngine();
}

// Clip a block to the page; false if nothing is left. Both corners move together.
static bool _clipBlock(int32_t W, int32_t H, int32_t &sx, int32_t &sy, int32_t &dx, int32_t &dy, int32_t &w, int32_t &h)
{
  if (sx < 0) { w += sx; dx -= sx; sx = 0; }
  if (sy < 0) { h += sy; dy -= sy; sy = 0; }
  if (dx < 0) { w += dx; sx -= dx; dx = 0; }
  if (dy < 0) { h += dy; sy -= dy; dy = 0; }
  if (sx + w > W) w = W - sx;
  if (dx + w > W) w = W - dx;
  if (sy + h > H) h = H - sy;
  if (dy + h > H) h = H - dy;
  return w > 0 && h > 0;
}

bool RPiPico_RA8876::copyRect(uint8_t srcPage, int32_t sx, int32_t sy,
                              uint8_t dstPage, int32_t dx, int32_t dy, int32_t w, int32_t h)
{
  if (srcPage >= RA8876_PAGES || dstPage >= RA8876_PAGES) return false;
  if (!_clipBlock(_init_width, _init_height, sx, sy, dx, dy, w, h)) return false;
  begin_tft_write();
  bool ok = _bteRun(RA8876_ROP_S0 << 4 | RA8876_BTE_MEMORY_COPY_ROP,
                    pageAddress(srcPage), sx, sy, pageAddress(srcPage), sx, sy,
                    pageAddress(dstPage), dx, dy, w, h);
  end_tft_write();
  return ok;
}

bool RPiPico_RA8876::copyRectTransparent(uint8_t srcPage, int32_t sx, int32_t sy,
                                         uint8_t dstPage, int32_t dx, int32_t dy, int32_t w, int32_t h,
                                         uint16_t transparentColor)
{
  if (srcPage >= RA8876_PAGES || dstPage >= RA8876_PAGES) return false;
  if (!_clipBlock(_init_width, _init_height, sx, sy, dx, dy, w, h)) return false;
  begin_tft_write();
  _setColorRegs(RA8876_BGCR, transparentColor);   // the chroma key is the background colour
  bool ok = _bteRun(RA8876_BTE_MEMORY_COPY_CHROMA,
                    pageAddress(srcPage), sx, sy, pageAddress(srcPage), sx, sy,
                    pageAddress(dstPage), dx, dy, w, h);
  end_tft_write();
  return ok;
}

bool RPiPico_RA8876::blendRect(uint8_t page0, int32_t x0, int32_t y0,
                               uint8_t page1, int32_t x1, int32_t y1,
                               uint8_t dstPage, int32_t dx, int32_t dy, int32_t w, int32_t h, uint8_t alpha)
{
  if (page0 >= RA8876_PAGES || page1 >= RA8876_PAGES || dstPage >= RA8876_PAGES) return false;
  if (alpha > 32) alpha = 32;
  // Clip against both sources and the destination
  int32_t ax = x0, ay = y0, bx = dx, by = dy, aw = w, ah = h;
  if (!_clipBlock(_init_width, _init_height, ax, ay, bx, by, aw, ah)) return false;
  x1 += ax - x0; y1 += ay - y0;
  int32_t cx = x1, cy = y1, ex = bx, ey = by;
  if (!_clipBlock(_init_width, _init_height, cx, cy, ex, ey, aw, ah)) return false;
  ax += cx - x1; ay += cy - y1;
  begin_tft_write();
  _hwPrepare();
  _regWrite(RA8876_APB_CTRL, alpha);
  bool ok = _bteRun(RA8876_BTE_MEMORY_COPY_ALPHA,
                    pageAddress(page0), ax, ay, pageAddress(page1), cx, cy,
                    pageAddress(dstPage), ex, ey, aw, ah);
  end_tft_write();
  return ok;
}

/***************************************************************************************
**                         Scrolling
***************************************************************************************/
static void _fillAny(RPiPico_RA8876 *tft, int32_t x, int32_t y, int32_t w, int32_t h, uint16_t color)
{
  if (w <= 0 || h <= 0) return;
  if (w >= 2 && h >= 2 && tft->hwFillRect(x, y, w, h, color)) return;
  tft->startWrite();
  tft->setWindow(x, y, x + w - 1, y + h - 1);
  tft->pushBlock(color, w * h);
  tft->endWrite();
}

// The move goes through the library's scratch page: copying a block onto an overlapping
// part of itself is not safe in one step.
bool RPiPico_RA8876::scrollRect(int32_t x, int32_t y, int32_t w, int32_t h, int32_t dx, int32_t dy, int32_t fillColor)
{
  if (!_panelFound) return false;
  if (x < 0) { w += x; x = 0; }
  if (y < 0) { h += y; y = 0; }
  if (x + w > _init_width)  w = _init_width - x;
  if (y + h > _init_height) h = _init_height - y;
  if (w <= 0 || h <= 0) return false;

  int32_t adx = dx < 0 ? -dx : dx, ady = dy < 0 ? -dy : dy;
  bool ok = true;
  if (adx < w && ady < h && (adx || ady)) {
    int32_t mw = w - adx, mh = h - ady;
    int32_t sx = x + (dx < 0 ? adx : 0), sy = y + (dy < 0 ? ady : 0);
    int32_t tx = x + (dx > 0 ? adx : 0), ty = y + (dy > 0 ? ady : 0);
    if (dx == 0 && dy < 0) {
      // Straight up: the engine copies top to bottom, so each row is read before anything is
      // written over it -- one copy in place
      ok = copyRect(_drawPage, sx, sy, _drawPage, tx, ty, mw, mh);
    } else {
      ok = copyRect(_drawPage, sx, sy, RA8876_PAGE_SCRATCH, 0, 0, mw, mh) &&
           copyRect(RA8876_PAGE_SCRATCH, 0, 0, _drawPage, tx, ty, mw, mh);
    }
  }
  if (fillColor >= 0) {
    uint16_t c = (uint16_t)fillColor;
    if (adx >= w || ady >= h) _fillAny(this, x, y, w, h, c);
    else {
      if (dy > 0) _fillAny(this, x, y, w, ady, c);
      if (dy < 0) _fillAny(this, x, y + h - ady, w, ady, c);
      int32_t ry = y + (dy > 0 ? ady : 0), rh = h - ady;
      if (dx > 0) _fillAny(this, x, ry, adx, rh, c);
      if (dx < 0) _fillAny(this, x + w - adx, ry, adx, rh, c);
    }
  }
  return ok;
}

/***************************************************************************************
**                         Off-screen composition
***************************************************************************************/
void RPiPico_RA8876::beginComposite(int32_t x, int32_t y, int32_t w, int32_t h, bool copyBackground)
{
  if (_composite) endComposite();
  if (x < 0) { w += x; x = 0; }
  if (y < 0) { h += y; y = 0; }
  if (x + w > _init_width)  w = _init_width - x;
  if (y + h > _init_height) h = _init_height - y;
  if (w <= 0 || h <= 0 || !_panelFound) return;
  _compX = x; _compY = y; _compW = w; _compH = h;
  _compPrevPage = _drawPage;
  if (copyBackground) copyRect(_drawPage, x, y, RA8876_PAGE_COMPOSE, x, y, w, h);
  setDrawPage(RA8876_PAGE_COMPOSE);
  _composite = true;
}

void RPiPico_RA8876::endComposite(void)
{
  if (!_composite) return;
  _composite = false;
  setDrawPage(_compPrevPage);
  copyRect(RA8876_PAGE_COMPOSE, _compX, _compY, _compPrevPage, _compX, _compY, _compW, _compH);
}

/***************************************************************************************
**                         Picture-in-picture
***************************************************************************************/
bool RPiPico_RA8876::pipShow(uint8_t pip, uint8_t srcPage, int32_t sx, int32_t sy,
                             int32_t dx, int32_t dy, int32_t w, int32_t h)
{
  if (!_panelFound || (pip != 1 && pip != 2) || srcPage >= RA8876_PAGES) return false;
  sx &= ~3; dx &= ~3; w &= ~3;          // controller limit: multiples of 4
  if (sx < 0 || sy < 0 || dx < 0 || dy < 0 || w <= 0 || h <= 0) return false;
  if (sx + w > _init_width || sy + h > _init_height) return false;
  if (dx + w > _init_width)  w = (_init_width - dx) & ~3;
  if (dy + h > _init_height) h = _init_height - dy;
  if (w <= 0 || h <= 0) return false;

  begin_tft_write();
  _streamEnd();
  _mpwctr = (_mpwctr & ~RA8876_MPWCTR_CFG_PIP2) | (pip == 2 ? RA8876_MPWCTR_CFG_PIP2 : 0);
  _regWrite(RA8876_MPWCTR, _mpwctr);
  uint32_t a = pageAddress(srcPage);
  const uint16_t v[] = { (uint16_t)dx, (uint16_t)dy };
  for (uint8_t i = 0; i < 2; i++) {     // where it shows
    _regWrite(RA8876_PWDULX0 + 2 * i,     v[i] & 0xFF);
    _regWrite(RA8876_PWDULX0 + 2 * i + 1, v[i] >> 8);
  }
  _regWrite(RA8876_PISA0,     a & 0xFF);
  _regWrite(RA8876_PISA0 + 1, (a >> 8) & 0xFF);
  _regWrite(RA8876_PISA0 + 2, (a >> 16) & 0xFF);
  _regWrite(RA8876_PISA0 + 3, a >> 24);
  _regWrite(RA8876_PIW0,      _init_width & 0xFF);
  _regWrite(RA8876_PIW0 + 1,  _init_width >> 8);
  _regWrite(RA8876_PWIULX0,     sx & 0xFF);
  _regWrite(RA8876_PWIULX0 + 1, sx >> 8);
  _regWrite(RA8876_PWIULX0 + 2, sy & 0xFF);
  _regWrite(RA8876_PWIULX0 + 3, sy >> 8);
  _regWrite(RA8876_PWW0,     w & 0xFF);
  _regWrite(RA8876_PWW0 + 1, w >> 8);
  _regWrite(RA8876_PWH0,     h & 0xFF);
  _regWrite(RA8876_PWH0 + 1, h >> 8);
  _mpwctr |= (pip == 1 ? RA8876_MPWCTR_PIP1_EN : RA8876_MPWCTR_PIP2_EN);
  _regWrite(RA8876_MPWCTR, _mpwctr);
  end_tft_write();
  return true;
}

void RPiPico_RA8876::pipMove(uint8_t pip, int32_t dx, int32_t dy)
{
  if (!_panelFound || (pip != 1 && pip != 2) || dx < 0 || dy < 0) return;
  dx &= ~3;
  begin_tft_write();
  _streamEnd();
  _mpwctr = (_mpwctr & ~RA8876_MPWCTR_CFG_PIP2) | (pip == 2 ? RA8876_MPWCTR_CFG_PIP2 : 0);
  _regWrite(RA8876_MPWCTR, _mpwctr);
  _regWrite(RA8876_PWDULX0,     dx & 0xFF);
  _regWrite(RA8876_PWDULX0 + 1, dx >> 8);
  _regWrite(RA8876_PWDULX0 + 2, dy & 0xFF);
  _regWrite(RA8876_PWDULX0 + 3, dy >> 8);
  end_tft_write();
}

void RPiPico_RA8876::pipHide(uint8_t pip)
{
  if (!_panelFound || (pip != 1 && pip != 2)) return;
  begin_tft_write();
  _streamEnd();
  _mpwctr &= ~(pip == 1 ? RA8876_MPWCTR_PIP1_EN : RA8876_MPWCTR_PIP2_EN);
  _regWrite(RA8876_MPWCTR, _mpwctr);
  end_tft_write();
}

/***************************************************************************************
**                         Character ROM text
***************************************************************************************/
// UTF-8 text: decoded to ISO 8859-1 codes (anything else shows as '?')
int16_t RPiPico_RA8876::_romText(int32_t x, int32_t y, const char *s, uint16_t len, uint8_t f)
{
  uint8_t  codes[64];
  uint16_t n = 0, i = 0;
  int16_t  sum = 0;
  while (i < len) {
    uint16_t uniCode = decodeUTF8((uint8_t*)s, &i, len - i);
    if (uniCode < 32) continue;
    codes[n++] = _romCode(uniCode);
    if (n == sizeof(codes)) {
      sum += _romTextRaw(x + sum, y, codes, n, f);
      n = 0;
    }
  }
  if (n) sum += _romTextRaw(x + sum, y, codes, n, f);
  return sum;
}

// The controller draws the characters itself: one SPI command per character. Characters
// are drawn whole: those not wholly inside the viewport are left out.
int16_t RPiPico_RA8876::_romTextRaw(int32_t x, int32_t y, const uint8_t *codes, uint16_t n, uint8_t f)
{
  uint8_t sc = _romScale();
  int32_t cw = _romCharWidth(f) * sc;
  int32_t ch = f * sc;
  int16_t total = n * cw;
  if (!_panelFound || _vpOoB || n == 0) return total;

  int32_t xd = x + _xDatum;
  int32_t yd = y + _yDatum;
  if (yd < _vpY || yd + ch > _vpH) return total;
  uint16_t first = 0, last = n;
  while (first < n && xd + first * cw < _vpX) first++;
  while (last > first && xd + last * cw > _vpW) last--;
  if (first >= last) return total;

  bool transparent = (textcolor == textbgcolor);
  uint8_t size = (f == ROM_FONT_16) ? 0 : (f == ROM_FONT_24) ? 1 : 2;

  begin_tft_write();
  _hwPrepare();
  _activeWindowFull();
  _regWriteCached(RA8876_CCR0, size << 4);                       // internal ROM, ISO 8859-1
  _regWriteCached(RA8876_CCR1, (transparent ? RA8876_CCR1_TRANSPARENT : 0) | (sc - 1) << 2 | (sc - 1));
  _setColorRegs(RA8876_FGCR, textcolor);
  if (!transparent) _setColorRegs(RA8876_BGCR, textbgcolor);
  _textMode();

  int32_t tx = xd + first * cw;
  _regWrite(RA8876_F_CURX0,     tx & 0xFF);
  _regWrite(RA8876_F_CURX0 + 1, tx >> 8);
  _regWrite(RA8876_F_CURX0 + 2, yd & 0xFF);
  _regWrite(RA8876_F_CURX0 + 3, yd >> 8);
  _selectReg(RA8876_MRWDP);
  for (uint16_t i = first; i < last; i++) {
    _xfer(RA8876_SPI_DATAWRITE << 8 | codes[i]);
    if (!_waitEngine()) break;
  }
  _graphicMode();
  end_tft_write();
  return total;
}
