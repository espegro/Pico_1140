#pragma once
#include <stdint.h>
#include <stdio.h>
#include "flash_disk.h"

class RK11 {

  public:
	FlashDisk rk05;
    uint16_t read16(uint32_t a);
    void write16(uint32_t a, uint16_t v);
    void reset();
    void step();

  private:
	uint16_t rkds, rker, rkcs, rkwc, rkba, rkda;
    size_t bcnt;
    uint32_t drive, sector, surface, cylinder,rkba18,rkdelay;

    void rknotready();
    void rkready();
    void readwrite();
    void seek();
};
