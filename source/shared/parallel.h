#pragma once

#include <cstddef>
#include <functional>

namespace kodanuki {

/**
 * Run work once for each item below count on threads threads (at least
 * one, the caller among them) that share one counter, and return when
 * every item is done.
 */
void inParallel(int threads, std::size_t count, const std::function<void(std::size_t)>& work);

}
