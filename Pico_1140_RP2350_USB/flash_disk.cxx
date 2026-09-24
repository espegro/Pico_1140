#include "flash_disk.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>

#include "hardware/flash.h"
#include "hardware/regs/addressmap.h"
#include "pico/flash.h"
#include "pico/platform.h"
#include "pico/stdlib.h"

extern "C" const uint8_t embedded_disk_start[];
extern "C" const uint8_t embedded_disk_end[];

namespace {

constexpr uint32_t disk_image_offset = 1u * 1024u * 1024u;
constexpr uint32_t overlay_offset = 4u * 1024u * 1024u;
constexpr uint32_t bank_size = 6u * 1024u * 1024u;
constexpr uint32_t bank_header_size = FLASH_SECTOR_SIZE;
constexpr uint32_t record_size = 3u * FLASH_PAGE_SIZE;
constexpr uint32_t record_data_offset = FLASH_PAGE_SIZE;
constexpr uint32_t record_count = (bank_size - bank_header_size) / record_size;
constexpr uint32_t sector_size = 512;
constexpr uint32_t rk05_cylinders = 203;
constexpr uint32_t rk05_surfaces = 2;
constexpr uint32_t rk05_sectors_per_surface = 12;
constexpr uint32_t logical_disk_size =
    rk05_cylinders * rk05_surfaces * rk05_sectors_per_surface * sector_size;
constexpr uint32_t max_disk_sectors = logical_disk_size / sector_size;
constexpr uint32_t empty_offset = std::numeric_limits<uint32_t>::max();
constexpr uint32_t bank_magic = 0x50443131;   // "PD11"
constexpr uint32_t record_magic = 0x524b3035; // "RK05"
constexpr uint32_t format_version = 1;

static_assert(PICO_FLASH_SIZE_BYTES >= 16u * 1024u * 1024u,
              "The internal disk layout requires 16 MB flash");
static_assert(overlay_offset + 2u * bank_size <= PICO_FLASH_SIZE_BYTES,
              "Flash overlay exceeds the configured flash size");

struct BankHeader {
    uint32_t magic;
    uint32_t version;
    uint32_t sequence;
    uint32_t image_size;
    uint32_t image_crc;
    uint32_t header_crc;
};

struct RecordHeader {
    uint32_t magic;
    uint32_t sector;
    uint32_t data_crc;
    uint32_t header_crc;
};

struct FlashOperation {
    uint32_t offset;
    const uint8_t *data;
    size_t length;
    bool erase;
};

std::array<uint32_t, max_disk_sectors> sector_map;
alignas(4) std::array<uint8_t, sector_size> sector_buffer;
alignas(4) std::array<uint8_t, sector_size> compaction_buffer;
uint32_t buffered_sector = empty_offset;
bool buffer_dirty;
uint32_t disk_size;
uint32_t base_image_size;
uint32_t disk_crc;
uint32_t active_bank;
uint32_t bank_sequence;
uint32_t next_record;
bool initialized;

const uint8_t *flash_pointer(uint32_t offset) {
    return reinterpret_cast<const uint8_t *>(XIP_BASE + offset);
}

uint32_t crc32(const void *data, size_t length) {
    const auto *bytes = static_cast<const uint8_t *>(data);
    uint32_t crc = 0xffffffffu;
    for (size_t i = 0; i < length; ++i) {
        crc ^= bytes[i];
        for (unsigned bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}

uint32_t bank_offset(uint32_t bank) {
    return overlay_offset + bank * bank_size;
}

uint32_t slot_offset(uint32_t bank, uint32_t slot) {
    return bank_offset(bank) + bank_header_size + slot * record_size;
}

void __not_in_flash_func(run_flash_operation)(void *opaque) {
    auto *operation = static_cast<FlashOperation *>(opaque);
    if (operation->erase)
        flash_range_erase(operation->offset, operation->length);
    else
        flash_range_program(operation->offset, operation->data, operation->length);
}

bool erase_flash(uint32_t offset, size_t length) {
    FlashOperation operation{offset, nullptr, length, true};
    return flash_safe_execute(run_flash_operation, &operation, UINT32_MAX) == PICO_OK;
}

bool program_flash(uint32_t offset, const uint8_t *data, size_t length) {
    FlashOperation operation{offset, data, length, false};
    return flash_safe_execute(run_flash_operation, &operation, UINT32_MAX) == PICO_OK;
}

bool erased(const uint8_t *data, size_t length) {
    for (size_t i = 0; i < length; ++i)
        if (data[i] != 0xff)
            return false;
    return true;
}

bool valid_header(const BankHeader &header) {
    if (header.magic != bank_magic || header.version != format_version ||
        header.image_size != disk_size || header.image_crc != disk_crc)
        return false;
    return header.header_crc == crc32(&header, offsetof(BankHeader, header_crc));
}

bool write_bank_header(uint32_t bank, uint32_t sequence) {
    alignas(4) std::array<uint8_t, FLASH_PAGE_SIZE> page;
    page.fill(0xff);
    BankHeader header{bank_magic, format_version, sequence, disk_size, disk_crc, 0};
    header.header_crc = crc32(&header, offsetof(BankHeader, header_crc));
    std::memcpy(page.data(), &header, sizeof(header));
    return program_flash(bank_offset(bank), page.data(), page.size());
}

bool write_record(uint32_t bank, uint32_t slot, uint32_t sector,
                  const uint8_t *data) {
    if (slot >= record_count)
        return false;

    const uint32_t offset = slot_offset(bank, slot);
    alignas(4) std::array<uint8_t, 2 * FLASH_PAGE_SIZE> payload;
    std::memcpy(payload.data(), data, payload.size());
    if (!program_flash(offset + record_data_offset, payload.data(), payload.size()))
        return false;

    alignas(4) std::array<uint8_t, FLASH_PAGE_SIZE> page;
    page.fill(0xff);
    RecordHeader header{record_magic, sector, crc32(data, sector_size), 0};
    header.header_crc = crc32(&header, offsetof(RecordHeader, header_crc));
    std::memcpy(page.data(), &header, sizeof(header));
    return program_flash(offset, page.data(), page.size());
}

bool valid_record(uint32_t offset, RecordHeader *header_out = nullptr) {
    RecordHeader header;
    std::memcpy(&header, flash_pointer(offset), sizeof(header));
    if (header.magic != record_magic || header.sector >= disk_size / sector_size ||
        header.header_crc != crc32(&header, offsetof(RecordHeader, header_crc)))
        return false;
    if (header.data_crc != crc32(flash_pointer(offset + record_data_offset), sector_size))
        return false;
    if (header_out)
        *header_out = header;
    return true;
}

void scan_bank(uint32_t bank) {
    sector_map.fill(empty_offset);
    next_record = 0;
    for (uint32_t slot = 0; slot < record_count; ++slot) {
        const uint32_t offset = slot_offset(bank, slot);
        RecordHeader header;
        if (valid_record(offset, &header))
            sector_map[header.sector] = offset + record_data_offset;
        if (!erased(flash_pointer(offset), record_size))
            next_record = slot + 1;
    }
}

bool format_bank(uint32_t bank) {
    constexpr uint32_t erase_chunk = FLASH_BLOCK_SIZE;
    for (uint32_t offset = bank_offset(bank); offset < bank_offset(bank) + bank_size;
         offset += erase_chunk) {
        if (!erase_flash(offset, erase_chunk))
            return false;
    }
    return true;
}

bool compact_overlay() {
    const uint32_t old_bank = active_bank;
    const uint32_t new_bank = old_bank ^ 1u;
    if (!format_bank(new_bank))
        return false;

    uint32_t slot = 0;
    for (uint32_t sector = 0; sector < disk_size / sector_size; ++sector) {
        if (sector_map[sector] == empty_offset)
            continue;
        std::memcpy(compaction_buffer.data(), flash_pointer(sector_map[sector]),
                    sector_size);
        if (!write_record(new_bank, slot, sector, compaction_buffer.data()))
            return false;
        ++slot;
    }

    const uint32_t new_sequence = bank_sequence + 1;
    if (!write_bank_header(new_bank, new_sequence))
        return false;

    active_bank = new_bank;
    bank_sequence = new_sequence;
    scan_bank(active_bank);
    return true;
}

bool commit_sector(uint32_t sector, const uint8_t *data) {
    if (next_record >= record_count && !compact_overlay())
        return false;
    const uint32_t offset = slot_offset(active_bank, next_record);
    if (!write_record(active_bank, next_record, sector, data))
        return false;
    sector_map[sector] = offset + record_data_offset;
    ++next_record;
    return true;
}

void load_sector(uint32_t sector, uint8_t *destination) {
    const uint32_t overlay = sector_map[sector];
    if (overlay != empty_offset) {
        std::memcpy(destination, flash_pointer(overlay), sector_size);
    } else if ((sector + 1) * sector_size <= base_image_size) {
        std::memcpy(destination, embedded_disk_start + sector * sector_size,
                    sector_size);
    } else {
        std::memset(destination, 0, sector_size);
    }
}

bool flush_buffer() {
    if (!buffer_dirty)
        return true;
    if (!commit_sector(buffered_sector, sector_buffer.data()))
        return false;
    buffer_dirty = false;
    return true;
}

} // namespace

bool FlashDisk::init() {
    cursor_ = 0;
    if (initialized)
        return true;

    if (reinterpret_cast<uintptr_t>(embedded_disk_start) != XIP_BASE + disk_image_offset)
        return false;
    base_image_size = static_cast<uint32_t>(embedded_disk_end - embedded_disk_start);
    disk_size = logical_disk_size;
    if (!base_image_size || base_image_size > disk_size ||
        base_image_size % sector_size)
        return false;
    disk_crc = crc32(embedded_disk_start, base_image_size);

    BankHeader headers[2];
    std::memcpy(&headers[0], flash_pointer(bank_offset(0)), sizeof(BankHeader));
    std::memcpy(&headers[1], flash_pointer(bank_offset(1)), sizeof(BankHeader));
    const bool valid0 = valid_header(headers[0]);
    const bool valid1 = valid_header(headers[1]);

    if (!valid0 && !valid1) {
        if (!format_bank(0) || !write_bank_header(0, 1))
            return false;
        active_bank = 0;
        bank_sequence = 1;
    } else if (valid1 && (!valid0 || headers[1].sequence > headers[0].sequence)) {
        active_bank = 1;
        bank_sequence = headers[1].sequence;
    } else {
        active_bank = 0;
        bank_sequence = headers[0].sequence;
    }

    scan_bank(active_bank);
    buffered_sector = empty_offset;
    buffer_dirty = false;
    initialized = true;
    return true;
}

bool FlashDisk::seek(uint32_t position) {
    if (!initialized || position > disk_size)
        return false;
    cursor_ = position;
    return true;
}

bool FlashDisk::read(void *buffer, size_t length, size_t *transferred) {
    auto *output = static_cast<uint8_t *>(buffer);
    size_t done = 0;
    while (done < length && cursor_ < disk_size) {
        const uint32_t sector = cursor_ / sector_size;
        const uint32_t in_sector = cursor_ % sector_size;
        const size_t amount = std::min<size_t>({length - done, sector_size - in_sector,
                                               disk_size - cursor_});
        if (buffered_sector == sector) {
            std::memcpy(output + done, sector_buffer.data() + in_sector, amount);
        } else {
            const uint32_t overlay = sector_map[sector];
            if (overlay != empty_offset) {
                std::memcpy(output + done, flash_pointer(overlay) + in_sector,
                            amount);
            } else if ((sector + 1) * sector_size <= base_image_size) {
                std::memcpy(output + done,
                            embedded_disk_start + sector * sector_size + in_sector,
                            amount);
            } else {
                std::memset(output + done, 0, amount);
            }
        }
        cursor_ += amount;
        done += amount;
    }
    if (transferred)
        *transferred = done;
    return done == length;
}

bool FlashDisk::write(const void *buffer, size_t length, size_t *transferred) {
    const auto *input = static_cast<const uint8_t *>(buffer);
    size_t done = 0;
    while (done < length && cursor_ < disk_size) {
        const uint32_t sector = cursor_ / sector_size;
        const uint32_t in_sector = cursor_ % sector_size;
        const size_t amount = std::min<size_t>({length - done, sector_size - in_sector,
                                               disk_size - cursor_});
        if (buffered_sector != sector) {
            if (!flush_buffer())
                break;
            load_sector(sector, sector_buffer.data());
            buffered_sector = sector;
        }
        std::memcpy(sector_buffer.data() + in_sector, input + done, amount);
        buffer_dirty = true;
        cursor_ += amount;
        done += amount;
    }
    if (transferred)
        *transferred = done;
    return done == length;
}

bool FlashDisk::sync() {
    return flush_buffer();
}

uint32_t FlashDisk::size() const {
    return disk_size;
}
