# Real3D Pro-1000 (Sega Model 3, Step 1.5) - Renderer Format Specification

Target: *The Lost World* (`lostwsga`, stepping 1.5, PPC 100 MHz). Written for the
Lost World port; the formats still apply to Sega Rally 2 (Step 2.1), whose
memory map differences are in `docs/ARCHITECTURE.md`.
Source of truth: behaviour of the Supermodel reference (`Src/Model3/Real3D.cpp`,
`Model3.cpp`, `TileGen.cpp`, `Src/Graphics/New3D/*`, `Render2D.cpp`). Where the
reference is a guess or internally inconsistent this is flagged with **[AMBIG]**.

Conventions used below:
- "word" = 32-bit device word *as the Real3D/tilegen sees it* (see 1.2).
- Bit ranges are inclusive, bit 0 = LSB. `w[n]` = word n of a structure.
- `f32(x)` = reinterpret the 32 bits as IEEE-754 single.
- Culling/model/texture "addresses" inside data structures are **word addresses**.

---------------------------------------------------------------------------
## 1. Memory map, byte order, buffering

### 1.1 PowerPC address map (Real3D + tilegen part)

| PPC range | Size | Function | Index used by device |
|---|---|---|---|
| `84000000-8400003F` | | Real3D status (read, 32-bit) | `reg = addr & 0x3F` |
| `88000000-88000007` | | Command/flush port (any 32-bit write, data ignored) -> `Flush()` | |
| `8C000000-8C3FFFFF` | 4 MB | Culling RAM low | word = `(addr & 0x3FFFFF)/4` |
| `8E000000-8E0FFFFF` | 1 MB | Culling RAM high | word = `(addr & 0xFFFFF)/4` |
| `90000000-9000000B` | | VROM texture port | address bits ignored |
| `94000000-940FFFFF` | 1 MB | Texture FIFO (any address appends; address ignored) | |
| `98000000-983FFFFF` | 4 MB | Polygon RAM (writes beyond 4 MB ignored) | word = `(addr & 0x3FFFFF)/4` |
| `9C00000x` | | Config regs: `+0` ping-pong size, `+4` flags, `+8` ping-pong+update size (words) | `(addr&0xF)/4` |
| `F1000000-F10FFFFF` | 1 MB | Tilegen VRAM (patterns, scroll tables, mask, name tables) | `addr & 0x1FFFFF` |
| `F1100000-F111FFFF` | 128 KB | Tilegen palette (32768 x 32-bit) | |
| `F1180000-F11800FF` | | Tilegen registers | `addr & 0xFF` |

The CPU cannot read back culling RAM, polygon RAM or VROM (no read handlers).
`C2000000` Real3D DMA exists only on Step 2.x. On Step 1.x the game moves bulk
data with the NCR 53C810 SCRIPTS "memory move", which performs
`Write32(dest, Read32(src))` through the same bus handlers as CPU stores, so
DMA'd data obeys exactly the same byte-order rule as CPU stores.

Real3D internal **culling address space** (24-bit word addresses inside nodes):
- `0x000000-0x0FFFFF` -> culling RAM low, word `addr`.
- `0x800000-0x83FFFF` -> culling RAM high, word `addr & 0x3FFFF`.
- anything else -> invalid (reference: node ignored / null).

Internal **model / texture-source address space** (24-bit word address):
- `< 0x100000` -> polygon RAM word `addr`.
- `>= 0x100000` -> VROM word `addr` (NOT minus 0x100000). 24 bits of words = 64 MB.

### 1.2 Byte order (critical)

Real3D and tilegen are little-endian PCI devices on a big-endian PPC bus. For
every 32-bit store of register value `V` by the CPU (or SCSI DMA) the device
receives `W = bswap32(V)`:

    W = ((V & 0xFF) << 24) | ((V & 0xFF00) << 8) | ((V >> 8) & 0xFF00) | (V >> 24)

Equivalent statement in terms of raw bytes: if the CPU-side memory image of the
store is bytes `b0 b1 b2 b3` (big-endian, `b0` = byte at the lowest address),
the device word is the **little-endian** interpretation `b0 | b1<<8 | b2<<16 | b3<<24`.
Supermodel stores `W` natively in a little-endian host array, so its RAM dumps
have exactly the CPU byte image and are read as LE u32. All bit layouts in
this document refer to `W`. Floats in nodes/matrices are `f32(W)` (games use
byte-reversed stores, so they arrive as normal IEEE floats).

This applies to: culling RAM, polygon RAM, texture FIFO, texture port,
config registers, tilegen VRAM/palette/registers. Status reads are the inverse:
device value `R` is returned to the CPU as `bswap32(R)`.

16-bit texels inside a word: texel `2k` = `W & 0xFFFF`, texel `2k+1` = `W >> 16`
(i.e. LE u16 at byte offset `2n` of the byte image).

### 1.3 VROM (Lost World: 32 MB)

ROM region `vrom`: 16 files (`mpr-19902.26` .. `mpr-19917.41`, 2 MB each),
`stride=32, chunk_size=2`, **no byte swap**: file *k* (k=0..15) supplies bytes
`[32n+2k, 32n+2k+1]` from its own bytes `[2n, 2n+1]`. Resulting 32 MB image is
read as little-endian words/half-words directly (VROM word `i` = LE u32 at byte
`4i`; texel = LE u16).

VROM space is 64 MB. Rule: if VROM <= 32 MB, it is mirrored into the upper
32 MB (byte offset `0x2000000`), so word addresses `0x800000-0xFFFFFF` alias
`0x000000-0x7FFFFF`.
**[AMBIG / reference bug]** Supermodel's lower copy is
`CopyTo(&vrom[0], 32*100000)` = only 3,200,000 bytes (0x30D400); the rest of
the lower 32 MB stays zero. The upper mirror is complete. A clean
implementation should mirror the full 32 MB into both halves (superset of the
reference). Model addresses `< 0x100000` never reach VROM anyway (they hit
polygon RAM).

