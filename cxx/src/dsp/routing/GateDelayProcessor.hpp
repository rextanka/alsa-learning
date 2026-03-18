/**
 * @file GateDelayProcessor.hpp
 * @brief Gate delay / pulse shaper — delays note-on gate by a fixed time.
 *
 * Type name: GATE_DELAY
 *
 * When a note_on event arrives, GATE_DELAY suppresses its gate_out for
 * delay_time seconds then raises it high for the duration of the note.
 * If the note ends before delay_time elapses, gate_out never fires.
 * On note_off, gate_out drops immediately (no trailing pulse).
 *
 * Ports (PORT_CONTROL):
 *   gate_out OUT unipolar [0,1]
 *
 * Parameters:
 *   delay_time (0.0 – 2.0 s, default 0.0) — time from note_on before gate fires
 *
 * Usage — wolf-whistle pitch effect:
 *   Add a second VCO with GATE_DELAY → ADSR2:ext_gate_in. ADSR2 drives a
 *   pitch CV offset; the delayed trigger fires after the initial note, creating
 *   a rising pitch glide that starts mid-note.
 *
 * Usage — percussion trill with offset:
 *   GATE_DELAY:gate_out → ADSR:ext_gate_in delays the trill onset so the
 *   first beat is dry and rhythm shifts with each successive retriggering.
 */

#ifndef GATE_DELAY_PROCESSOR_HPP
#define GATE_DELAY_PROCESSOR_HPP

#include "../Processor.hpp"
#include <algorithm>

namespace audio {

class GateDelayProcessor : public Processor {
public:
    explicit GateDelayProcessor(int sample_rate)
        : sample_rate_(sample_rate)
    {
        declare_port({"gate_out", PORT_CONTROL, PortDirection::OUT, true}); // unipolar

        declare_parameter({"delay_time", "Delay Time (s)", 0.0f, 2.0f, 0.0f, true});
    }

    PortType output_port_type() const override { return PortType::PORT_CONTROL; }

    void reset() override {
        state_        = State::Idle;
        samples_left_ = 0;
    }

    bool apply_parameter(const std::string& name, float value) override {
        if (name == "delay_time") {
            delay_time_ = std::clamp(value, 0.0f, 2.0f);
            return true;
        }
        return false;
    }

    // Lifecycle — dispatched by Voice::note_on() / note_off() for all mod_sources.
    void on_note_on(double /*frequency*/) override {
        if (delay_time_ <= 0.0f) {
            state_        = State::High;
            samples_left_ = 0;
        } else {
            state_        = State::Counting;
            samples_left_ = static_cast<int>(delay_time_ * static_cast<float>(sample_rate_));
        }
    }

    void on_note_off() override {
        state_        = State::Idle;
        samples_left_ = 0;
    }

protected:
    void do_pull(std::span<float> output,
                 const VoiceContext* /*ctx*/ = nullptr) override {
        for (auto& s : output) {
            switch (state_) {
                case State::Counting:
                    s = 0.0f;
                    if (--samples_left_ <= 0) state_ = State::High;
                    break;
                case State::High:
                    s = 1.0f;
                    break;
                case State::Idle:
                default:
                    s = 0.0f;
                    break;
            }
        }
    }

private:
    enum class State { Idle, Counting, High };

    int   sample_rate_;
    float delay_time_ = 0.0f;
    State state_        = State::Idle;
    int   samples_left_ = 0;
};

} // namespace audio

#endif // GATE_DELAY_PROCESSOR_HPP
