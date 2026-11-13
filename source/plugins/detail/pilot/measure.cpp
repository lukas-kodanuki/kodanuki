#include "plugins/detail/pilot/measure.h"

#include "plugins/transport/paths/light_walk.h"
#include "shared/parallel.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>

namespace kodanuki {

constexpr int pilotStep            = 8;
constexpr int pilotMembers         = 3;
constexpr int pilotEvents          = 16;
constexpr int pilotChunk           = 64;
constexpr double pilotWidestBounce = 0.1;
constexpr double pilotEndless      = std::numeric_limits<double>::infinity();

struct PilotEvent {
	math::Vector place;
	math::Vector heading;
	std::uint64_t route;
	math::Vector blocker;
	bool blocked;
};

struct PilotTrail {
	std::array<PilotEvent, pilotEvents> events;
	int count;
	std::uint64_t route;
	double widest;
};

struct PilotGrid {
	int across;
	int down;
};

struct PilotStart {
	int x;
	int y;
	int throughX;
	int throughY;
};

std::uint64_t routeMixed(std::uint64_t route, std::uint64_t step) {
	std::uint64_t value = route ^ (step + 0x9e3779b97f4a7c15ull + (route << 6) + (route >> 2));
	value ^= value >> 31;
	value *= 0xbf58476d1ce4e5b9ull;
	value ^= value >> 29;
	return value;
}

PilotTrail emptyTrail() {
	return PilotTrail {
		.events = {},
		.count  = 0,
		.route  = 0,
		.widest = 0,
	};
}

Walk pilotWalk(const Tracing& tracing, const PilotStart& start, LightWalk* leads) {
	const PixelSample sample = {
		.x        = start.x,
		.y        = start.y,
		.index    = 0,
		.group    = 0,
		.branches = 1,
	};
	Walk walk         = walkStarted(tracing, sample, leads);
	const Ray ray     = pixelRay(tracing.lens, start.throughX, start.throughY);
	walk.ray          = ray;
	walk.path.heading = ray.direction;
	walk.path.drawnAt = ray.origin;
	walk.path.legFrom = ray.origin;
	return walk;
}

void blockedTrail(PilotTrail& trail, const Hit& hit) {
	if (trail.count == 0 || hit.kind == HitKind::Missed) {
		return;
	}
	PilotEvent& last = trail.events[std::size_t(trail.count - 1)];
	last.blocker     = last.blocked ? last.blocker : math::Vector(hit.point);
	last.blocked     = true;
}

void notedTrail(PilotTrail& trail, const Tracing& tracing, const Wanted& wanted, const Hit& hit) {
	const std::uint64_t material = tracing.scene.leaves[hit.leaf].material;
	trail.route                  = routeMixed(trail.route, (material << 3) | std::uint64_t(wanted.kind));

	const PilotEvent event = {
		.place   = hit.point,
		.heading = wanted.ray.direction,
		.route   = trail.route,
		.blocker = hit.point,
		.blocked = false,
	};

	trail.events[std::size_t(trail.count)] = event;
	trail.count += 1;
}

bool trailEnds(PilotTrail& trail, const Walk& walk) {
	trail.widest = std::max(trail.widest, double(walk.path.sharpness));
	return trail.widest >= pilotWidestBounce || trail.count == pilotEvents;
}

PilotTrail flownTrail(const Tracing& tracing, const PilotStart& start) {
	std::vector<double> registers;
	PilotTrail trail = emptyTrail();
	LightWalk lead   = idleLead();
	Walk walk        = pilotWalk(tracing, start, &lead);
	while (walk.phase != WalkPhase::Done) {
		const Wanted wanted = wantedRay(tracing, walk, &lead);
		const Hit hit       = intersect(tracing.geometry, wanted.ray);
		const bool lighting = walk.phase == WalkPhase::Lighting;
		if (lighting && walk.lines.items[walk.line].shadow.cell == noCell) {
			blockedTrail(trail, hit);
		}
		if (!lighting && trailEnds(trail, walk)) {
			return trail;
		}
		if (!lighting && hit.kind != HitKind::Missed) {
			notedTrail(trail, tracing, wanted, hit);
		}

		const Answer answer = {
			.hit       = hit,
			.registers = registers,
			.leads     = &lead,
		};

		walkAnswered(tracing, walk, answer);
	}
	return trail;
}

double acrossRay(const PilotEvent& self, const PilotEvent& other) {
	const math::Vector apart = self.place - other.place;
	const math::Vector along = self.heading * math::dot(apart, self.heading);
	return math::length(apart - along);
}

Need needAt(const math::Vector& place, double footprint, bool direct) {
	return Need {
		.place     = place,
		.footprint = footprint,
		.direct    = direct,
	};
}

double eventWidth(const std::array<PilotTrail, pilotMembers>& trails, int index) {
	const PilotEvent& event = trails[0].events[std::size_t(index)];
	double width            = pilotEndless;
	for (int member = 1; member < pilotMembers; ++member) {
		const PilotTrail& other = trails[std::size_t(member)];
		if (index < other.count && other.events[std::size_t(index)].route == event.route) {
			width = std::min(width, acrossRay(event, other.events[std::size_t(index)]));
		}
	}
	return std::isinf(width) && index > 0 ? 0 : width;
}

void measuredTrails(const std::array<PilotTrail, pilotMembers>& trails, const View& view, std::vector<Need>& needs) {
	const PilotTrail& self = trails[0];
	for (int index = 0; index < self.count; ++index) {
		const PilotEvent& event = self.events[std::size_t(index)];
		const double width      = eventWidth(trails, index);
		needs.push_back(needAt(event.place, width, index == 0));
		if (!event.blocked) {
			continue;
		}
		needs.push_back(needAt(event.blocker, std::isinf(width) ? footprintAt(view, event.place) : width, false));
	}
}

std::array<PilotStart, pilotMembers> pilotStarts(const Lens& lens, const PilotGrid& grid, int point) {
	const int x     = std::min(point % grid.across * pilotStep + pilotStep / 2, lens.width - 1);
	const int y     = std::min(point / grid.across * pilotStep + pilotStep / 2, lens.height - 1);
	const int right = x + 1 < lens.width ? x + 1 : x - 1;
	const int below = y + 1 < lens.height ? y + 1 : y - 1;

	const PilotStart base = {
		.x        = x,
		.y        = y,
		.throughX = x,
		.throughY = y,
	};

	PilotStart beside = base;
	beside.throughX   = right;
	PilotStart under  = base;
	under.throughY    = below;
	return { base, beside, under };
}

std::vector<Need> chunkNeeds(const PilotFlight& flight, const PilotGrid& grid, int first) {
	const int points = grid.across * grid.down;
	std::vector<Need> needs;
	for (int point = first; point < std::min(first + pilotChunk, points); ++point) {
		const std::array<PilotStart, pilotMembers> starts = pilotStarts(flight.tracing.lens, grid, point);
		std::array<PilotTrail, pilotMembers> trails       = {};
		for (int member = 0; member < pilotMembers; ++member) {
			trails[std::size_t(member)] = flownTrail(flight.tracing, starts[std::size_t(member)]);
		}
		measuredTrails(trails, flight.view, needs);
	}
	return needs;
}

std::vector<Need> pilotNeeds(const PilotFlight& flight) {
	const PilotGrid grid = {
		.across = (flight.tracing.lens.width + pilotStep - 1) / pilotStep,
		.down   = (flight.tracing.lens.height + pilotStep - 1) / pilotStep,
	};

	const int chunks = (grid.across * grid.down + pilotChunk - 1) / pilotChunk;
	std::vector<std::vector<Need>> found(std::size_t(std::max(chunks, 0)));
	inParallel(flight.threads, found.size(), [&](std::size_t chunk) {
		found[chunk] = chunkNeeds(flight, grid, int(chunk) * pilotChunk);
	});
	std::vector<Need> needs;
	for (const std::vector<Need>& one : found) {
		needs.insert(needs.end(), one.begin(), one.end());
	}
	return needs;
}

}