### 1.4 Buffering / ping-pong / when data becomes visible

- There is **one** copy of each RAM in the reference; no flush-time copy. The
  renderer reads RAM directly at the end of each emulated frame.
- Config regs (Lost World writes them at boot): `R0 = pingPongMemSize`,
  `R8 = pingPongAndUpdateMemSize` (words; typical 0x5FFC..0xBFFC and
  0x18000..0x40000). Update buffering is active only if `R8 > R0`.
- After the ping-pong bit flips (see §8) and until the start of the next
  VBlank, writes to **culling RAM high** and **polygon RAM** are deferred
  (kept in update buffers carved out of low culling RAM words `[0,R0)` and
  `[R0,R8)` respectively) and applied in order at the start of the next VBlank.
  If a buffer overflows, it is applied immediately and the write goes through.
- Writes to **culling RAM low** are never deferred.
- Net effect for a renderer that draws at end of frame: the frame shows all
  high-culling/polygon writes made *before* the flip line of this frame plus
  everything deferred from the previous frame; writes after the flip appear
  next frame. A simple implementation: queue post-flip writes, apply them at
  VBlank start.
- **[AMBIG]** Real hardware very likely has two physical ping-pong culling
  memories (games write ping-pong data twice in consecutive frames); not
  emulated. Treat high culling RAM as single memory.
- Texture FIFO upload timing: see §6.1.

---------------------------------------------------------------------------
## 2. Viewports

Traversal starts at culling address **0x800000** (high culling RAM word 0)
every frame. Each viewport node is a block of words `vp[0..0x25]`:

| Word | Bits | Meaning |
|---|---|---|
| 0x00 | 3-4 | priority (0..3) |
| | 5 | 1 = viewport disabled (node still parsed, children not traversed) |
| | 8-9 | "select" (stored, unused) |
| | 10-31 | viewport number (unused) |
| 0x01 | 0-31 | next viewport pointer. Stop if `== 0x01000000` exactly, or if `(p & 0xFFFFFF) == 0`. Otherwise recurse on `p & 0xFFFFFF`. |
| 0x02 | | child pointer (typed node pointer, §3.3) |
| 0x03 | | `cota` f32 (used for texture LOD, §6.6) |
| 0x04,0x05,0x06 | | sun vector f32 (Real3D eye coords, see §4.2 for mapping) |
| 0x07 | | sun (diffuse) intensity f32, clamped to [0,1] |
| 0x08 | | `cv` f32 = 1/(left-right)-ish frustum coef |
| 0x09 | | `cw` f32 |
| 0x0A | | `io` f32 |
| 0x0B | | `jo` f32 |
| 0x0C..0x13 | | clip-plane coefs f32: `bnlu,bnlv,bntu,bntw,bnru,bnrv,bnbu,bnbw` |
| 0x14 | 0-15 / 16-31 | width / height, unsigned 14.2 fixed (/4) |
| 0x15 | 0-11 | env (coordinate-system) matrix index (Model 3 games: 0) |
| 0x16 | 0-23 | matrix base (culling address) |
| 0x17 | 0-23 | LOD blend table base (culling address) |
| 0x1A | 0-15 / 16-31 | X / Y origin, unsigned 12.4 (/16); Y measured from **top** of 496x384 screen |
| 0x1C | 0-15 / 16-31 | line-of-sight sample X / Y (/16, rounded); Y from top |
| 0x1D | 0-15 | spotlight centre Y, signed 13.3 (/8), from top |
| | 16-31 | spotlight Y size (unsigned) |
| 0x1E | 0-15 | spotlight centre X, signed 13.3 (/8) |
| | 16-31 | spotlight X size |
| 0x1F | | spotlight extent f32 |
| 0x20 | 0-7 | scroll-fog value /255 |
| | 8-10 | spotlight-on-fog colour index |
| | 11-13 | spotlight colour index |
| 0x21 | | spotlight start: `1.0 / f32(w)` |
| 0x22 | 16-23 / 8-15 / 0-7 | fog colour R / G / B (/255) |
| 0x23 | | fog density = `abs(f32)`; if not `< 99999` treat as 0 |
| 0x24 | 0-7 | scroll attenuation /255 |
| | 8-15 | ambient light intensity /255 |
| | 16-23 | fog attenuation (spot-on-fog strength) /255 |
| 0x25 | 0-15 | fog start: `int16 / 255` |
| | 16-23 | fog ambient /255 |

Colour index (3 bits) -> RGB: bit2 = R, bit1 = G, bit0 = B (each 0 or 1).

Ordering: viewports form a singly linked list via `vp[1]`; list order is kept.
Rendering iterates priority 0,1,2,3; within a priority, viewports in list
order. Disabled viewports still contribute fog parameters (§7.4).

Screen placement (native 496x384, GL-style origin bottom-left):
`x = vpX`, `y_gl = 384 - (vpY + vpHeight)`, `w = vpWidth`, `h = vpHeight`.

---------------------------------------------------------------------------
## 3. Culling (scene-graph) nodes

### 3.1 Layout (Step 1.5: 10 words; `off = 0`)

Step 1.0 omits words 1-2 (`off = 2`, words >=3 are read at `n - 2`). Step 1.5
uses **off = 0**; all indices below are for 1.5.

