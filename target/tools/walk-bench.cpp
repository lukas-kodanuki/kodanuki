#include "extras/execution_correct_waves/answer.h"
#include "extras/execution_correct_waves/packet.h"
#include "frame/run.h"
#include "material/material.h"
#include "sdf/scene.h"
#include "transport/camera.h"
#include "transport/feature.h"
#include "tree/pass.h"
#include "walk/hit.h"
#include "walk/leaf.h"
#include "walk/span.h"
#include "walk/walk.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <string>
#include <vector>

#include <linux/perf_event.h>
#include <sys/ioctl.h>
#include <sys/syscall.h>
#include <unistd.h>

namespace kodanuki {
Quartic faceAlong(const LeafRun& run, std::uint32_t face, const Leg& leg);
}

using namespace kodanuki;

// A research tool: times the walk (firstMet), the hit (hitOf) and the packet walk on fixed
// ray sets of a scene, one thread, and prints a checksum of every result.

using Clock = std::chrono::steady_clock;

struct RaySet {
	std::string name;
	std::vector<Ray> rays;
	RayKind kind;
};

double secondsSince(const Clock::time_point& start) {
	return std::chrono::duration<double>(Clock::now() - start).count();
}

std::uint64_t mixed(std::uint64_t hash, const void* data, std::size_t size) {
	const unsigned char* bytes = static_cast<const unsigned char*>(data);
	for (std::size_t i = 0; i < size; ++i) {
		hash = (hash ^ bytes[i]) * 0x100000001B3ull;
	}
	return hash;
}

std::uint64_t metHash(std::uint64_t hash, const Met& met) {
	const int kind = int(met.kind);
	hash           = mixed(hash, &kind, sizeof(kind));
	if (met.kind == MetKind::Missed) {
		return hash;
	}
	hash = mixed(hash, &met.distance, sizeof(met.distance));
	return mixed(hash, &met.payload, sizeof(met.payload));
}

std::uint64_t hitHash(std::uint64_t hash, const Hit& hit) {
	const int kind = int(hit.kind);
	hash           = mixed(hash, &kind, sizeof(kind));
	if (hit.kind == HitKind::Missed) {
		return hash;
	}
	const double values[8] = { hit.distance, hit.point.x, hit.point.y, hit.point.z, hit.normal.x, hit.normal.y, hit.normal.z, hit.share };
	hash                   = mixed(hash, values, sizeof(values));
	hash                   = mixed(hash, &hit.leaf, sizeof(hit.leaf));
	return mixed(hash, &hit.other, sizeof(hit.other));
}

math::Vector cosineAround(const math::Vector& n, std::mt19937& random) {
	std::uniform_real_distribution<double> unit(0, 1);
	const double u            = unit(random);
	const double v            = unit(random);
	const double r            = std::sqrt(u);
	const double phi          = 2 * M_PI * v;
	const math::Vector helper = std::abs(n.x) > 0.5 ? math::Vector { 0, 1, 0 } : math::Vector { 1, 0, 0 };
	const math::Vector a      = math::normalized(math::cross(helper, n));
	const math::Vector b      = math::cross(n, a);
	return math::normalized(a * (r * std::cos(phi)) + b * (r * std::sin(phi)) + n * std::sqrt(std::max(0.0, 1 - u)));
}

