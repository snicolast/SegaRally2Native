"""FNV-1a, 64 bits, as src/common/fnv1a.h computes it: the names of the
texture pack images, the 2D replacement cells and the text glyphs
(make_text_atlas.py) come from it. tests/fnv_test.py checks that this file
and the C++ header agree."""

BASIS = 0xcbf29ce484222325
PRIME = 0x100000001b3
MASK = 0xFFFFFFFFFFFFFFFF


def fnv1a_mix(h, v):
    return ((h ^ v) * PRIME) & MASK


def fnv1a_bytes(data, h=BASIS):
    for b in bytes(data):
        h = ((h ^ b) * PRIME) & MASK
    return h


def fnv1a_words(words, h=BASIS):
    """words: iterable of 32-bit unsigned values, hashed as whole values"""
    for w in words:
        h = ((h ^ int(w)) * PRIME) & MASK
    return h
