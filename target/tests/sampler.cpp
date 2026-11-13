#include "plugins/transport/paths/sampler.h"
#include "check.h"
#include "core/frame.h"
#include "core/plugin.h"
#include "plugins/sampler/highlight_guide/survey.h"
#include "plugins/sampler/sobol/sobol.h"
#include "plugins/transport/paths/feature.h"
#include "plugins/transport/paths/paths.h"
#include "syntax/evaluator.h"

#include <array>
#include <cmath>
#include <cstdio>
#include <random>
#include <string>
#include <vector>

namespace kodanuki {

constexpr int pairSeeds   = 8;
constexpr int pairThreads = 4;

struct Arm {
	const SceneFile& file;
	const Tree& tree;
	const Features& features;
	const ChosenMembers& prepared;
	const std::vector<Plugin>& preparations;
	std::string execution;
};

struct Spread {
	std::array<double, 3> mean;
	std::array<double, 3> error;
	Image first;
};

struct Pairing {
	const char* what;
	std::string text;
	ChosenMembers off;
	ChosenMembers on;
	std::vector<Plugin> beforehand;
	std::vector<Plugin> preparations;
};

struct SphereBins {
	int rows;
	int columns;
};

struct MirrorCase {
	const char* what;
	MirrorBalls mirrors;
	LampPoint point;
};

Image armFrame(const Arm& arm, int seed) {
	Shot shot        = arm.file.shot;
	shot.render.seed = seed;

	const Geometry geometry = treeGeometry(arm.file.scene, arm.tree);

	const Lens lens           = lensOf(shot.camera, shot.render);
	const Observing observing = noObserving();

	const PathsWorld paths = {
		.features    = arm.features,
		.smoothGlass = SmoothGlass::Exact,
		.sampling    = arm.prepared.sampling,
		.strategies  = arm.prepared.strategies,
	};

	const FrameInput input = {
		.geometry       = geometry,
		.looks          = arm.file.looks,
		.lighting       = arm.file.lighting,
		.shot           = shot,
		.lens           = lens,
		.transport      = testPlugin("transport/paths"),
		.transportWorld = &paths,
		.preparations   = arm.preparations,
		.stopping       = testPlugin("stopping/fixed_count"),
		.perFrame       = 0,
		.execution      = arm.execution,
		.threads        = pairThreads,
		.observing      = observing,
	};

	return stillFrame(input).image;
}

std::array<double, 3> channelMeans(const Image& image) {
	std::array<double, 3> sums = { 0, 0, 0 };
	for (std::size_t k = 0; k < image.rgb.size(); ++k) {
		sums[k % 3] += double(image.rgb[k]);
	}
	const double pixels = double(image.width * image.height);
	return { sums[0] / pixels, sums[1] / pixels, sums[2] / pixels };
}

Spread armSpread(const Arm& arm) {
	std::vector<std::array<double, 3>> means;

	Spread spread = {
		.mean  = { 0, 0, 0 },
		.error = { 0, 0, 0 },
		.first = armFrame(arm, 1),
	};

	means.push_back(channelMeans(spread.first));
	for (int seed = 2; seed <= pairSeeds; ++seed) {
		means.push_back(channelMeans(armFrame(arm, seed)));
	}
	for (int c = 0; c < 3; ++c) {
		double sum     = 0;
		double squares = 0;
		for (const std::array<double, 3>& mean : means) {
			sum += mean[c];
			squares += mean[c] * mean[c];
		}
		const double n  = double(means.size());
		spread.mean[c]  = sum / n;
		spread.error[c] = std::sqrt(std::max(squares / n - spread.mean[c] * spread.mean[c], 0.0) / (n - 1));
	}
	return spread;
}

void checkPairing(const Pairing& pairing) {
	const SceneFile file    = sceneOrExit(writeScene("sampler", pairing.text.c_str()));
	const TestTree tree     = testTree(file, pairThreads);
	const Features features = featuresOf(featureQueryOf(file, pairThreads));

	const Arm off = {
		.file         = file,
		.tree         = tree.tree,
		.features     = features,
		.prepared     = pairing.off,
		.preparations = pairing.beforehand,
		.execution    = "execution/packets",
	};

	const Arm on = {
		.file         = file,
		.tree         = tree.tree,
		.features     = features,
		.prepared     = pairing.on,
		.preparations = pairing.preparations,
		.execution    = "execution/packets",
	};

	const Arm scalar = {
		.file         = file,
		.tree         = tree.tree,
		.features     = features,
		.prepared     = pairing.on,
		.preparations = pairing.preparations,
		.execution    = "execution/single",
	};

	const Spread a = armSpread(off);
	const Spread b = armSpread(on);
	double worst   = 0;
	for (int c = 0; c < 3; ++c) {
		const double combined = std::sqrt(a.error[c] * a.error[c] + b.error[c] * b.error[c]);
		worst                 = std::max(worst, std::abs(a.mean[c] - b.mean[c]) / std::max(combined, 1e-12));
	}
	std::printf("pairing %s: off %.5f %.5f %.5f (error %.1e), on %.5f %.5f %.5f (error %.1e), worst gap %.2f standard errors\n", pairing.what, a.mean[0], a.mean[1], a.mean[2], a.error[1], b.mean[0], b.mean[1], b.mean[2], b.error[1], worst);
	check(worst < 4, "a Sampler or Strategy member leaves the mean unchanged");
	check(a.first.rgb != b.first.rgb, "a Sampler or Strategy member changes the frame");
	check(armFrame(scalar, 1).rgb == b.first.rgb, "waves draw a Sampler or Strategy member's frame of one path at a time to the bit");
}

const char* pairStart = R"(
	render {
		width     = 64;
		height    = 40;
		tolerance = 0;
		samples   = 16;
	}