std::vector<RaySet> raySets(const Geometry& geometry, const Lens& lens, const Looks& looks) {
	RaySet camera = { "camera", {}, RayKind::Camera };
	RaySet bounce = { "bounce", {}, RayKind::Scatter };
	RaySet shadow = { "shadow", {}, RayKind::Shadow };
	RaySet inside = { "inside", {}, RayKind::Inside };
	std::mt19937 random(7);
	std::uniform_real_distribution<double> unit(0, 1);
	const Region& region = geometry.tree.region;
	for (int ty = 0; ty + 1 < lens.height; ty += 2) {
		for (int tx = 0; tx + 3 < lens.width; tx += 4) {
			for (int k = 0; k < 8; ++k) {
				camera.rays.push_back(pixelRay(lens, tx + k % 4, ty + k / 4));
			}
		}
	}
	for (const Ray& ray : camera.rays) {
		const Hit hit = intersect(geometry, ray);
		if (hit.kind != HitKind::Surface) {
			continue;
		}
		const double lift            = 4e-3 + 2 * hit.loosened;
		const std::uint32_t material = hit.leaf == noLeaf ? noMaterial : geometry.scene.leaves[hit.leaf].material;
		if (material != noMaterial && material < looks.materials.size() && looks.materials[material].kind == MaterialKind::Glass) {
			for (int k = 0; k < 4; ++k) {
				const math::Vector into = math::normalized(ray.direction * 0.8 - hit.normal * (0.6 * unit(random)));
				inside.rays.push_back(Ray { .origin = hit.point - hit.normal * lift, .direction = into, .far = std::numeric_limits<double>::infinity(), .inside = true, .within = material });
			}
		}
		const math::Vector origin = hit.point + hit.normal * lift;
		bounce.rays.push_back(Ray { .origin = origin, .direction = cosineAround(hit.normal, random), .far = std::numeric_limits<double>::infinity(), .inside = false, .within = 0 });
		const math::Vector target = { region.lower[0] + region.size[0] * unit(random), region.lower[1] + region.size[1] * (0.6 + 0.4 * unit(random)), region.lower[2] + region.size[2] * unit(random) };
		const math::Vector toward = target - origin;
		const double reach        = math::length(toward);
		shadow.rays.push_back(Ray { .origin = origin, .direction = toward * (1 / reach), .far = reach, .inside = false, .within = 0 });
	}
	return { camera, bounce, shadow, inside };
}

std::vector<Packet> packetsOf(const RaySet& set) {
	std::vector<Packet> packets;
	std::vector<Packet> open(8);
	for (Packet& packet : open) {
		packet.present = 0;
	}
	for (const Ray& ray : set.rays) {
		const int descending             = descendingOf(ray);
		Packet& packet                   = open[std::size_t(descending)];
		const int lane                   = std::popcount(unsigned(packet.present));
		packet.rays[std::size_t(lane)]   = ray;
		packet.tokens[std::size_t(lane)] = 0;
		packet.present |= std::uint8_t(1u << lane);
		packet.descending = descending;
		packet.kind       = set.kind;
		if (lane == 7) {
			packets.push_back(packet);
			packet.present = 0;
		}
	}
	for (Packet& packet : open) {
		if (packet.present != 0) {
			for (int lane = std::popcount(unsigned(packet.present)); lane < 8; ++lane) {
				packet.rays[std::size_t(lane)] = packet.rays[0];
			}
			packets.push_back(packet);
		}
	}
	return packets;
}

struct Timed {
	double seconds;
	std::uint64_t hash;
	double instructions = 0;
	double cycles       = 0;
};

// User-space instructions of this thread, for A/B counts that the shared cores do not disturb.
struct Counter {
	int fd = -1;

	explicit Counter(std::uint64_t config) {
		perf_event_attr attr = {};
		attr.type            = PERF_TYPE_HARDWARE;
		attr.size            = sizeof(attr);
		attr.config          = config;
		attr.exclude_kernel  = 1;
		attr.exclude_hv      = 1;
		fd                   = int(syscall(SYS_perf_event_open, &attr, 0, -1, -1, 0));
	}

	~Counter() {
		if (fd >= 0) {
			close(fd);
		}
	}

	std::uint64_t now() const {
		std::uint64_t value = 0;
		if (fd < 0 || read(fd, &value, sizeof(value)) != sizeof(value)) {
			return 0;
		}
		return value;
	}
};

thread_local Counter counter(PERF_COUNT_HW_INSTRUCTIONS);
thread_local Counter cycleCounter(PERF_COUNT_HW_CPU_CYCLES);

Timed timedWalk(const Geometry& geometry, const RaySet& set, int rounds) {
	std::uint64_t hash            = 0xCBF29CE484222325ull;
	const std::uint64_t counted0  = counter.now();
	const std::uint64_t cycled0   = cycleCounter.now();
	const Clock::time_point start = Clock::now();
	for (int r = 0; r < rounds; ++r) {
		for (const Ray& ray : set.rays) {
			const Met met = firstMet(geometry, ray);
			hash          = r == 0 ? metHash(hash, met) : hash ^ std::uint64_t(met.kind);
		}
	}
	return { secondsSince(start) / rounds, hash, double(counter.now() - counted0) / rounds, double(cycleCounter.now() - cycled0) / rounds };
}

