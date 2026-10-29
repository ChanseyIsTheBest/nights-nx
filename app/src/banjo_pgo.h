// banjo-nx: profile collection of the PGO-instrumented Switch build (BANJO_PGO=generate), see banjo_pgo.cpp.
// From sa2-nx (app/src/sa2_pgo.h, GPL-3.0, same project family). In every other build these do nothing.

#pragma once

namespace banjo::pgo {

// Picks this session's file (pgo/banjo_pgo_<N>.stream next to the executable) and starts the 2-minute dumps.
void Start();
// Writes every count so far to this session's file (the whole session each time, replacing the previous dump).
void DumpNow();
// The last dump, then the dump thread ends.
void Stop();

}  // namespace banjo::pgo
