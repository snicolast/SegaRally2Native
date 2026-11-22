# Sega Model 3 Sound Board: Implementation Spec (target: The Lost World, `lostwsga`, Step 1.5)

Written for the Lost World port. The SCSP part still applies to Sega Rally 2; the Digital Sound
Board it also has is in `src/audio/dsb2.h`.

Sources studied (read-only reference): Supermodel `Src/Model3/SoundBoard.*`, `Src/Sound/SCSP.cpp`,
`SCSPDSP.*`, `SCSPLFO.cpp`, `Src/Model3/Model3.cpp`, `IRQ.cpp`, `GameLoader.cpp`, `OSD/SDL/Audio.cpp`,
`Config/Games.xml`. A few facts were also confirmed by reading the Lost World sound program ROM
(epr-19940.21). Those are tagged **[ROM]**.

Tags used in this document:
- **[SM]**: what Supermodel does. It is known to work with this game, so treat it as the reference.
- **[HW?]**: probable real-hardware behaviour, where it differs from Supermodel or is unknown.
- **[AMBIG]**: open question. The implementer has to choose.
- **[BUG]**: a Supermodel defect. Don't reproduce it unless you need bit-exact output.

Conventions: "word" means 16 bits. Bit 0 is the LSB. Addresses are 68K addresses unless stated
otherwise. All SCSP registers and RAM are described as the 68K sees them (big-endian).

---------------------------------------------------------------------------------------------------

## 1. Sound board (68000 + 2 x SCSP)

### 1.1 CPU and clocks
- CPU: MC68000 (Musashi configured as a plain 68000). Clock: 11.2896 MHz (45.1584 MHz / 4).
- SCSP output sample rate: 44 100 Hz. That gives exactly **256 68K cycles per output sample**
  (11 289 600 / 44 100).
- Supermodel generates **735 samples per emulated frame** (`44100/60`), so one sound frame is
  735 x 256 = **188 160 68K cycles**. The PPC frame runs at 57.524160 Hz (424 lines). See 1.7 for
  what this mismatch means.

### 1.2 68K memory map [SM]
The decode uses address bits 23..20 (`(a>>20)&0xF`). The 68K bus is 24 bits.

| Range | Device | Notes |
|---|---|---|
| 000000-0FFFFF | SCSP #1 ("master") sound RAM, 1 MB | Also the 68K's work RAM and stack (initial SSP = 0x2000 [ROM]). The vector table lives here (see 1.4). |
| 100000-1FFFFF | SCSP #1 registers | Only the low 16 bits of the address are decoded (`addr & 0xFFFF`), so the 64 KB block is mirrored across the whole 1 MB. Real decode is 100000-100FFF [HW?]. |
| 200000-2FFFFF | SCSP #2 ("slave") sound RAM, 1 MB | Index is `a & 0xFFFFF`. |
| 300000-3FFFFF | SCSP #2 registers | Same mirroring as #1. |
| 400001 (byte write) | Control register `ctrlReg` | Only an 8-bit write to exactly 0x400001 is decoded. Bit 4 selects the sample ROM bank. Other bits are ignored [SM]. Reads of 0x4xxxxx return 0. |
| 600000-6FFFFF | Program ROM, 512 KB | Index is `a & 0x7FFFF`, so the ROM is mirrored twice. Read-only (writes are ignored). |
| 800000-FFFFFF | Sample ROM window, 8 MB | Index is `bankBase + (a & 0x7FFFFF)`. Read-only. |
| 500000-5FFFFF, 700000-7FFFFF | unmapped | Reads return 0. Writes are ignored. |

**Sample ROM banking [SM]:** `bankBase = (ctrlReg & 0x10) ? 0x800000 : 0`. The sample image is
16 MB. An image of 8 MB or less (Lost World: exactly 8 MB) is **mirrored** into both halves, so for
Lost World the bank bit has no effect. A source comment describes a finer MAME-style split (low 2 MB
fixed, 0xA00000-0xFFFFFF banked). That split is not implemented, and Lost World doesn't need it.
- [ROM] Lost World writes `ctrlReg = 0x20 | (n & 0x0F)` (the byte 0x20 comes from ROM 0x608068).
  Bit 4 never appears to be set. Bits 0-3 and bit 5 have unknown meaning. Store them and ignore them.

**Access widths:**
- 8-bit RAM and ROM accesses use big-endian byte order: byte at address A = high byte of the word at
  A&~1 when A is even, low byte when A is odd.
- 32-bit accesses are split into two 16-bit accesses: high word at A, low word at A+2.
  For RAM and ROM, wrap within the region (`(a+2) & mask`).

**SCSP register access widths [SM]:**
- 8- and 16-bit accesses work.
- A 32-bit *write* becomes two 16-bit writes (high word to A, low word to A+2), each with its
  register side effects.
- A 32-bit *read* returns 0xFFFFFFFF [BUG-ish]. Recommendation: implement it as two 16-bit reads.

### 1.3 ROM loading and byte order
Games.xml for lostwsga:
- `sound_program`: `epr-19940.21` (512 KB, CRC 0xB06FFE5F) at offset 0.
- `sound_samples`: `mpr-19934.22` (4 MB, CRC 0xC7D8E194) at 0x000000 and `mpr-19935.24` (4 MB,
  CRC 0x91C1B618) at 0x400000. Total 8 MB.
- Both regions have `stride=1 chunk_size=1 byte_swap=true`. The loader turns that into stride 2,
  layout "10", which swaps every byte pair.
- No `<mpeg_board>` element, so **no DSB / MPEG board**. Audio type is `QuadFrontRear`.

Net result: the raw ROM files are stored word-swapped relative to 68K order.
**68K byte at ROM offset A = file[A ^ 1].** The same rule applies to the sample ROM after
concatenating .22 then .24. (Supermodel then byte-swaps again to host order for speed. That's
internal.)

[ROM] Reset vectors after the swap: SSP = 0x00002000, PC = 0x00600120.
Autovector table in ROM:
- level 1 (vector 0x64) → 0x60072E
- level 2 (vector 0x68) → 0x60307A
- level 3 (vector 0x6C) → 0x6005BA
- all other vectors → 0x6005B0 (default handler)

### 1.4 Reset [SM]
1. Copy the first 16 bytes of the program ROM (68K order) to sound RAM #1 at 0x000000-0x00000F.
   This covers SSP, PC and vectors 2-3.
2. `ctrlReg = 0`.
3. Pulse 68K RESET. The CPU fetches SSP from 0x000000 and PC from 0x000004, both now in RAM.