	function sphere(r) {
		return length(p) - r;
	}

	function box(size) {
		let q = abs(p) - size / 2;
		return length(max(q, 0)) + min(max(q.x, max(q.y, q.z)), 0);
	}

	camera {
		eye    = [0, -4, 1.5];
		target = [0, 0, 0.4];
		fov    = 0.7;
	}

)";

std::string lampsText() {
	return std::string(pairStart) + R"(
		sky gradient {
			zenith  = [0.2, 0.3, 0.5];
			horizon = [0.5, 0.5, 0.5];
			ground  = [0.1, 0.1, 0.1];
		}

		light sphere {
			at     = [1.2, -1, 2];
			radius = 0.6;
			power  = [3, 3, 2.5];
		}

		light quad {
			at     = [-2, -1, 0.2];
			along  = [0, 1.5, 0];
			across = [0, 0, 1.5];
			power  = [2, 2, 3];
		}

		light point {
			at    = [0, 1, 2.5];
			power = [1, 0.8, 0.6];
		}

		paint(matte([0.6, 0.6, 0.6])) at([0, 0, -0.1]) box([6, 6, 0.2]);
		paint(plastic([0.8, 0.2, 0.1], roughness = 0.3)) at([-0.6, 0, 0.5]) sphere(0.5);
		paint(metal([0.9, 0.8, 0.5], roughness = 0.4)) at([0.6, 0.3, 0.4]) sphere(0.4);
	)";
}

std::string skyText() {
	return std::string(pairStart) + R"(
		sky gradient {
			zenith  = [0.3, 0.5, 1.2];
			horizon = [0.9, 0.8, 0.6];
			ground  = [0.05, 0.05, 0.05];
		}

		paint(matte([0.6, 0.6, 0.6])) at([0, 0, -0.1]) box([6, 6, 0.2]);
		paint(matte([0.7, 0.5, 0.3])) at([0, 0, 0.5]) sphere(0.5);
	)";
}

std::string featuredSkyText() {
	return std::string(pairStart) + R"(
		sky physical {
			haze     = 1.3;
			ground   = [0.28, 0.30, 0.26];
			glow     = [1, 0.8, 0.55];
			strength = 0.5;
			spread   = 0.12;
		}

		clouds {
			height    = 1500;
			thickness = 800;
			cover     = 0.5;
			density   = 0.03;
			span      = 2500;
		}

		light sun {
			direction = [-0.56, -0.75, 0.34];
			power     = [3, 3, 3];
		}

		paint(matte([0.6, 0.6, 0.6])) at([0, 0, -0.1]) box([6, 6, 0.2]);
		paint(matte([0.7, 0.5, 0.3])) at([0, 0, 0.5]) sphere(0.5);
	)";
}

std::string paintedSkyText() {
	return std::string(pairStart) + R"(
		sky painted {
			glow     = [1, 0.8, 0.55];
			strength = 0.5;
		}

		stars {
			count    = 6000;
			strength = 0.8;
			angle    = 0.02;
		}

		clouds {
			height    = 1500;
			thickness = 800;
			cover     = 0.5;
			density   = 0.03;
			span      = 2500;
		}

		light sun {
			direction = [-0.49, -0.86, -0.12];
			power     = [3, 3, 3];
		}

		light moon {
			direction = [0.3, 0.8, 0.5];
			angle     = 0.1;
			power     = [2, 2, 2];
		}

		paint(matte([0.6, 0.6, 0.6])) at([0, 0, -0.1]) box([6, 6, 0.2]);
		paint(matte([0.7, 0.5, 0.3])) at([0, 0, 0.5]) sphere(0.5);
	)";
}

std::string glowText() {
	return std::string(pairStart) + R"(
		sky gradient {
			zenith  = [0, 0, 0];
			horizon = [0, 0, 0];
			ground  = [0, 0, 0];
		}

		paint(matte([0.6, 0.6, 0.6])) at([0, 0, -0.1]) box([6, 6, 0.2]);
		paint(matte(#000000, glow = #FFD0A0, strength = 4)) at([0.3, 0.6, 1.2]) box([1.2, 0.6, 0.1]);
		paint(matte([0.5, 0.6, 0.7])) at([-0.4, 0, 0.35]) sphere(0.35);
	)";
}

std::string guideText() {
	return std::string(pairStart) + R"(
		sky gradient {
			zenith  = [0.05, 0.05, 0.08];
			horizon = [0.05, 0.05, 0.05];
			ground  = [0.02, 0.02, 0.02];
		}

		light sphere {
			at     = [-1.2, 0.8, 1.6];
			radius = 0.25;
			power  = [3, 3, 2.5];
		}

		paint(plastic([0.6, 0.6, 0.6], roughness = 0.5)) at([0, 0, -0.1]) box([6, 6, 0.2]);
		paint(metal([0.9, 0.9, 0.9], roughness = 0.03)) at([0.3, 0.4, 0.5]) sphere(0.5);
	)";
}

ChosenMembers withSampling(const Sampling& sampling) {
	ChosenMembers prepared = coreChosen();
	prepared.sampling      = sampling;
	return prepared;
}

Sampling glowSampling() {
	Sampling sampling = coreSampling();
	sampling.aims     = { SamplerMember::Lamps, SamplerMember::GlowingSurfaces };
	return sampling;
}

