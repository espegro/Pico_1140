#pragma once

#include <cstddef>
#include <cstdint>

class FlashDisk {
  public:
    bool init();
    bool seek(uint32_t position);
    bool read(void *buffer, size_t length, size_t *transferred);
    bool write(const void *buffer, size_t length, size_t *transferred);
    bool sync();
    uint32_t size() const;

  private:
    uint32_t cursor_ = 0;
};
