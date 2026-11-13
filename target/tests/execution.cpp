#include "check.h"
#include "core/frame.h"
#include "core/plugin.h"
#include "core/render.h"
#include "core/surfaces.h"
#include "plugins/execution/packets/answer.h"
#include "plugins/execution/packets/packet.h"
#include "plugins/execution/packets/queue.h"
#include "plugins/geometry/tree/span.h"
#include "plugins/geometry/tree/walk.h"
#include "plugins/transport/paths/camera.h"
#include "plugins/transport/paths/feature.h"
#include "plugins/transport/paths/light/light.h"
#include "plugins/transport/paths/paths.h"
#include "syntax/evaluator.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <utility>
#include <vector>

namespace kodanuki {

struct KindRays {
	RayKind kind;
	std::vector<Ray> rays;
};

struct SceneRays {
	std::array<KindRays, rayKindCount> kinds;
};

struct LaneCheck {
	const Geometry& geometry;
	const Packet& packet;
	const WaveTricks& tricks;
};

struct Mismatches {
	int met;
	int hit;
	int lanes;
};

std::uint64_t nextBits(std::uint64_t& state) {
	state = state * 6364136223846793005ull + 1442695040888963407ull;
	return state >> 11;
}

double unitDraw(std::uint64_t& state) {
	return double(nextBits(state)) / double(1ull << 53);
}

math::Vector drawnAround(const math::Vector& normal, std::uint64_t& state) {
	const math::UnitPair pair = { unitDraw(state), unitDraw(state) };
	return math::cosineHemisphere(math::basisAround(normal), pair);
}

Ray rayFrom(const math::Vector& origin, const math::Vector& direction, double far) {
	return Ray {
		.origin    = origin,
		.direction = math::normalized(direction),
		.far       = float(far),
		.inside    = false,
		.within    = noMaterial,
	};
}

void addShadows(std::vector<Ray>& rays, const Lights& lights, const math::Vector& origin) {
	for (const Lamp& lamp : lights.lamps) {
		const bool far               = lamp.kind == LightKind::Directional || lamp.kind == LightKind::Sun;
		const math::Vector toward    = far ? -lamp.direction : lamp.at - origin;
		const double reach           = far ? std::numeric_limits<double>::infinity() : math::length(toward);
		const math::Vector direction = math::length(toward) > 0 ? toward : math::Vector { 0, 0, 1 };
		rays.push_back(rayFrom(origin, direction, reach));
	}
}

struct Follow {
	const Lights& lights;
	const Hit& hit;
	std::uint32_t within;
};

void addFollowers(SceneRays& rays, const Follow& follow, std::uint64_t& state) {
	const math::Vector outward = follow.hit.normal;
	const math::Vector above   = follow.hit.point + outward * 4e-3;
	rays.kinds[1].rays.push_back(rayFrom(above, drawnAround(outward, state), 1e30));
	Ray inside    = rayFrom(follow.hit.point - outward * 4e-3, drawnAround(-outward, state), 1e30);
	inside.inside = true;
	inside.within = follow.within;
	rays.kinds[2].rays.push_back(inside);
	addShadows(rays.kinds[3].rays, follow.lights, above);
}

std::uint32_t hitMaterial(const Geometry& geometry, const Hit& hit) {
	return hit.leaf != noLeaf ? geometry.scene.leaves[hit.leaf].material : noMaterial;
}

SceneRays sceneRays(const Geometry& geometry, const Lens& lens, const Lights& lights) {
	SceneRays rays = {
		.kinds = { KindRays { RayKind::Camera, {} }, KindRays { RayKind::Scatter, {} }, KindRays { RayKind::Inside, {} }, KindRays { RayKind::Shadow, {} } },
	};

	std::uint64_t state = 17;
	for (int y = 0; y < lens.height; ++y) {
		for (int x = 0; x < lens.width; ++x) {
			const Ray ray = pixelRay(lens, x, y);
			rays.kinds[0].rays.push_back(ray);
			const Hit hit = intersect(geometry, ray);
			if (hit.kind != HitKind::Missed) {
				const Follow follow = {
					.lights = lights,
					.hit    = hit,
					.within = hitMaterial(geometry, hit),
				};

				addFollowers(rays, follow, state);
			}
		}
	}
	return rays;
}

bool bitEqualMet(const Met& a, const Met& b) {
	return a.kind == b.kind && std::memcmp(&a.distance, &b.distance, sizeof(double)) == 0 && a.payload == b.payload && a.entered == b.entered && std::memcmp(&a.side, &b.side, sizeof(double)) == 0;
}

bool sameStop(const Met& a, const Met& b) {
	return a.kind == b.kind && a.payload == b.payload;
}

bool sameBits(const void* a, const void* b, std::size_t size) {
	return std::memcmp(a, b, size) == 0;
}

bool bitEqualHit(const Hit& a, const Hit& b) {
	return a.kind == b.kind && sameBits(&a.distance, &b.distance, sizeof(a.distance)) && sameBits(&a.point, &b.point, sizeof(a.point)) && sameBits(&a.normal, &b.normal, sizeof(a.normal)) && a.leaf == b.leaf && a.other == b.other && sameBits(&a.share, &b.share, sizeof(a.share)) && sameBits(&a.loosened, &b.loosened, sizeof(a.loosened)) && a.leafKind == b.leafKind && a.cell == b.cell;
}

Mismatches checkedPacket(const LaneCheck& check) {
	const PacketMet met = walkedPacket(check.geometry, check.packet, check.tricks);

	const PacketAnswer answer = {
		.met    = met,
		.tricks = check.tricks,
	};

	const std::array<Hit, packetWidth> hits = packetHits(check.geometry, check.packet, answer);
	const bool bare                         = check.tricks.bareShadows && check.packet.kind == RayKind::Shadow;
	Mismatches wrong                        = { 0, 0, 0 };
	for (int lane = 0; lane < packetWidth; ++lane) {
		if (((check.packet.present >> lane) & 1) == 0) {
			continue;
		}
		const Ray& ray    = check.packet.rays[std::size_t(lane)];
		const Hit hit     = intersect(check.geometry, ray);
		const Hit& got    = hits[std::size_t(lane)];
		const Met scalar  = firstMet(check.geometry, ray);
		const Met& packed = met.lanes[std::size_t(lane)];
		wrong.met += (bare && !ray.inside ? sameStop(scalar, packed) : bitEqualMet(scalar, packed)) ? 0 : 1;
		wrong.hit += (bare ? hit.kind == got.kind : bitEqualHit(hit, got)) ? 0 : 1;
		wrong.lanes += 1;
	}
	return wrong;
}

Packet packetOf(const std::vector<Ray>& rays, RayKind kind) {
	Packet packet = {
		.rays       = {},
		.tokens     = {},
		.present    = 0,
		.descending = descendingOf(rays.front()),
		.kind       = kind,
	};

	for (std::size_t lane = 0; lane < rays.size(); ++lane) {
		packet.rays[lane]   = rays[lane];
		packet.tokens[lane] = std::uint32_t(lane);
		packet.present      = std::uint8_t(packet.present | (1u << lane));
	}
	return packet;
}

Mismatches added(const Mismatches& a, const Mismatches& b) {
	return Mismatches {
		.met   = a.met + b.met,
		.hit   = a.hit + b.hit,
		.lanes = a.lanes + b.lanes,
	};
}

struct GroupCheck {
	const Geometry& geometry;
	const KindRays& rays;
	const WaveTricks& tricks;
};

Mismatches groupChecked(const GroupCheck& group, const std::vector<Ray>& rays) {
	const Packet packet = packetOf(rays, group.rays.kind);

	const LaneCheck check = {
		.geometry = group.geometry,
		.packet   = packet,
		.tricks   = group.tricks,
	};

	return checkedPacket(check);
}

Mismatches randomGroups(const GroupCheck& group, std::uint64_t seed) {
	std::array<std::vector<Ray>, 8> octants;
	std::vector<Ray> shuffled = group.rays.rays;
	std::uint64_t state       = seed;
	for (std::size_t k = shuffled.size(); k > 1; --k) {
		std::swap(shuffled[k - 1], shuffled[nextBits(state) % k]);
	}
	Mismatches wrong = { 0, 0, 0 };
	for (const Ray& ray : shuffled) {
		std::vector<Ray>& bucket = octants[std::size_t(descendingOf(ray))];
		bucket.push_back(ray);
		if (bucket.size() == std::size_t(packetWidth) || nextBits(state) % packetWidth == 0) {
			wrong = added(wrong, groupChecked(group, bucket));
			bucket.clear();
		}
	}
	for (const std::vector<Ray>& bucket : octants) {
		wrong = bucket.empty() ? wrong : added(wrong, groupChecked(group, bucket));
	}
	return wrong;
}

Mismatches queuedGroups(const Geometry& geometry, const SceneRays& rays) {
	RayQueues queues = emptyQueues(64);
	for (const KindRays& kind : rays.kinds) {
		for (std::size_t k = 0; k < kind.rays.size(); ++k) {
			const HeldRay held = {
				.ray   = kind.rays[k],
				.token = std::uint32_t(k),
			};

			queued(queues, kind.kind, held);
		}
	}
	const WaveTricks tricks = waveTricks();
	Mismatches wrong        = { 0, 0, 0 };
	while (!drained(queues)) {
		const Packet packet = nextPacket(queues);

		const LaneCheck check = {
			.geometry = geometry,
			.packet   = packet,
			.tricks   = tricks,
		};

		wrong = added(wrong, checkedPacket(check));
	}
	return wrong;
}

Mismatches everyTrick(const Geometry& geometry, const SceneRays& rays) {
	Mismatches wrong = queuedGroups(geometry, rays);
	for (int tricks = 0; tricks < 8; ++tricks) {
		const WaveTricks on = {
			.bareShadows = (tricks & 1) != 0,
			.missedLanes = (tricks & 2) != 0,
			.groupedHits = (tricks & 4) != 0,
		};

		for (const KindRays& kind : rays.kinds) {
			const GroupCheck group = {
				.geometry = geometry,
				.rays     = kind,
				.tricks   = on,
			};

			wrong = added(wrong, randomGroups(group, std::uint64_t(tricks + 1)));
		}
	}
	return wrong;
}

struct Drawing {
	const SceneFile& file;
	const Geometry& geometry;
};

void checkLanes(const char* path, const Drawing& drawing) {
	Render render         = drawing.file.shot.render;
	render.width          = 80;
	render.height         = 50;
	const Lens lens       = lensOf(drawing.file.shot.camera, render);
	const SceneRays rays  = sceneRays(drawing.geometry, lens, lightsOf(drawing.file.lighting));
	const Mismatches gone = everyTrick(drawing.geometry, rays);
	std::printf("execution %s: %d lanes, %d stops and %d hits off the scalar walk\n", path, gone.lanes, gone.met, gone.hit);
	check(gone.lanes > 0 && gone.met == 0 && gone.hit == 0, "every lane of a packet stops and hits as the scalar walk");
}

enum class MemberSet : std::uint8_t {
	Core,
	Defaults,
	Connected,
};

struct FrameRun {
	std::string execution;
	int threads;
	MemberSet members;
};

ChosenMembers chosenMembers(MemberSet members) {
	if (members == MemberSet::Core) {
		return coreChosen();
	}
	ChosenMembers chosen = defaultChosen();
	if (members == MemberSet::Connected) {
		const EmissionShare cosine = {
			.member = SamplerMember::Cosine,
			.share  = 0.5,
		};

		const EmissionShare mirrors = {
			.member = SamplerMember::TowardMirrors,
			.share  = 0.5,
		};

		chosen.strategies.members.push_back(StrategyMember::Connect);
		chosen.sampling.emission = { cosine, mirrors };
	}
	return chosen;
}

struct TileCount {
	int begun;
	int placed;
};

void countedTile(void* state, const StepEvent& event) {
	TileCount& count = *static_cast<TileCount*>(state);
	count.begun += event.step == LoopStep::Pass && !event.done ? int(event.items) : 0;
	count.placed += event.step == LoopStep::PlaceTile ? 1 : 0;
}

Framed framed(const Drawing& drawing, const FrameRun& run) {
	const bool defaults = run.members != MemberSet::Core;
	Shot shot           = drawing.file.shot;
	shot.render.samples = defaults ? 16 : shot.render.samples;
	shot.render.batch   = defaults ? 4 : shot.render.batch;

	const ChosenMembers chosen             = chosenMembers(run.members);
	const std::vector<Plugin> preparations = testPreparations(defaults ? defaultPreparations() : std::vector<std::string> {});
	const Features features                = featuresOf(featureQueryOf(drawing.file, 1));
	const Lens lens                        = lensOf(shot.camera, shot.render);
	TileCount tiles                        = { .begun = 0, .placed = 0 };
	Observing observing                    = noObserving();
	observing.frames.push_back(Watcher { .observed = countedTile, .state = &tiles });

	const PathsWorld paths = {
		.features    = features,
		.smoothGlass = SmoothGlass::Exact,
		.sampling    = chosen.sampling,
		.strategies  = chosen.strategies,
	};

	const FrameInput input = {
		.geometry       = drawing.geometry,
		.looks          = drawing.file.looks,
		.lighting       = drawing.file.lighting,
		.shot           = shot,
		.lens           = lens,
		.transport      = testPlugin("transport/paths"),
		.transportWorld = &paths,
		.preparations   = preparations,
		.stopping       = testPlugin(defaults ? "stopping/confidence_interval" : "stopping/fixed_count"),
		.perFrame       = 0,
		.execution      = run.execution,
		.threads        = run.threads,
		.observing      = observing,
	};

	const Framed rendered = stillFrame(input);
	check(tiles.begun > 0 && tiles.placed == tiles.begun, "a frame tells its observers of every tile of its passes placed");
	return rendered;
}

const char* memberSetName(MemberSet members) {
	switch (members) {
		case MemberSet::Core: return "core";
		case MemberSet::Defaults: return "default";
		case MemberSet::Connected: return "default and Connect";
	}

	return "core";
}

double imageDistance(const Image& a, const Image& b) {
	double squares = 0;
	for (std::size_t k = 0; k < a.rgb.size(); ++k) {
		squares += double(a.rgb[k] - b.rgb[k]) * double(a.rgb[k] - b.rgb[k]);
	}
	return std::sqrt(squares / double(a.rgb.size()));
}

double imageMean(const Image& image) {
	double total = 0;
	for (const float value : image.rgb) {
		total += double(value);
	}
	return total / double(image.rgb.size());
}

bool withinNoise(const Framed& a, const Framed& b, double noise) {
	const double pixels = double(a.image.rgb.size()) / 3.0;
	const bool noisy    = imageDistance(a.image, b.image) <= 1.25 * noise;
	const bool unbiased = std::abs(imageMean(a.image) - imageMean(b.image)) <= 4.0 * noise / std::sqrt(pixels);
	const bool effort   = std::abs(a.samples - b.samples) <= 0.1 * a.samples;
	return a.image.rgb.size() == b.image.rgb.size() && noisy && unbiased && effort;
}

void checkFrames(const char* path, const Drawing& drawing, MemberSet members) {
	SceneFile reseeded = drawing.file;
	reseeded.shot.render.seed += 1;

	const Drawing other = {
		.file     = reseeded,
		.geometry = drawing.geometry,
	};

	const Framed one   = framed(drawing, FrameRun { "execution/single", 8, members });
	const Framed again = framed(other, FrameRun { "execution/single", 8, members });
	const Framed wave  = framed(other, FrameRun { "execution/packets", 1, members });
	const Framed waves = framed(other, FrameRun { "execution/packets", 8, members });
	const double noise = imageDistance(one.image, again.image);
	const bool same    = withinNoise(one, wave, noise) && withinNoise(one, waves, noise);
	std::printf("execution %s, %s members: waves on 1 and 8 threads %s one at a time, %.4g and %.4g apart at a noise of %.4g between two seeds\n", path, memberSetName(members), same ? "within noise of" : "differ from", imageDistance(one.image, wave.image), imageDistance(one.image, waves.image), noise);
	check(noise > 0 && same, "waves draw the frame of one ray at a time within noise, on one thread and on eight");
}

void checkSlabFrames(const char* path, const Drawing& drawing) {
	const TestTree unbuilt = passedTestTree(drawing.file, passesWithoutFaceSlabs(), 8);

	const Geometry geometry = treeGeometry(drawing.file.scene, unbuilt.tree);

	const Drawing bare = {
		.file     = drawing.file,
		.geometry = geometry,
	};

	const Framed one       = framed(drawing, FrameRun { "execution/single", 8, MemberSet::Defaults });
	const Framed waves     = framed(drawing, FrameRun { "execution/packets", 8, MemberSet::Defaults });
	const Framed bareOne   = framed(bare, FrameRun { "execution/single", 8, MemberSet::Defaults });
	const Framed bareWaves = framed(bare, FrameRun { "execution/packets", 8, MemberSet::Defaults });
	const bool same        = one.image.rgb == bareOne.image.rgb && waves.image.rgb == bareWaves.image.rgb;
	std::printf("execution %s: the frames with the face slabs on %s those with them off (%zu and %zu run words)\n", path, same ? "equal" : "differ from", treeOf(drawing.geometry).runs.size(), unbuilt.tree.runs.size());
	check(same && one.samples == bareOne.samples && waves.samples == bareWaves.samples, "the face slabs change no byte of the frame, with either Execution member");
}

bool sameShownSurface(const Surface& a, const Surface& b) {
	const bool placed = sameBits(&a.distance, &b.distance, sizeof(a.distance)) && sameBits(&a.normal, &b.normal, sizeof(a.normal));
	const bool named  = a.leaf == b.leaf && a.material == b.material;
	const bool looks  = sameBits(&a.albedo, &b.albedo, sizeof(a.albedo)) && sameBits(&a.roughness, &b.roughness, sizeof(a.roughness));
	return placed && named && looks;
}

bool bitEqualSurface(const Surface& a, const Surface& b) {
	return sameShownSurface(a, b) && sameBits(&a.onward, &b.onward, sizeof(float));
}

int surfacesOff(const std::vector<Surface>& a, const std::vector<Surface>& b) {
	if (a.size() != b.size()) {
		return int(std::max(a.size(), b.size()));
	}
	int off = 0;
	for (std::size_t k = 0; k < a.size(); ++k) {
		off += bitEqualSurface(a[k], b[k]) ? 0 : 1;
	}
	return off;
}

struct SurfaceAim {
	const Lens& lens;
	FilmPlan film;
};

std::vector<Surface> surfacesBy(const FrameInput& frame, const std::shared_ptr<void>& transported, const FrameRun& run, const SurfaceAim& aim) {
	const FrameInput input = {
		.geometry       = frame.geometry,
		.looks          = frame.looks,
		.lighting       = frame.lighting,
		.shot           = frame.shot,
		.lens           = aim.lens,
		.transport      = frame.transport,
		.transportWorld = frame.transportWorld,
		.preparations   = frame.preparations,
		.stopping       = frame.stopping,
		.perFrame       = frame.perFrame,
		.execution      = run.execution,
		.threads        = run.threads,
		.observing      = frame.observing,
	};

	const StepQuery query = {
		.lens        = aim.lens,
		.film        = aim.film,
		.change      = Change::None,
		.keepsGuides = false,
	};

	Renderer renderer            = testRenderer(input, firstStep(query));
	renderer.drawing.transported = transported;
	return tracedSurfaces(renderer);
}

int phasedOff(const std::vector<Surface>& phased, const std::vector<Surface>& fine, const Lens& lens, const FilmPlan& film) {
	if (phased.size() * std::size_t(film.factor * film.factor) != fine.size()) {
		return int(phased.size());
	}
	const int fineWidth = lens.width * film.factor;
	int off             = 0;
	for (int y = 0; y < lens.height; ++y) {
		for (int x = 0; x < lens.width; ++x) {
			const int column = x * film.factor + film.phase % film.factor;
			const int row    = y * film.factor + film.phase / film.factor;
			const Surface& a = phased[std::size_t(y) * std::size_t(lens.width) + std::size_t(x)];
			const Surface& b = fine[std::size_t(row) * std::size_t(fineWidth) + std::size_t(column)];
			off += sameShownSurface(a, b) ? 0 : 1;
		}
	}
	return off;
}

struct SurfaceCount {
	int pixels;
	int missed;
	int skyOff;
};

SurfaceCount skySurfaces(const Tracing& tracing, const std::vector<Surface>& surfaces) {
	const Lens& lens    = tracing.lens;
	SurfaceCount counts = { 0, 0, 0 };
	for (int y = 0; y < lens.height; ++y) {
		for (int x = 0; x < lens.width; ++x) {
			const Surface& surface = surfaces[std::size_t(y) * std::size_t(lens.width) + std::size_t(x)];
			const bool missed      = intersect(tracing.geometry, pixelRay(lens, x, y)).kind == HitKind::Missed;
			counts.pixels += 1;
			counts.missed += missed ? 1 : 0;
			counts.skyOff += missed && !bitEqualSurface(surface, missedSurface()) ? 1 : 0;
		}
	}
	return counts;
}

int recordsOff(const Tracing& tracing, int samples) {
	std::vector<double> registers;
	int off = 0;
	for (int k = 0; k < samples; ++k) {
		const PixelSample sample = {
			.x        = (k * 37) % tracing.lens.width,
			.y        = (k * 23) % tracing.lens.height,
			.index    = std::uint32_t(k),
			.group    = std::uint32_t(k),
			.branches = 1,
		};

		const Ray ray       = startedPath(tracing, sample).ray;
		const Hit hit       = intersect(tracing.geometry, ray);
		const Recorded kept = recordedPath(tracing, sample);

		const CameraHit met = {
			.ray       = ray,
			.hit       = hit,
			.registers = registers,
		};

		off += sameShownSurface(cameraSurface(tracing, met), kept.surface) ? 0 : 1;
	}
	return off;
}

void checkSurfaces(const char* path, const Drawing& drawing) {
	Render render                          = drawing.file.shot.render;
	render.width                           = 82;
	render.height                          = 51;
	const Lens lens                        = lensOf(drawing.file.shot.camera, render);
	const Features features                = featuresOf(featureQueryOf(drawing.file, 1));
	const ChosenMembers chosen             = coreChosen();
	const std::vector<Plugin> preparations = {};
	const Observing observing              = noObserving();

	const PathsWorld paths = {
		.features    = features,
		.smoothGlass = SmoothGlass::Exact,
		.sampling    = chosen.sampling,
		.strategies  = chosen.strategies,
	};

	const FrameInput input = {
		.geometry       = drawing.geometry,
		.looks          = drawing.file.looks,
		.lighting       = drawing.file.lighting,
		.shot           = drawing.file.shot,
		.lens           = lens,
		.transport      = testPlugin("transport/paths"),
		.transportWorld = &paths,
		.preparations   = preparations,
		.stopping       = testPlugin("stopping/fixed_count"),
		.perFrame       = 0,
		.execution      = "execution/single",
		.threads        = 1,
		.observing      = observing,
	};

	const StepQuery still = {
		.lens        = lens,
		.film        = stillFilm(),
		.change      = Change::None,
		.keepsGuides = false,
	};

	Renderer renderer                       = testRenderer(input, firstStep(still));
	const std::shared_ptr<void> transported = frameTransport(renderer, stillFilm(), true);
	const Tracing& tracing                  = tracingOf(transported.get());

	const SurfaceAim aim = {
		.lens = lens,
		.film = stillFilm(),
	};

	const FilmPlan film = {
		.factor = 2,
		.phase  = 3,
	};

	const SurfaceAim jittered = {
		.lens = lens,
		.film = film,
	};

	Render fineRender   = render;
	fineRender.width    = render.width * film.factor;
	fineRender.height   = render.height * film.factor;
	const Lens fineLens = lensOf(drawing.file.shot.camera, fineRender);

	const SurfaceAim fine = {
		.lens = fineLens,
		.film = stillFilm(),
	};

	const std::vector<Surface> one         = surfacesBy(input, transported, FrameRun { "execution/single", 3, MemberSet::Core }, aim);
	const std::vector<Surface> wave        = surfacesBy(input, transported, FrameRun { "execution/packets", 1, MemberSet::Core }, aim);
	const std::vector<Surface> waves       = surfacesBy(input, transported, FrameRun { "execution/packets", 6, MemberSet::Core }, aim);
	const std::vector<Surface> phasedOne   = surfacesBy(input, transported, FrameRun { "execution/single", 2, MemberSet::Core }, jittered);
	const std::vector<Surface> phasedWaves = surfacesBy(input, transported, FrameRun { "execution/packets", 6, MemberSet::Core }, jittered);
	const std::vector<Surface> fineOne     = surfacesBy(input, transported, FrameRun { "execution/single", 6, MemberSet::Core }, fine);
	const int off                          = surfacesOff(one, wave) + surfacesOff(one, waves);
	const int phaseOff                     = surfacesOff(phasedOne, phasedWaves);
	const int centreOff                    = phasedOff(phasedOne, fineOne, lens, film);
	const SurfaceCount sky                 = skySurfaces(tracing, one);
	const int records                      = recordsOff(tracing, 400);
	std::printf("execution %s: %d of %d surfaces off between the members, %d at factor 2, phase 3, %d off the shown pixel's centre, %d of %d sky pixels off a miss, %d of 400 records off their camera surface\n", path, off, sky.pixels, phaseOff, centreOff, sky.skyOff, sky.missed, records);
	check(one.size() == std::size_t(sky.pixels) && off == 0, "the G-buffer of the waves is that of one ray at a time to the byte, on one thread and on six");
	check(phasedOne.size() == one.size() && phaseOff == 0, "at a factor of 2 the jittered G-buffer of the waves is that of one ray at a time to the byte, at the drawn size");
	check(centreOff == 0, "a jittered drawn pixel's surface is that of the shown pixel its phase aims at, through that pixel's centre");
	check(sky.skyOff == 0, "a pixel whose ray meets nothing shows the missed surface");
	check(records == 0, "a sample's recorded first surface is the camera surface of its camera ray");
}

void checkExecutionScene(const char* path) {
	SceneFile file           = sceneOrExit(path);
	file.shot.render.width   = 96;
	file.shot.render.height  = 60;
	file.shot.render.samples = 4;
	const TestTree tree      = testTree(file, 8);

	const Geometry geometry = treeGeometry(file.scene, tree.tree);

	const Drawing drawing = {
		.file     = file,
		.geometry = geometry,
	};

	checkLanes(path, drawing);
	checkFrames(path, drawing, MemberSet::Core);
	checkFrames(path, drawing, MemberSet::Defaults);
	checkFrames(path, drawing, MemberSet::Connected);
	checkSlabFrames(path, drawing);
	checkSurfaces(path, drawing);
}

void checkExecution() {
	checkExecutionScene("scenes/magic-cube.sdf");
	checkExecutionScene("scenes/chess-board.sdf");
	checkExecutionScene("scenes/gallery.sdf");
	checkExecutionScene("scenes/sapphire.sdf");
}

}
