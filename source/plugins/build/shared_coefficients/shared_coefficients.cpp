#include "plugins/build/shared_coefficients/shared_coefficients.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace kodanuki {

constexpr std::size_t leastRunSlots = 1024;

struct Sharing {
	const Tree& tree;
	const std::vector<std::uint32_t>& grown;
	std::vector<std::uint32_t>& fresh;
	SharedRuns& runs;
};

SharedRuns emptySharedRuns() {
	return SharedRuns {
		.slots = std::vector<std::uint32_t>(leastRunSlots, 0),
		.taken = 0,
	};
}

const std::uint32_t* heldRun(const Sharing& sharing, std::uint32_t offset) {
	const std::uint32_t base = std::uint32_t(sharing.tree.runs.size());
	return offset < base ? sharing.tree.runs.data() + offset : sharing.fresh.data() + (offset - base);
}

std::uint32_t runLength(const std::uint32_t* run) {
	return faceOffsets(run).back();
}

std::uint64_t runHash(const std::uint32_t* run) {
	std::uint64_t hash         = 0xCBF29CE484222325ull;
	const std::uint32_t length = runLength(run);
	for (std::uint32_t k = 0; k < length; ++k) {
		hash = (hash ^ run[k]) * 0x100000001B3ull;
	}
	return hash;
}

bool sameRun(const std::uint32_t* a, const std::uint32_t* b) {
	const std::uint32_t length = runLength(a);
	return length == runLength(b) && std::equal(a, a + length, b);
}

std::size_t runSlot(const Sharing& sharing, const std::uint32_t* run) {
	const std::vector<std::uint32_t>& slots = sharing.runs.slots;
	std::size_t slot                        = std::size_t(runHash(run)) & (slots.size() - 1);
	while (slots[slot] != 0 && !sameRun(heldRun(sharing, slots[slot] - 1), run)) {
		slot = (slot + 1) & (slots.size() - 1);
	}
	return slot;
}

void widenedRuns(const Sharing& sharing) {
	SharedRuns& runs = sharing.runs;
	if (runs.taken * 2 < runs.slots.size()) {
		return;
	}
	std::vector<std::uint32_t> slots(runs.slots.size() * 2, 0);
	for (const std::uint32_t entry : runs.slots) {
		if (entry != 0) {
			std::size_t slot = std::size_t(runHash(heldRun(sharing, entry - 1))) & (slots.size() - 1);
			while (slots[slot] != 0) {
				slot = (slot + 1) & (slots.size() - 1);
			}
			slots[slot] = entry;
		}
	}
	runs.slots = slots;
}

std::uint32_t sharedRun(const Sharing& sharing, std::uint32_t local) {
	const std::uint32_t* run = sharing.grown.data() + local;
	const std::size_t slot   = runSlot(sharing, run);
	if (sharing.runs.slots[slot] != 0) {
		return sharing.runs.slots[slot] - 1;
	}
	const std::uint32_t offset = std::uint32_t(sharing.tree.runs.size() + sharing.fresh.size());
	sharing.fresh.insert(sharing.fresh.end(), run, run + runLength(run));
	sharing.runs.slots[slot] = offset + 1;
	sharing.runs.taken += 1;
	widenedRuns(sharing);
	return offset;
}

Subtree sharedCoefficients(const Tree& tree, const Subtree& subtree, SharedRuns& runs) {
	Subtree shared           = subtree;
	shared.runs              = {};
	const std::uint32_t base = std::uint32_t(tree.runs.size());

	const Sharing sharing = {
		.tree  = tree,
		.grown = subtree.runs,
		.fresh = shared.runs,
		.runs  = runs,
	};

	for (SurfaceLeaf& leaf : shared.surfaces) {
		leaf.run = sharedRun(sharing, leaf.run - base);
	}
	if (shared.root.kind == CellKind::Surface) {
		shared.root.surface.run = sharedRun(sharing, shared.root.surface.run - base);
	}
	return shared;
}

}
