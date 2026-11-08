#include "extras/detail_correct_split_crowded_leaves/split_crowded_leaves.h"

#include "tree/tree.h"

namespace kodanuki {

double boxSide(const Box& box) {
	return box.axes[0].upper - box.axes[0].lower;
}

bool crowdsLeaf(const CrowdedQuery& query) {
	if (query.plan.kind != CellKind::Surface || query.plan.run.empty()) {
		return false;
	}
	return runHeader(query.plan.run[0]).count > crowdedFaces && boxSide(query.box) > crowdedReach * query.tolerance;
}

}