Sampling skySampling() {
	Sampling sampling = coreSampling();
	sampling.aims     = { SamplerMember::Lamps, SamplerMember::SkyMap };
	return sampling;
}

Sampling sobolSampling() {
	Sampling sampling = coreSampling();
	sampling.sequence = SamplerMember::Sobol;
	return sampling;
}

Sampling guideSampling() {
	Sampling sampling  = coreSampling();
	sampling.direction = SamplerMember::HighlightGuide;
	return sampling;
}

Sampling eachLampSampling() {
	Sampling sampling  = coreSampling();
	sampling.selection = SamplerMember::EachLampFirst;
	return sampling;
}

bool sobolNet(std::uint64_t stream, std::uint64_t dimension, int k) {
	const std::uint32_t count = std::uint32_t(1) << k;
	for (int a = 0; a <= k; ++a) {
		std::vector<int> held(count, 0);
		for (std::uint32_t i = 0; i < count; ++i) {
			const math::Key first = {
				.stream     = stream,
				.index      = i,
				.coordinate = dimension,
			};

			math::Key second      = first;
			second.coordinate     = dimension + 1;
			const std::uint32_t x = std::uint32_t(sobolNumber(first) * double(1u << a));
			const std::uint32_t y = std::uint32_t(sobolNumber(second) * double(1u << (k - a)));
			held[(y << a) + x] += 1;
		}
		for (const int one : held) {
			if (one != 1) {
				return false;
			}
		}
	}
	return true;
}

bool sobolEven(std::uint64_t stream, std::uint64_t dimension, int k) {
	const std::uint32_t count = std::uint32_t(1) << k;
	std::vector<int> held(count, 0);
	for (std::uint32_t i = 0; i < count; ++i) {
		const math::Key key = {
			.stream     = stream,
			.index      = i,
			.coordinate = dimension,
		};

		held[std::uint32_t(sobolNumber(key) * double(count))] += 1;
	}
	return std::all_of(held.begin(), held.end(), [](int one) {
		return one == 1;
	});
}

void checkSobolNets() {
	bool nets = true;
	bool even = true;
	for (const std::uint64_t stream : { 0x0123456789ABCDEFULL, 0xFEDCBA9876543210ULL, 0x5555AAAA33331111ULL }) {
		for (int k = 1; k <= 8; ++k) {
			nets = nets && sobolNet(stream, 0, k) && sobolNet(stream, sharedCoordinates, k);
		}
		for (std::uint64_t dimension = 0; dimension < sobolDimensions; ++dimension) {
			for (int k = 1; k <= 8; ++k) {
				even = even && sobolEven(stream, dimension, k);
			}
		}
	}
	check(nets, "every power-of-two prefix of the first Sobol pair of the shared and of the own coordinates holds one point per elementary interval");
	check(even, "every power-of-two prefix of every Sobol coordinate holds one point per interval");
}

math::Vector binDirection(const SphereBins& bins, int bin, const math::UnitPair& within) {
	const int row       = bin / bins.columns;
	const int column    = bin % bins.columns;
	const double height = -1 + 2 * (row + within.u) / bins.rows;
	const double angle  = 2 * math::pi * (column + within.v) / bins.columns;
	const double ring   = std::sqrt(std::max(1 - height * height, 0.0));
	return { ring * std::cos(angle), ring * std::sin(angle), height };
}

int binOf(const SphereBins& bins, const math::Vector& direction) {
	const double angle = std::atan2(direction.y, direction.x);
	const double turn  = angle < 0 ? angle + 2 * math::pi : angle;
	const int row      = std::min(int((direction.z + 1) / 2 * bins.rows), bins.rows - 1);
	const int column   = std::min(int(turn / (2 * math::pi) * bins.columns), bins.columns - 1);
	return row * bins.columns + column;
}

double binMass(const MirrorCase& tested, const SphereBins& bins, int bin) {
	const int steps   = 32;
	const double area = 4 * math::pi / (bins.rows * bins.columns);
	double sum        = 0;
	for (int k = 0; k < steps * steps; ++k) {
		const math::UnitPair within = { (k % steps + 0.5) / steps, (k / steps + 0.5) / steps };

		const MirrorAsk ask = {
			.point     = tested.point,
			.direction = binDirection(bins, bin, within),
		};

		sum += mirrorDensity(tested.mirrors, ask);
	}
	return sum / (steps * steps) * area;
}

void checkMirrorCase(const MirrorCase& tested) {
	const SphereBins bins = {
		.rows    = 32,
		.columns = 64,
	};

	const int draws = 1000000;
	std::vector<double> counts(bins.rows * bins.columns, 0);
	std::mt19937_64 random(7);
	std::uniform_real_distribution<double> unit(0, 1);
	int found = 0;
	for (int k = 0; k < draws; ++k) {
		const MirrorDraw draw = {
			.point = tested.point,
			.pick  = unit(random),
			.pair  = { unit(random), unit(random) },
		};

		const MirrorDirection drawn = mirrorDirection(tested.mirrors, draw);
		found += drawn.found ? 1 : 0;
		counts[binOf(bins, drawn.direction)] += drawn.found ? 1 : 0;
	}
	double chi       = 0;
	double total     = 0;
	double pooled[2] = { 0, 0 };
	int degrees      = 0;
	for (int bin = 0; bin < bins.rows * bins.columns; ++bin) {
		const double expected = binMass(tested, bins, bin) * draws;
		total += expected;
		if (expected < 5) {
			pooled[0] += counts[bin];
			pooled[1] += expected;
			continue;
		}
		chi += (counts[bin] - expected) * (counts[bin] - expected) / expected;
		degrees += 1;
	}
	chi += pooled[1] > 0 ? (pooled[0] - pooled[1]) * (pooled[0] - pooled[1]) / pooled[1] : pooled[0];
	const double bound = degrees + 3.09 * std::sqrt(2.0 * degrees) + 10;
	std::printf("sampler toward mirrors %s: chi squared %.1f over %d bins (bound %.1f), density integral %.4f, drawn share %.4f\n", tested.what, chi, degrees, bound, total / draws, double(found) / draws);
	check(chi < bound, "toward mirrors draws by the density it reports");
	checkNear(Near { total / draws, double(found) / draws, 0.01, "toward mirrors' density integrates to the share of draws found" });
}