Every other vector (including the autovectors at 0x64-0x7C) is read from RAM. The 68K program
installs them itself. RAM is otherwise zero at power-on.

### 1.5 Interrupts into the 68K
- Only SCSP #1 drives the 68K IPL lines. The SCSP computes a level from 0 to 7 (section 3.10) and
  sets the 68K's IPL to it, level-sensitive. 0 means no interrupt.
- The IACK cycle returns **autovector**: the vector address is 0x60 + 4*level. The acknowledge does
  *not* clear anything. The level stays asserted until the SCSP recomputes it.
- The level is recomputed:
  - once per output sample (just before the 68K's 256-cycle slice),
  - on every write to SCIRE,
  - after every read of the MIDI input register.
- SCSP #2 has no interrupt connection in Supermodel. Its timer and interrupt registers are inert.

### 1.6 Per-sample scheduling [SM]
For each of the 735 samples in a frame:
```
  for s in 0..734:
     generate one stereo sample for SCSP#1 and one for SCSP#2   (sections 3 and 4; slots, then DSP, then mix)
     timers_add_ticks(1)            // SCSP#1 timers A/B/C (3.9)
     update_68k_irq_level()         // 3.10
     overshoot = run_68k(256 - overshoot) - (256 - overshoot)   // carry the cycle overshoot forward
```
- Audio for sample s is produced *before* the 68K runs its slice. Register writes the 68K makes
  during slice s therefore take effect from sample s+1.
- SCSP #1's output goes to the front L/R buffers. SCSP #2's output goes to the rear L/R buffers.

### 1.7 Relation to the PPC frame [SM]
- **Single-threaded mode:**
  1. `RunMainBoardFrame()` runs the PPC for one full video frame. All MIDI bytes written during
     that frame queue in the FIFO.
  2. Render.
  3. `SoundBoard.RunFrame()` runs 735 samples.

  So MIDI is delivered to the 68K in one burst per frame.
- **Multi-threaded mode (default):** a separate sound thread runs. It is woken by the host audio
  callback and runs whole 735-sample frames until the host buffer is full, so 68K and SCSP time
  follows the 44.1 kHz audio clock rather than video.
- 735 samples per 57.524 Hz video frame equals 42 280 samples/s of emulated time. A clean-room
  design should pick one approach:
  - (a) run 735 samples per video frame (Supermodel single-thread behaviour; the sound board runs
    about 4% slow), or
  - (b) run `44100/57.52416 ≈ 766.6` samples per frame, keeping a fractional accumulator, so that
    68K time matches PPC time. **[AMBIG]**: (b) is closer to hardware.

---------------------------------------------------------------------------------------------------

## 2. PPC ↔ 68K MIDI interface

### 2.1 PPC side registers
Decoded when `addr>>24` is 0xF0 (or the mirror 0xFE) and `(addr>>16)&0xFF == 0x08`.

**8-bit writes:**
- `0xF0080000` (`addr&0xF == 0`, MIDI data): push the byte into the **single MIDI-in FIFO of
  SCSP #1**, then deassert PPC IRQ bit 0x40. If a DSB were attached it would receive the same byte.
  Lost World has none.
- `0xF0080004` (`addr&0xF == 4`, MIDI control): `midiCtrlPort = data`. If `(data & 0x20) == 0`,
  deassert IRQ 0x40.
- Any other offset is ignored. 16-, 32- and 64-bit writes to this block are ignored. A 64-bit write
  is split into 32-bit writes, so it is ignored too.

**8-bit reads:**
- `0xF0080000` → **0x00**.
- `0xF0080004` → **0x83**.
- Other byte offsets → 0x00.

**Wider reads:** 16-bit → 0xFFFF. 32-bit → 0xFFFFFFFF. 64-bit → two 32-bit reads.

The PPC never sees 68K MIDI-out data in Supermodel (no back channel).

### 2.2 PPC IRQ controller (bit 0x40 = "SND")
- `irqEnable` is at `0xF0100014`.
- `irqState`: read at `0xF0100018`. Writing to `0xF0100018` deasserts the written bits.
- The PPC external interrupt line is high while `(irqState & irqEnable) != 0`, or while any bit
  above 0xFF is set.

### 2.3 How MIDI IRQs are generated [SM]
There is no real UART model. Inside `RunMainBoardFrame`, at VBlank start:
1. Run the PPC in chunks of 1000 cycles while IRQ2 is enabled and pending and
   `vBlankCycles > 1000`. Subtract each chunk from `vBlankCycles`.
2. Then:
   ```
   n = 0
   while (midiCtrlPort & 0x20):
       if (irqEnable & 0x40) == 0: break
       IRQ.Assert(0x40); ppc_execute(1000); vBlankCycles -= 1000
       if ++n > 128: break        // at most 129 MIDI IRQs per frame
   ```
3. Run the PPC for the rest of `vBlankCycles`. Note that this counter is unsigned and can
   underflow; clamp it at 0.
4. Assert IRQ 0x0D, end VBlank.
5. Run 384 active lines. Assert IRQ 0x02 at the *start* of line 383. Lost World soft-locks
   otherwise.

The PPC ISR typically writes one or more MIDI bytes, and each data write deasserts 0x40. Games
usually write 0x27 to the control port to start a burst and 0x06 to stop it. Others leave 0x37 set
and toggle the enable.

### 2.4 68K side: the MIDI-in FIFO [SM]
- One global FIFO with **256 entries**. A real SCSP has a tiny FIFO; Supermodel enlarged it to
  absorb a whole frame of bytes. There is no overflow detection: the write pointer silently laps the
  read pointer.
- Any **read** of SCSP common register 0x04 (8-bit or 16-bit, either byte, on *either* SCSP) pops
  the FIFO:
  ```
  value = (reg04 & 0xFF00) | fifo[r]
  if r != w: r = (r+1) & 0xFF
  ```
  When the FIFO is empty, the read returns the stale `fifo[r]`. The register is then updated and the
  IRQ level recomputed.
- Status bits MOFULL(12), MOEMPTY(11), MIOVF(10), MIFULL(9), MIEMPTY(8) in register 0x04 are never
  updated by Supermodel. They read back as whatever the 68K last wrote (normally 0). [HW?] Real
  hardware reports FIFO state there.
  - [ROM] Lost World polls `btst #4,$100404` (MOFULL) before each MIDI-out write. Returning 0 is
    correct.
- **MIDI interrupt (source bit 3):** asserted while the FIFO is non-empty *and* SCIEB bit 3 is set.
  It is level-like: after the ISR reads one byte and executes RTE, the interrupt fires again until
  the FIFO drains.
- [ROM] Lost World's level-3 ISR (0x6005BA) reads `$100405`. If a RAM flag at 0x0C3F is set, it
  echoes the byte to `$100407` (MIDI out).

### 2.5 MIDI out
- A write to SCSP register 0x06 (low byte = MOBUF) pushes a byte into a MIDI-out FIFO.
- [BUG] Supermodel writes MIDI-out bytes into the *input* FIFO array (indices 0-31), which can
  corrupt pending input bytes. Implement a separate 32-entry out-FIFO instead. Nothing on the PPC
  side ever reads it.

---------------------------------------------------------------------------------------------------

## 3. SCSP (YMF292)

Two identical chips. Each has:
- 32 slots,
- common registers,
- a DSP,
- its own sound RAM (1 MB window; see 3.11),
- its own 64-entry slot-output ring ("sound stack") used for FM.

Only SCSP #1 has working timers, interrupts and MIDI.

### 3.1 Register window (offset = `addr & 0xFFFF` inside the chip's register block)

| Offset | Content |
|---|---|
| 0x000-0x3FF | Slot registers. Slot n occupies `n*0x20 .. n*0x20+0x1F`. Words 0-0xB are used. |
| 0x400-0x42F | Common registers (24 words). Word writes and reads at ≥0x430 are ignored / return 0 [SM]. |
| 0x600-0x67F | Sound stack: 64 words, R/W. Each is a 16-bit slot output used for FM (3.7). |
| 0x700-0x77F | COEF[0..63]: 16-bit signed. |
| 0x780-0x7BF | MADRS[0..31]: 16-bit. 0x7C0-0x7FF mirrors MADRS. |
| 0x800-0xBFF | MPRO[0..127]: 4 words per step. Word k of step i is at `0x800 + i*8 + k*2`. |
| 0xC00-0xDFF | TEMP[0..127], read-only. Index `(addr>>2)&0x7F`. `addr&2 == 0` returns bits 23..16 (`TEMP>>16`); `addr&2 != 0` returns bits 15..0. |
| 0xE00-0xE7F | MEMS[0..31], read-only. Same high/low split as TEMP. |
| 0xE80-0xEBF | MIXS[0..15], read-only. Same split. |
| 0xEC0-0xEDF | EFREG[0..15], read-only. One word each. |
| 0xEE0-0xEE3 | EXTS[0..1], read-only. |

- 8-bit reads of 0x700 and above return 0 [SM]. Only 16-bit reads return DSP data.
- A byte write is stored into the addressed byte of its word, using big-endian byte position. It
  then triggers the same side effects as a write to that word.

### 3.2 Slot registers (per slot, byte offset within the slot; bit fields of each 16-bit word)

| Off | Bits | Field |
|---|---|---|
| 0x00 | 12 KYONEX, 11 KYONB, 10-9 SBCTL, 8-7 SSCTL, 6-5 LPCTL, 4 PCM8B, 3-0 SA[19:16] | |
| 0x02 | 15-0 SA[15:0] | Start address (byte address in sound RAM) |
| 0x04 | 15-0 LSA | Loop start (in samples, relative to SA) |
| 0x06 | 15-0 LEA | Loop end (in samples, relative to SA) |
| 0x08 | 15-11 D2R, 10-6 D1R, 5 EGHOLD, 4-0 AR | |
| 0x0A | 14 LPSLNK, 13-10 KRS, 9-5 DL, 4-0 RR | |
| 0x0C | 9 STWINH, 8 SDIR, 7-0 TL | |
| 0x0E | 15-12 MDL, 11-6 MDXSL, 5-0 MDYSL | |
| 0x10 | 14-11 OCT (4-bit signed), 9-0 FNS | |
| 0x12 | 15 LFORE, 14-10 LFOF, 9-8 PLFOWS, 7-5 PLFOS, 4-3 ALFOWS, 2-0 ALFOS | |
| 0x14 | 6-3 ISEL, 2-0 IMXL | |
| 0x16 | 15-13 DISDL, 12-8 DIPAN, 7-5 EFSDL, 4-0 EFPAN | |
| 0x18-0x1F | unused (stored) | |

Supermodel field extraction quirks:
- **MDL** is extracted as `(w>>12) & 0x1E`. Because w is 16 bits, this equals `(w>>12) & 0xE`: the
  LSB of MDL is dropped, so MDL takes even values 0..14. The source says this is needed for correct
  FM in VF3. [AMBIG] Real MDL is 4 bits. Use the true 4-bit value with the formula in 3.7, or
  reproduce the mask for exactness.
- **PLFOS** is extracted as `(w>>5) & 0xE`, which also drops its LSB (values 0, 2, 4, 6).
  [AMBIG] The real field is 3 bits.
- **LFORE** (LFO reset) is not implemented.

Register-write side effects [SM] (triggered by a write to the named word):
- **Word 0x00:** if KYONEX=1, run the key processing in 3.3, then clear KYONEX in the written slot.
  KYONEX always reads back 0.
- **Word 0x0A:** recompute `RR_step = DRTABLE[clamp(0 + 2*RR)]`. This uses base 0, with no KRS
  scaling. Also recompute `DLthr = 0x1F - DL`.
- **Word 0x10:** recompute the phase step from OCT/FNS.
- **Word 0x12:** recompute the LFO steps and tables (only when PLFOS/ALFOS are non-zero).
- Everything else (SA, LSA, LEA, TL, pans, ISEL/IMXL, MD*, SBCTL, SSCTL, SDIR, STWINH, LPCTL) is
  read live every sample. AR, D1R, D2R and KRS are latched only at key-on.

### 3.3 Key on / key off [SM]
On KYONEX, for every slot s of *that* chip:
```
  if KYONB(s)==1 and EGstate(s)==RELEASE : start(s)
  if KYONB(s)==0                          : EGstate(s)=RELEASE ; KYONB(s) cleared   (key off; slot stays active until EG reaches 0)
```

`start(s)`:
```
  active=1; back=0; cur=0; nxt=1<<12
  base   = SA            (PCM8B=1)
         = SA & 0x7FFFE  (16-bit)
  step   = pitch_step()
  EG: latch rates (3.5); level = 0x17F; state = ATTACK
  recompute LFOs (phases are NOT reset)
```

Notes:
- A slot whose KYONB=1 but which is not in RELEASE is **not** restarted.
- [BUG] A slot that *stopped* (end of a one-shot sample) keeps its EG state, often ATTACK or DECAY,
  so under the rule above the game's next key-on of that slot is ignored until an unrelated key-off
  sets RELEASE. Lost World reuses slots this way and loses sounds (T-Rex roars). The port also
  starts a slot that is inactive, as MAME does.
- At power-up all slot state is zero. ATTACK is enum 0, so [AMBIG] a slot keyed on by the very first
  KYONEX would not start. Recommended: initialise every slot to state RELEASE, level 0, inactive.
- "Stop" (end of a non-looping sample, or release reaching 0) sets `active=0` and **clears KYONB in
  the register**, which is visible to the 68K. The EG state is left as it was.

### 3.4 Pitch and sample fetch
- Position is fixed point with 12 fractional bits (`SHIFT = 12`), counted in *samples*.
- Phase step: `oct = (OCT ^ 8) - 8` (range -8..7), `e = oct + 2`.
  `step = (1024 + FNS) << e` if e ≥ 0, else `>> -e`.
  Equivalently: step in samples = `2^OCT * (1 + FNS/1024)` per 44.1 kHz output sample.

Per output sample, for an active slot:
1. If **SSCTL ≠ 0**, return 0 immediately. Position, EG and LFO don't advance, and nothing is
   written to the stack [SM]. Noise and other SSCTL sources are not implemented.
2. **Pitch LFO:** if PLFOS ≠ 0, `st = (step * PLFO()) >> 12`; otherwise `st = step` (3.6).
3. **Fetch addresses**, relative to `base` in sound RAM:
   - 16-bit: `a1 = (cur >> 11) & 0x7FFFE`, `a2 = (nxt >> 11) & 0x7FFFE` (byte offsets).
   - 8-bit: `a1 = cur >> 12`, `a2 = nxt >> 12`.
4. **FM offset** (3.7) is added to a1/a2 if MDL, MDXSL or MDYSL is non-zero.
5. **Fetch and interpolate.** `f = cur & 0xFFF`.
   - 16-bit: `p = int16 BIG-ENDIAN word at RAM[base + a]`.
   - 8-bit: `p = int8 at RAM byte base + a, shifted << 8`.
   - `sample = (p1*(4096-f) + p2*f) >> 12` (linear interpolation, arithmetic shift).
6. **SBCTL:** if bit 0 is set, `sample ^= 0x7FFF`. If bit 1 is set, `sample = (int16)(sample ^ 0x8000)`.
7. **Advance:** `cur = back ? cur - st : cur + st`, then `nxt = cur + 4096`. Let `A = cur >> 12`.
8. **LPSLNK:** if `A >= LSA` and not `back` and LPSLNK=1 and the EG is in ATTACK, switch the EG to
   DECAY1.
9. **Loop handling**, applied independently to the pair (cur, A=cur>>12) and to the pair
   (nxt, nxt>>12). Here `X` is the 20.12 position and `x = X>>12`:
   - LPCTL=0 (off): if `x >= LSA` and `x >= LEA`, stop the slot (see 3.3).
   - LPCTL=1 (normal): if `x >= LEA`, `X = (LSA<<12) + (X - (LEA<<12))`.
   - LPCTL=2 (reverse):
     - if `x >= LSA` and not back: `X = (LEA<<12) - (X - (LSA<<12))`; `back = 1`.
     - else if back and (`x < LSA` or X is negative as int32): `X = (LEA<<12) - ((LSA<<12) - X)`.
   - LPCTL=3 (ping-pong):
     - if `x >= LEA`: `X = (LEA<<12) - (X - (LEA<<12))`; `back = 1`.
     - else if back and (`x < LSA` or X is negative): `X = (LSA<<12) + ((LSA<<12) - X)`; `back = 0`.
10. **If SDIR = 0:**
    - ALFO: if ALFOS ≠ 0, `sample = (sample * ALFO()) >> 12`.
    - EG: if the state (before update) is ATTACK, `sample = (sample * eg) >> 12`; otherwise
      `sample = (sample * EG_TABLE[eg >> 2]) >> 12`. Here `eg = EG_update()` (3.5), a 12-bit value.

    If SDIR = 1, the EG and ALFO are bypassed **and the EG does not advance**.
11. **Sound-stack write** (if STWINH = 0):
    - SDIR = 0: `stack[ptr] = (sample * PAN4(TL, pan=0, sdl=7)) >> 13`, which is ≈ `2*TLgain*sample`.
    - SDIR = 1: `stack[ptr] = 2*sample`.

    The value is stored as int16 (truncated). See 3.8 for PAN4. Inactive slots write nothing; their
    entry keeps its old value.
12. Return `sample`. This is post-EG. TL is *not* applied here; TL is applied during mixing.

Sample format in RAM: signed two's-complement PCM.
- 16-bit samples are big-endian (MSB at the even byte address).
- 8-bit samples are one byte each, in 68K address order.
- No ADPCM or compressed formats exist on the SCSP.
- The "base & 0x7FFFE" and "& 0x7FFFE" masks limit 16-bit playback to a 512 KB window [SM]. 8-bit
  reads are unmasked [AMBIG]. Recommended: wrap all slot RAM accesses to the chip's RAM size.

### 3.5 Envelope generator
- State: level `L` in 10.16 fixed point (`L>>16` ranges 0..0x3FF; 0x3FF is maximum volume).
- States, in order: ATTACK=0, DECAY1=1, DECAY2=2, RELEASE=3.

Rates latched at key-on:
```
  oct = (OCT^8)-8
  base = (KRS != 0xF) ? oct + 2*KRS + ((FNS>>9)&1) : 0
  ARstep  = ARTABLE[clamp(base + 2*AR , 0, 63)]
  D1Rstep = DRTABLE[clamp(base + 2*D1R, 0, 63)]
  D2Rstep = DRTABLE[clamp(base + 2*D2R, 0, 63)]
  RRstep  = DRTABLE[clamp(base + 2*RR , 0, 63)]   // later overwritten with base 0 on a word-0x0A write
  DLthr   = 0x1F - DL ;  EGHOLD latched
```

Tables, for i = 2..63:
- `ARTABLE[i] = int(1023*1000/(44100*ARTimes[i]) * 65536)`, or `1024<<16` (instant) when
  `ARTimes[i] == 0`.
- `DRTABLE[i] = int(1023*1000/(44100*DRTimes[i]) * 65536)`.
- Index 0 and 1 → 0 (rate is infinite; the EG never moves).

Times in ms:
```
ARTimes = {inf,inf,8100,6900,6000,4800,4000,3400,3000,2400,2000,1700,1500,1200,1000,860,760,600,500,430,
  380,300,250,220,190,150,130,110,95,76,63,55,47,38,31,27,24,19,15,13,12,9.4,7.9,6.8,6.0,4.7,3.8,3.4,
  3.0,2.4,2.0,1.8,1.6,1.3,1.1,0.93,0.85,0.65,0.53,0.44,0.40,0.35,0,0}
DRTimes = {inf,inf,118200,101300,88600,70900,59100,50700,44300,35500,29600,25300,22200,17700,14800,12700,
  11100,8900,7400,6300,5500,4400,3700,3200,2800,2200,1800,1600,1400,1100,920,790,690,550,460,390,340,
  270,230,200,170,140,110,98,85,68,57,49,43,34,28,25,22,18,14,12,11,8.5,7.1,6.1,5.4,4.3,3.6,3.1}
```

`EG_update()` runs once per sample, only when SDIR = 0 and the slot is active:
```
ATTACK : L += ARstep
         if L >= 0x3FF<<16:
             L = 0x3FF<<16
             if !LPSLNK: state = DECAY1 (or DECAY2 if D1Rstep >= 1024<<16)
         if EGHOLD: return 0xFFC     // output held at max while attacking
DECAY1 : L -= D1Rstep; clamp at 0
         if (L>>21) <= DLthr: state = DECAY2
DECAY2 : if D2R == 0: return (L>>16)<<2       // hold
         L -= D2Rstep; clamp at 0
RELEASE: L -= RRstep
         if L <= 0: L = 0; stop slot (active = 0, KYONB cleared)
return (L>>16) << 2        // 12-bit "eg"
```

Attenuation curve:
- In **ATTACK** the gain is *linear*: `eg/4096`. It starts at 0x17F/0x3FF ≈ 0.374, not at 0.
- In the other states the gain is **dB-linear**:
  `EG_TABLE[i] = int(4096 * 10^((3*(i-1023)/32)/20))` for i = 0..1023. That is 0.09375 dB per
  step, with -95.9 dB at i = 0.

### 3.6 LFO
Each slot has a PLFO (pitch) and an ALFO (amplitude). Each LFO has an 8.8 phase (16-bit, wraps).
- `phase_step = uint(256 * (LFOFreq[LFOF] * 256 / 44100))`.
- Per call: `phase += phase_step`, then `idx = phase >> 8`.
- The phase advances only on samples where that LFO is applied, that is, when its depth is non-zero.

```
LFOFreq[32] = {0.17,0.19,0.23,0.27,0.34,0.39,0.45,0.55,0.68,0.78,0.92,1.10,1.39,1.60,1.87,2.27,
               2.87,3.31,3.92,4.79,6.15,7.18,8.60,10.8,14.4,17.2,21.5,28.7,43.1,57.4,86.1,172.3} Hz
```

Waveforms (WS: 0 = saw, 1 = square, 2 = triangle, 3 = noise), for i = 0..255:
- **PLFO**, signed -128..127:
  - saw: `i<128 ? i : i-256`
  - square: `i<128 ? 127 : -128`
  - triangle: `i<64 ? 2i : i<128 ? 255-2i : i<192 ? 256-2i : 2i-511`
  - noise: `128 - rand8`
- **ALFO**, 0..255:
  - saw: `255-i`
  - square: `i<128 ? 255 : 0`
  - triangle: `i<128 ? 255-2i : 2i-256`
  - noise: `rand8`

Noise tables are filled once from `rand()`.

Depth tables:
- `PSCALE = {0,7,13.5,27,55,112,230,494}` cents.
  `PLFO() = int(256 * 2^((PSCALE[PLFOS]*v/128)/1200)) << 4` (a 4.12 ratio), where v is the PLFO
  waveform value.
- `ASCALE = {0,0.4,0.8,1.5,3,6,12,24}` dB.
  `ALFO() = int(256 * 10^((-ASCALE[ALFOS]*v/256)/20)) << 4`, where v is the ALFO waveform value.

### 3.7 FM / modulation (sound stack) [SM]
- Each chip has `stack[64]` (int16) and `ptr` (6 bits).
- During the slot loop, slot n's output goes to `stack[ptr]` and `ptr` is then incremented, once per
  slot and for every slot whether active or not. So 32 increments per sample, and the stack always
  holds the last 64 slot outputs.

Modulation applies when MDL, MDXSL or MDYSL is non-zero. At the moment slot n is processed:
```
  m = (stack[(ptr + MDXSL) & 63] + stack[(ptr + MDYSL) & 63]) / 2      // C integer division (truncates toward 0)
  m = (m << 10) >> (24 - MDL)                                          // arithmetic shift
  if 16-bit: m <<= 1 ; a1 = (a1+m) & 0x7FFFE ; a2 = (a2+m) & 0x7FFFE
  else       :          a1 = (a1+m) & 0x7FFFF ; a2 = (a2+m) & 0x7FFFF
```
The modulation changes only the fetch address, never the stored position. The offset in samples is
`m_avg * 2^(MDL-14)`. `(ptr+k)` refers to the output written `64-k` slot-steps ago. For example,
k = 0x20 is the same slot one sample earlier, and k = 0x3F is the previous slot in this sample.
- [BUG] For SCSP #2, Supermodel reads **SCSP #1's** stack and pointer, and at that moment the pointer
  has already advanced one step. Use each chip's own stack.

### 3.8 Level, pan and send gains
A single gain function, `PAN4(tl, pan, sdl)`, is used for everything. Result is in 20.12 fixed point.

1. **TL attenuation:** add the dB weights of the set TL bits:
   bit0 0.4, bit1 0.8, bit2 1.5, bit3 3, bit4 6, bit5 12, bit6 24, bit7 48.
   `TLg = 10^(-dB/20)`.
2. **Pan:**
   - `pdB = 3*b0 + 6*b1 + 12*b2 + 24*b3`, using bits 0-3 of pan.
   - `P = 10^(-pdB/20)`, except `P = 0` when `(pan & 0xF) == 0xF`.
   - If pan bit 4 = 0: L = P, R = 1. Otherwise: L = 1, R = P.
3. **Send level:** `SDLg = {0, -36, -30, -24, -18, -12, -6, 0 dB}[sdl]`. sdl = 0 means mute
   (gain 0); otherwise `10^(dB/20)`.
4. `PAN4L = uint(4096 * 4.0 * L * TLg * SDLg)`. PAN4R is the same with R.

Per-slot uses (`sample` is the value returned in 3.4):
- **Direct out:** `outL += (sample * PAN4L(TL, DIPAN, DISDL)) >> 12`, and likewise for R.
- **DSP send:** `MIXS[ISEL] += (sample * PAN4L(TL, 0, IMXL)) >> 10`, i.e. 16x the gain (see 4.4).
- **Stack (FM) value:** see 3.4 step 11.

### 3.9 Common registers (offset from 0x400; word bits)

| Off | Bits / meaning | Supermodel behaviour |
|---|---|---|
| 0x00 | 9 MEM4MB, 8 DAC18B, 7-4 VER, 3-0 MVOL | DAC18B selects the output scaling (3.12). MVOL and MEM4MB are ignored. [ROM] Lost World writes 0x0300 to both chips (MEM4MB = 1, DAC18B = 1, MVOL = 0). It may set MVOL later through indirect writes. Supermodel ignores MVOL, so ignore it too, or [AMBIG] apply `MVOL/15`. |
| 0x02 | 8-7 RBL, 6-0 RBP | DSP ring length = `8192 << RBL` words. Ring base word address = `RBP << 12`. [ROM] Lost World: 0x0138 on both chips → 32 K words at byte 0x70000. |
| 0x04 | 12 MOFULL, 11 MOEMPTY, 10 MIOVF, 9 MIFULL, 8 MIEMPTY, 7-0 MIBUF | A read pops the MIDI FIFO (2.4). |
| 0x06 | 7-0 MOBUF | A write pushes to MIDI out (2.5). |
| 0x08 | 15-11 MSLC (W), 10-7 CA, 6-5 SGC, 4-0 EG (R) | A write keeps only bits 15-11. A read returns `CA<<7 \| SGC<<5 \| EG` for slot MSLC, where `CA = (cur>>24) & 0xF` (sample index bits 15..12), `SGC` = EG state, and `EG = 0x1F - (L>>21)`. [BUG] The read overwrites the register without MSLC, so later reads monitor slot 0. Keep MSLC separately. |
| 0x12-0x17 | DMA (DMEA, DRGA, DGATE/DDIR/DEXE/DTLG) | **Not implemented.** Writes are stored with no action. Lost World doesn't appear to need DMA [AMBIG]. |
| 0x18 | 10-8 TACTL (prescale), 7-0 TIMA | Timer A (3.10). |
| 0x1A | 10-8 TBCTL, 7-0 TIMB | Timer B. |
| 0x1C | 10-8 TCCTL, 7-0 TIMC | Timer C. |
| 0x1E | SCIEB: interrupt enable, bits 10-0 | |
| 0x20 | SCIPD: pending | A plain write stores the value. [HW?] Only bit 5 is writable on hardware. |
| 0x22 | SCIRE: a write of 1 clears the matching SCIPD bit | Side effects in 3.10. |
| 0x24, 0x26, 0x28 | SCILV0, SCILV1, SCILV2 (bits 7-0) | Level encoding (3.10). |
| 0x2A, 0x2C, 0x2E | MCIEB, MCIPD, MCIRE (main-CPU interrupt) | No main CPU on Model 3. Values are kept in globals. MCIRE clears MCIPD. Unimportant. |

Timer, interrupt and SCILV writes take effect only on SCSP #1. On #2 they are just stored.

SCIPD/SCIEB source bits:
- 0 EXT0
- 1 EXT1
- 2 EXT2
- 3 MIDI-in
- 4 DMA end
- 5 CPU manual
- 6 Timer A
- 7 Timer B
- 8 Timer C
- 9 MIDI-out
- 10 sample (1 fs)

Supermodel implements only 3, 6, 7 and 8.

### 3.10 Timers and the interrupt controller (SCSP #1) [SM]
Each timer keeps an internal counter `C` in 8.8 form. Initial value is 0xFFFF, meaning expired.

**Write to the TIMx word:**
```
C = (TIMx & 0xFE) << 8
```
[BUG?] Bit 0 of the count is dropped; the hardware would use the full 8 bits.

**Per output sample (`ticks = 1`):**
```
if C <= 0xFF00:
    C += 1 << (8 - prescale)        // prescale = bits 10-8; the count advances by 1 every 2^prescale samples
    if C > 0xFF00: C = 0xFFFF; SCIPD |= bit (A: 0x40, B: 0x80, C: 0x100)
    TIMx low byte = C >> 8          // visible counter
```
- With prescale 0 the timer fires after `0x100 - start` samples.
- With prescale p > 0 Supermodel fires `2^p - 1` samples early compared with an exact
  overflow-to-0x100 model.
- After firing, the timer stops until rewritten.

**SCIRE write:**
```
SCIPD &= ~SCIRE
then, for each timer whose C == 0xFFFF, set its SCIPD bit again
update IRQ
```
So acknowledging a timer without reloading it re-pends it immediately.

**Level decode:**
`level(src) = bit(src, SCILV0) | bit(src, SCILV1)<<1 | bit(src, SCILV2)<<2`, using bit index
`min(src, 7)`. Timers B and C both use bit 7 (`IrqTimBC`). MIDI uses bit 3. Timer A uses bit 6.
Levels are recomputed on every write to 0x24-0x29.

**IRQ update:**
```
pend = SCIPD ; if MIDI FIFO non-empty: pend |= 0x08
lvl = 0
if pend & SCIEB & 0x040: lvl = LV(TimA)
if pend & SCIEB & 0x180: lvl = max(lvl, LV(TimBC))
if pend & SCIEB & 0x008: lvl = max(lvl, LV(MIDI))   (and SCIPD bit 3 is cleared)
set 68K IPL = lvl
```

[ROM] Lost World's init copies 10 words from ROM 0x6002D0 to registers 0x418..0x42B:

| Register | Value |
|---|---|
| TIMA | 0x00CE |
| TIMB | 0x03C6 |
| TIMC | 0x0100 |
| SCIEB | 0x00C8 |
| SCIPD | 0x0000 |
| SCIRE | 0x00C8 |
| SCILV0 | 0x0088 |
| SCILV1 | 0x0048 |
| SCILV2 | 0x0000 |
| MCIEB | 0x0020 |

Derived configuration:
- Enabled sources: MIDI, Timer A, Timer B.
- Levels: **MIDI = 3, Timer A = 2, Timer B = 1**. These match the ROM's autovector handlers.
- Timer A: prescale 0, start 0xCE → 50 samples (882 Hz). Its ISR at 0x60307A reloads it from the
  ROM word at 0x60806A (0x00CE, adjusted by ±0x80 depending on a flag) and writes SCIRE = 0x40.
- Timer B: prescale 3, start 0xC6 → 457 samples in Supermodel (464 on exact hardware). Its ISR at
  0x60072E rewrites 0x03C6 and SCIRE = 0x80.

### 3.11 Sound RAM
- Each chip addresses its own RAM. In Supermodel, SCSP #1 uses 68K 0x000000-0x0FFFFF and SCSP #2
  uses 0x200000-0x2FFFFF, 1 MB each.
- Slot fetch is `RAM + base + offset`.
- DSP access is at word index `ADDR` (byte `2*ADDR`). DSP word addresses above 0x7FFFF map to 0.
- [AMBIG] The game sets MEM4MB = 1 (4 Mbit = 512 KB), and MAME uses 512 KB per SCSP. Supermodel
  exposes 1 MB with no mirroring, and its slot masks (0x7FFFE) effectively use 512 KB for 16-bit
  data. A 1 MB array per chip with a 512 KB slot-address wrap is a safe choice.
- The 68K program copies sample data from the sample ROM window (0x800000+) into SCSP RAM. The SCSPs
  cannot read the ROMs in Supermodel.

### 3.12 Per-sample mixing and output [SM]
For each output sample:
```
outF_L = outF_R = outR_L = outR_R = 0          (int32)
for sl in 0..31:
    process slot sl of SCSP#1 (3.4); add direct out to outF_*; add DSP send to MIXS of DSP#1; stack#1 ptr++
    process slot sl of SCSP#2; add to outR_*; DSP#2 send; stack#2 ptr++
run DSP#1 step, then DSP#2 step (section 4)
for i in 0..15:   // effect returns, per chip, using that chip's slot i EFSDL/EFPAN
    if EFSDL(slot i) != 0: out_L += (EFREG[i] * PAN4L(0, EFPAN, EFSDL)) >> 12 ; likewise for R
```
EXTS (DSP inputs 0x30/0x31; slots 16/17 EFSDL/EFPAN on hardware) are always 0 and aren't mixed.

Output scaling (per chip, using that chip's DAC18B bit):
- DAC18B = 1: `out = clamp(out, -131072, 131071)`, emitted as is (no shift).
- DAC18B = 0: `out = clamp(out >> 2, -32768, 32767)`.

[ROM] Lost World sets DAC18B = 1 on both chips. Supermodel's values can therefore exceed 16 bits;
they are scaled by `SoundVolume/100` (default 1.0) and finally clamped to int16 by the host
output. [AMBIG] A real 18-bit DAC would mean dividing by 4 (`CORRECT_FOR_18BIT_DAC`, disabled in
Supermodel). Copy Supermodel for loudness parity.

A "Balance" option (default 0) multiplies each slot's sample by `1+bal` (#1) or `1-bal` (#2).
Ignore it.

Host output (`QuadFrontRear`):
- 4 channels: FL, FR, RL, RR, each `clamp16`.
- Stereo: `L = clamp16((FL+RL)/2)`, `R = clamp16((FR+RR)/2)`.
- Mono: the average of all four.

---------------------------------------------------------------------------------------------------

## 4. SCSP DSP (one per chip) [SM: Supermodel's default MAME-derived core; `LegacySoundDSP=false`]

### 4.1 State
- **COEF[64]**: int16. The multiplier uses `COEF >> 3` (13-bit signed).
- **MADRS[32]**: uint16.
- **MPRO[128][4]**: uint16. Word 0 is the most significant; it sits at the lowest address.
- **TEMP[128]**: 24-bit. **MEMS[32]**: 24-bit. **MIXS[16]**: 20-bit inputs held in int32.
- **EXTS[2]**: 16-bit, always 0. **EFREG[16]**: int16 outputs.
- **DEC**: unsigned counter, decremented once per sample.
- **RBP, RBL**: from common register 0x02. RBL is stored as a length in words: 8192 << field.
- **Running flag**: false at reset. It is set by any write (8/16/32-bit) whose address is exactly
  **0xBF0** (MPRO step 126, word 0). At that moment `LastStep` = 1 + index of the last non-zero
  instruction.
  - [AMBIG] Recommended: once running, always execute all 128 steps. Zero instructions have no side
    effects, so this matches Supermodel except when MPRO changes after the 0xBF0 write. Before the
    first start the DSP does nothing: EFREG stays 0 and MIXS are not cleared.

### 4.2 Instruction fields (per step i: W0..W3 = MPRO[i][0..3])

| Word | Bits and fields |
|---|---|
| W0 | TRA = 14-8 (7 bits), TWT = 7, TWA = 6-0 |
| W1 | XSEL = 15, YSEL = 14-13, IRA = 11-6 (6 bits), IWT = 5, IWA = 4-0 |
| W2 | TABLE = 15, MWT = 14, MRD = 13, EWT = 12, EWA = 11-8, ADRL = 7, FRCL = 6, SHIFT = 5-4, YRL = 3, NEGB = 2, ZERO = 1, BSEL = 0 |
| W3 | NOFL = 15, COEF = 14-9 (6 bits), MASA = 6-2 (5 bits), ADREB = 1, NXADR = 0 |

### 4.3 One DSP sample
The local registers below start at 0 **every sample**: ACC, SHIFTED, X, Y, B, INPUTS, MEMVAL,
FRC_REG, Y_REG, ADRS_REG. [HW?] On hardware they probably persist. Supermodel resets them.

```
if not running: return
EFREG[0..15] = 0
for step in 0..LastStep-1:            (or 0..127)
  1 INPUTS: IRA<=0x1F: MEMS[IRA]; 0x20..0x2F: MIXS[IRA-0x20]<<4; 0x30..0x31: EXTS[IRA-0x30]<<8;
            IRA>=0x32: ABORT the whole program for this sample (no DEC--, no MIXS clear) [SM quirk].
            INPUTS = sign-extend 24 bits.
  2 if IWT: MEMS[IWA] = MEMVAL (from an earlier MRD); if IRA==IWA: INPUTS = MEMVAL
  3 B = ZERO ? 0 : (BSEL ? ACC : sext24(TEMP[(TRA+DEC)&0x7F]));  if NEGB (and !ZERO): B = -B
  4 X = XSEL ? INPUTS : sext24(TEMP[(TRA+DEC)&0x7F])
  5 Y = YSEL 0: FRC_REG | 1: COEF[COEF]>>3 | 2: (Y_REG>>11)&0x1FFF | 3: (Y_REG>>4)&0x0FFF
  6 if YRL: Y_REG = INPUTS                (after Y was taken)
  7 SHIFTED from the *previous* ACC:
        SHIFT 0: clamp(ACC, -0x800000, 0x7FFFFF)
        SHIFT 1: clamp(ACC*2, same range)
        SHIFT 2: sext24(ACC*2)            (wrap, no saturation)
        SHIFT 3: sext24(ACC)
  8 Y = sext13(Y);  ACC = (int64(X)*Y >> 12) + B        (int32 result)
  9 if TWT: TEMP[(TWA+DEC)&0x7F] = SHIFTED
 10 if FRCL: FRC_REG = (SHIFT==3) ? SHIFTED & 0xFFF : (SHIFTED>>11) & 0x1FFF
 11 if MRD or MWT:
        A = MADRS[MASA]
        if !TABLE: A += DEC
        if ADREB:  A += ADRS_REG & 0xFFF
        if NXADR:  A += 1
        A &= TABLE ? 0xFFFF : (RBL-1)
        A += RBP << 12 ;  if A > 0x7FFFF: A = 0          (word address)
        only if step is ODD:
          MRD: MEMVAL = NOFL ? (uint16 RAM[A]) << 8 : UNPACK(RAM[A])
          MWT: RAM[A] = NOFL ? (SHIFTED >> 8) : PACK(SHIFTED)
 12 if ADRL: ADRS_REG = (SHIFT==3) ? (SHIFTED>>12) & 0xFFF : (INPUTS >> 16)   (arithmetic shift)
 13 if EWT: EFREG[EWA] += SHIFTED >> 8                 (int16 wrap)
DEC -= 1
MIXS[0..15] = 0
```
RAM[A] is the big-endian 16-bit word at byte address 2*A of that chip's sound RAM.

Memory accesses happen only on odd steps, matching the hardware's alternating memory slots.
Programs place memory operations accordingly.

### 4.4 Float format of ring-buffer words (when NOFL = 0)
16-bit word layout: `S(15) | E(14..11) | M(10..0)`.

**PACK(v)**, where v is a 24-bit signed value:
```
s = bit23(v)
t = (v ^ (v<<1)) & 0xFFFFFF
e = number of leading zero bits of t starting at bit 23, capped at 12    // counts redundant sign bits
if e < 12: v = (v << e) & 0x3FFFFF  else: v = v << 11
m = (v >> 11) & 0x7FF
word = s<<15 | e<<11 | m
```

**UNPACK(w):**
```
s = bit15, e = bits14..11, m = bits10..0
u = m << 11
if e > 11: e = 11; u |= s<<22
else: u |= (s^1)<<22
u |= s<<23
u = sext24(u) >> e          (arithmetic)
```

### 4.5 Slot → DSP → output path summary
- **Slot sends:** `MIXS[ISEL] += sample * TLg * IMXLg * 16`. Here sample is the post-EG 16-bit
  value and IMXL uses the SDL table (0 = off, then -36 dB … 0 dB).
- **DSP input:** reading IRA 0x20+n gives `MIXS[n] << 4`, so a full-scale 16-bit slot at 0 dB
  arrives at about ±2^23 (24-bit full scale).
- **DSP output:** each program writes EFREG[0..15] through `SHIFTED >> 8` (16-bit).
- **Effect mix:** each EFREG[i] is mixed into that chip's output with the EFSDL/EFPAN fields of
  **slot i** (slots 0-15) of the same chip. Gain is `4 * PAN * SDL`, with TL = 0 dB (3.8, 3.12).
  - Legacy core only: Supermodel's legacy mode uses `EFSDL << 14` in the table index, which is a
    different gain. Not used by default.

---------------------------------------------------------------------------------------------------

## 5. Lost World specifics and implementation notes

1. **Board configuration:**
   - Step 1.5, 68K + 2 SCSP, **no DSB/MPEG board**.
   - Audio `QuadFrontRear`: SCSP #1 → front pair, SCSP #2 → rear pair.
   - Both chips run every sample. #2 is driven only by the 68K writing its registers and RAM at
     0x200000/0x300000. No MIDI or interrupts come from #2. [ROM] The 68K initialises both chips:
     reg 0x00 = 0x0300, reg 0x02 = 0x0138, slot clear routine run for both bases.
2. **Interrupts in use** [ROM]:
   - L1 = Timer B (sequencer tick, about 96 Hz),
   - L2 = Timer A (about 882 Hz),
   - L3 = MIDI-in (byte receive).

   These levels come from SCILV registers the game writes, not from fixed wiring.
3. **MIDI flow:**
   - The PPC sets `midiCtrlPort` bit 5 and enables IRQ 0x40. Each frame the harness raises 0x40 up
     to 129 times during VBlank.
   - The PPC ISR writes bytes to 0xF0080000. Bytes go into a 256-entry FIFO.
   - During the next sound frame the 68K takes a level-3 interrupt and reads `$100405` per byte
     until the FIFO is empty.
4. **PPC reads** of 0xF0080000/4 return the constants 0x00/0x83. There is no handshake or
   back-pressure. Keep the FIFO large; 256 is enough given at most ~129 IRQs per frame.
5. **Timing:**
   - 256 68K cycles per sample, 735 samples (188 160 cycles) per Supermodel frame.
   - The 68K runs after audio generation for each sample.
   - The sound frame runs after the PPC frame in single-thread mode.
   - Sound timers tick once per 44.1 kHz sample.
6. **Byte order:**
   - ROM files are word-swapped (68K byte A = file[A^1]).
   - SCSP RAM and registers are big-endian from the 68K's view.
   - 16-bit PCM and DSP ring words are big-endian in sound RAM.
7. **Supermodel deviations to decide on** (exact-match vs hardware):
   - MDL and PLFOS LSB masking (3.2).
   - Timer count bit 0 dropped and early fire (3.10).
   - Slave FM reading the master's stack (3.7).
   - MIDI-out writes into the in-FIFO (2.5).
   - MSLC lost after a monitor read (3.9).
   - Initial EG state ATTACK (3.3).
   - DSP locals reset every sample, odd-step memory only, LastStep computed at the 0xBF0 write (4.1).
   - 32-bit SCSP reads return 0xFFFFFFFF (1.2).
   - 512 KB vs 1 MB RAM (3.11).
   - MVOL ignored and DAC18B output not divided by 4 (3.9, 3.12).
   - DMA not implemented.
   - SSCTL ≠ 0 slots are silent.
