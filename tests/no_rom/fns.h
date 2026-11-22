// Stand-in for build/gen/fns.h (written by tools/recomp.py from the ROM), so
// the game's own code builds without the ROM: the GitHub workflow copies this
// folder to build/gen. Only what src/ uses of the generated code is here.
#pragma once
#include "ppc_rt.h"
#include "game_hooks.h"

void fn_FFF00100(Ctx& c);   // the reset vector, where runtime.cpp starts the game

struct FnEntry { uint32_t addr; void (*fn)(Ctx&); };
extern const FnEntry g_fn_table[];
extern const int g_fn_count;
