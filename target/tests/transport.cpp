#include "check.h"
#include "extras/strategy_correct_join/join.h"
#include "extras/strategy_correct_join/pass.h"
#include "frame/frame.h"
#include "frame/run.h"
#include "material/microfacet.h"
#include "syntax/evaluator.h"
#include "transport/feature.h"
#include "transport/light_walk.h"
#include "transport/strategy.h"

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace kodanuki {

struct Window {
	int x0;
	int y0;
	int x1;
	int y1;
};

struct FrameAsk {
	const char* name;
	std::string text;
	std::vector<StrategyMember> strategies;
	int threads;
};

double windowMean(const Image& image, const Window& window) {
	double sum = 0;
	for (int y = window.y0; y < window.y1; ++y) {
		for (int x = window.x0; x < window.x1; ++x) {
			const std::size_t pixel = (std::size_t(y) * std::size_t(image.width) + std::size_t(x)) * 3;
			sum += double(image.rgb[pixel] + image.rgb[pixel + 1] + image.rgb[pixel + 2]) / 3;
		}
	}
	return sum / double((window.x1 - window.x0) * (window.y1 - window.y0));
}

Window wholeOf(const Image& image) {
	return Window {
		.x0 = 0,
		.y0 = 0,
		.x1 = image.width,
		.y1 = image.height,
	};
}

Image renderedFrame(const FrameAsk& ask) {
	const SceneFile file = sceneOrExit(writeScene(ask.name, ask.text.c_str()));
	const TestTree tree  = testTree(file, 8);

	const Geometry geometry = {
		.scene = file.scene,
		.tree  = tree.tree,
	};

	Prepared chosen           = corePrepared();
	chosen.strategies.members = ask.strategies;

	const std::vector<PreparationMember> preparations = {};
	const Features features                           = featuresOf(featureQueryOf(file, 1));
	const Lens lens                                   = lensOf(file.shot.camera, file.shot.render);
	Progress progress                                 = freshProgress();

	const FrameInput input = {
		.geometry     = geometry,
		.looks        = file.looks,
		.lighting     = file.lighting,
		.shot         = file.shot,
		.lens         = lens,
		.chosen       = chosen,
		.preparations = preparations,
		.features     = features,
		.stopping     = StoppingMember::FixedCount,
		.smoothGlass  = SmoothGlass::Exact,
		.execution    = ExecutionMember::OneAtATime,
		.threads      = ask.threads,
		.progress     = progress,
	};

	return renderFrame(input).image;
}

const char* sceneStart = R"(
	render {
		width     = 64;
		height    = 40;
		tolerance = 0;
		samples   = 256;
	}

	function sphere(r) {
		return length(p) - r;
	}

	function box(size) {
		let q = abs(p) - size / 2;
		return length(max(q, 0)) + min(max(q.x, max(q.y, q.z)), 0);
	}

)";

std::string litPlaneText(const char* light) {
	return std::string(sceneStart) + R"(
		camera {
			eye    = [0, -3, 2];
			target = [0, 0, 0];
			fov    = 0.3;
		}

		sky gradient {
			zenith  = [0, 0, 0];
			horizon = [0, 0, 0];
			ground  = [0, 0, 0];
		}

		)" +
		   light + R"(

		paint(matte([0.5, 0.5, 0.5])) at([0, 0, -0.1]) box([4, 4, 0.2]);
	)";
}

struct PlaneLight {
	const char* light;
	double irradiance;
};

void checkLitPlane(const PlaneLight& lighting) {
	const FrameAsk ask = {
		.name       = "plane",
		.text       = litPlaneText(lighting.light),
		.strategies = coreStrategies().members,
		.threads    = 8,
	};

	const Window middle = {
		.x0 = 31,
		.y0 = 19,
		.x1 = 33,
		.y1 = 21,
	};

	const double got  = windowMean(renderedFrame(ask), middle);
	const double want = 0.5 / M_PI * lighting.irradiance;
	std::printf("transport plane under %s: %.4f, want %.4f\n", lighting.light, got, want);
	checkNear(Near { got, want, 0.02 * want, "a matte plane is lit as the light says" });
}

std::string strategyText() {
	return std::string(sceneStart) + R"(
		camera {
			eye    = [0, -4, 1.5];
			target = [0, 0, 0.4];
			fov    = 0.7;
		}

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

		paint(matte([0.6, 0.6, 0.6])) at([0, 0, -0.1]) box([6, 6, 0.2]);
		paint(plastic([0.8, 0.2, 0.1], roughness = 0.3)) at([-0.6, 0, 0.5]) sphere(0.5);
		paint(metal([0.9, 0.8, 0.5], roughness = 0.4)) at([0.6, 0.3, 0.4]) sphere(0.4);
		paint(glass(refraction = 1.5, roughness = 0.2)) at([0.1, -0.8, 0.3]) sphere(0.3);
	)";
}

