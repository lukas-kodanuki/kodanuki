#include "tree/detail.h"
#include "tree/quadric.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numbers>
#include <optional>
#include <string>
#include <vector>

namespace kodanuki {

constexpr double leastShare     = 1e-4;
constexpr double usualShare     = 0.08;
constexpr double usualNoise     = 7;
constexpr double leastNoise     = 1e-9;
constexpr double leastFootprint = 0.02;
constexpr double mostFootprint  = 0.25;

DetailAnswer shareAnswer(double share) {
	return DetailAnswer {
		.share   = share,
		.limit   = std::numeric_limits<double>::infinity(),
		.verdict = Verdict::Keep,
	};
}

DetailAnswer limitAnswer(double limit) {
	return DetailAnswer {
		.share   = 0,
		.limit   = limit,
		.verdict = Verdict::Keep,
	};
}

DetailAnswer verdictAnswer(bool split) {
	return DetailAnswer {
		.share   = 0,
		.limit   = std::numeric_limits<double>::infinity(),
		.verdict = split ? Verdict::Split : Verdict::Keep,
	};
}

DetailAnswer hiddenAnswer(const Detail& detail, const DetailQuery& query) {
	if (query.question != DetailQuestion::Verdict) {
		return verdictAnswer(false);
	}

	const HiddenQuery asked = {
		.plan      = query.plan,
		.box       = query.box,
		.tolerance = query.tolerance,
	};

	return verdictAnswer(hidesLeaf(detail.hidden, asked));
}

DetailAnswer crowdedAnswer(const DetailQuery& query) {
	if (query.question != DetailQuestion::Verdict) {
		return verdictAnswer(false);
	}

	const CrowdedQuery asked = {
		.plan      = query.plan,
		.box       = query.box,
		.tolerance = query.tolerance,
	};

	return verdictAnswer(crowdsLeaf(asked));
}

DetailAnswer answerDetail(DetailMember member, const Detail& detail, const DetailQuery& query) {
	switch (member) {
		case DetailMember::ByDistance: return shareAnswer(detail.share);
		case DetailMember::FocusOnView: return shareAnswer(focusOnViewShare(detail.screen, query.box));
		case DetailMember::Pilot: return limitAnswer(pilotLimit(detail.pilot, query.box, detail.share));
		case DetailMember::SplitHiddenPrimitives: return hiddenAnswer(detail, query);
		case DetailMember::SplitCrowdedLeaves: return crowdedAnswer(query);
	}

	return shareAnswer(0);
}

bool hasMember(const Detail& detail, DetailMember member) {
	return std::find(detail.members.begin(), detail.members.end(), member) != detail.members.end();
}

double cellTolerance(const Detail& detail, const Box& box) {
	if (!hasMember(detail, DetailMember::ByDistance)) {
		return detail.least;
	}
	const Plan pending = pendingPlan();

	const DetailQuery query = {
		.question  = DetailQuestion::Tolerance,
		.box       = box,
		.plan      = pending,
		.tolerance = 0,
		.deeper    = true,
	};

	double share = 0;
	double limit = std::numeric_limits<double>::infinity();
	for (const DetailMember member : detail.members) {
		const DetailAnswer answer = answerDetail(member, detail, query);
		share                     = std::max(share, answer.share);
		limit                     = std::min(limit, answer.limit);
	}
	return std::max(detail.least, std::min(share * footprintOver(detail.view, box), limit));
}

math::Interval pointSpan(double x) {
	return math::Interval {
		.lower = x,
		.upper = x,
	};
}

double pointTolerance(const Detail& detail, const math::Vector& p) {
	const Box spot = {
		.axes = { pointSpan(p.x), pointSpan(p.y), pointSpan(p.z) },
	};

	return cellTolerance(detail, spot);
}

Verdict cellVerdict(const Detail& detail, const DetailQuery& query) {
	if (!query.deeper) {
		return Verdict::Keep;
	}
	for (const DetailMember member : detail.members) {
		const Verdict verdict = answerDetail(member, detail, query).verdict;
		if (verdict != Verdict::Keep) {
			return verdict;
		}
	}
	return Verdict::Keep;
}

Plan pendingPlan() {
	const Patch patch = {
		.weights = {},
		.step    = 0,
		.bent    = false,
	};

	const Fitted fitted = {
		.patch = patch,
		.stray = 0,
	};

	const CellPrograms none = {
		.objects  = {},
		.programs = {},
	};

	return Plan {
		.kind     = CellKind::Empty,
		.programs = none,
		.run      = {},
		.fitted   = fitted,
		.patched  = false,
		.seamed   = false,
		.leafKind = LeafKind::Held,
		.loosened = 0,
	};
}

math::Vector nearestPoint(const View& view, const Box& box) {
	return {
		std::clamp(view.eye.x, box.axes[0].lower, box.axes[0].upper),
		std::clamp(view.eye.y, box.axes[1].lower, box.axes[1].upper),
		std::clamp(view.eye.z, box.axes[2].lower, box.axes[2].upper),
	};
}

double boxRadius(const Box& box) {
	const math::Vector extent = { 2 * boxHalf(box, 0), 2 * boxHalf(box, 1), 2 * boxHalf(box, 2) };
	return 0.5 * math::length(extent);
}

double widestCosine(const View& view, const Box& box, const math::Vector& towards) {
	const double middle = math::length(towards);
	const double off    = std::acos(std::clamp(math::dot(towards, view.forward) / middle, -1.0, 1.0));
	const double widest = off + std::asin(std::min(boxRadius(box) / middle, 1.0));
	return widest < 0.5 * std::numbers::pi ? std::cos(widest) : 0.0;
}

double footprintOver(const View& view, const Box& box) {
	if (!(view.spread > 0)) {
		return 0;
	}
	const double closest       = math::length(nearestPoint(view, box) - view.eye);
	const math::Vector centre  = { boxCentre(box, 0), boxCentre(box, 1), boxCentre(box, 2) };
	const math::Vector towards = centre - view.eye;
	if (closest <= 0 || math::length(towards) <= 0) {
		return 0;
	}
	const double cosine = widestCosine(view, box, towards);
	return closest * view.spread * std::max(cosine * cosine, view.corner);
}

View sceneView(const Shot& shot) {
	const math::Vector forward = math::normalized(shot.camera.target - shot.camera.eye);
	const math::Vector right   = math::normalized(math::cross(forward, shot.camera.up));
	const double reach         = std::tan(double(shot.camera.fov) / 2);
	const double wide          = reach * shot.render.width / shot.render.height;
	return View {
		.eye     = shot.camera.eye,
		.forward = forward,
		.right   = right,
		.up      = math::cross(right, forward),
		.spread  = 2 * reach / shot.render.height,
		.corner  = 1 / (1 + reach * reach + wide * wide),
		.wide    = wide,
		.tall    = reach,
		.open    = shot.camera.aperture > 0,
	};
}

double footprintShare(double noise) {
	const double tolerance = std::max(noise, leastNoise);
	return std::min(std::max(usualShare * std::sqrt(tolerance / usualNoise), leastFootprint), mostFootprint);
}

std::vector<DetailMember> defaultDetailMembers() {
	return { DetailMember::ByDistance, DetailMember::FocusOnView, DetailMember::Pilot, DetailMember::SplitHiddenPrimitives, DetailMember::SplitCrowdedLeaves };
}

std::optional<DetailMember> detailMemberNamed(const std::string& name) {
	const std::array<std::string, 5> names = { "by_distance", "focus_on_view", "pilot", "split_hidden_primitives", "split_crowded_leaves" };
	for (std::size_t k = 0; k < names.size(); ++k) {
		if (names[k] == name) {
			return DetailMember(k);
		}
	}
	return std::nullopt;
}

double longestSide(const Region& region) {
	return std::max({ region.size[0], region.size[1], region.size[2] });
}

Detail sceneDetail(const SceneDetailQuery& query) {
	const View view = sceneView(query.shot);

	Detail detail = {
		.members = query.members,
		.view    = view,
		.least   = leastShare * longestSide(query.region),
		.share   = footprintShare(query.shot.render.tolerance),
		.screen  = emptyScreen(),
		.pilot   = unmeasuredPilot(query.region),
		.hidden  = noHiddenPrimitives(),
	};

	detail.screen = hasMember(detail, DetailMember::FocusOnView) ? frameScreen(view) : emptyScreen();
	detail.hidden = hasMember(detail, DetailMember::SplitHiddenPrimitives) ? hiddenPrimitives(query.scene) : noHiddenPrimitives();
	return detail;
}

}
