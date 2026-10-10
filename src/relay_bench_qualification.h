#pragma once

namespace stagecore::stagelaser {

// Manual, one-shot relay qualification. Never use with a laser connected.
// This intentionally never returns into StageCore runtime.
[[noreturn]] void run_relay_bench_qualification();

}  // namespace stagecore::stagelaser