Timed timedIntersect(const Geometry& geometry, const RaySet& set, int rounds) {
	std::uint64_t hash            = 0xCBF29CE484222325ull;
	const std::uint64_t counted0  = counter.now();
	const std::uint64_t cycled0   = cycleCounter.now();
	const Clock::time_point start = Clock::now();
	for (int r = 0; r < rounds; ++r) {
		for (const Ray& ray : set.rays) {
			const Hit hit = intersect(geometry, ray);
			hash          = r == 0 ? hitHash(hash, hit) : hash ^ std::uint64_t(hit.kind);
		}
	}
	return { secondsSince(start) / rounds, hash, double(counter.now() - counted0) / rounds, double(cycleCounter.now() - cycled0) / rounds };
}

Timed timedPackets(const Geometry& geometry, const std::vector<Packet>& packets, int rounds, bool hits) {
	const WaveTricks tricks       = waveTricks();
	std::uint64_t hash            = 0xCBF29CE484222325ull;
	const std::uint64_t counted0  = counter.now();
	const std::uint64_t cycled0   = cycleCounter.now();
	const Clock::time_point start = Clock::now();
	for (int r = 0; r < rounds; ++r) {
		for (const Packet& packet : packets) {
			const PacketMet met = walkedPacket(geometry, packet, tricks);
			if (!hits) {
				for (int lane = 0; lane < 8; ++lane) {
					hash = ((packet.present >> lane) & 1) != 0 && r == 0 ? metHash(hash, met.lanes[std::size_t(lane)]) : hash;
				}
				continue;
			}
			const PacketAnswer answer             = { .met = met, .tricks = tricks };
			const std::array<Hit, packetWidth> hs = packetHits(geometry, packet, answer);
			for (int lane = 0; lane < 8; ++lane) {
				hash = ((packet.present >> lane) & 1) != 0 && r == 0 ? hitHash(hash, hs[std::size_t(lane)]) : hash;
			}
		}
	}
	return { secondsSince(start) / rounds, hash, double(counter.now() - counted0) / rounds, double(cycleCounter.now() - cycled0) / rounds };
}

struct CellBox {
	double lower[3];
	double size;
	std::uint32_t surface;
	bool found;
};

CellBox cellOf(const Geometry& geometry, const math::Vector& p) {
	const Tree& tree = geometry.tree;
	CellBox box      = { { tree.region.lower[0], tree.region.lower[1], tree.region.lower[2] }, tree.region.size[0], 0, false };
	CellEntry entry  = tree.root;
	auto half        = [&](CellBox& b) {
		int octant = 0;
		b.size *= 0.5;
		for (int axis = 0; axis < 3; ++axis) {
			if (math::component(p, axis) >= b.lower[axis] + b.size) {
				b.lower[axis] += b.size;
				octant |= 1 << axis;
			}
		}
		return octant;
	};
	while (entry.kind == CellKind::Branch) {
		const PackedNode& node = tree.nodes[entry.payload];
		const int octant       = half(box);
		if (((node.coarse >> octant) & 1) != 0) {
			entry = slotEntry(node, octant * 8);
			continue;
		}
		const int fine = half(box);
		entry          = slotEntry(node, octant * 8 + fine);
	}
	box.found   = entry.kind == CellKind::Surface;
	box.surface = entry.payload;
	return box;
}

math::Vector cutGradient(const Geometry& geometry, std::uint32_t surface, const math::Vector& p) {
	thread_local std::vector<math::Dual> registers;
	const Tree& tree                = geometry.tree;
	const ProgramList& list         = tree.lists[tree.surfaces[surface].programs];
	const KeptSteps steps           = referencedSteps(geometry.scene, tree, tree.references[list.first]);
	const math::Dual zero           = math::constantAs<math::Dual>(0);
	const Inputs<math::Dual> inputs = {
		.arguments = nullptr,
		.p         = { math::coordinateAs<math::Dual>(p.x, 0), math::coordinateAs<math::Dual>(p.y, 1), math::coordinateAs<math::Dual>(p.z, 2) },
		.n         = { zero, zero, zero },
	};
	const math::Dual value = evaluateKept(steps, inputs, registers);
	return { value.gradient[0], value.gradient[1], value.gradient[2] };
}