void checkMirrorDensity() {
	const Ball above = {
		.middle = { 0, 0, 2 },
		.radius = 0.8,
	};

	const Ball overlapping = {
		.middle = { 0.6, 0.3, 1.6 },
		.radius = 0.5,
	};

	const Ball level = {
		.middle = { 2, 0, 0.1 },
		.radius = 0.6,
	};

	const MirrorBalls mirrors = {
		.balls = { above, overlapping, level },
	};

	const LampPoint face = {
		.light   = 0,
		.point   = { 0, 0, 0 },
		.normal  = { 0, 0, 1 },
		.density = 1,
	};

	const LampPoint round = {
		.light   = 0,
		.point   = { 0.1, 0.1, 1.9 },
		.normal  = { 0, 0, 0 },
		.density = 1,
	};

	const MirrorCase fromFace = {
		.what    = "from a lamp's face",
		.mirrors = mirrors,
		.point   = face,
	};

	const MirrorCase fromInside = {
		.what    = "from a point light inside a ball",
		.mirrors = mirrors,
		.point   = round,
	};

	checkMirrorCase(fromFace);
	checkMirrorCase(fromInside);
}

void checkGathered() {
	const SceneFile file = sceneOrExit(writeScene("gathered", glowText().c_str()));
	const TestTree tree  = testTree(file, pairThreads);

	const Geometry geometry = treeGeometry(file.scene, tree.tree);

	const SceneFile sky       = sceneOrExit(writeScene("charted", skyText().c_str()));
	const math::Vector middle = { 0, 0, 0 };
	const std::size_t grid    = gatheredGlowGrid(geometry, file.looks, 6).places.size();
	std::printf("sampler glow cells: %zu in the grid\n", grid);
	check(grid > 0, "a glowing slab gathers glow cells");
	const Lights skyLights     = lightsOf(sky.lighting);
	const Features skyFeatures = featuresOf(featureQueryOf(sky, 1));

	const SkyChartQuery chart = {
		.lights   = skyLights,
		.features = skyFeatures,
		.middle   = middle,
	};

	check(chartedSky(chart).charted, "a sky with no lamps is charted");
}

Lump testLump(const math::Vector& toward, const std::array<double, 3>& factor, double weight) {
	const math::Vector axis = math::normalized(toward);
	const math::Basis basis = math::basisAround(axis);
	return Lump {
		.axis      = axis,
		.across    = basis.tangent,
		.up        = basis.bitangent,
		.mean      = { 0.03, -0.02 },
		.precision = { 0, 0, 0 },
		.factor    = { float(factor[0]), float(factor[1]), float(factor[2]) },
		.height    = 0,
		.weight    = float(weight),
		.point     = axis * 2,
	};
}

SeenLumps testLumps() {
	Guide guide                        = noGuide();
	guide.live                         = true;
	guide.material                     = 0;
	guide.count                        = 2;
	guide.share                        = 0.5;
	const math::Vector first           = { 0.3, 0.1, 0.9 };
	const math::Vector second          = { -0.5, 0.2, 0.7 };
	const std::array<double, 3> narrow = { 0.08, 0.02, 0.06 };
	const std::array<double, 3> wide   = { 0.15, 0, 0.1 };
	guide.lumps[0]                     = testLump(first, narrow, 2);
	guide.lumps[1]                     = testLump(second, wide, 1);
	std::vector<PackedLump> packed;
	const PackedGuide head = packedGuide(guide, 0, packed);
	const math::Vector off = { 0.1, -0.2, 0 };
	return seenLumps(unpackedGuide(head, packed.data()), off);
}

math::Vector lumpOutgoing() {
	const math::Vector outgoing = { 0.2, -0.3, 0.9 };
	return math::normalized(outgoing);
}

double lumpBinMass(const SeenLumps& lumps, const SphereBins& bins, int bin) {
	const int steps   = 32;
	const double area = 4 * math::pi / (bins.rows * bins.columns);
	double sum        = 0;
	for (int k = 0; k < steps * steps; ++k) {
		const math::UnitPair within = { (k % steps + 0.5) / steps, (k / steps + 0.5) / steps };

		const LumpPair pair = {
			.normal   = { 0, 0, 1 },
			.outgoing = lumpOutgoing(),
			.incoming = binDirection(bins, bin, within),
		};

		sum += lumpDensity(lumps, pair);
	}
	return sum / (steps * steps) * area;
}

