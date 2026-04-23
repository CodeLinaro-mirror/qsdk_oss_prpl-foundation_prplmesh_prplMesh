#pragma once
#include <cstdint>

namespace tid_to_link_utils {

// ================= GET FUNCTIONS =================

inline uint8_t get_direction(uint8_t control) { return (control >> 6) & 0x03; }

inline uint8_t get_default_link_mapping(uint8_t control) { return (control >> 5) & 0x01; }

inline uint8_t get_mapping_switch_time(uint8_t control) { return (control >> 4) & 0x01; }

inline uint8_t get_expected_duration_present(uint8_t control) { return (control >> 3) & 0x01; }

inline uint8_t get_link_mapping_size(uint8_t control) { return (control >> 2) & 0x01; }

// ================= SET FUNCTIONS =================

inline void set_direction(uint8_t &control, uint8_t val)
{
    control = (control & ~(0x03 << 6)) | ((val & 0x03) << 6);
}

inline void set_default_link_mapping(uint8_t &control, uint8_t val)
{
    control = (control & ~(0x01 << 5)) | ((val & 0x01) << 5);
}

inline void set_mapping_switch_time(uint8_t &control, uint8_t val)
{
    control = (control & ~(0x01 << 4)) | ((val & 0x01) << 4);
}

inline void set_expected_duration_present(uint8_t &control, uint8_t val)
{
    control = (control & ~(0x01 << 3)) | ((val & 0x01) << 3);
}

inline void set_link_mapping_size(uint8_t &control, uint8_t val)
{
    control = (control & ~(0x01 << 2)) | ((val & 0x01) << 2);
}

} // namespace tid_to_link_utils