void checkStrategies() {
	FrameAsk ask = {
		.name       = "strategies",
		.text       = strategyText(),
		.strategies = coreStrategies().members,
		.threads    = 8,
	};

	const Image both = renderedFrame(ask);
	ask.strategies   = { StrategyMember::Meet };
	const Image meet = renderedFrame(ask);
	const double a   = windowMean(both, wholeOf(both));
	const double b   = windowMean(meet, wholeOf(meet));
	std::printf("transport strategies: meet and aim %.5f, meet alone %.5f\n", a, b);
	checkNear(Near { b / a, 1, 0.02, "meet alone and meet with aim agree in the mean" });
}

void checkThreads() {
	FrameAsk ask = {
		.name       = "threads",
		.text       = strategyText(),
		.strategies = coreStrategies().members,
		.threads    = 8,
	};

	ask.text.replace(ask.text.find("samples   = 256"), 15, "samples   = 8");
	const Image many = renderedFrame(ask);
	ask.threads      = 1;
	const Image one  = renderedFrame(ask);
	check(many.rgb == one.rgb, "a frame is the same to the bit on one thread and on eight");
}

double quadIrradiance(double half, double height) {
	double sum      = 0;
	const int steps = 200;
	const double d  = 2 * half / steps;
	for (int i = 0; i < steps; ++i) {
		for (int j = 0; j < steps; ++j) {
			const double x       = -half + (i + 0.5) * d;
			const double y       = -half + (j + 0.5) * d;
			const double squared = x * x + y * y + height * height;
			sum += height * height / (squared * squared) * d * d;
		}
	}
	return sum / (4 * half * half);
}