**w[0] - flags**
| Bits | Meaning |
|---|---|
| 0-1 | node type: 0 = viewport (ignored if reached as a culling node), 1 = root, 2 = culling |
| 2 | valid colour table (load colour-table address, §3.6) |
| 3 | child is a **4-entry LOD table** (code-defined; doc name "indirect child") |
| 4 | use translation `w[4..6]` instead of matrix (doc name "no matrix") |
| 5 / 6 | use sibling / use child (doc only; not tested by reference) |
| 7 | reset matrix (§4.3) |
| 8, 9 | discard bits: if both set, node and its children are skipped (sibling too, since return happens before) |
| 10-18 | node ID |
| 24-31 | UF ref / 3D model / point / point ref (bit 28) / animation / billboard / child billboard / extra child (doc only) |

**w[1]** (Step 1.5+): bit0 = model scale valid -> `modelScale = f32(w1 & ~3)`;
bit1 = disable culling (inherited by descendants).
Model scale does **not** scale vertices. It is only (a) a multiplier on
culling/blend radius, and (b) the factor normals are divided by after
transformation (the node matrix already contains the scale).

**w[2]** (Step 1.5+): if bit 15 set ("texture replace"): `texOffX = 32*((w2>>7)&0x3F)`,
`texOffY = 32*(w2 & 0x1F)`, `texPage = (w2>>14)&1`. Otherwise inherit.
**[AMBIG]** doc lists Y offset as 7 bits (0-6); reference uses 5 bits.

**w[3]**: bits 0-11 matrix index; bits 12-18 LOD table index (0x7F = none);
bits 19-31 colour-table address part 1.

**w[4],w[5],w[6]**: translation x,y,z (f32), used when w0 bit4 set.

**w[7]**: bits 0-23 child address; bits 24-26 child pointer type (§3.3);
bits 28-31 colour-table part 2. Reference masks `child = w7 & 0x07FFFFFF`.

**w[8]**: bits 0-23 sibling address; bit 24 = null sibling; bits 25-31
colour-table part 3. Reference masks `sib = w8 & 0x01FFFFFF`.

**w[9]**: bits 0-15 culling radius (float16, §3.7); bits 16-31 blend (LOD) radius (float16).

### 3.2 Traversal algorithm (per culling node at address A)

    node = cull(A); if null or stack depth >= 1024: return
    if type == 0 (viewport): return
    if (w0 & 0x300) == 0x300: return
    # siblings FIRST (before own children), unless colour-table node:
    ctNode = (w0 & 7) == 6 ; pointRef = w0 bit 28
    if (!ctNode || pointRef) and !(sib & 0x1000000) and sib != 0:
        visit_culling_node(sib)          # sibling is always a culling node
    if w0 bit2: colorTableAddr = ... (§3.6)   # global, NOT restored on return
    push(attribs: texOffX/Y, texPage, modelScale, nodeAlpha, disableCulling)
    apply w1, w2 (Step 1.5)
    pushMatrix
    if w0 bit4: M = M * Translate(w4,w5,w6)
    elif (w3 & 0xFFF) != 0: M = M * Matrix[w3 & 0xFFF]      # index 0 = no matrix
    if w0 bit7: resetMatrix(M)
    (x,y,z) = M translation column (view space, after coord fix-up)
    cullR  = f16(w9 & 0xFFFF) * modelScale
    blendR = f16(w9 >> 16)    * modelScale
    outside = any of:
        z*bnlu - x*bnlv > cullR,  z*bntu + y*bntw > cullR,
        z*bnru - x*bnrv > cullR,  z*bnbu + y*bnbw > cullR
    noLod = (lodIdx == 0x7F)
    LODscale = (disableCulling || noLod) ? +inf : max(0, blendR / |(x,y,z)|)
    lod = LODTable[lodIdx]       # 4 x {deleteSize, blendFactor}
    if disableCulling || (!outside && (noLod || LODscale >= lod[3].deleteSize)):
        descend child (below)
    popMatrix; pop(attribs)

Descending the child when **w0 bit3** (LOD table): `L = cull(child & 0xFFFFFF)`
(4 words). Pick `k` = first of 0,1,2 with
`LODscale >= lod[k].deleteSize && (L[k] & 0x01000000)`; else `k = 3`.
`a = clamp(lod[k].blendFactor * (LODscale - lod[k].deleteSize), 0, 1)`,
snapped: `a > 31/32 -> 1`, `a < 1/32 -> 0`. Save parent alpha `P`;
`nodeAlpha = P*a`. If `child & 0x01000000` == 0 the entries are culling nodes,
else they are models. Visit/draw `L[k] & 0xFFFFFF`; if `a < 1` and `k != 3`
also visit/draw `L[k+1] & 0xFFFFFF` with `nodeAlpha = (1-a)*P` (cross-fade).

Otherwise (no LOD table): `a = noLod ? 1 : clamp(lod[3].blendFactor*(LODscale-lod[3].deleteSize),0,1)`,
`nodeAlpha *= a`, then `DescendNodePtr(child)` (§3.3).

### 3.3 Typed node pointers (viewport w2, culling node w7)

    if (p & 0xFFFFFF) == 0: ignore
    switch (p >> 24) & 0x5:
        0 -> culling node at p & 0xFFFFFF
        1 -> model at p & 0xFFFFFF (model address space, §1.1)
        4 -> pointer list at p & 0xFFFFFF (culling address)
        5 -> ignored
(bit 25 = 0x02000000 is not part of the type test.)

### 3.4 Pointer lists

Sequence of words starting at the list address. For each entry `e`, in order:
- if `e & 0x01000000` is clear: visit culling node `e & 0xFFFFFF` (note: a zero
  address is *not* filtered here; it resolves to low culling word 0).
