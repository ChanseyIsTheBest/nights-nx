# Differential tests of GLSL passes (2026-10-20)

How the exact shader passes of tools/deko3d_glsl_opt.py are checked (docs/deko3d-shader-optimisations.md 2.37-2.40):
the shader before and after a pass, with the same specialization defines, compiled by uam, decompiled by Ryujinx's
shader translator, turned into Python and run on random inputs; the outputs (and discards) must be equal bit for bit.

- `shtool/`: decompiles a Maxwell program (SPH + code, as in a DKSH) to GLSL with Ryujinx's translator (the disassembly
  as comments). Build once, against the fork's Ryujinx build:
  `DOTNET_ROOT="D:\New folder\dotnet10" "D:/New folder/dotnet10/dotnet.exe" build tools/difftest/shtool/shtool.csproj -c Release -o tools/difftest/shtool/out`
  (the csproj references `D:/New folder/Ryubing-src/ryubing/build/Ryujinx.Graphics.Shader.dll` and friends).
- `difftest_ps.py`: the transpiler (shtool's GLSL -> Python) and a suite of small synthetic pixel shaders for uam flag
  A/B tests (`python difftest_ps.py <flags A> <flags B>`).
- `difftest_game.py`: whole game shaders. `python difftest_game.py <before.glsl> <after.glsl> <trials> [N=value ...]`.
  Texture fetches are a fixed function of the binding and the coordinates' bits (the same in both programs), sizes a
  fixed function of the binding, derivatives 0, the special functions Python's (the same in both), every instruction's
  result rounded to float32, constant buffer words a function of the trial's seed and the word.
  Pitfalls found on the way (both gave false mismatches): Python computes in doubles (round every assignment to
  float32), and constants drawn in read order differ between two programs that read them in another order (key them).
  Loops in the decompile (`do`) are not supported: test the specialised variants. Check a deliberately broken copy too
  (a wrong arm, a changed constant) so the test is known to catch differences.
- `batch_gs.py`: guardsel on a list of shaders, each with up to two of its logged variants' constants
  (`python batch_gs.py <hash,hash,...> [trials]`; paths inside: the base GLSL folder and the emulator's variant log).
- `difftest_vs_skin.py` (2026-10-20): the skinonce pass (constant 913) on a fur vertex shader. The shader before the
  pass (A), after it in the write mode (W, 913 = 1, stores the rows) and the read mode (R, loads them), each compiled
  with flags 1471 and decompiled; A and W run on the same random bone words, R on W's stores; every output
  (gl_Position, varyings) must be equal bit for bit in all three, and W must store 12 words.
  `python difftest_vs_skin.py <before.glsl> <after.glsl> [trials] [N=value ...]` (the variant's 904 / 905). Integers are
  typed 32-bit from the decompile's declarations (bitfieldExtract signed for int temps). The read program's skin
  buffer is found by trying each storage buffer (a shader that fetches its vertex data from memory has the address
  window as well). Results: all four fur shaders 150 / 150; controls (a row read from the wrong slot, a stored row
  1 ulp off) caught.
- `uam_ops.py`: compiles one GLSL stage with uam and prints its instruction / GPR counts and Maxwell opcodes
  (`python uam_ops.py <glsl> frag|vert [N=value ...] [--seq]`).

Paths are this machine's (uam at out/deko3d_tools/uam-build, the GLSL under out/deko3d_shaders). Game data: the shaders
are the game's; nothing here is to be committed with them.

Limits found by other ports (2026-10-20): shaders with the txq pass (constant 908) read texture sizes from the size
buffer in the after-program and from textureSize() in the before-program; the harness gives the two different values
(KeyedCB words vs TSIZE), so test them with 908=0. A division whose operand is not a plain name is not wrapped in DIV;
a division by zero there ends the trial as 'zerodiv' (a mismatch only if the other program does not get there).
