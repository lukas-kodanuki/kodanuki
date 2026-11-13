#include "plugins/geometry/tree/walk.h"
#include "plugins/geometry/tree/span.h"

#include <cstdint>

namespace kodanuki {

struct CoarseChild {
	const PackedNode& node;
	int octant;
};

Met walkNode(const Walker& walker, const PackedNode& node, const CellSpan& span);

Met walkSlot(const Walker& walker, const CellEntry& entry, const CellSpan& span) {
	switch (entry.kind) {
		case CellKind::Empty: return missedMet();
		case CellKind::Filled: return walker.ray.inside ? missedMet() : solidMet(walker, entry.payload, span);
		case CellKind::Surface: return surfaceMet(walker, entry.payload, span);
		case CellKind::Branch: return walkNode(walker, treeOf(walker.geometry).nodes[entry.payload], span);
	}

	return missedMet();
}

Met walkCoarse(const Walker& walker, const CoarseChild& coarse, const CellSpan& span) {
	const Middles middles = middlesOf(span);
	for (int step = 0; step < 8; ++step) {
		const CellEntry entry = slotEntry(coarse.node, coarse.octant * 8 + (step ^ walker.descending));
		if (entry.kind == CellKind::Empty) {
			continue;
		}
		const CellSpan fine = halvedSpan(span, middles, step);
		if (!crosses(fine)) {
			continue;
		}
		const Met met = walkSlot(walker, entry, fine);
		if (met.kind != MetKind::Missed) {
			return met;
		}
	}
	return missedMet();
}

Met walkChild(const Walker& walker, const CoarseChild& coarse, const CellSpan& span) {
	if (((coarse.node.coarse >> coarse.octant) & 1) != 0) {
		return walkSlot(walker, slotEntry(coarse.node, coarse.octant * 8), span);
	}
	return walkCoarse(walker, coarse, span);
}

Met walkNode(const Walker& walker, const PackedNode& node, const CellSpan& span) {
	const Middles middles = middlesOf(span);
	for (int step = 0; step < 8; ++step) {
		const int octant = step ^ walker.descending;
		if (((node.busy >> octant) & 1) == 0) {
			continue;
		}
		const CellSpan child = halvedSpan(span, middles, step);
		if (!crosses(child)) {
			continue;
		}

		const CoarseChild coarse = {
			.node   = node,
			.octant = octant,
		};

		const Met met = walkChild(walker, coarse, child);
		if (met.kind != MetKind::Missed) {
			return met;
		}
	}
	return missedMet();
}

Geometry treeGeometry(const Scene& scene, const Tree& tree) {
	return Geometry {
		.scene  = scene,
		.region = tree.region,
		.world  = &tree,
		.kernel = firstHit,
	};
}

const Tree& treeOf(const Geometry& geometry) {
	return *static_cast<const Tree*>(geometry.world);
}

bool holdsTree(const Geometry& geometry) {
	return geometry.kernel == firstHit;
}

Met firstMet(const Geometry& geometry, const Ray& ray) {
	const Walker walker = {
		.geometry   = geometry,
		.ray        = ray,
		.descending = descendingOf(ray),
	};

	const CellSpan span = rootSpan(walker);
	if (span.upper < span.lower) {
		return missedMet();
	}
	return walkSlot(walker, treeOf(geometry).root, span);
}

Hit firstHit(const Geometry& geometry, const Ray& ray) {
	const Walker walker = {
		.geometry   = geometry,
		.ray        = ray,
		.descending = descendingOf(ray),
	};

	return hitOf(walker, firstMet(geometry, ray));
}

}