std::string slabText(const char* slab) {
	return std::string(sceneStart) + R"(
		camera {
			eye    = [0, -0.3, 3];
			target = [0, 0, 0];
			fov    = 0.3;
		}

		sky gradient {
			zenith  = [0, 0, 0];
			horizon = [0, 0, 0];
			ground  = [0, 0, 0];
		}

		paint(matte(#000000, glow = #FFFFFF, strength = 1)) at([0, 0, -0.1]) box([4, 4, 0.2]);
		)" +
		   slab;
}

void checkSlab() {
	FrameAsk ask = {
		.name       = "slab",
		.text       = slabText(""),
		.strategies = coreStrategies().members,
		.threads    = 8,
	};

	const Window middle = {
		.x0 = 22,
		.y0 = 14,
		.x1 = 42,
		.y1 = 26,
	};

	const double bare      = windowMean(renderedFrame(ask), middle);
	ask.text               = slabText("paint(glass(refraction = 1.5, roughness = 0)) at([0, 0, 0.5]) box([3, 3, 0.4]);");
	const double through   = windowMean(renderedFrame(ask), middle);
	const double reflected = dielectricReflectance(3 / std::sqrt(9.09), 1 / 1.5);
	const double want      = (1 - reflected) / (1 + reflected);
	std::printf("transport slab: a glowing plane reads %.4f bare and %.4f through an exact glass slab, %.4f of it; want %.4f\n", bare, through, through / bare, want);
	checkNear(Near { through / bare, want, 0.01 * want, "a plane seen through an exact glass slab keeps the slab's transmission, (1 - R) / (1 + R)" });
}

struct JoinProbe {
	const Tracing& tracing;
	math::Vector point;
	math::Vector heading;
};

LightPath standingLightPath(const JoinProbe& probe, std::vector<double>& registers) {
	const Ray ray = {
		.origin    = probe.point - probe.heading,
		.direction = probe.heading,
		.far       = 10,
		.inside    = false,
		.within    = noMaterial,
	};

	const Hit hit = intersect(probe.tracing.geometry, ray);

	const PathNumbers numbers = {
		.stream    = 7,
		.index     = 0,
		.group     = 0,
		.branches  = 1,
		.sequenced = false,
	};

	const LightWeighing weighing = {
		.meet        = 1,
		.aim         = 1,
		.connections = 0,
		.cut         = 0,
		.fresh       = false,
	};

	const Wavelengths wavelengths = sampledWavelengths(0.3);

	const Surfacing surfacing = {
		.hit         = hit,
		.heading     = probe.heading,
		.medium      = noMaterial,
		.wavelengths = wavelengths,
		.registers   = registers,
	};

	return LightPath {
		.wavelengths = wavelengths,
		.throughput  = flatSpectrum(1),
		.start       = 1,
		.dispersed   = false,
		.vertex      = surfaceAt(probe.tracing, surfacing),
		.heading     = probe.heading,
		.drawnAt     = ray.origin,
		.legFrom     = ray.origin,
		.crossings   = 0,
		.depth       = 1,
		.medium      = noMaterial,
		.lamp        = 0,
		.relayed     = true,
		.rules       = freshRules(),
		.weighing    = weighing,
		.numbers     = numbers,
	};
}

bool landsInPixel(const Tracing& tracing, const LensLine& line, const math::Vector& point) {
	const Lens& lens          = tracing.lens;
	const math::Vector offset = point - lens.eye;
	const double ahead        = math::dot(offset, lens.forward);
	const double u            = math::dot(offset, lens.right) / (ahead * lens.reach * lens.aspect);
	const double v            = math::dot(offset, lens.up) / (ahead * lens.reach);
	const int x               = int((u + 1) * 0.5 * lens.width);
	const int y               = int((1 - v) * 0.5 * lens.height);
	return line.pixel == std::uint32_t(y * lens.width + x);
}

void checkJoinLine() {
	const SceneFile file      = sceneOrExit(writeScene("join", litPlaneText("light sphere { at = [0, 0, 1]; radius = 0.2; power = [1, 1, 1]; }").c_str()));
	const TestTree tree       = testTree(file, 8);
	const Materials materials = materialsOf(file.looks, SmoothGlass::Exact);
	const Lights lights       = lightsOf(file.lighting);
	const Lens lens           = lensOf(file.shot.camera, file.shot.render);
	const Sampling sampling   = coreSampling();
	const Features features   = featuresOf(featureQueryOf(file, 1));
	Strategies strategies     = coreStrategies();
	strategies.members.push_back(StrategyMember::Join);
	strategies.lampShares = { 1 };

	const Geometry geometry = {
		.scene = file.scene,
		.tree  = tree.tree,
	};

	const Tracing tracing = {
		.geometry   = geometry,
		.scene      = file.scene,
		.looks      = file.looks,
		.materials  = materials,
		.lights     = lights,
		.lens       = lens,
		.sampling   = sampling,
		.strategies = strategies,
		.features   = features,
		.limits     = transportLimits(file.shot.render),
		.seed       = 1,
		.film       = stillFilm(),
		.recording  = false,
	};

	std::vector<double> registers;
	int found  = 0;
	int landed = 0;
	for (int k = 0; k < 64; ++k) {
		const JoinProbe probe = {
			.tracing = tracing,
			.point   = { -0.25 + 0.5 * double(k % 8) / 7, -0.25 + 0.5 * double(k / 8) / 7, 0 },
			.heading = math::normalized({ 0.3, -0.2, -1 }),
		};

		const LightPath path = standingLightPath(probe, registers);

		const LightStanding standing = {
			.path      = path,
			.registers = registers,
		};

		const LensLine line = joinedLine(tracing, standing);
		found += line.found ? 1 : 0;
		landed += line.found && landsInPixel(tracing, line, probe.point) ? 1 : 0;
	}
	std::printf("transport join: %d of 64 surface points joined, %d in the pixel the camera projects them to\n", found, landed);
	check(found == 64, "every surface point in view is joined to the lens");
	check(landed == 64, "a join line lands in the pixel whose camera ray passes its surface point");
}

void checkCarriedSpread() {
	const double paths = 4096;
	const Colour c     = { 0.5, 0.25, 1 };

	const CarriedLight constant = {
		.sums    = { c * paths },
		.squares = { c * c * paths },
		.paths   = std::uint64_t(paths),
	};

	const CarriedLight half = {
		.sums    = { c * (paths / 2) },
		.squares = { c * c * (paths / 2) },
		.paths   = std::uint64_t(paths),
	};

	const Colour none   = carriedSpread(constant, 0);
	const Colour spread = carriedSpread(half, 0);
	const double want   = c.red * c.red / (4 * paths);
	std::printf("transport carried spread: constant %.3g, half the paths %.6g (want %.6g)\n", none.red + none.green + none.blue, spread.red, want);
	check(none.red == 0 && none.green == 0 && none.blue == 0, "the spread of a constant splat is zero");
	checkNear(Near { spread.red, want, 1e-9 * want, "the spread of a splat half the paths bring is the variance of the mean" });
}

void checkTransport() {
	const PlaneLight lightings[] = {
		{ "light directional { direction = [0, 0, 1]; power = [1, 1, 1]; }", 1 },
		{ "light point { at = [0, 0, 1]; power = [1, 1, 1]; }", 1 },
		{ "light sphere { at = [0, 0, 1]; radius = 0.2; power = [1, 1, 1]; }", 1 },
		{ "light quad { at = [-0.1, -0.1, 1]; along = [0, 0.2, 0]; across = [0.2, 0, 0]; power = [1, 1, 1]; }", quadIrradiance(0.1, 1) },
		{ "light sun { direction = [0.3, 0.2, 0.6]; angle = 0.0093; power = [1, 1, 1]; }", 0.6 / 0.7 },
	};

	for (const PlaneLight& lighting : lightings) {
		checkLitPlane(lighting);
	}
	checkStrategies();
	checkSlab();
	checkThreads();
	checkJoinLine();
	checkCarriedSpread();
}

}
