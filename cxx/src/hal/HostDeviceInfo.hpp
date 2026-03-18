/**
 * @file HostDeviceInfo.hpp
 * @brief Shared device descriptor populated by platform enumerators.
 *
 * Populated at query time (not cached) by CoreAudioDevices or AlsaDevices.
 * All fields are plain data — no platform types, no OS handles.
 */

#ifndef HOST_DEVICE_INFO_HPP
#define HOST_DEVICE_INFO_HPP

#include <string>
#include <vector>

namespace hal {

struct HostDeviceInfo {
    int         index;                       ///< 0-based query index
    std::string name;                        ///< Human-readable device name
    int         default_sample_rate;         ///< Nominal/current sample rate (Hz)
    int         default_block_size;          ///< Current hardware period size (frames)
    std::vector<int> supported_sample_rates; ///< All rates the device accepts
    std::vector<int> supported_block_sizes;  ///< Power-of-2 period sizes in hardware range
};

} // namespace hal

#endif // HOST_DEVICE_INFO_HPP