void checkLumpDensity() {
	const SeenLumps lumps = testLumps();

	const SphereBins bins = {
		.rows    = 32,
		.columns = 64,
	};

	const int draws = 1000000;
	std::vector<double> counts(bins.rows * bins.columns, 0);
	std::mt19937_64 random(11);
	std::uniform_real_distribution<double> unit(0, 1);
	int found = 0;
	for (int k = 0; k < draws; ++k) {
		const LumpQuery query = {
			.normal   = { 0, 0, 1 },
			.outgoing = lumpOutgoing(),
			.pick     = unit(random),
			.pair     = { unit(random), unit(random) },
		};

		const LumpDraw drawn = lumpDirection(lumps, query);
		found += drawn.found ? 1 : 0;
		counts[binOf(bins, drawn.incoming)] += drawn.found ? 1 : 0;
	}
	double chi       = 0;
	double total     = 0;
	double pooled[2] = { 0, 0 };
	int degrees      = 0;
	for (int bin = 0; bin < bins.rows * bins.columns; ++bin) {
		const double expected = lumpBinMass(lumps, bins, bin) * draws;
		total += expected;
		if (expected < 5) {
			pooled[0] += counts[bin];
			pooled[1] += expected;
			continue;
		}
		chi += (counts[bin] - expected) * (counts[bin] - expected) / expected;
		degrees += 1;
	}
	chi += pooled[1] > 0 ? (pooled[0] - pooled[1]) * (pooled[0] - pooled[1]) / pooled[1] : pooled[0];
	const double bound = degrees + 3.09 * std::sqrt(2.0 * degrees) + 10;
	std::printf("sampler highlight guide lumps: chi squared %.1f over %d bins (bound %.1f), density integral %.4f, drawn share %.4f\n", chi, degrees, bound, total / draws, double(found) / draws);
	check(degrees > 20 && chi < bound, "the guide's lumps draw by the density they report");
	checkNear(Near { total / draws, double(found) / draws, 0.01, "the lumps' density integrates to the share of draws found" });
}

void checkGuided() {
	const SceneFile file      = sceneOrExit(writeScene("guided", guideText().c_str()));
	const TestTree tree       = testTree(file, pairThreads);
	const Materials materials = materialsOf(file.looks, SmoothGlass::Exact);
	const Lights lights       = lightsOf(file.lighting);
	const Lens lens           = lensOf(file.shot.camera, file.shot.render);
	const Sampling sampling   = guideSampling();
	const Strategies chosen   = coreStrategies();
	const Features features   = featuresOf(featureQueryOf(file, 1));

	const Geometry geometry = treeGeometry(file.scene, tree.tree);

	const Tracing tracing = {
		.geometry   = geometry,
		.scene      = file.scene,
		.looks      = file.looks,
		.materials  = materials,
		.lights     = lights,
		.lens       = lens,
		.sampling   = sampling,
		.strategies = chosen,
		.features   = features,
		.limits     = transportLimits(file.shot.render),
		.seed       = 1,
		.film       = stillFilm(),
		.recording  = false,
	};

	const GuideFrame frame = {
		.tracing = tracing,
		.tunings = defaultGuideTunings(),
		.threads = pairThreads,
	};

	const HighlightSurvey survey = surveyOf(frame);
	const HighlightGuides guides = pixelGuides(frame, survey);
	std::size_t guided           = 0;
	for (const GuideRow& row : guides.rows) {
		guided += row.heads.size();
	}
	std::printf("sampler highlight guide: %zu of %d pixels guided, %zu broad survey cells filed\n", guided, lens.width * lens.height, survey.filed.size());
	check(guided >= 50 && !survey.filed.empty(), "a floor seeing a lamp in a polished ball is guided");
}

std::string causticText() {
	return std::string(pairStart) + R"(
		sky gradient {
			zenith  = [0.02, 0.02, 0.03];
			horizon = [0.02, 0.02, 0.02];
			ground  = [0.01, 0.01, 0.01];
		}

		light sphere {
			at     = [0.2, 0.6, 2.2];
			radius = 0.15;
			power  = [6, 6, 5];
		}

		paint(matte([0.7, 0.7, 0.7])) at([0, 0, -0.1]) box([6, 6, 0.2]);
		paint(metal([0.95, 0.9, 0.8], roughness = 0.05)) at([0.7, 0.4, 0.45]) sphere(0.45);
		paint(glass(refraction = 1.5, roughness = 0)) at([-0.5, 0.1, 0.4]) sphere(0.4);
	)";
}

std::string mediumText(const std::string& material) {
	return std::string(pairStart) + R"(
		sky gradient {
			zenith  = [0.02, 0.02, 0.03];
			horizon = [0.02, 0.02, 0.02];
			ground  = [0.01, 0.01, 0.01];
		}

		light sphere {
			at     = [0.2, 0.6, 2.2];
			radius = 0.15;
			power  = [6, 6, 5];
		}

		paint(matte([0.7, 0.7, 0.7])) at([0, 0, -0.1]) box([6, 6, 0.2]);
		paint()" +
		   material + R"() at([-0.2, 0.1, 0.5]) sphere(0.5);
	)";
}

std::string roomText() {
	return R"(
		render {
			width     = 64;
			height    = 40;
			tolerance = 0;
			samples   = 16;
		}

		function sphere(r) {
			return length(p) - r;
		}

		function box(size) {
			let q = abs(p) - size / 2;
			return length(max(q, 0)) + min(max(q.x, max(q.y, q.z)), 0);
		}

		camera {
			eye    = [0, -1.7, 1.2];
			target = [0, 1, 0.8];
			fov    = 1.1;
		}

		sky gradient {
			zenith  = [0, 0, 0];
			horizon = [0, 0, 0];
			ground  = [0, 0, 0];
		}

		light sphere {
			at     = [-1.2, 1.2, 2.6];
			radius = 0.12;
			power  = [4, 4, 3.5];
		}

		paint(matte([0.7, 0.7, 0.7])) at([0, 0, 1.5]) cut {
			box([4.4, 4.4, 3.4]);
			box([4, 4, 3]);
		}

		paint(plastic([0.2, 0.5, 0.8], roughness = 0.3)) at([0.5, 0.6, 0.4]) sphere(0.4);
		paint(matte([0.8, 0.3, 0.2])) at([-0.6, 0.9, 0.3]) box([0.6, 0.6, 0.6]);
	)";
}

