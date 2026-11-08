#include "frame/stopping.h"
#include "check.h"
#include "frame/frame.h"
#include "frame/run.h"
#include "syntax/evaluator.h"
#include "transport/feature.h"

#include <array>
#include <cmath>
#include <cstdio>
#include <random>
#include <vector>

namespace kodanuki {

constexpr int stoppingSeeds   = 8;
constexpr int stoppingThreads = 4;

const char* stoppingText = R"(
	render {
		width     = 64;
		height    = 40;
		tolerance = 6;
		samples   = 64;
		batch     = 8;
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

	sky gradient {
		zenith  = [0.2, 0.3, 0.5];
		horizon = [0.5, 0.5, 0.5];
		ground  = [0.1, 0.1, 0.1];
	}

	light sphere {
		at     = [1.2, -1, 2];
		radius = 0.2;
		power  = [3, 3, 2.5];
	}

	paint(matte([0.6, 0.6, 0.6])) at([0, 0, -0.1]) box([6, 6, 0.2]);
	paint(plastic([0.8, 0.2, 0.1], roughness = 0.3)) at([-0.6, 0, 0.5]) sphere(0.5);
	paint(metal([0.9, 0.8, 0.5], roughness = 0.1)) at([0.6, 0.3, 0.4]) sphere(0.4);
)";

struct StoppingArm {
	const SceneFile& file;
	const Tree& tree;
	StoppingMember member;
};

struct StoppingSpread {
	std::array<double, 3> mean;
	std::array<double, 3> error;
	double samples;
};

Rendered stoppedFrame(const StoppingArm& arm, int seed) {
	Shot shot        = arm.file.shot;
	shot.render.seed = seed;

	const Geometry geometry = {
		.scene = arm.file.scene,
		.tree  = arm.tree,
	};

	const Prepared chosen                     = corePrepared();
	const Features features                   = featuresOf(featureQueryOf(arm.file, 1));
	const std::vector<PreparationMember> none = {};
	const Lens lens                           = lensOf(shot.camera, shot.render);
	Progress progress                         = freshProgress();

	const FrameInput input = {
		.geometry     = geometry,
		.looks        = arm.file.looks,
		.lighting     = arm.file.lighting,
		.shot         = shot,
		.lens         = lens,
		.chosen       = chosen,
		.preparations = none,
		.features     = features,
		.stopping     = arm.member,
		.smoothGlass  = SmoothGlass::Exact,
		.execution    = ExecutionMember::Waves,
		.threads      = stoppingThreads,
		.progress     = progress,
	};

	return renderFrame(input);
}

std::array<double, 3> frameMeans(const Image& image) {
	std::array<double, 3> sums = { 0, 0, 0 };
	for (std::size_t k = 0; k < image.rgb.size(); ++k) {
		sums[k % 3] += double(image.rgb[k]);
	}
	const double pixels = double(image.width * image.height);
	return { sums[0] / pixels, sums[1] / pixels, sums[2] / pixels };
}

StoppingSpread stoppingSpread(const StoppingArm& arm) {
	std::vector<std::array<double, 3>> means;

	StoppingSpread spread = {
		.mean    = { 0, 0, 0 },
		.error   = { 0, 0, 0 },
		.samples = 0,
	};

	for (int seed = 1; seed <= stoppingSeeds; ++seed) {
		const Rendered rendered = stoppedFrame(arm, seed);
		means.push_back(frameMeans(rendered.image));
		spread.samples += rendered.samples / stoppingSeeds;
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

void checkStoppedMeans() {
	const SceneFile file = sceneOrExit(writeScene("stopping", stoppingText));
	const TestTree tree  = testTree(file, stoppingThreads);

	const StoppingArm fixed = {
		.file   = file,
		.tree   = tree.tree,
		.member = StoppingMember::FixedCount,
	};

	const StoppingArm interval = {
		.file   = file,
		.tree   = tree.tree,
		.member = StoppingMember::ConfidenceInterval,
	};

	const StoppingSpread a = stoppingSpread(fixed);
	const StoppingSpread b = stoppingSpread(interval);
	double worst           = 0;
	for (int c = 0; c < 3; ++c) {
		const double combined = std::sqrt(a.error[c] * a.error[c] + b.error[c] * b.error[c]);
		worst                 = std::max(worst, std::abs(a.mean[c] - b.mean[c]) / std::max(combined, 1e-12));
	}
	std::printf("stopping: fixed %.5f %.5f %.5f at %.1f samples, confidence %.5f %.5f %.5f at %.1f samples, worst gap %.2f standard errors\n", a.mean[0], a.mean[1], a.mean[2], a.samples, b.mean[0], b.mean[1], b.mean[2], b.samples, worst);
	check(worst < 4, "the confidence interval leaves the mean of a fixed count unchanged");
	check(b.samples > 16 && b.samples < 64, "the confidence interval draws fewer samples where pixels are quiet");
}

void checkOwnTrialUnread() {
	const Grade grade = {
		.exposure = 1,
		.curve    = Curve::Filmic,
	};

	const Stopping stopping = {
		.member    = StoppingMember::ConfidenceInterval,
		.samples   = 1024,
		.batch     = 8,
		.tolerance = 2,
		.grade     = grade,
	};

	Trial trial = {
		.samples = 16,
		.width   = 7,
		.height  = 5,
		.wanted  = std::vector<float>(35, 0.0f),
		.totals  = std::vector<Colour>(35, Colour { 0, 0, 0 }),
	};

	std::mt19937 generator(7);
	std::uniform_real_distribution<float> draw(0.0f, 2000.0f);
	for (float& wanted : trial.wanted) {
		wanted = draw(generator);
	}
	bool unread = true;
	for (int pixel = 0; pixel < 35; ++pixel) {
		const CountQuery query = {
			.x     = pixel % 7,
			.y     = pixel / 7,
			.trial = trial,
		};

		const float own                  = trial.wanted[std::size_t(pixel)];
		trial.wanted[std::size_t(pixel)] = 0;
		const int low                    = samplesFor(stopping, query);
		trial.wanted[std::size_t(pixel)] = 1e9f;
		const int high                   = samplesFor(stopping, query);
		trial.wanted[std::size_t(pixel)] = own;
		unread                           = unread && low == high && low % 8 == 0 && low >= 16 && low <= 1024;
	}
	check(unread, "a pixel's count is whole batches set by its neighbours' trials alone");
}

void checkStopping() {
	checkOwnTrialUnread();
	checkStoppedMeans();
}

}
