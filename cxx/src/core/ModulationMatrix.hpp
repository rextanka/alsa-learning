/**
 * @file ModulationMatrix.hpp
 * @brief RT-Safe central hub for managing and summing modulation connections.
 */

#ifndef MODULATION_MATRIX_HPP
#define MODULATION_MATRIX_HPP

#include <vector>
#include <array>
#include <algorithm>
#include <cstdint>

namespace audio {

/**
 * @brief Modulation Targets available for routing.
 */
enum class ModulationTarget : uint8_t {
    Pitch = 0,      // Octave-based frequency shift
    Cutoff,         // Octave-based filter cutoff shift
    Resonance,      // Linear resonance offset
    Amplitude,      // Linear gain factor
    Count
};

/**
 * @brief Modulation Sources available for routing.
 */
enum class ModulationSource : uint8_t {
    Envelope = 0,
    LFO,
    Velocity,
    Aftertouch,
    Count
};

/**
 * @brief A single connection between a source and a target.
 */
struct ModulationConnection {
    ModulationSource source;
    ModulationTarget target;
    float intensity; // Bipolar intensity (bipolar modulation)
    bool active;

    ModulationConnection() 
        : source(ModulationSource::Count)
        , target(ModulationTarget::Count)
        , intensity(0.0f)
        , active(false) {}
};

/**
 * @brief Manages routing and summing of modulation signals.
 * 
 * Optimized for RT-safety by using fixed-size storage for connections.
 */
class ModulationMatrix {
public:
    static constexpr size_t MAX_CONNECTIONS = 16;

    ModulationMatrix() {
        connections_.fill(ModulationConnection());
    }

    /**
     * @brief Add or update a modulation connection.
     * 
     * @param source Source component
     * @param target Destination parameter
     * @param intensity Bipolar scaling factor
     */
    void set_connection(ModulationSource source, ModulationTarget target, float intensity) {
        // Find existing connection to update
        for (auto& conn : connections_) {
            if (conn.active && conn.source == source && conn.target == target) {
                conn.intensity = intensity;
                return;
            }
        }

        // Find empty slot for new connection
        for (auto& conn : connections_) {
            if (!conn.active) {
                conn.source = source;
                conn.target = target;
                conn.intensity = intensity;
                conn.active = true;
                return;
            }
        }
    }

    /**
     * @brief Remove a connection.
     */
    void clear_connection(ModulationSource source, ModulationTarget target) {
        for (auto& conn : connections_) {
            if (conn.active && conn.source == source && conn.target == target) {
                conn.active = false;
                return;
            }
        }
    }

    /**
     * @brief Sum all modulation for a specific target.
     * 
     * @param target Target to sum for
     * @param source_values Array of current source values (e.g., [Env, LFO, Vel, AT])
     * @return float The total modulation delta
     */
    float sum_for_target(ModulationTarget target, const std::array<float, static_cast<size_t>(ModulationSource::Count)>& source_values) const {
        float sum = 0.0f;
        for (const auto& conn : connections_) {
            if (conn.active && conn.target == target) {
                sum += source_values[static_cast<size_t>(conn.source)] * conn.intensity;
            }
        }
        return sum;
    }

    /**
     * @brief Reset all connections.
     */
    void clear_all() {
        for (auto& conn : connections_) {
            conn.active = false;
        }
    }

private:
    std::array<ModulationConnection, MAX_CONNECTIONS> connections_;
};

} // namespace audio

#endif // MODULATION_MATRIX_HPP
