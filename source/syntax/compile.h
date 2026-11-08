#pragma once

#include "syntax/evaluator.h"

#include <cstdint>
#include <vector>

namespace kodanuki {

std::uint32_t specialise(Evaluator& evaluator, const Pattern& pattern);
void pushArguments(Evaluator& evaluator, const std::vector<Value>& arguments);

}
