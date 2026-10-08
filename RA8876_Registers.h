/***************************************************************************************
  RA8876_Registers.h -- RA8876 register addresses and bit values used by RPiPico-RA8876.

  Only what this library uses. Names follow the RAiO datasheet. Values cross-checked against
  the RA8876_RP2040 library (RA8876Registers.h) and the TFT_eSPI_RA8876 RA8876 driver.
 ***************************************************************************************/
#ifndef _RPIPICO_RA8876_REGISTERS_H_
#define _RPIPICO_RA8876_REGISTERS_H_

// ---- 4-wire SPI protocol: the first byte of every CS-low transfer says what follows ------
#define RA8876_SPI_CMDWRITE    0x00   // next byte: register address
#define RA8876_SPI_STATUSREAD  0x40   // next byte: status register (read)
#define RA8876_SPI_DATAWRITE   0x80   // next byte(s): data for the selected register
#define RA8876_SPI_DATAREAD    0xC0   // next byte: data from the selected register (read)

// ---- Status register (read with RA8876_SPI_STATUSREAD) -----------------------------------
#define RA8876_STSR_WFIFO_FULL    0x80  // host memory write FIFO full
#define RA8876_STSR_WFIFO_EMPTY   0x40  // host memory write FIFO empty
#define RA8876_STSR_RFIFO_FULL    0x20  // host memory read FIFO full
#define RA8876_STSR_RFIFO_EMPTY   0x10  // host memory read FIFO empty
#define RA8876_STSR_CORE_BUSY     0x08  // geometry engine, BTE or text write in progress
#define RA8876_STSR_SDRAM_READY   0x04  // SDRAM initialised and ready
#define RA8876_STSR_INHIBIT       0x02  // operation inhibited (reset / PLL not locked)

// ---- System ------------------------------------------------------------------------------
#define RA8876_SRR      0x00   // software reset / chip ID
#define RA8876_CCR      0x01   // chip configuration (PLL enable, host interface)
#define RA8876_MACR     0x02   // memory access control (read/write direction)
#define RA8876_ICR      0x03   // input control: graphic/text mode, memory select
#define RA8876_MRWDP    0x04   // memory read/write data port

#define RA8876_ICR_GRAPHIC   0x00
#define RA8876_ICR_TEXT      0x04

// PLL
#define RA8876_PPLLC1   0x05   // pixel clock
#define RA8876_PPLLC2   0x06
#define RA8876_MPLLC1   0x07   // SDRAM clock
#define RA8876_MPLLC2   0x08
#define RA8876_SPLLC1   0x09   // core clock
#define RA8876_SPLLC2   0x0A

// Interrupts (only the flag register is polled; the INT pin is not needed)
#define RA8876_INTEN    0x0B
#define RA8876_INTF     0x0C
#define RA8876_INT_VSYNC     0x10  // VSYNC time-base flag/enable

// ---- Display (main window, PIP) ----------------------------------------------------------
#define RA8876_MPWCTR   0x10   // main/PIP window control
#define RA8876_MPWCTR_PIP1_EN   0x80
#define RA8876_MPWCTR_PIP2_EN   0x40
#define RA8876_MPWCTR_CFG_PIP2  0x10   // 0: registers 0x2A-0x3B configure PIP1, 1: PIP2
#define RA8876_MPWCTR_16BPP     0x04   // main image colour depth 16 bpp
#define RA8876_PIPCDEP  0x11   // PIP colour depth: bits 3:2 PIP1, bits 1:0 PIP2 (01 = 16 bpp)
#define RA8876_DPCR     0x12   // display configuration: PCLK edge, display on, scan direction
#define RA8876_DPCR_PCLK_FALLING 0x80
#define RA8876_DPCR_DISPLAY_ON   0x40
#define RA8876_DPCR_COLOR_BAR    0x20   // test pattern
#define RA8876_DPCR_VSCAN_BTT    0x08   // scan bottom to top (bit 4 is reserved: it blanks the panel)
#define RA8876_PCSR     0x13   // panel sync polarity

// Panel timing
#define RA8876_HDWR     0x14
#define RA8876_HDWFTR   0x15
#define RA8876_HNDR     0x16
#define RA8876_HNDFTR   0x17
#define RA8876_HSTR     0x18
#define RA8876_HPWR     0x19
#define RA8876_VDHR0    0x1A
#define RA8876_VDHR1    0x1B
#define RA8876_VNDR0    0x1C
#define RA8876_VNDR1    0x1D
#define RA8876_VSTR     0x1E
#define RA8876_VPWR     0x1F

// Main image: what the panel shows
#define RA8876_MISA0    0x20   // start address [7:0] .. 0x23 [31:24]
#define RA8876_MIW0     0x24   // image width [7:0], 0x25 [12:8]
#define RA8876_MWULX0   0x26   // window upper-left X/Y, 0x26..0x29