math::Vector fieldGradient(const Scene& scene, const math::Vector& p, double h) {
	thread_local std::vector<double> registers;
	math::Vector g = { 0, 0, 0 };
	for (int axis = 0; axis < 3; ++axis) {
		math::Vector step = { 0, 0, 0 };
		step.x            = axis == 0 ? h : 0;
		step.y            = axis == 1 ? h : 0;
		step.z            = axis == 2 ? h : 0;
		const double up   = sceneDistance(scene, Probe { .p = p + step, .registers = registers });
		const double down = sceneDistance(scene, Probe { .p = p - step, .registers = registers });
		const double d    = (up - down) / (2 * h);
		g.x               = axis == 0 ? d : g.x;
		g.y               = axis == 1 ? d : g.y;
		g.z               = axis == 2 ? d : g.z;
	}
	return g;
}

double degreesBetween(const math::Vector& a, const math::Vector& b) {
	const double c = std::abs(math::dot(math::normalized(a), math::normalized(b)));
	return std::acos(std::min(1.0, c)) * 180 / M_PI;
}

void printPercentiles(const char* name, std::vector<double> values) {
	if (values.empty()) {
		return;
	}
	std::sort(values.begin(), values.end());
	auto at = [&](double q) {
		return values[std::min(values.size() - 1, std::size_t(q * double(values.size())))];
	};
	std::printf("    %-34s n %7zu  p50 %8.4f  p90 %8.4f  p99 %8.4f  p99.9 %8.4f  max %8.3f deg\n", name, values.size(), at(0.5), at(0.9), at(0.99), at(0.999), values.back());
}

struct FaceNormal {
	math::Vector gradient;
	double distance;
};

FaceNormal faceNormal(const LeafRun& run, std::uint32_t face, const double* u) {
	double g[3];
	double value = 0;
	for (int axis = 0; axis < 3; ++axis) {
		Leg leg;
		for (int k = 0; k < 3; ++k) {
			leg.base[k] = float(u[k]);
			leg.rate[k] = k == axis ? 1.0f : 0.0f;
		}
		const Quartic q = faceAlong(run, face, leg);
		g[axis]         = q.linear;
		value           = q.constant;
	}
	const math::Vector gradient = { g[0], g[1], g[2] };
	return { gradient, std::abs(value) / std::max(math::length(gradient), 1e-30) };
}

bool facedStop(const Geometry& geometry, const Met& met) {
	return met.kind == MetKind::Surface && passLandedFace(geometry.tree.runs.data() + geometry.tree.surfaces[met.payload].run, met.face, nullptr).exact;
}

