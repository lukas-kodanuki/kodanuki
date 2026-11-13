#include "core/history.h"

namespace kodanuki {

Change joinedChange(Change a, Change b) {
	return Change(std::uint8_t(a) | std::uint8_t(b));
}

bool changedBy(Change change, Change flag) {
	return (std::uint8_t(change) & std::uint8_t(flag)) != 0;
}

bool sameVector(const math::Vector& a, const math::Vector& b) {
	return a.x == b.x && a.y == b.y && a.z == b.z;
}

bool sameSize(const Lens& a, const Lens& b) {
	return a.width == b.width && a.height == b.height;
}

bool sameLens(const Lens& a, const Lens& b) {
	const bool placed = sameVector(a.eye, b.eye) && sameVector(a.forward, b.forward) && sameVector(a.right, b.right) && sameVector(a.up, b.up);
	const bool shaped = a.reach == b.reach && a.aspect == b.aspect && a.aperture == b.aperture && a.focus == b.focus;
	return placed && shaped && sameSize(a, b);
}

Change lensChange(const FrameStep& last, const StepQuery& query) {
	const Lens shownBefore = shownLens(last.lens, last.film);
	const Lens shownNext   = shownLens(query.lens, query.film);
	const Change moved     = sameLens(last.lens, query.lens) ? Change::None : Change::Moved;
	return joinedChange(moved, sameSize(shownBefore, shownNext) ? Change::None : Change::Cut);
}

FrameStep firstStep(const StepQuery& query) {
	return FrameStep {
		.index       = 0,
		.lens        = query.lens,
		.before      = query.lens,
		.film        = query.film,
		.change      = joinedChange(query.change, Change::Cut),
		.still       = 0,
		.keepsGuides = query.keepsGuides,
	};
}

FrameStep nextStep(const FrameStep& last, const StepQuery& query) {
	const Change change = joinedChange(query.change, lensChange(last, query));
	return FrameStep {
		.index       = last.index + 1,
		.lens        = query.lens,
		.before      = last.lens,
		.film        = query.film,
		.change      = change,
		.still       = change == Change::None ? last.still + 1 : 0,
		.keepsGuides = query.keepsGuides && sameSize(last.lens, query.lens),
	};
}

History emptyHistory() {
	const FrameRecord none = {
		.width    = 0,
		.height   = 0,
		.parts    = {},
		.surfaces = {},
	};

	return History {
		.record    = none,
		.estimates = {},
		.states    = States { .held = {} },
	};
}

}
