import { defineConfig } from "vitest/config";

export default defineConfig({
  test: {
    include: ["tests/**/*.test.ts"],
    pool: "threads",
    // Heavy example programs whose primary lowering overflows the 105-cell window
    // trigger the full size-rescue candidate matrix and can take ~20s to compile.
    // The disk compile cache (tests/helpers/compile-cache.ts) makes re-runs and
    // cross-file duplicates of those compiles free; this generous timeout keeps a
    // cold compile from spuriously timing out under parallel load. (We keep the
    // default per-file isolation: these tests are CPU-bound, so cross-file thread
    // parallelism matters far more than the small module-eval overhead that
    // disabling isolation would save.)
    testTimeout: 30000,
    hookTimeout: 30000,
  },
});
