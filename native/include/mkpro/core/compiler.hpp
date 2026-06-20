#pragma once

#include "mkpro/core/result.hpp"

#include <string>

namespace mkpro {

CompileResult compile_source(std::string source, const CompileOptions& options = {});

}  // namespace mkpro
