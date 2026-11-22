// Model 3 sound board: a 68000 (Musashi) driving two SCSPs. The PowerPC talks
// to it through the MIDI port at 0xF0080000. See docs/sound_spec.md.
#pragma once
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

#include "audio_stream.h"
#include "dsb2.h"
#include "scsp.h"

class AudioOut;

class SoundBoard {
public:
    bool init(std::vector<uint8_t> program, std::vector<uint8_t> samples, AudioOut* out);
    void midi_write(uint8_t b) {
        midi_bytes_++;
        scsp_[0].midi_in(b);
        if (dsb_.ready()) dsb_.command(b);   // the DSB2 listens to the same port
    }
    // The Digital Sound Board (dsb2.h), optional: call after init()
    bool init_dsb(std::vector<uint8_t> program, std::vector<uint8_t> mpeg) {
        return dsb_.init(std::move(program), std::move(mpeg));
    }
    // Frame log: SCSP and DSB2 time of the last frame, MIDI bytes since the last call
    void frame_times(double& scsp, double& dsb, uint32_t& midi) { scsp = last_scsp_ms_, dsb = last_dsb_ms_, midi = midi_bytes_, midi_bytes_ = 0; }
    void set_music_gain(float g) { music_gain_ = g * kGameGain; }   // like set_volume: 0..10, 5 = as on the cabinet
    // Runs the sound board for one PowerPC frame (keeps 44.1 kHz in sync)
    void run_frame(double frame_hz);
    // Debugging: also write everything generated to a 16-bit stereo WAV file
    void dump_wav(const char* path);
    void close_wav();
    // Sega Rally 2 mixes much hotter than Star Wars Trilogy: at the setting 5 its
    // race (engine, skids) filled the limiter all the time. 5 = gain 1, where
    // the loudest race peaks reach about 0.7 of full scale
    static constexpr float kGameGain = 0.2f;
    void set_volume(float gain) { gain_ = gain * kGameGain; }
    // Extra level of the rear pair (slave SCSP), in dB: on the cabinet those
    // speakers sit in the seat by the player's head, and the T-Rex roars and
    // many effects play there
    void set_rear_boost(int db) { rear_gain_ = std::pow(10.0f, float(db) / 20.0f); }
    void set_dsp_enabled(bool on) { scsp_[0].set_dsp_enabled(on); scsp_[1].set_dsp_enabled(on); }
    // Muted: the board keeps running but nothing reaches the output (the game
    // fast-forwarding behind a loading screen); unmuted, it fades in over 20 ms
    void set_muted(bool m) { muted_ = m; }

    // 68000 bus (called from the Musashi callbacks)
    uint8_t read8(uint32_t a);
    uint16_t read16(uint32_t a);
    void write8(uint32_t a, uint8_t v);
    void write16(uint32_t a, uint16_t v);

private:
    std::vector<uint8_t> program_, samples_;
    std::vector<uint8_t> ram_[2];
    Scsp scsp_[2];
    Dsb2 dsb_;
    double last_scsp_ms_ = 0, last_dsb_ms_ = 0;   // the last run_frame (frame log)
    uint32_t midi_bytes_ = 0;
    float music_gain_ = 5.0f;     // the MPEG music against the SCSP mix
    uint8_t ctrl_ = 0;
    AudioOut* out_ = nullptr;
    double sample_acc_ = 0;
    int overshoot_ = 0;
    std::vector<float> buffer_;
    bool ready_ = false;
    float rear_gain_ = 2.0f;   // +6 dB
    float gain_ = 5.0f;   // the original mix peaks around a quarter of full scale
    std::atomic<bool> muted_{false};
    bool was_muted_ = false;
    int fade_ = 0;        // samples of fade-in left
    FILE* wav_ = nullptr;
    uint32_t wav_frames_ = 0;
    Limiter wav_limiter_;
};