// PIP windows (registers address PIP1 or PIP2 per RA8876_MPWCTR_CFG_PIP2)
#define RA8876_PWDULX0  0x2A   // display position X [7:0], 0x2B [12:8], Y 0x2C/0x2D
#define RA8876_PISA0    0x2E   // image start address 0x2E..0x31
#define RA8876_PIW0     0x32   // image width 0x32/0x33
#define RA8876_PWIULX0  0x34   // window position inside the image X 0x34/0x35, Y 0x36/0x37
#define RA8876_PWW0     0x38   // window width 0x38/0x39
#define RA8876_PWH0     0x3A   // window height 0x3A/0x3B

// ---- Canvas: where drawing goes ----------------------------------------------------------
#define RA8876_CVSSA0   0x50   // canvas start address 0x50..0x53
#define RA8876_CVS_IMWTH0 0x54 // canvas image width 0x54/0x55
#define RA8876_AWUL_X0  0x56   // active window X 0x56/0x57, Y 0x58/0x59
#define RA8876_AW_WTH0  0x5A   // active window width 0x5A/0x5B
#define RA8876_AW_HT0   0x5C   // active window height 0x5C/0x5D
#define RA8876_AW_COLOR 0x5E   // canvas addressing mode and colour depth
#define RA8876_CURH0    0x5F   // graphic read/write cursor X 0x5F/0x60, Y 0x61/0x62
#define RA8876_F_CURX0  0x63   // text cursor X 0x63/0x64, Y 0x65/0x66

// ---- Geometry engine ---------------------------------------------------------------------
#define RA8876_DLHSR0   0x68   // rectangle start X 0x68/0x69, Y 0x6A/0x6B
#define RA8876_DLHER0   0x6C   // rectangle end X 0x6C/0x6D, Y 0x6E/0x6F (inclusive)
#define RA8876_DCR1     0x76
#define RA8876_DRAW_SQUARE_FILL 0xE0

// ---- PWM (backlight) ---------------------------------------------------------------------
#define RA8876_PSCLR    0x84
#define RA8876_PMUXR    0x85
#define RA8876_PCFGR    0x86
#define RA8876_TCMPB0L  0x88
#define RA8876_TCMPB0H  0x89
#define RA8876_TCNTB0L  0x8A
#define RA8876_TCNTB0H  0x8B
#define RA8876_TCMPB1L  0x8C
#define RA8876_TCMPB1H  0x8D
#define RA8876_TCNTB1L  0x8E
#define RA8876_TCNTB1H  0x8F

// ---- Block transfer engine ---------------------------------------------------------------
#define RA8876_BTE_CTRL0 0x90  // write 0x10 to start; bit 4 reads 1 while busy
#define RA8876_BTE_CTRL1 0x91  // ROP code << 4 | operation
#define RA8876_BTE_COLR  0x92  // colour depths of S0, S1 and destination
#define RA8876_S0_STR0   0x93  // source 0 address 0x93..0x96, width 0x97/0x98, X/Y 0x99..0x9C
#define RA8876_S0_WTH0   0x97
#define RA8876_S0_X0     0x99
#define RA8876_S1_STR0   0x9D  // source 1 address 0x9D..0xA0, width 0xA1/0xA2, X/Y 0xA3..0xA6
#define RA8876_S1_WTH0   0xA1
#define RA8876_S1_X0     0xA3
#define RA8876_DT_STR0   0xA7  // destination address 0xA7..0xAA, width 0xAB/0xAC, X/Y 0xAD..0xB0
#define RA8876_DT_WTH0   0xAB
#define RA8876_DT_X0     0xAD
#define RA8876_BTE_WTH0  0xB1  // block width 0xB1/0xB2, height 0xB3/0xB4
#define RA8876_APB_CTRL  0xB5  // alpha level for window alpha blending (0..32)

#define RA8876_BTE_START             0x10
#define RA8876_BTE_COLR_16BPP        0x25   // S0, S1 and destination all 16 bpp
#define RA8876_BTE_MEMORY_COPY_ROP   2
#define RA8876_BTE_MEMORY_COPY_CHROMA 5
#define RA8876_BTE_MEMORY_COPY_ALPHA 10
#define RA8876_ROP_S0                12     // destination = source 0

// ---- Text (character ROM) ----------------------------------------------------------------
#define RA8876_CCR0     0xCC   // font source << 6 | size << 4 | ISO 8859 set
#define RA8876_CCR1     0xCD   // full align << 7 | transparent << 6 | rotate << 4 | x scale << 2 | y scale
#define RA8876_FLDR     0xD0   // extra line gap (pixels)
#define RA8876_F2FSSR   0xD1   // extra character gap (pixels)
#define RA8876_FGCR     0xD2   // foreground red/green/blue 0xD2..0xD4
#define RA8876_BGCR     0xD5   // background red/green/blue 0xD5..0xD7

#define RA8876_CCR1_TRANSPARENT  0x40

// ---- SDRAM -------------------------------------------------------------------------------
#define RA8876_SDRAR    0xE0
#define RA8876_SDRMD    0xE1
#define RA8876_SDR_REF0 0xE2
#define RA8876_SDR_REF1 0xE3
#define RA8876_SDRCR    0xE4

#endif // _RPIPICO_RA8876_REGISTERS_H_
