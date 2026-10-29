// banjo-nx (2026-10-22): what each shard of the bulk copies (banjo_hot_shard_<k>.cpp) includes before its copies,
// as banjo_hot_guest.cpp does for the hand-picked list: the generated code's macros (a copy is generated statements),
// the hook machinery (banjo_hot_guest.h) and the hand-written natives a copy asks first (hot/hand.txt).
// The method is bejeweled3-nx's (bulk localization, its docs/optimising-functions.md).
#pragma once

#include "renut_pch.h"

#include "cpu/banjo_hot_guest.h"
#include "cpu/hot/banjo_hand_natives.h"
