#include "extras/sampler_correct_each_lamp_first/each_lamp_first.h"

namespace kodanuki {

constexpr std::uint32_t lampsWithOwnRay = 4;

Selection eachLampFirst(const SelectionQuery& query) {
	const bool lamp = query.light < query.lights.lamps.size();
	return query.depth == 0 && lamp && query.owned < lampsWithOwnRay ? Selection::Own : Selection::Shared;
}

}
