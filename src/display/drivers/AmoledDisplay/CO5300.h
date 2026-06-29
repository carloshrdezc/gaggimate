
#ifndef CO5300_H
#define CO5300_H

#include "Arduino_GFX_Library.h"

class CO5300 : public Arduino_CO5300 {
  public:
    // PRO-293: GFX "GFX Library for Arduino" 1.6.x renamed CO5300_MAXWIDTH/MAXHEIGHT
    // to CO5300_TFTWIDTH/TFTHEIGHT and dropped the `ips` parameter from the
    // Arduino_CO5300 base constructor. The `ips` argument is retained on this
    // subclass for call-site compatibility (GaggiMate passes false) but is no
    // longer forwarded to the base. See CO5300.cpp.
    CO5300(Arduino_DataBus *bus, int8_t rst = GFX_NOT_DEFINED, uint8_t r = 0, bool ips = false, int16_t w = CO5300_TFTWIDTH,
           int16_t h = CO5300_TFTHEIGHT, uint8_t col_offset1 = 0, uint8_t row_offset1 = 0, uint8_t col_offset2 = 0,
           uint8_t row_offset2 = 0, uint8_t color_order = CO5300_MADCTL_RGB);
    void setRotation(uint8_t r) override;

  private:
    uint8_t _color_order;
};

#endif