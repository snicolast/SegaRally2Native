// Digital Sound Board 2 (Sega Rally 2's music): a 68000 at 11 MHz that
// takes the same command bytes the PowerPC sends to the sound board (MIDI
// port 0xF0080000) and drives an MPEG audio decoder reading MPEG-1 Layer II
// streams from a 16 MB ROM. Here the 68000 runs its own program (epr-20641,
// Musashi, in its own context next to the sound board's) and the decoder is
// minimp3; the music comes out at 32 kHz and is resampled to 44.1 kHz.
//
// 68000 map: 0x000000 program ROM (128 KB), 0xC00001 command latch,
// 0xC00003 command status (bit 0 = valid), 0xE00003 decoder command FIFO,
// 0xE80001 decoder status, 0xF00000-0xF1FFFF RAM.
// Interrupts: level 1 = a command byte arrived, level 2 = 1 kHz timer.
#pragma once
#include <cstdint>
#include <deque>
#include <vector>

class Dsb2 {
public:
    bool init(std::vector<uint8_t> program, std::vector<uint8_t> mpeg);
    bool ready() const { return ready_; }
    // Decodes stream `track` of the board's table (program 0x8008: a pointer
    // per track to its start / end in the MPEG ROM) into a 16-bit stereo WAV
    // file, for the PC menu's music (ui/menu_music.h). `channel` 0 / 1: the
    // tune on the stream's left / right channel, on both sides (Sega Rally 2
    // keeps a different tune on each)
    static bool extract_track(const std::vector<uint8_t>& program, const std::vector<uint8_t>& mpeg, int track,
                              int channel, const char* wav_path);
    void command(uint8_t b) { fifo_.push_back(b); }
    // Runs the board for `samples` output samples at 44.1 kHz and adds its
    // music to `stereo` (interleaved L, R; full scale = `scale` per int16 step)
    void run(int samples, float* stereo, int stride, float scale);

    // 68000 bus
    uint8_t read8(uint32_t a);
    uint16_t read16(uint32_t a) { return uint16_t(read8(a) << 8 | read8(a + 1)); }
    void write8(uint32_t a, uint8_t v);
    void write16(uint32_t a, uint16_t v) { write8(a, uint8_t(v >> 8)), write8(a + 1, uint8_t(v)); }

private:
    void decoder_byte(uint8_t b);
    void start(uint32_t from, uint32_t to);
    bool decode_frame();   // appends one MPEG frame to pcm_; false at the end

    std::vector<uint8_t> program_, mpeg_, ram_;
    std::vector<uint8_t> context_;   // the 68000's Musashi context while it is not running
    std::deque<uint8_t> fifo_;
    uint8_t latch_ = 0;
    bool ready_ = false;
    int64_t cycles_ = 0, next_timer_ = 0, frames_ = 0;

    // Decoder command parser
    int state_ = 0;
    uint32_t start_ = 0, end_ = 0;
    uint8_t volume_[2] = {0xFF, 0xFF};
    int mono_ = 0;   // 0 stereo, 1 left channel on both sides, 2 right

    // Playback: the current stream [pos_, stop_) and the loop to continue with
    bool playing_ = false, looping_ = false;
    uint32_t pos_ = 0, stop_ = 0, loop_start_ = 0, loop_end_ = 0;
    void* decoder_ = nullptr;      // mp3dec_t
    std::vector<int16_t> pcm_;     // decoded stereo frames not played yet
    size_t pcm_read_ = 0;
    int hz_ = 32000;
    double phase_ = 0;             // position between pcm_ frames, for the resampling
};
