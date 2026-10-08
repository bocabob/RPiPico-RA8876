/***************************************************************************************
  RP2040_SPI.c -- pixel streaming and DMA for RPiPico-RA8876. #included by RPiPico-RA8876.cpp.

  Both functions write into a stream opened by setWindow(). See RP2040_SPI.h for the byte order.
 ***************************************************************************************/
#ifdef _RPIPICO_RA8876_H_

/***************************************************************************************
** Function name:           pushBlock
** Description:             Write a block of pixels of the same colour
***************************************************************************************/
void RPiPico_RA8876::pushBlock(uint16_t color, uint32_t len)
{
  if (!_streamOpen) {                 // after setAddrWindow(), or no panel
    if (!_panelFound) return;
    _streamBegin();
  }
  uint16_t swapped = (uint16_t)((color << 8) | (color >> 8));
  _pixCount += len;
  // The TX FIFO holds 8 frames: keep it topped up in groups to cut the status polling
  while (len >= 4) {
    while ((_spiHw->sr & SPI_SSPSR_TNF_BITS) == 0) {}
    _spiHw->dr = swapped;
    while ((_spiHw->sr & SPI_SSPSR_TNF_BITS) == 0) {}
    _spiHw->dr = swapped;
    while ((_spiHw->sr & SPI_SSPSR_TNF_BITS) == 0) {}
    _spiHw->dr = swapped;
    while ((_spiHw->sr & SPI_SSPSR_TNF_BITS) == 0) {}
    _spiHw->dr = swapped;
    len -= 4;
  }
  while (len--) {
    while ((_spiHw->sr & SPI_SSPSR_TNF_BITS) == 0) {}
    _spiHw->dr = swapped;
  }
}

/***************************************************************************************
** Function name:           pushPixels
** Description:             Write a sequence of pixels
***************************************************************************************/
// _swapBytes true: the data are ordinary RGB565 values, swap each one for the RA8876.
// _swapBytes false: the data are stored byte-swapped already (the TFT_eSPI default), send as is.
void RPiPico_RA8876::pushPixels(const void* data_in, uint32_t len)
{
  if (!_streamOpen) {                 // after setAddrWindow(), or no panel
    if (!_panelFound) return;
    _streamBegin();
  }
  const uint16_t *data = (const uint16_t*)data_in;
  _pixCount += len;
  if (_swapBytes) {
    while (len--) {
      uint16_t color = *data++;
      while ((_spiHw->sr & SPI_SSPSR_TNF_BITS) == 0) {}
      _spiHw->dr = (uint16_t)((color << 8) | (color >> 8));
    }
  }
  else {
    while (len--) {
      while ((_spiHw->sr & SPI_SSPSR_TNF_BITS) == 0) {}
      _spiHw->dr = *data++;
    }
  }
}

////////////////////////////////////////////////////////////////////////////////////////
// DMA
////////////////////////////////////////////////////////////////////////////////////////

/***************************************************************************************
** Function name:           dmaBusy
** Description:             Check if DMA is busy
***************************************************************************************/
bool RPiPico_RA8876::dmaBusy(void) {
  if (!DMA_Enabled) return false;

  if (dma_channel_is_busy(dma_tx_channel)) return true;

  // The last frames may still be in the SPI FIFO
  while (_spiHw->sr & SPI_SSPSR_BSY_BITS) {};

  return false;
}

/***************************************************************************************
** Function name:           dmaWait
** Description:             Wait until DMA is over (blocking!)
***************************************************************************************/
void RPiPico_RA8876::dmaWait(void)
{
  if (!DMA_Enabled) return;

  while (dma_channel_is_busy(dma_tx_channel));

  // The last frames may still be in the SPI FIFO
  while (_spiHw->sr & SPI_SSPSR_BSY_BITS) {};
}

/***************************************************************************************
** Function name:           pushPixelsDMA
** Description:             Push pixels to the window set by setAddrWindow()
***************************************************************************************/
void RPiPico_RA8876::pushPixelsDMA(uint16_t* image, uint32_t len)
{
  if ((len == 0) || (!DMA_Enabled)) return;

  dmaWait();

  if (!_streamOpen) {                 // after setAddrWindow(), or no panel
    if (!_panelFound) return;
    _streamBegin();
  }
  _pixCount += len;
  channel_config_set_bswap(&dma_tx_config, _swapBytes);
  dma_channel_configure(dma_tx_channel, &dma_tx_config, &_spiHw->dr, (uint16_t*)image, len, true);
}

/***************************************************************************************
** Function name:           pushImageDMA
** Description:             Push image to a window
***************************************************************************************/
// This will clip to the viewport
void RPiPico_RA8876::pushImageDMA(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t* image, uint16_t* buffer)
{
  if ((x >= _vpW) || (y >= _vpH) || (!DMA_Enabled)) return;

  int32_t dx = 0;
  int32_t dy = 0;
  int32_t dw = w;
  int32_t dh = h;

  if (x < _vpX) { dx = _vpX - x; dw -= dx; x = _vpX; }
  if (y < _vpY) { dy = _vpY - y; dh -= dy; y = _vpY; }

  if ((x + dw) > _vpW ) dw = _vpW - x;
  if ((y + dh) > _vpH ) dh = _vpH - y;

  if (dw < 1 || dh < 1) return;

  uint32_t len = dw*dh;

  if (buffer == nullptr) {
    buffer = image;
    dmaWait();
  }

  // If image is clipped, copy pixels into a contiguous block
  if ( (dw != w) || (dh != h) ) {
    for (int32_t yb = 0; yb < dh; yb++) {
      memmove((uint8_t*) (buffer + yb * dw), (uint8_t*) (image + dx + w * (yb + dy)), dw << 1);
    }
  }
  // else, if a buffer pointer has been provided copy whole image to the buffer
  else if (buffer != image || _swapBytes) {
    memcpy(buffer, image, len*2);
  }

  dmaWait(); // In case we did not wait earlier

  begin_tft_write();
  setWindow(x, y, x + dw - 1, y + dh - 1);   // leaves the pixel stream open for the DMA
  if (!_streamOpen) return;                 // no panel

  _pixCount += len;
  channel_config_set_bswap(&dma_tx_config, _swapBytes);
  dma_channel_configure(dma_tx_channel, &dma_tx_config, &_spiHw->dr, (uint16_t*)buffer, len, true);
  // The stream is closed by endWrite() / the next drawing call, after dmaWait()
}

/***************************************************************************************
** Function name:           initDMA
** Description:             Initialise the DMA engine - returns true if init OK
***************************************************************************************/
bool RPiPico_RA8876::initDMA(bool ctrl_cs)
{
  if (DMA_Enabled) return false;

  (void)ctrl_cs;

  dma_tx_channel = dma_claim_unused_channel(false);

  if (dma_tx_channel < 0) return false;

  dma_tx_config = dma_channel_get_default_config(dma_tx_channel);

  channel_config_set_transfer_data_size(&dma_tx_config, DMA_SIZE_16);
  channel_config_set_dreq(&dma_tx_config, spi_get_index(_spi) ? DREQ_SPI1_TX : DREQ_SPI0_TX);

  DMA_Enabled = true;
  return true;
}

/***************************************************************************************
** Function name:           deInitDMA
** Description:             Disconnect the DMA engine from SPI
***************************************************************************************/
void RPiPico_RA8876::deInitDMA(void)
{
  if (!DMA_Enabled) return;
  dma_channel_unclaim(dma_tx_channel);
  DMA_Enabled = false;
}

#endif // _RPIPICO_RA8876_H_
