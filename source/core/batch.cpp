#include "core/batch.h"

#include "sdf/program.h"

#include <cstddef>
#include <limits>
#include <type_traits>

namespace kodanuki {

static_assert(std::is_trivially_copyable_v<Ray>, "a slot's ray is plain data");
static_assert(std::is_trivially_copyable_v<Hit>, "a slot's hit is plain data");
static_assert(sizeof(Ray) == 36, "a ray is nine floats and a word");
static_assert(sizeof(Hit) == 56, "a hit is nine floats and five words");
static_assert(std::is_trivially_copyable_v<PixelSample>, "a slot's sample is plain data");
static_assert(std::is_trivially_copyable_v<Surface>, "a surface is plain data");

Pool emptyPool(int capacity) {
	const std::size_t slots = std::size_t(capacity);
	return Pool {
		.capacity = capacity,
		.phases   = std::vector<WalkPhase>(slots, WalkPhase::Free),
		.answered = std::vector<std::uint8_t>(slots, 0),
		.kinds    = std::vector<RayKind>(slots, RayKind::Camera),
		.rays     = std::vector<Ray>(slots),
		.hits     = std::vector<Hit>(slots),
		.samples  = std::vector<PixelSample>(slots),
	};
}

std::string rayKindName(RayKind kind) {
	switch (kind) {
		case RayKind::Camera: return "camera";
		case RayKind::Scatter: return "scatter";
		case RayKind::Inside: return "inside";
		case RayKind::Shadow: return "shadow";
	}

	return "camera";
}

bool waitsOnRay(WalkPhase phase) {
	return phase == WalkPhase::Leading || phase == WalkPhase::Flying || phase == WalkPhase::Lighting || phase == WalkPhase::Probing;
}

Surface missedSurface() {
	return Surface {
		.distance  = std::numeric_limits<float>::infinity(),
		.normal    = { 0, 0, 0 },
		.leaf      = noLeaf,
		.material  = noMaterial,
		.albedo    = { 0, 0, 0 },
		.roughness = 1,
		.onward    = std::numeric_limits<float>::infinity(),
	};
}

} // namespace kodanuki
