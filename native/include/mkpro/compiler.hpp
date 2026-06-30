#pragma once

// Public compiler API. Emulator-backed behavior digests are a separate
// debug/regression surface in mkpro/core/compiler_behavior_digest.hpp so they do
// not become a transitive optimizer dependency.
#include "mkpro/core/compiler.hpp"
