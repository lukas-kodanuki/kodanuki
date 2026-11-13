#include "core/geometry.h"

#include "sdf/program.h"

namespace kodanuki {

Hit intersect(const Geometry& geometry, const Ray& ray) {
	return geometry.kernel(geometry, ray);
}

Hit missedHit() {
	return Hit {
		.kind     = HitKind::Missed,
		.distance = 0,
		.point    = { 0, 0, 0 },
		.normal   = { 0, 0, 0 },
		.leaf     = noLeaf,
		.other    = noLeaf,
		.share    = 0,
		.loosened = 0,
		.leafKind = LeafKind::Held,
		.cell     = noLeaf,
	};
}

} // namespace kodanuki