void facedStudy(const Geometry& geometry, const RaySet& set) {
	std::size_t surfaces   = 0;
	std::size_t faced      = 0;
	std::size_t leafOff    = 0;
	std::size_t unfaced[9] = {};
	std::size_t solids     = 0;
	std::size_t passings   = 0;
	std::vector<double> angles;
	for (const Ray& ray : set.rays) {
		const Met met = firstMet(geometry, ray);
		solids += met.kind == MetKind::Solid ? 1 : 0;
		passings += met.kind == MetKind::Passing ? 1 : 0;
		if (met.kind != MetKind::Surface) {
			continue;
		}
		surfaces += 1;
		if (!facedStop(geometry, met)) {
			const LeafRun run = surfaceLeafRun(geometry.tree, met.payload);
			const int reason  = met.face < 0 ? 0 : run.header.passWords == 0 ? 1 + int(faceKind(run.header, std::uint32_t(met.face)))
																			 : 5 + int(faceKind(run.header, std::uint32_t(met.face)));
			unfaced[reason] += 1;
			if (std::getenv("KODANUKI_SHOW") != nullptr && reason >= 5 && unfaced[reason] <= 4) {
				const std::uint32_t* w = run.words + 1 + tableWordCount(run.header.count);
				const Hit sdf          = intersect(geometry, ray);
				std::printf("    unfaced %s at %.4f %.4f %.4f: faces %u, face %d, words %08x %08x (paired %d), sdf leaf %u, programs %u\n", set.name.c_str(), sdf.point.x, sdf.point.y, sdf.point.z, run.header.count, met.face, w[0], run.header.passWords > 1 ? w[1] : 0u, int(run.header.passWords), sdf.leaf, geometry.tree.lists[geometry.tree.surfaces[met.payload].programs].count);
			}
			continue;
		}
		faced += 1;
		const Walker walker = { .geometry = geometry, .ray = ray, .descending = descendingOf(ray) };
		const Hit fast      = hitOf(walker, met);
		const Hit sdf       = surfaceHit(hitQueryOf(walker, met), met.payload);
		if (fast.leaf != sdf.leaf && leafOff < 6) {
			const LeafRun run = surfaceLeafRun(geometry.tree, met.payload);
			std::printf("    leaf %u vs sdf %u (other %u share %.3f) at %.4f %.4f %.4f; programs %u, faces %u, face %d kind %d, side %.2e, inside %d\n", fast.leaf, sdf.leaf, sdf.other, sdf.share, fast.point.x, fast.point.y, fast.point.z, geometry.tree.lists[geometry.tree.surfaces[met.payload].programs].count, run.header.count, met.face, int(faceKind(run.header, std::uint32_t(met.face))), met.side, int(ray.inside));
		}
		leafOff += fast.leaf != sdf.leaf ? 1 : 0;
		angles.push_back(std::acos(std::clamp(math::dot(fast.normal, sdf.normal), -1.0, 1.0)) * 180 / M_PI);
	}
	std::printf("  faced %s: %zu of %zu surface stops (%.1f%%) skip the SDF; leaf differs on %zu; solid stops %zu, passing stops %zu\n", set.name.c_str(), faced, surfaces, 100.0 * double(faced) / double(std::max<std::size_t>(surfaces, 1)), leafOff, solids, passings);
	printPercentiles("normal against the SDF's", angles);
	std::printf("    unfaced: no face %zu; undescribed run, landing on plane %zu quadric %zu patch %zu ring %zu; described run, face not exact: plane %zu quadric %zu patch %zu ring %zu\n", unfaced[0], unfaced[1], unfaced[2], unfaced[3], unfaced[4], unfaced[5], unfaced[6], unfaced[7], unfaced[8]);
}

void normalStudy(const Geometry& geometry, const RaySet& set) {
	std::vector<double> byKind[4];
	const char* names[4] = { "plane face", "quadric face", "patch face", "ring face" };
	std::vector<double> nearWall[2];
	std::vector<double> landed;
	for (std::size_t i = 0; i < set.rays.size(); i += 2) {
		const Hit hit = intersect(geometry, set.rays[i]);
		if (hit.kind != HitKind::Surface || hit.share > 0) {
			continue;
		}
		const CellBox box = cellOf(geometry, hit.point);
		if (!box.found || box.surface != hit.cell) {
			continue;
		}
		double u[3];
		double wall = 1;
		for (int axis = 0; axis < 3; ++axis) {
			u[axis] = std::clamp((math::component(hit.point, axis) - box.lower[axis]) / box.size, 0.0, 1.0);
			wall    = std::min({ wall, u[axis], 1 - u[axis] });
		}
		const LeafRun run  = surfaceLeafRun(geometry.tree, hit.cell);
		std::uint32_t best = 0;
		FaceNormal chosen  = faceNormal(run, 0, u);
		for (std::uint32_t f = 1; f < run.header.count; ++f) {
			const FaceNormal other = faceNormal(run, f, u);
			if (other.distance < chosen.distance) {
				chosen = other;
				best   = f;
			}
		}
		const double error = degreesBetween(chosen.gradient, hit.normal);
		byKind[int(faceKind(run.header, best))].push_back(error);
		landed.push_back(chosen.distance * box.size / std::max(hit.loosened, 1e-12));
		if (faceKind(run.header, best) != FaceKind::Plane) {
			nearWall[wall < 0.1 ? 0 : 1].push_back(error);
		}
	}
	std::printf("  normal study %s: the landing face's gradient against the SDF normal\n", set.name.c_str());
	for (int k = 0; k < 4; ++k) {
		printPercentiles(names[k], byKind[k]);
	}
	printPercentiles("curved, hit within 0.1 of a wall", nearWall[0]);
	printPercentiles("curved, hit inside", nearWall[1]);
	printPercentiles("(landing face's distance, tol)", landed);
}

