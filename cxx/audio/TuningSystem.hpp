#pragma once

#include <string>
#include <string_view>
#include <stdexcept>
#include <unordered_map>
#include <cctype>

namespace audio {

/**
 * @brief Represents a musical note with name and octave.
 */
class Note {
public:
    Note(int midi_note) : midi_note_(midi_note) {}

    /**
     * @brief Construct from string like "C4", "A#2", "Gb3".
     */
    Note(std::string_view name) {
        if (name.empty()) throw std::invalid_argument("Note name cannot be empty");

        static const std::unordered_map<std::string, int> name_to_offset = {
            {"C", 0}, {"C#", 1}, {"DB", 1}, {"D", 2}, {"D#", 3}, {"EB", 3},
            {"E", 4}, {"F", 5}, {"F#", 6}, {"GB", 6}, {"G", 7}, {"G#", 8},
            {"AB", 8}, {"A", 9}, {"A#", 10}, {"BB", 10}, {"B", 11}
        };

        std::string upper_name;
        size_t i = 0;
        
        // Extract note name (e.g., "A", "A#", "Gb")
        upper_name += static_cast<char>(std::toupper(name[i++]));
        if (i < name.size() && (name[i] == '#' || std::tolower(name[i]) == 'b')) {
            upper_name += static_cast<char>(std::toupper(name[i++]));
        }

        auto it = name_to_offset.find(upper_name);
        if (it == name_to_offset.end()) {
            throw std::invalid_argument("Invalid note name: " + upper_name);
        }

        // Extract octave
        if (i >= name.size()) {
            throw std::invalid_argument("Octave missing in note name");
        }

        int octave = std::stoi(std::string(name.substr(i)));
        
        // MIDI note 0 is C-1 (or C0 depending on convention, we'll use C-1 = 0, so C4 = 60)
        // Formula: (octave + 1) * 12 + semitone_offset
        midi_note_ = (octave + 1) * 12 + it->second;
    }

    int midi_note() const { return midi_note_; }

    bool operator==(const Note& other) const { return midi_note_ == other.midi_note_; }

private:
    int midi_note_;
};

/**
 * @brief Base class for musical tuning systems.
 */
class TuningSystem {
public:
    virtual ~TuningSystem() = default;
    virtual double get_frequency(Note note) const = 0;
};

/**
 * @brief Standard 12-tone equal temperament tuning.
 */
class TwelveToneEqual : public TuningSystem {
public:
    TwelveToneEqual(double reference_hz = 440.0, int reference_note = 69) // A4 = 69
        : reference_hz_(reference_hz)
        , reference_note_(reference_note)
    {}

    double get_frequency(Note note) const override {
        // f = f_ref * 2^((n - n_ref) / 12)
        return reference_hz_ * std::pow(2.0, static_cast<double>(note.midi_note() - reference_note_) / 12.0);
    }

private:
    double reference_hz_;
    int reference_note_;
};

} // namespace audio
