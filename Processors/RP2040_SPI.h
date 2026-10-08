/***************************************************************************************
  RP2040_SPI.h -- RP2040/RP2350 hardware SPI support for RPiPico-RA8876.

  The SPI port runs permanently in 16-bit frames, MSB first. One frame carries either an RA8876
  command (prefix byte + payload byte) or, inside a pixel stream, one RGB565 pixel.

  Pixel byte order: the RA8876 takes the LOW byte of each pixel first. A 16-bit frame sends its
  high byte first, so an ordinary RGB565 colour is byte-swapped on the way out. The macros keep
  TFT_eSPI's meanings:
    tft_Write_16(C)  / tft_Write_16N(C)  C is an ordinary RGB565 colour
    tft_Write_16S(C)                     C is already byte-swapped (sprite buffers, readRect())
  These macros are used inside RPiPico_RA8876 member functions only: they reach the SPI port
  through the member _spiHw and count pixels in _pixCount (see setWindow()).
 ***************************************************************************************/
#ifndef _RPIPICO_RA8876_RP2040_SPI_H_
#define _RPIPICO_RA8876_RP2040_SPI_H_

#include <LittleFS.h>
#define FONT_FS_AVAILABLE
#define SPIFFS LittleFS

#include "hardware/spi.h"
#include "hardware/gpio.h"
#include "hardware/dma.h"
#include "hardware/clocks.h"

// Processor ID reported by getSetup()
#define PROCESSOR_ID 0x2040

// If smooth fonts are enabled the filing system may need to be loaded
#if defined (SMOOTH_FONT)
  #include <FS.h>
#endif

// Wait until there is room in the transmit FIFO
#define RA8876_SPI_TX_WAIT   while (!(_spiHw->sr & SPI_SSPSR_TNF_BITS)) {}

// Wait for the end of transmission, then empty the receive FIFO and clear its overrun flag
#define SPI_BUSY_CHECK  do { while (_spiHw->sr & SPI_SSPSR_BSY_BITS) {}                    \
                             while (_spiHw->sr & SPI_SSPSR_RNE_BITS) (void)_spiHw->dr;     \
                             _spiHw->icr = SPI_SSPICR_RORIC_BITS; } while (0)

// Pixels, sent only into an open stream (setWindow() opens one; nothing is sent when no panel
// answered init()). The following macros do not wait for the end of transmission.
#define tft_Write_16(C)   do { if (_streamOpen) { uint16_t _c = (uint16_t)(C); RA8876_SPI_TX_WAIT;     \
                               _spiHw->dr = (uint16_t)((_c << 8) | (_c >> 8)); _pixCount++; } } while (0)
#define tft_Write_16N(C)  tft_Write_16(C)
#define tft_Write_16S(C)  do { if (_streamOpen) { uint16_t _c = (uint16_t)(C); RA8876_SPI_TX_WAIT;     \
                               _spiHw->dr = _c; _pixCount++; } } while (0)

// Wait for a DMA transfer to complete
#define DMA_BUSY_CHECK dmaWait()

#endif // _RPIPICO_RA8876_RP2040_SPI_H_