bool sameDouble(double a, double b) {
	return std::memcmp(&a, &b, sizeof(double)) == 0;
}

bool sameHit(const Hit& a, const Hit& b) {
	return a.kind == b.kind && sameDouble(a.distance, b.distance) && sameDouble(a.normal.x, b.normal.x) && sameDouble(a.normal.y, b.normal.y) && sameDouble(a.normal.z, b.normal.z) && a.leaf == b.leaf && a.other == b.other && sameDouble(a.share, b.share);
}

// Compares the packet hits of a ray set against the scalar ones (--compare).
void compareHits(const Geometry& geometry, const RaySet& set, const std::vector<Packet>& packets) {
	std::size_t packetDiffer = 0, packetCount = 0;
	const WaveTricks tricks = waveTricks();
	for (const Packet& packet : packets) {
		const PacketMet met                   = walkedPacket(geometry, packet, tricks);
		const PacketAnswer answer             = { .met = met, .tricks = tricks };
		const std::array<Hit, packetWidth> hs = packetHits(geometry, packet, answer);
		for (int lane = 0; lane < packetWidth; ++lane) {
			if (((packet.present >> lane) & 1) == 0) {
				continue;
			}
			const Hit scalar = intersect(geometry, packet.rays[std::size_t(lane)]);
			packetCount += 1;
			packetDiffer += sameHit(scalar, hs[std::size_t(lane)]) || packet.kind == RayKind::Shadow ? 0 : 1;
		}
	}
	std::printf("  %-7s packet != scalar %zu of %zu\n", set.name.c_str(), packetDiffer, packetCount);
}

// Times hitOf alone on the stops of a ray set's walk, for profiling the hit (--hits n).
void timedHits(const Geometry& geometry, const RaySet& set, int rounds) {
	std::vector<Met> mets;
	for (const Ray& ray : set.rays) {
		mets.push_back(firstMet(geometry, ray));
	}
	std::uint64_t hash            = 0xCBF29CE484222325ull;
	const Clock::time_point start = Clock::now();
	const std::uint64_t counted0  = counter.now();
	for (int r = 0; r < rounds; ++r) {
		for (std::size_t i = 0; i < set.rays.size(); ++i) {
			const Walker walker = {
				.geometry   = geometry,
				.ray        = set.rays[i],
				.descending = descendingOf(set.rays[i]),
			};

			const Hit hit = hitOf(walker, mets[i]);
			hash          = r == 0 ? hitHash(hash, hit) : hash ^ std::uint64_t(hit.kind);
		}
	}
	const double count = double(set.rays.size()) * rounds;
	std::printf("  %-7s hits alone: %.3f Mrays/s, %.0f instructions/ray  %016llx\n", set.name.c_str(), count / secondsSince(start) * 1e-6, double(counter.now() - counted0) / count, static_cast<unsigned long long>(hash));
}