std::string coatsText() {
	return std::string(pairStart) + R"(
		sky gradient {
			zenith  = [0.02, 0.02, 0.03];
			horizon = [0.02, 0.02, 0.02];
			ground  = [0.01, 0.01, 0.01];
		}

		light sphere {
			at     = [0.2, 0.6, 2.2];
			radius = 0.15;
			power  = [6, 6, 5];
		}

		let gilt = metal([0.95, 0.75, 0.35], roughness = 0.6);

		paint(matte([0.7, 0.7, 0.7])) at([0, 0, -0.1]) box([6, 6, 0.2]);
		coat(bump(grain, 0.03, span = 0.05)) coat(polish(1), facing([0, 0, 1], level = 0.2, soft = 0.4)) paint(gilt) at([0.7, 0.4, 0.45]) sphere(0.45);
		coat(rough(0.6), above(0.4, soft = 0.2, ragged = 0.1, grain = 0.1)) paint(glass(refraction = 1.5, absorb = [0.5, 0.2, 0.1], scatter = 2, roughness = 0)) at([-0.5, 0.1, 0.4]) sphere(0.4);
		coat(gilt, below(0.3, soft = 0.1)) coat(glow([1, 0.5, 0.2], strength = 3), inside(sphere(0.65))) paint(plastic([0.2, 0.5, 0.8], roughness = 0.3)) at([0, -0.6, 0.3]) sphere(0.3);
	)";
}

std::string rulesText(const std::string& lamps, const std::string& shapes) {
	return std::string(pairStart) + R"(
		sky gradient {
			zenith  = [0.02, 0.02, 0.03];
			horizon = [0.02, 0.02, 0.02];
			ground  = [0.01, 0.01, 0.01];
		}

		let mirror = metal([0.9, 0.9, 0.9], roughness = 0);
		let lens   = glass(refraction = 1.5, roughness = 0);

		paint(matte([0.7, 0.7, 0.7])) at([0, 0, -0.1]) box([6, 6, 0.2]);
	)" + lamps +
		   shapes;
}

std::string rulesLamp(const std::string& fields) {
	return R"(
		light sphere {
			at     = [0.2, 0.6, 2.2];
			radius = 0.15;
			power  = [6, 6, 5];
	)" + fields +
		   R"(
		}

	)";
}

ChosenMembers withStrategies(const Sampling& sampling, const std::vector<StrategyMember>& members) {
	ChosenMembers prepared      = withSampling(sampling);
	prepared.strategies.members = members;
	return prepared;
}

Sampling mirrorSampling() {
	Sampling sampling = coreSampling();

	const EmissionShare mirrors = {
		.member = SamplerMember::TowardMirrors,
		.share  = 1,
	};

	sampling.emission = { mirrors };
	return sampling;
}

Sampling fullSampling() {
	Sampling sampling  = coreSampling();
	sampling.direction = SamplerMember::HighlightGuide;

	const EmissionShare cosine = {
		.member = SamplerMember::Cosine,
		.share  = 0.5,
	};

	const EmissionShare mirrors = {
		.member = SamplerMember::TowardMirrors,
		.share  = 0.5,
	};

	sampling.emission = { cosine, mirrors };
	return sampling;
}

Sampling guidedMirrorSampling() {
	Sampling sampling  = mirrorSampling();
	sampling.direction = SamplerMember::HighlightGuide;
	return sampling;
}

void checkFeaturePairings(const char* what, const std::string& text) {
	const std::vector<Plugin> none          = {};
	const std::vector<Plugin> passing       = testPreparations({ "sampler/toward_mirrors", "strategy/join" });
	const std::vector<StrategyMember> meet  = { StrategyMember::Meet };
	const std::vector<StrategyMember> every = { StrategyMember::Meet, StrategyMember::Aim, StrategyMember::Join, StrategyMember::Connect };

	const Pairing aimed = {
		.what         = what,
		.text         = text,
		.off          = withStrategies(coreSampling(), meet),
		.on           = coreChosen(),
		.beforehand   = none,
		.preparations = none,
	};

	const Pairing joined = {
		.what         = what,
		.text         = text,
		.off          = coreChosen(),
		.on           = withStrategies(coreSampling(), every),
		.beforehand   = none,
		.preparations = passing,
	};

	checkPairing(aimed);
	checkPairing(joined);
}

