import { defineConfig } from "vitest/config";

export default defineConfig({
  test: {
    include: ["tests/**/*.test.ts"],
    pool: "threads",
    // Several tests compile the heaviest example programs (e.g. rambo-iii is
    // ~2.5s on its own) and run in parallel threads, so the 5s Vitest default is
    // too tight under load — heavy-compile tests already worked around this with
    // explicit 15-20s overrides. Make that the suite-wide default so the search
    // for additional lowering candidates cannot cause spurious timeouts.
    testTimeout: 20000,
    hookTimeout: 20000,
  },
});