int main(int argc, char** argv) {
	if (argc < 2) {
		std::fprintf(stderr, "usage: kodanuki_walk_bench scene.sdf [--size w h] [--rounds n] [--threads n]\n");
		return 1;
	}
	SceneFile file  = sceneOrExit(argv[1]);
	Choices choices = defaultChoices();
	int rounds      = 3;
	bool study      = false;
	bool compare    = false;
	int hitRounds   = 0;
	std::string phase;
	choices.threads = 3;
	for (int i = 2; i < argc; ++i) {
		const std::string flag = argv[i];
		if (flag == "--size") {
			file.shot.render.width  = std::atoi(argv[++i]);
			file.shot.render.height = std::atoi(argv[++i]);
		}
		if (flag == "--normals") {
			study = true;
		}
		if (flag == "--rounds") {
			rounds = std::atoi(argv[++i]);
		}
		if (flag == "--hits") {
			hitRounds = std::atoi(argv[++i]);
		}
		if (flag == "--compare") {
			compare = true;
		}
		if (flag == "--phase") {
			phase = argv[++i];
		}
		if (flag == "--threads") {
			choices.threads = std::atoi(argv[++i]);
		}
	}
	const Clock::time_point built0 = Clock::now();
	const Features features        = featuresOf(featureQueryOf(file, choices.threads));
	const BuiltGeometry built      = builtGeometry(file, choices, features);
	const double buildSeconds      = secondsSince(built0);

	const Geometry geometry = {
		.scene = file.scene,
		.tree  = built.tree,
	};

	const Lens lens       = lensOf(file.shot.camera, file.shot.render);
	std::size_t described = 0;
	for (const SurfaceLeaf& leaf : built.tree.surfaces) {
		described += runHeader(built.tree.runs[leaf.run]).passWords;
	}
	std::printf("pass words %zu (%.3f MB) in a tree of %.3f MB (%zu surface leaves)\n", described, double(described) * 4 / 1048576.0, treeStats(built.tree).megabytes, built.tree.surfaces.size());
	std::printf("%s %dx%d build %.2f s\n", argv[1], lens.width, lens.height, buildSeconds);
	std::uint64_t all = 0;
	// ONLY=mask times the hit (1), the packet walk (2) and the packet hits (4) beside the walk, or not the walk (8).
	const char* only = std::getenv("ONLY");
	const int timed  = only == nullptr ? 7 : std::atoi(only);
	for (const RaySet& set : raySets(geometry, lens, file.looks)) {
		const std::vector<Packet> packets = packetsOf(set);
		const double count                = double(set.rays.size());
		if (set.rays.empty()) {
			continue;
		}
		if (hitRounds > 0) {
			timedHits(geometry, set, hitRounds);
			continue;
		}
		if (compare) {
			compareHits(geometry, set, packets);
			continue;
		}
		if (study) {
			facedStudy(geometry, set);
			continue;
		}
		const bool scalar      = phase != "packet";
		const bool packed      = phase != "walk";
		const Timed walk       = timedWalk(geometry, set, scalar && (timed & 8) == 0 ? rounds : 0);
		const Timed hit        = timedIntersect(geometry, set, scalar && phase != "walk" && (timed & 1) != 0 ? rounds : 0);
		const Timed packetWalk = timedPackets(geometry, packets, packed && (timed & 2) != 0 ? rounds : 0, false);
		const Timed packetHit  = timedPackets(geometry, packets, packed && phase != "packet" && (timed & 4) != 0 ? rounds : 0, true);
		std::printf("  %-7s %7zu rays: walk %6.3f  +hit %6.3f  packet walk %6.3f  +hit %6.3f Mrays/s (%zu packets)  %016llx %016llx\n", set.name.c_str(), set.rays.size(), count / walk.seconds * 1e-6, count / hit.seconds * 1e-6, count / packetWalk.seconds * 1e-6, count / packetHit.seconds * 1e-6, packets.size(), static_cast<unsigned long long>(hit.hash ^ walk.hash), static_cast<unsigned long long>(packetHit.hash ^ packetWalk.hash));
		std::printf("  %-7s instructions per ray: walk %7.0f  +hit %7.0f  packet walk %7.0f  +hit %7.0f\n", set.name.c_str(), walk.instructions / count, hit.instructions / count, packetWalk.instructions / count, packetHit.instructions / count);
		std::printf("  %-7s cycles per ray:       walk %7.0f  +hit %7.0f  packet walk %7.0f  +hit %7.0f\n", set.name.c_str(), walk.cycles / count, hit.cycles / count, packetWalk.cycles / count, packetHit.cycles / count);
		all = mixed(all, &hit.hash, 8);
		all = mixed(all, &walk.hash, 8);
	}
	std::printf("checksum %016llx\n", static_cast<unsigned long long>(all));
	return 0;
}