void checkRulePairings() {
	const std::string balls = R"(
		paint(mirror) at([0.6, 0.4, 0.35]) sphere(0.35);
		paint(lens) at([-0.5, 0.1, 0.4]) sphere(0.4);
		paint(plastic([0.6, 0.3, 0.2], roughness = 0.4)) at([0.1, 0.9, 0.3]) sphere(0.3);
	)";

	checkFeaturePairings("rules: a hidden pane", rulesText(rulesLamp(""), balls + "rule(camera = 0) paint(matte([0.8, 0.8, 0.8])) at([0, -0.9, 0.4]) box([1.2, 0.04, 0.8]);"));
	checkFeaturePairings("rules: a shadowless ball", rulesText(rulesLamp(""), balls + "rule(shadows = 0) paint(matte([0.6, 0.6, 0.7])) at([0, -0.3, 0.9]) sphere(0.25);"));
	checkFeaturePairings("rules: a shadowless ball over fog", rulesText(rulesLamp(""), balls + "paint(glass(refraction = 1, scatter = 0.5, roughness = 0)) at([0.1, 0.3, 1.2]) box([3, 3, 0.3]); rule(shadows = 0) paint(matte([0.6, 0.6, 0.7])) at([0.15, 0.5, 1.75]) sphere(0.3);"));
	checkFeaturePairings("rules: causticless glass and mirror", rulesText(rulesLamp(""), "with rule(caustics = 0) {" + balls + "}"));
	checkFeaturePairings("rules: a lamp without caustics", rulesText(rulesLamp("caustics = 0;"), balls));
	checkFeaturePairings("rules: a lamp reaching one group", rulesText(rulesLamp("reaches = [stage];"), "group(stage) " + balls));
}

void checkMediumPairings(const char* what, const std::string& material) {
	checkFeaturePairings(what, mediumText(material));
}

void checkStrategyPairings() {
	const std::vector<Plugin> none              = {};
	const std::vector<Plugin> passing           = testPreparations({ "sampler/toward_mirrors", "strategy/join" });
	const std::vector<Plugin> surveyed          = testPreparations({ "sampler/toward_mirrors", "sampler/highlight_guide", "strategy/join" });
	const std::vector<StrategyMember> meet      = { StrategyMember::Meet };
	const std::vector<Plugin> shared            = testPreparations({ "strategy/join" });
	const std::vector<StrategyMember> joined    = { StrategyMember::Meet, StrategyMember::Aim, StrategyMember::Join };
	const std::vector<StrategyMember> aimed     = { StrategyMember::Meet, StrategyMember::Aim };
	const std::vector<StrategyMember> connected = { StrategyMember::Meet, StrategyMember::Aim, StrategyMember::Connect };
	const std::vector<StrategyMember> every     = { StrategyMember::Meet, StrategyMember::Aim, StrategyMember::Join, StrategyMember::Connect };

	const Pairing join = {
		.what         = "Join",
		.text         = causticText(),
		.off          = coreChosen(),
		.on           = withStrategies(coreSampling(), joined),
		.beforehand   = none,
		.preparations = passing,
	};

	const Pairing towardMirrors = {
		.what         = "Join with TowardMirrors",
		.text         = causticText(),
		.off          = withStrategies(coreSampling(), joined),
		.on           = withStrategies(mirrorSampling(), joined),
		.beforehand   = passing,
		.preparations = passing,
	};

	const Pairing guided = {
		.what         = "Join with the guide",
		.text         = causticText(),
		.off          = withStrategies(coreSampling(), meet),
		.on           = withStrategies(guidedMirrorSampling(), joined),
		.beforehand   = none,
		.preparations = surveyed,
	};

	const Pairing connect = {
		.what         = "Connect",
		.text         = roomText(),
		.off          = withStrategies(coreSampling(), aimed),
		.on           = withStrategies(coreSampling(), connected),
		.beforehand   = none,
		.preparations = shared,
	};

	const Pairing all = {
		.what         = "every strategy with the guide",
		.text         = causticText(),
		.off          = withStrategies(coreSampling(), meet),
		.on           = withStrategies(fullSampling(), every),
		.beforehand   = none,
		.preparations = surveyed,
	};

	for (const Pairing& pairing : { join, towardMirrors, guided, connect, all }) {
		checkPairing(pairing);
	}
}

std::string replacedText(const std::string& text, const std::string& from, const std::string& to) {
	std::string replaced = text;
	replaced.replace(replaced.find(from), from.size(), to);
	return replaced;
}

void checkUnguided() {
	const std::string text  = replacedText(guideText(), "roughness = 0.5)", "roughness = 0.5, guided = 0)");
	const SceneFile file    = sceneOrExit(writeScene("unguided", text.c_str()));
	const TestTree tree     = testTree(file, pairThreads);
	const Features features = featuresOf(featureQueryOf(file, pairThreads));

	const ChosenMembers plain        = coreChosen();
	const ChosenMembers steered      = withSampling(guideSampling());
	const std::vector<Plugin> none   = {};
	const std::vector<Plugin> survey = testPreparations({ "sampler/highlight_guide" });

	const Arm core = {
		.file         = file,
		.tree         = tree.tree,
		.features     = features,
		.prepared     = plain,
		.preparations = none,
		.execution    = "execution/packets",
	};

	const Arm guided = {
		.file         = file,
		.tree         = tree.tree,
		.features     = features,
		.prepared     = steered,
		.preparations = survey,
		.execution    = "execution/packets",
	};

	check(armFrame(core, 1).rgb == armFrame(guided, 1).rgb, "the highlight guide leaves a frame whose only guidable floor says guided = 0 as it was");
}

