// nights-nx (tools/renut_hook_remap_nights.py): the addresses both Renut's renderer and the port's own code hook
// (src/nights_frame_hooks.cpp); the port's hook calls renut_native_sub_X when Renut's renderer is chosen.
#pragma once

#include <cstdint>

#include <rex/ppc.h>

extern "C" void renut_native_sub_82346560(PPCContext& ctx, uint8_t* base);
extern "C" void renut_native_sub_82346CC8(PPCContext& ctx, uint8_t* base);