- if `e & 0x02000000` is set: this was the last entry; stop.
Entries with bit 24 set are skipped but still checked for the end bit. There
is no other terminator.

### 3.5 LOD blend table

Base = culling address `vp[0x17] & 0xFFFFFF`. 128 entries x 8 words. Entry `i`:
`{deleteSize0, blendFactor0, deleteSize1, blendFactor1, ..., deleteSize3, blendFactor3}` as f32.

### 3.6 Colour table address

    cta = (w3 >> 19) | ((w7 >> 28) << 13) | ((w8 >> 25) << 17)
    cta &= 0x0FFFFF          # word address in POLYGON RAM

Initial value 0x400; global state that persists across nodes, viewports
**and frames** (not saved/restored by push/pop). Entry format (polygon RAM word):
R = bits 16-23, G = 8-15, B = 0-7.

### 3.7 float16 (culling/blend radius)

`u` 16 bits: `e = u >> 10` (6-bit two's complement: `e >= 32 -> e - 64`),
`m = u & 0x3FF`. Value = `2^e * (1 + m/1024)`. Always positive; `u = 0` gives 1.0.

(32-bit "pro float" variant, not used by the culling path: sign bit31, 6-bit
signed exponent bits 25-30, mantissa bits 0-24 -> top 23 bits.)

### 3.8 Node attribute stack

Push on entering each culling node (max 128 effective entries; push fails
silently past 128 but pop still pops), reset at every viewport:
texOff=(0,0), page=0, modelScale=1, nodeAlpha=1, disableCulling=false.
Matrix stack is pushed/popped likewise. Recursion depth limit 1024.

---------------------------------------------------------------------------
## 4. Matrices, coordinate system, projection

### 4.1 Matrix storage

Matrix `i` = 12 consecutive f32 words at culling address `matrixBase + 12*i`:

    m[0..2]  = translation (tx, ty, tz)
    m[3..5]  = row 0 (r00 r01 r02)
    m[6..8]  = row 1
    m[9..11] = row 2

    | m3  m4  m5  m0 |
    | m6  m7  m8  m1 |
    | m9  m10 m11 m2 |
    | 0   0   0   1  |

Column vectors, post-multiply: `M_current = M_current * M_node`.

### 4.2 Coordinate system / matrix 0

At each viewport: `M = C`, where (GL view space from Real3D):

    C = | 0  1  0  0 |
        | 0  0 -1  0 |
        |-1  0  0  0 |
        | 0  0  0  1 |       i.e. GL.x = R.y, GL.y = -R.z, GL.z = -R.x

Real3D eye space is x = forward, y = right, z = down. If `vp[0x15]&0xFFF == 0`
(all Model 3 games): `M = C * Matrix[0]` (matrix 0 = game's world->eye/
coordinate-swap matrix, typically `[[0,0,-1],[1,0,0],[0,-1,0]]`), and
`coordSysRot = rotation part of M`. Otherwise (Pro-1000 SDK only):
`coordSysRot = C`, `M = C * inverse(Matrix[idx])` (rigid inverse `[R^T | -R^T t]`).
The sun vector uses the same mapping: `sunGL = (vp[5], -vp[6], -vp[4])`.

### 4.3 Reset matrix (w0 bit 7)

Keep translation and scale of current M, replace rotation by `coordSysRot`:
`T = transpose(rot3x3(M))` with zero translation, columns normalised (divide by
column lengths of M); `M = M * T * coordSysRot`.

### 4.4 Projection

Frustum tangents: `left = -jo/cv`, `right = (1-jo)/cv`,
`bottom = -(1-io)/cw`, `top = io/cw`. Reference projection (reverse-Z, infinite far):

    P = | 2/(r-l)   0        (r+l)/(r-l)   0    |
        | 0         2/(t-b)  (t+b)/(t-b)   0    |
        | 0         0        0             near |
        | 0         0       -1             0    |     near = 1e-3

Depth written = `near / (-z_eye)`; cleared to 0; depth test GEQUAL (opaque),
GREATER (translucent). No near/far from data (only clip-plane coefs used for culling).

---------------------------------------------------------------------------
## 5. Models and polygons

A model is a sequence of polygons; each = 7 header words + 4 words per *new*
(non-shared) vertex. Next polygon = `hdr + 7 + 4*(nVerts - nShared)`.
Model ends when: `h[6] == 0` (checked first, polygon not processed), or after a
polygon with `h[1]` bit2 (last), or when the next polygon would exceed memory
(polygon RAM 1M words; VROM 16M words).

### 5.1 Header words

**h[0]**
| Bits | Meaning |
|---|---|
| 0,1,2,3 | vertex 0/1/2/3 of the PREVIOUS polygon is reused (see 5.3) |
| 4 | anti-aliasing (unused) |
| 5 | points (unused) |
| 6 | 0 = triangle, 1 = quad |
| 7 | specular enable |
| 8 | discard bit "2" |
| 9 | discard bit "1". Both 8 & 9 set: polygon not drawn (still feeds vertex reuse). Only bit 9: alpha halved |
| 10-23 | polygon number (often 0; unused). **[AMBIG]** doc says bits 10-24, code uses mask `0xFFFC00` |
| 25 | clockwise winding (unused; culling uses the face normal) |
| 26-31 | specular coefficient: `(h0>>26)/63` |

**h[1]**: bits 8-31 face normal X, signed, `(int32)h1 >> 8` / 2^22 (2.22 fixed).
bit0 no-LOS-return; bit1 colour source: 1 = RGB in h[4], 0 = colour table;
bit2 last polygon; bit3 smooth shading (per-vertex normals); bit4 double-sided;
bit5 fixed shading; bit6 UV scale (1 = 16.0 integer texels, 0 = 13.3 -> x1/8);
bit7 edge-on translucency (unused).

**h[2]**: bits 8-31 face normal Y (same format). bit0 V mirror; bit1 U mirror;
bits 2-3 microtexture min LOD; bit4 microtexture enable; bits 5-7 microtexture id.

**h[3]**: bits 8-31 face normal Z. bits 0-2 texture height code, bits 3-5 width
code: size = `32 << code`, codes 6,7 treated as 0 (32). bit6 V (Y) wrap
smoothing, bit7 U (X) wrap smoothing.

**h[4]**: bits 8-31: if h1 bit1 = 1, RGB: R = bits 24-31, G = 16-23, B = 8-15;
else colour index = bits 8-19 (12 bits) into colour table (§3.6); bits 20-31 =
"sensor colour" index (unused). bit7 translator map (vertex colour x16).
bit6 texture page. bits 0-4 texture X high 5 bits.

**h[5]**: bits 8-31 texture NP (unsigned; Step 1.5 scale 1/16384, 10.14).
bit7 texture X low bit. bits 0-4 texture Y.
`texX = 32 * (((h4 & 0x1F) << 1) | ((h5 >> 7) & 1))` (0..2016),
`texY = 32 * (h5 & 0x1F)` (0..992).

**h[6]**
| Bits | Meaning |
|---|---|
| 0-2 | translucency mode; non-zero => "texture alpha" in use |
| 3 | layered polygon (decal; stencil) |
| 4 | high priority (drawn in an overlay pass, §7.3) |
| 5-6 | shininess 0..3 |
| 7-9 | texture format 0..7 (§6.5) |
| 10 | texture enable |
| 11-15 | light modifier / fog intensity: `value/16` (0..1.9375) |
| 16 | 1 = lighting disabled (luminous) |
| 17 | translucency layer select (0 -> trans layer A, 1 -> layer B) |
| 18-23 | transparency: if bit 23 set -> opaque (alpha 255); else alpha = `((h6>>18)&0x3F)*255/32` and polygon is "poly-alpha" |
| 24-30 | translator map offset; value 2 => texture RGB inverted (1-rgb) |
| 31 | alpha test / contour (1-bit texture transparency) |

"polyAlpha" = (h6 bit 23 == 0). Polygons with final face alpha 0 are dropped.

### 5.2 Vertex words (4 per new vertex)

    v0: bits 8-31 X (signed), bits 0-7 normal X or fixed-shade value
    v1: bits 8-31 Y (signed), bits 0-7 normal Y
    v2: bits 8-31 Z (signed), bits 0-7 normal Z
    v3: bits 16-31 U (unsigned 16), bits 0-15 V (unsigned 16)

Position = `((int32)v >> 8) / 2048.0` on Step 1.5 (13.11; Step 1.0: /128).
Node model scale is NOT applied to positions.
Per-vertex normal (only if h1 bit3 smooth): `(2*int8(b) + 1) / 255` per axis.
Flat (h1 bit3 = 0): every vertex gets the face normal (overwrites reused normals too).
Fixed shade (if h1 bit5 && !bit3): from v0 low byte: specular enabled ->
`b/255` (unsigned), else `(2*int8(b)+1)/255`.
UV: `u = U * uvScale / texWidth`, `v = V * uvScale / texHeight`, uvScale =
1 (h1 bit6) or 1/8. Normalised over the texture rectangle (wrap at 1.0).

### 5.3 Shared-vertex reuse (exact)

State `prev[0..3]` (position, normal, fixed shade, raw 16-bit U/V) persists
across polygons AND across model boundaries (never reset; a model's first
polygon may reuse the previous model's vertices).

    j = 0
    for i in 0..3: if h0 bit i: v[j++] = prev[i]
    for (; j < nVerts; j++): v[j] = decode next 4 vertex words
    (flat shading: overwrite all normals with face normal)
    prev[0..3] = v[0..3]        # all 4 slots copied, even for triangles;
                                # slot 3 of a triangle is undefined/stale

Number of new vertices = nVerts - popcount(h0 & 0xF). Reused vertices keep
their raw U/V; if this polygon's texture state differs from the previous
polygon's, their normalised UVs are recomputed with the new width/height/uvScale.

### 5.4 Primitive assembly, facing

Quad -> triangles (0,1,2), (0,2,3). Back-face test uses the **face normal**,
not winding: transform face normal with `rot3x3(M)`, discard fragment if
`dot(viewPos, Nface_view) > 0`. Double-sided (h1 bit4): emit an extra copy with
negated face and vertex normals (skipped if discarded).

### 5.5 Dynamic vs ROM models

Models in polygon RAM (`addr < 0x100000`) must be re-decoded each frame.
VROM models are immutable and can be cached by address, *unless* any polygon
uses the colour table (h1 bit1 == 0): colour-table contents can change, so such
models are re-decoded (reference treats them as dynamic). Per-instance state
(matrix, tex offset/page, model scale, node alpha) is applied at draw time.

---------------------------------------------------------------------------
## 6. Textures

### 6.1 Texture FIFO (0x94000000) and flush

Each 32-bit write appends `W` to a 1 MB (256K-word) FIFO (overflow = dropped).
On flush the FIFO is parsed:

    i = 0
    while i < count - 2:
        sizeWords = (2 + fifo[i] / 2) / 4        # integer math
        if sizeWords == 0: break
        upload(header = fifo[i+1], texels = u16 stream starting at fifo[i+2])
        i += sizeWords
    count = 0

**[AMBIG]** the meaning of `fifo[i]` (length) is inferred only from this formula.
Flush happens when `0x88000000` is written **before** the ping-pong flip of the
current frame; if written after the flip, the upload is deferred to the start
of the next VBlank. Data stays in the FIFO until some flush.

### 6.2 Texture port (0x90000000), Step 1.5 behaviour

Step 1.5 is NOT single-word. Writes are collected: 1st write = source word
address, 2nd = header, the **3rd write triggers** the upload (its data is
discarded) and resets the counter. Source: if `addr < 0x100000` -> polygon RAM
word `addr`, else VROM word `addr & 0xFFFFFF`; texels read as LE u16 from there.
(Step 1.0 single-word variant: data = VROM word address; `(2 + vrom[a]/2)/4`
words starting at `a` are pushed into the FIFO.)

### 6.3 Upload header

| Bits | Meaning |
|---|---|
| 0-5 | X = 32*v |
| 7-11 | Y = 32*v (within page) |
| 14-16 | width = 32 << v |
| 17-19 | height = 32 << v |
| 20 | page (0/1) |
| 21 | 8-bit: write to low byte of texel |
| 22 | 8-bit: write to high byte of texel |
| 23 | 1 = 16-bit texels, 0 = 8-bit |
| 24-31 | type: 0 = base + mipmaps, 1 = base only, 2 = mipmaps only, 0x80 = ignored (gamma table?) ; others ignored |

Bit 6 and bits 12-13 unused.

### 6.4 Texture RAM layout and swizzle

Texture RAM = 2048 x 2048 u16 = two pages of 2048x1024; page p occupies rows
`1024p .. 1024p+1023`. Mip levels live inside the same page:

    mipXBase = {0,1024,1536,1792,1920,1984,2016,2032,2040,2044,2046,2047}
    mipYBase = {0, 512, 768, 896, 960, 992,1008,1016,1020,1022,1023}
    level L of a texture at (x,y,w,h): (mipXBase[L] + x>>L, mipYBase[L] + y>>L), size (w>>L, h>>L)

Upload: level 0 at (x, y + 1024*page) unless type 2; then (type 0/2) for
L = 1,2,...: while `w >= 4 && h >= 4` (checked before halving): w/=2, h/=2,
store at the level-L position. Data for successive levels follows contiguously.

Storing one level: `tileX = min(8,w)`, `tileY = min(8,h)`. Tiles are consumed in
raster order (y-major over tiles, x within row), each tile = `tileX*tileY`
consecutive source u16 (16-bit) or `max(1, tileX*tileY/2)` u16 (8-bit).

16-bit: texel at tile-local (xx,yy) = `src[D(xx,yy)]` with

    D(xx,yy) = (yy>>1)*(2*tileX) + (xx>>1)*4 + (yy&1)*2 + ((xx&1)^1)

(i.e. 2x2 blocks in raster order; in each block the order is (1,0),(0,0),(1,1),(0,1)).
Applies to tileX = 8, 4, 2 (the decode8x8/8x4/8x2 tables).

8-bit: for each destination texel (xx,yy):
`idx = (yy^1)*tileX + (xx^1)`, `k = D_table[idx] / 2` (D_table = same table as
16-bit for this tileX), `byte = (src16[k] >> (8*((xx&1)^1))) & 0xFF`.
For an 8x8 tile this simplifies to: byte offset in the tile's byte stream
`= (yy>>1)*16 + (xx>>1)*4 + (1-(yy&1))*2 + (1-(xx&1))`.
The byte is written to the low byte (header bit21) or high byte (bit22) of the
destination u16, other byte preserved. Both bits set: the byte is written to
both halves (`byte*0x0101`; reference logs it as never observed). Neither bit
set: nothing written.

### 6.5 Texel formats (h6 bits 7-9; with contour h6 bit31 formats 1-4 become 8-11)

| Fmt | Decode of u16 `t` | Alpha |
|---|---|---|
| 0 | ARGB1555: R=bits10-14, G=5-9, B=0-4 (/31) | `1 - bit15` (bit15 = transparent) |
| 1 | L = bits0-3, A = bits4-7 (/15) | A |
| 2 | A = bits0-3, L = bits4-7 | A |
| 3 | L = bits8-11, A = bits12-15 | A |
| 4 | A = bits8-11, L = bits12-15 | A |
| 5 | L8 = bits0-7 (/255) | 0 if L8==0xFF else 1 |
| 6 | L8 = bits8-15 | 0 if 0xFF else 1 |
| 7 | RGBA4444: R=12-15, G=8-11, B=4-7, A=0-3 | A (alpha test forced off) |
| 8 | (1+contour) L4 = bits0-3 | 0 if nibble==15 else 1 |
| 9 | (2+contour) L4 = bits4-7 | same |
| 10 | (3+contour) L4 = bits8-11 | same |
| 11 | (4+contour) L4 = bits12-15 | same |

Luminance formats give grey RGB = L (later multiplied by polygon colour).
If neither texture-alpha (h6 bits0-2) nor alpha test is set, texture alpha is forced to 1.

### 6.6 Addressing, wrapping, filtering, LOD

- Base texel position: `(texX + node.texOffX, texY + node.texOffY)` in page
  `h4bit6 XOR node.texPage`. Fetches wrap inside the page: `x & (2047>>L)`, `y & (1023>>L)`.
- Wrap per axis: mirror flag (h2 bit1 = U, bit0 = V) and smoothing flag (h3 bit7 = U,
  bit6 = V). smooth -> repeat / mirror; not smooth -> repeat-clamp / mirror-clamp
  (bilinear does not blend across the tile edge). Alpha-tested polygons force
  non-smooth. Mirror: odd repetitions use `1 - fract(u)`.
- Bilinear filtering within a level, trilinear between levels. For alpha-tested
  polys, colour of the more opaque neighbour is propagated before blending (pixel dilate).
- LOD: `lodBase = dot(viewPos, Nface_view) * (-cota) * textureNP`;
  `lod = -log2(w_clip^-2 * lodBase)` = `-log2(lodBase / z_eye^2)`;
  clamp to `[0, floor(log2(min(w,h))) - 1]`.
- Microtexture (h2 bit4): 128x128 tile in the *other* page at
  x = {0,0,128,128,0,0,128,128}[id], y = {0,128,0,128,256,384,256,384}[id],
  blended in only when lod < 0, scale `(size/128) * {2,4,16,256}[minLOD]`,
  weight `min(0.5, -lod * 2^-minLOD * 0.5)`. (Rarely matters.)

---------------------------------------------------------------------------
## 7. Shading, blending, layering, 2D

### 7.1 Per-fragment pipeline (reference)

    if dot(viewPos, Nface) > 0: discard
    colour = faceRGB (/255), alpha = faceAlpha/255 (bit9-only discard: /2)
    if translator map (h4 bit7): rgb *= 16
    alpha *= nodeAlpha (LOD fade)
    tex = textured ? sample : 1
    if alphaTest and tex.a < 32/255: discard
    Step 1.5 luminous (lighting disabled): fixedShading ? rgb *= 1 + fixedShade + ambient
                                                        : rgb *= 1.5
    c = tex * colour ; if c.a < 1/32: discard
    if lighting enabled:
        s = fixedShading ? fixedShade : dot(sunGL, normalize-ish(N_view))
            (N_view = rot3x3(M) * N / modelScale)
        s = clamp(s, -1, 1); if sunClamp or c.a < 0.999: s = max(s, 0)
        I = s*sunIntensity + ambient + spotColour*spotLobe
        (Step 1.0 only: I = min(I,1); Step 1.5: no clamp here)
        c.rgb *= I
        if specular (h0 bit7):
            smooth: f = max(s,0)^{8,16,32,64}[shin] * {1.6,1.6,2.4,3.2}[shin]
            flat:   f = max(0, reflect(-sun, N).z)
            f *= specCoef * sunIntensity
            if colour.a < 1: c.a = max(c.a, f)
            c.rgb += f
    c.rgb = min(c.rgb, 1)
    fog = fogIntensity * clamp(fogStart + (-z_eye) * fogDensity, 0, 1)
          (fogIntensity = h6 bits11-15 / 16)
    c.rgb = mix(c.rgb, fogColour*fogAmbient + spotFogColour*fogAttenuation*fogColour*spotFogLobe, fog)

"fixedShading" = h1 bit5 && !bit3. sunClamp default true (cleared only if JTAG
Mars mode word bit 18 is set). Spotlight: centre in screen pixels (Y flipped
`384 - y`), radius `round(2047/max(1,size))` px per axis; `ellipse = max(0, 1 - |(p-c)/r|^2)`;
enabled when `-z_eye >= start`; range term `1/((d/(1+|ext|)) - 1)^2` with
`d = min(0, start + |ext| + z_eye)` (0 if ext == 0); `spotLobe = range*ellipse`,
`spotFogLobe = enabled*ellipse`.

### 7.2 Translucency

- Opaque pass: polygons with no poly-alpha and node alpha == 1. Texture-alpha
  polygons are drawn here with texels of alpha < 1 discarded.
- Translucent passes: polygons with texture-alpha, poly-alpha or node alpha < 1;
  layer A if h6 bit17 == 0, layer B otherwise. Texture-alpha texels with
  `tex.a * colour.a >= 1` are discarded here (already drawn opaque).
- Each translucent layer is its own buffer, depth-tested against opaque depth
  with depth writes on and NO blending: per pixel only the nearest translucent
  fragment of that layer survives. Final composite of the two layers over the
  opaque result:
    both a>0: rgb = mix(A.rgb, B.rgb, (B.a + 1 - A.a)/2), alpha 1;
    else whichever is non-zero (src-alpha blend).
- Opaque fragments at a higher priority clear both translucent buffers at that pixel.

### 7.3 Priorities and layering

- Viewport priority 0..3, drawn in increasing order; depth and stencil are
  cleared before each priority, so higher priority always covers lower.
- Within a priority: pass 1 = normal polys, then (if any exist) depth cleared
  and pass 2 = high-priority polys (h6 bit4) on top.
- Within a pass, per viewport: non-layered first, then layered (h6 bit3).
  Layered polys use stencil (lower 7 bits): passes only where stencil == 0 and
  increments it (one decal per pixel); reference also biases depth by 1+1/512.
- Stencil bit 7 records no-LOS-return (h1 bit0) for the LOS test.

### 7.4 Fog layers drawn before 3D (in the 3D colour buffer, alpha = coverage)

- Scroll fog: choose, starting at priority 0 and stopping at the first priority
  having any, the viewport with the largest `scrollFog` (vp[0x20]&0xFF). Fog is
  drawn only if some viewport with the same fog colour has density, start,
  scroll attenuation > 0, or spot-on-fog (fogAttenuation > 0 and spot-fog colour
  non-black). Fill that viewport's rectangle with
  `rgb = fogColour*fogAmbient (+ spot-fog term)`, `a = scrollFog`.
- Ambient fog: in the lowest priority that has viewports, among viewports with
  scrollFog == 0 and (density > 0 or start > 0), take minimum fogAmbient; fill
  with black, `a = 1 - fogAmbient`. (Lost World sets ambient but no density, so
  effectively off.)

### 7.5 Final frame composition

1. Clear to black (alpha 0).
2. Tilegen "bottom" surface (layers with priority bit = 0), src-alpha blend.
3. 3D opaque buffer (alpha 1 where drawn, fog alpha elsewhere, 0 empty), src-alpha blend.
4. 3D translucent composite (7.2).
5. Tilegen "top" surface (priority bit = 1).
Reference shifts the 2D layers by 2 native pixels (x-2, y+2 in GL coords)
relative to 3D **[AMBIG: empirical fix]**.
Block culling (JTAG Mercury mode word bit 2): screen cleared to white, no 3D.

### 7.6 Tile generator

Device words below are `bswap32` of the CPU value (§1.2), including registers.

Registers (`F1180000 + off`):
| Off | Bits | Meaning |
|---|---|---|
| 0x08 | | ping-pong flip line (active-display line, see §8) |
| 0x0C | | write = "tilegen draw frame" signal |
| 0x10 | 0-7 | IRQ acknowledge mask |
| 0x20 | 8+n | layer n above 3D (1) / below (0), n = 0..3 |
| | 12+n | layer n bit depth: 1 = 4-bit, 0 = 8-bit |
| 0x40 | 0-7 / 8-15 / 16-23 | colour offset R / G / B for layers 0,1 (signed int8, x2) |
| 0x44 | | same for layers 2,3 |
| 0x60+4n | 31 | layer n enable |
| | 16-24 | Y scroll (9 bits) |
| | 15 | line-scroll enable (X from VRAM table) |
| | 0-9 | X scroll (reference masks 10 bits; wraps at 512 anyway) |
**[AMBIG]** comments in the reference list R in bits 16-23 for 0x40/0x44, but
the code (which also matches the palette's R-low layout) uses R = bits 0-7, B = 16-23.

Layers: 0 = A, 1 = A', 2 = B, 3 = B'. Each 64x64 tiles of 8x8 (512x512),
visible window 496x384, independent wrap in X and Y.

VRAM (byte offsets in the 1 MB area, words little-endian device words):
- `0x00000-0xF5FFF` patterns.
- `0xF6000 + 0x400*n` line-scroll table layer n: 16-bit X per line; line `y` is in
  word `y/2`, **even lines in the high half**, odd lines in the low half.
- `0xF7000 + 4*y` stencil mask for line y: bits 16-31 for A/A', bits 0-15 for B/B';
  bit `(15 - x/32)` set -> primary layer (A or B) shown at x, clear -> alternate.
  Mask is screen-fixed (not scrolled).
- `0xF8000 + 0x2000*n` name table layer n: 64 rows x 32 words; word = tile pair,
  **left tile in bits 16-31, right tile in bits 0-15**. Pair index for screen
  pixel (x,y): `row = ((y + ys)/8) & 63`, `col = ((x + xs)/16) & 31`,
  `word = row*32 + col`; fine offsets `(x+xs)&15`, `(y+ys)&7`.

Tile entry `e` (16 bits):
- 4-bit: pattern index `((e & 0x3FFF) << 1) | (e >> 15)`; pattern byte address
  `index*32`; row `r` = word `index*8 + r`; pixel `i` (0 = left) = nibble at bits
  `(7-i)*4`; palette index = `(e & 0x7FF0) | nibble`.
- 8-bit: pattern index `e & 0x3FFF`; address `index*64`; row `r` = words
  `index*16 + 2r` (pixels 0-3) and `+1` (pixels 4-7), pixel at bits
  `(3-i%4)*8`; palette index = `(e & 0x7F00) | byte`.

Palette (`F1100000`, 32768 entries, 1 word each, low 16 bits used):
`bit15 = transparent`, B = bits 10-14, G = 5-9, R = 0-4 (note: R low, opposite to
texture ARGB1555). Channel8 = `c5*255/31` (0 if transparent), then add
`2*int8(offset)` from reg 0x40 (layers 0,1) or 0x44 (layers 2,3), clamp 0..255.
Transparent pixels are simply not written (reference does not implement the
"bottom-most layer shows transparent colour" rule).

Per scanline (rendered line by line during active display, so registers are
sampled per line): pair B/B' is drawn first, then A/A' over it; at each x the
mask selects primary vs alternate of each pair; a layer goes to the bottom or
top surface according to its 0x20 priority bit. Disabled layers draw nothing.

---------------------------------------------------------------------------
## 8. Status register, ping-pong, timing

- Frame: 57.524160 Hz, 424 lines: 40 lines VBlank first, then 384 active lines.
- VBlank start: apply deferred update buffers; if `0x88` was written after the
  previous flip, flush texture FIFO; then IRQs; at end of VBlank IRQ mask 0x0D asserted.
- During active line `i` (0..383): if `i == tilegen reg 0x08` the ping-pong bit
  toggles (games use ~238-239, about 66 % through the frame). At line 383 IRQ 0x02
  (VBlank) asserted.
- Status read `0x84000000` (device reg 0): device value
  `0xFDFFFFFF | (pingpong << 25)`; CPU sees `bswap32` = `0xFFFFFFFD | (pingpong << 1)`.
  Games poll this bit to know when they may start writing the next frame.
- `0x84000014/18/1C/20` (device regs 20,24,28,32): line-of-sight result for
  priority 0..3: float `1/(-z_eye)` at the viewport's LOS pixel (vp[0x1C]) after
  that priority's passes; LSB of the float set to 1 if nothing (or a no-LOS-return
  poly) was hit, cleared if geometry was hit. Other regs read 0xFFFFFFFF.
- "Draw frame" handshake (reference): `Flush()` (0x88) and tilegen 0x0C write must
  both occur; it only latches block-culling state. The 3D scene is rendered from
  memory every frame regardless.
- Mode words via JTAG: Mercury bit2 = block culling (white screen), Mars bit18 =
  disable sun clamp. Defaults: block culling off, sun clamp on.