void checkSwitchPairings() {
	checkUnguided();
	const std::vector<Plugin> none           = {};
	const std::vector<Plugin> mirror         = testPreparations({ "sampler/toward_mirrors", "strategy/join" });
	const std::vector<StrategyMember> joined = { StrategyMember::Meet, StrategyMember::Aim, StrategyMember::Join };
	const std::string glows                  = "paint(matte([0.5, 0.6, 0.7]))";
	const std::string unaimed                = "paint(matte(#000000, glow = #A0D0FF, strength = 3, aimed = 0)) at([-1, 0.8, 0.8]) box([0.5, 0.5, 0.5]);\n" + glows;

	const Pairing aimed = {
		.what         = "GlowingSurfaces by grid beside a glow that is not aimed at",
		.text         = replacedText(glowText(), glows, unaimed),
		.off          = coreChosen(),
		.on           = withSampling(glowSampling()),
		.beforehand   = none,
		.preparations = testPreparations({ "sampler/glowing_surfaces" }),
	};

	const Pairing relays = {
		.what         = "Join with TowardMirrors beside a metal that relays nothing",
		.text         = replacedText(causticText(), "roughness = 0.05)", "roughness = 0.05, relays = 0)"),
		.off          = withStrategies(coreSampling(), joined),
		.on           = withStrategies(mirrorSampling(), joined),
		.beforehand   = mirror,
		.preparations = mirror,
	};

	for (const Pairing& pairing : { aimed, relays }) {
		checkPairing(pairing);
	}
}

void checkSampler() {
	checkSobolNets();
	checkMirrorDensity();
	checkGathered();
	checkGuided();
	checkLumpDensity();
	const std::vector<Plugin> none              = {};
	const std::vector<StrategyMember> meetAlone = { StrategyMember::Meet };

	const Pairing sobol = {
		.what         = "Sobol",
		.text         = lampsText(),
		.off          = coreChosen(),
		.on           = withSampling(sobolSampling()),
		.beforehand   = none,
		.preparations = none,
	};

	const Pairing eachLamp = {
		.what         = "EachLampFirst",
		.text         = lampsText(),
		.off          = coreChosen(),
		.on           = withSampling(eachLampSampling()),
		.beforehand   = none,
		.preparations = none,
	};

	const Pairing skyMap = {
		.what         = "SkyMap",
		.text         = skyText(),
		.off          = coreChosen(),
		.on           = withSampling(skySampling()),
		.beforehand   = none,
		.preparations = testPreparations({ "sampler/sky_map" }),
	};

	const Pairing glowGrid = {
		.what         = "GlowingSurfaces by grid",
		.text         = glowText(),
		.off          = coreChosen(),
		.on           = withSampling(glowSampling()),
		.beforehand   = none,
		.preparations = testPreparations({ "sampler/glowing_surfaces" }),
	};

	const Pairing guide = {
		.what         = "HighlightGuide",
		.text         = guideText(),
		.off          = coreChosen(),
		.on           = withSampling(guideSampling()),
		.beforehand   = none,
		.preparations = testPreparations({ "sampler/highlight_guide" }),
	};

	const Pairing featuredSky = {
		.what         = "SkyMap over a physical sky with glow and clouds",
		.text         = featuredSkyText(),
		.off          = coreChosen(),
		.on           = withSampling(skySampling()),
		.beforehand   = none,
		.preparations = testPreparations({ "sampler/sky_map" }),
	};

	const Pairing paintedAim = {
		.what         = "Aim under a painted sky with glow, stars, a moon and clouds",
		.text         = paintedSkyText(),
		.off          = withStrategies(coreSampling(), meetAlone),
		.on           = coreChosen(),
		.beforehand   = none,
		.preparations = none,
	};

	const Pairing paintedSky = {
		.what         = "SkyMap over a painted sky with glow, stars, a moon and clouds",
		.text         = paintedSkyText(),
		.off          = coreChosen(),
		.on           = withSampling(skySampling()),
		.beforehand   = none,
		.preparations = testPreparations({ "sampler/sky_map" }),
	};

	for (const Pairing& pairing : { sobol, eachLamp, skyMap, featuredSky, paintedAim, paintedSky, glowGrid, guide }) {
		checkPairing(pairing);
	}
	checkStrategyPairings();
	checkSwitchPairings();
	checkMediumPairings("meet+aim+join+connect through a dispersing glass", "glass(refraction = 1.5, dispersion = 0.045, roughness = 0)");
	checkMediumPairings("meet+aim+join+connect through a dispersing rough glass", "glass(refraction = 1.5, dispersion = 0.045, roughness = 0.2)");
	checkMediumPairings("meet+aim+join+connect through a soap bubble", "glass(refraction = 1.0001, film = [420, 1.33], roughness = 0)");
	checkMediumPairings("meet+aim+join+connect through milk glass", "glass(refraction = 1.4, scatter = 6, lean = 0.3, roughness = 0.3)");
	checkMediumPairings("meet+aim+join+connect through smoke behind smooth glass", "glass(refraction = 1.33, scatter = 2, lean = -0.4, absorb = [0.5, 0.2, 0.1], roughness = 0)");
	checkFeaturePairings("meet+aim+join+connect through fog, crossed and aimed from", mediumText("glass(refraction = 1, scatter = 2, lean = 0.3, absorb = [0.3, 0.2, 0.1], roughness = 0)") + "paint(plastic([0.6, 0.3, 0.2], roughness = 0.4)) at([0.5, 0.6, 0.3]) sphere(0.3);");
	checkFeaturePairings("meet+aim+join+connect through rough fog, crossed and aimed from", mediumText("glass(refraction = 1, scatter = 2, lean = 0.3, absorb = [0.3, 0.2, 0.1], roughness = 0.4)") + "paint(plastic([0.6, 0.3, 0.2], roughness = 0.4)) at([0.5, 0.6, 0.3]) sphere(0.3);");
	checkMediumPairings("meet+aim+join+connect off a filmed metal", "metal([0.3, 0.3, 0.32], roughness = 0.12, film = [380, 1.45])");
	checkFeaturePairings("meet+aim+join+connect with coats: bumps, ladders, paint, glow", coatsText());
	checkRulePairings();
}

}
