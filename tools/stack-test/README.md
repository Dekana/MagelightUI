# Stack test kit

Magelight runs its pages on the game's main thread. The game gives that thread 1 MB of stack (from
`SkyrimSE.exe`'s header) and calls Present deep inside its frame, so before 1.3.0 pages ran in whatever the
game's frame left. A page that recursed in its scripts and then laid itself out, or a deeply nested page,
could use that up: `SEH exception 0xC00000FD ... in frame work` in `Magelight.log`, and the overlay off for
the session. Since 1.3.0 the page work runs on a stack of its own (`"ulFiberMB"`, 8 MB by default).

## In game: `MgStackTest/`

A manifest mod (`Data/Magelight/MgStackTest/`). `Numpad9` opens it; `"hotkeys": { "MgStackTest/test": "F7" }`
in `Magelight.json` rebinds it. **Run all tests** runs 14 tests that push the page into runaway recursion, in
its scripts and in the engine's layout; each must end cleanly and the page must stay up. Results show on the
page and go to `Magelight.log` as `[MgStackTest]` lines. On a Magelight before 1.3.0, expect the overlay to
go off at "Layout forced from the bottom of a recursion". Remove the folder when done.

## Offline: the probes

`build.bat` builds both into `C:\b\mgl-stackprobe` (`MG_PROBE_DIR`) against the stock SDK in
`extern/ultralight`. Each runs `MgStackTest/views/test/stack-tests.js` in Ultralight on a thread with a 1 MB
stack, of which `<usedKB>` is in use first, the way the game's frame uses it:

```
stack_probe.exe <usedKB> <reserveKB|auto|0> <sdkDir> <tests.js> [guaranteeKB]   the page work on that stack (pre-1.3.0)
fiber_probe.exe <usedKB> <sdkDir> <tests.js> [fiberMB] [noconcgc] [rounds]   the page work on a fiber (1.3.0)
```

`stack_probe`'s `reserveKB` passes a JavaScript stack reserve through `JSC_reservedZoneSize` /
`JSC_softReservedZoneSize` (an approach measured and set aside: it trades script depth for engine headroom and
cannot cover layout without a script on the stack). `fiber_probe` runs the thread on its own stack between
jobs, as the game does between frames, and churns garbage, so a collector scan of the wrong stack would show.

Measured on Ultralight 1.4.0 (2026-10-18): on the 1 MB stack the process dies with `0xC00000FD` at the layout
test from about 300 KB used, and a 500-level page fails to lay out at 600 KB used; on an 8 MB fiber every
test passes at any amount used, five rounds over with concurrent collection on or off.

A stack guarantee on the thread (`guaranteeKB`, `SetThreadStackGuarantee`, which a plugin may set on the
game's main thread) moves the overflow point up past where JavaScriptCore stops scripts, which it does not
account for: from 64 KB, plain recursion dies in `JSC::VM::throwException` at the first test with only 4 KB
used. The game did the same with `"ulFiberMB": 0` and 1020 KB left at Present, at the address of the report
that started this.
