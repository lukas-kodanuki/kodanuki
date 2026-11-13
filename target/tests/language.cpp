#include "check.h"
#include "plugins/transport/paths/paths.h"
#include "plugins/transport/paths/strategy.h"
#include "sdf/compare.h"
#include "sdf/scene.h"
#include "syntax/evaluator.h"

#include <array>
#include <cmath>
#include <string>
#include <vector>

namespace kodanuki {

double distanceAt(const Scene& scene, const std::array<double, 3>& p) {
	std::vector<double> registers;
	return sceneDistance(scene, {
									.p         = { p[0], p[1], p[2] },
									.registers = registers,
								});
}

void checkCompiledFn() {
	const Scene scene = sceneOrExit(writeScene("function", R"(
		function f(k) {
			let s = 0;
			for i in 0..3 {
				let s = s + i * k;
			}
			if p.x > 0 {
				let s = s * 2;
			}
			return s + length(p.yz);
		}

		at([1, 0, 0]) f(2);
	)"))
							.scene;
	check(scene.programs.size() == 1, "one program for one function");

	const Near doubled = {
		.got       = distanceAt(scene, { 2, 3, 4 }),
		.want      = 17,
		.tolerance = 1e-12,
		.what      = "loop sum doubled location local x > 0",
	};

	checkNear(doubled);

	const Near single = {
		.got       = distanceAt(scene, { 0, 3, 4 }),
		.want      = 11,
		.tolerance = 1e-12,
		.what      = "loop sum location local x < 0",
	};

	checkNear(single);
}

void checkPlacement() {
	const Scene scene = sceneOrExit(writeScene("place", R"(
		function ball(r) {
			return length(p) - r;
		}

		scale(2) ball(1);
		at([10, 0, 0]) turn(z, pi / 2) scale([1, 3, 1]) ball(0.5);
		with at([0, 10, 0]) {
			for i in 0..2 {
				at([i * 4, 0, 0]) ball(1);
			}
		}

	)"))
							.scene;
	check(scene.programs.size() == 1, "numbers stay arguments");
	check(scene.objects.size() == 4, "each top-level shape an object");

	const Near scaledBall = {
		.got       = distanceAt(scene, { 3, 0, 0 }),
		.want      = 1,
		.tolerance = 1e-12,
		.what      = "scale multiplies the distance back",
	};

	checkNear(scaledBall);

	const Near turnedBall = {
		.got       = distanceAt(scene, { 13, 0, 0 }),
		.want      = 0.5,
		.tolerance = 1e-12,
		.what      = "turned: the long axis now runs along x",
	};

	checkNear(turnedBall);

	const Near loopBall = {
		.got       = distanceAt(scene, { 4, 12, 0 }),
		.want      = 1,
		.tolerance = 1e-12,
		.what      = "with and loop compose",
	};

	checkNear(loopBall);
}

void checkJoins() {
	const Scene scene = sceneOrExit(writeScene("joins", R"(
		function ball(r) {
			return length(p) - r;
		}

		cut {
			ball(2);
			at([2, 0, 0]) ball(1);
		}

		at([0, 20, 0]) merge(1) {
			ball(1);
			at([2, 0, 0]) ball(1);
		}

	)"))
							.scene;

	const Near cutBall = {
		.got       = distanceAt(scene, { 1.5, 0, 0 }),
		.want      = 0.5,
		.tolerance = 1e-12,
		.what      = "cut removes the second ball",
	};

	checkNear(cutBall);

	const Near blended = {
		.got       = distanceAt(scene, { 1, 20, 0 }),
		.want      = -0.25,
		.tolerance = 1e-12,
		.what      = "smooth merge between equal balls",
	};

	checkNear(blended);
}

void checkRepeat() {
	const Scene scene = sceneOrExit(writeScene("repeat", R"(
		function ball(r) {
			return length(p) - r;
		}

		let seed = 1;

		shape drawn() {
			let seed = seed + 1;
			at([seed, 0, 0]) ball(0.25);
		}

		shape lattice() {
			turn(z, pi / 2) repeat(count = [3, 1, 4], step = [1, 0, 2]) {
				drawn();
			}
		}

		lattice();
		at([0, 0, 40]) repeat(count = [2, 1, 1], step = [5, 0, 0]) {
			ball(1);
		}

	)"))
							.scene;
	check(scene.leaves.size() == 14, "a repeat of [3, 1, 4] makes 12 copies");
	check(scene.objects.size() == 3, "copies in a shape are one merge, copies at the top level objects of their own");
	bool shared = true;
	bool placed = true;
	for (std::size_t k = 0; k < 12; ++k) {
		const Leaf& leaf = scene.leaves[k];
		const int i      = int(k % 3);
		const int z      = int(k / 3);
		shared           = shared && leaf.program == scene.leaves[0].program && leaf.arguments == scene.leaves[0].arguments;
		placed           = placed && std::abs(leaf.placement.origin[0]) < 1e-6 && std::abs(leaf.placement.origin[1] - (2 + i)) < 1e-6 && std::abs(leaf.placement.origin[2] - 2 * z) < 1e-6;
	}
	check(shared, "the copies share their program and arguments");
	check(placed, "copies stand at the turned steps, x fastest, and the block draws once");

	const Near copy = {
		.got       = distanceAt(scene, { 0, 4, 6 }),
		.want      = -0.25,
		.tolerance = 1e-6,
		.what      = "the last copy is a shape",
	};

	checkNear(copy);
}

void checkUnionParts() {
	const Scene scene = sceneOrExit(writeScene("unions", R"(
		function ball(r) {
			return length(p) - r;
		}

		shape row(n) {
			for i in 0..n {
				at([i, 0, 0]) ball(0.3);
			}
		}

		row(80);
		at([0, 10, 0]) merge(0.1) {
			for i in 0..80 {
				at([i, 0, 0]) ball(0.3);
			}
		}

		at([0, 20, 0]) row(5);
	)"))
							.scene;
	check(scene.objects.size() == 82, "a long hard union is cut into objects, a smooth one and a short one stay whole");
	check(scene.objectPrograms[80].steps.size() >= fewestUnionSteps, "the smooth union is as long as the one cut");
	check(scene.objectPrograms[0].steps.size() < fewestUnionSteps && scene.objectPrograms[81].steps.size() < fewestUnionSteps, "parts and the short union are short");

	const Near part = {
		.got       = distanceAt(scene, { 79, 0, 0 }),
		.want      = -0.3,
		.tolerance = 1e-12,
		.what      = "the cut union keeps its field",
	};

	checkNear(part);
}

void checkRules() {
	const SceneFile file           = sceneOrExit(writeScene("rules", R"(
		function ball(r) {
			return length(p) - r;
		}

		light sphere {
			at       = [0, 0, 4];
			radius   = 0.3;
			reaches  = [stage, props];
			caustics = 0;
		}

		light point {
			at = [0, 0, 5];
		}

		group(stage) rule(camera = 0, shadows = 0) ball(1);
		with group(props) rule(caustics = 0) {
			rule(caustics = 1, camera = 0) ball(2);
			group(stage) ball(3);
		}

	)"));
	const std::uint8_t hidden      = std::uint8_t(Rule::Hidden);
	const std::uint8_t shadowless  = std::uint8_t(Rule::Shadowless);
	const std::uint8_t causticless = std::uint8_t(Rule::Causticless);
	check(file.looks.groups.size() == 2 && file.looks.groups[0] == "stage" && file.looks.groups[1] == "props", "groups are named in the order first said");
	check(file.lighting.lights[0].reaches == 3 && !file.lighting.lights[0].caustics, "a light reaches its groups and refuses caustics");
	check(file.lighting.lights[1].reaches == 0 && file.lighting.lights[1].caustics, "a light without fields reaches every shape");
	check(file.scene.leaves[0].rules == (hidden | shadowless) && file.scene.leaves[0].groups == 1, "a leaf says its rules and group");
	check(file.scene.leaves[1].rules == hidden && file.scene.leaves[1].groups == 2, "an inner rule clears and sets bits");
	check(file.scene.leaves[2].rules == causticless && file.scene.leaves[2].groups == 3, "groups nest");
}

void checkAbsoluteImport() {
	const std::string imported = writeScene("imported", R"(
		function ball(r) {
			return length(p) - r;
		}

	)");
	const std::string text     = "import \"" + imported + "\";\nball(1);\n";
	const std::string importer = writeScene("importer", text.c_str());
	const Scene scene          = sceneOrExit(importer).scene;
	check(importer.starts_with('/') && scene.leaves.size() == 1, "an import by absolute path is read from that path, also from a scene given by an absolute path");
}

void checkRenderCaps() {
	const SceneFile capped       = sceneOrExit(writeScene("capped", R"(
		function ball(r) {
			return length(p) - r;
		}

		render { surfaces = 3; collisions = 5; }
		ball(1);
	)"));
	const SceneFile plain        = sceneOrExit(writeScene("plain", "function ball(r) {\n\treturn length(p) - r;\n}\n\nball(1);\n"));
	const TransportLimits limits = transportLimits(capped.shot.render);
	const TransportLimits common = transportLimits(plain.shot.render);
	check(limits.maximumDepth == 3 && limits.maximumCollisions == 5, "a render's surfaces and collisions cap the walk");
	check(common.maximumDepth == 10 && common.maximumCollisions == 256, "the caps default to 10 surfaces and 256 collisions");
}

void checkLoadErrors() {
	const std::string unended = writeScene("unended", "function ball(r) {\n\treturn length(p) - r;\n}\n\nball(1)\n");
	const LoadedScene parsed  = loadScene(unended, 0);
	check(parsed.error == unended + ":6:1: expected ';'" && parsed.file.scene.objects.empty(), "a parse error comes back as a value with its location");

	const std::string looped = writeScene("looped", "for i in 0..100000 {\n\tfor j in 0..100000 {\n\t\tblob(1);\n\t}\n}\n");
	const LoadedScene run    = loadScene(looped, 0);
	check(run.error == looped + ":3:3: unknown function 'blob'", "an error inside loops stops the load at the first");

	const std::string empty = writeScene("empty", "let a = 1;\n");
	check(loadScene(empty, 0).error == empty + ": the scene has no shapes", "a scene without shapes is an error");

	const std::string fine = writeScene("fine", "function ball(r) {\n\treturn length(p) - r;\n}\n\nball(1);\n");
	check(loadScene(fine, 0).error.empty(), "a load after a failed one starts clean");
}

std::string ballText(const std::string& body) {
	return "function ball(r) {\n\treturn length(p) - r;\n}\n\n" + body;
}

double placedX(const std::string& name, const std::string& stops, const std::string& at) {
	const std::string text = ballText("let s = " + stops + ";\nat([" + at + ", 0, 0]) ball(1);\n");
	return sceneOrExit(writeScene(name.c_str(), text.c_str())).scene.leaves[0].placement.origin[0];
}

void checkTime() {
	const std::string timed = writeScene("timed", ballText("at([time, 0, 0]) ball(1);\n").c_str());
	const SceneFile still   = sceneOrExit(timed);
	const SceneFile moved   = sceneAtOrExit(timed, 2.5);
	check(still.scene.leaves[0].placement.origin[0] == 0 && moved.scene.leaves[0].placement.origin[0] == 2.5, "a scene's time is 0, or the number it is loaded at");
	check(still.timed && !sceneOrExit(writeScene("untimed", ballText("ball(1);\n").c_str())).timed, "the load notes whether the file read time");

	const std::string changed = writeScene("changed", ballText("let time = 1;\nball(1);\n").c_str());
	check(loadScene(changed, 0).error == changed + ":5:1: time belongs to the clip and cannot be changed", "let time is an error");
}

void checkClip() {
	const SceneFile clipped = sceneOrExit(writeScene("clipped", ballText("clip {\n\tduration = 12.8;\n\trate     = 24;\n\tcuts     = [4.8, 8.8];\n}\n\nclip {\n\tball(1);\n\tat([0.5, 0, 0]) ball(1);\n}\n").c_str()));
	const Clip& clip        = clipped.shot.clip;
	check(clip.duration == 12.8 && clip.rate == 24 && clip.cuts == std::vector<double> { 4.8, 8.8 } && clipFrames(clip) == 307, "a clip block holds its duration, rate and cuts, and has round(duration * rate) frames");
	check(clipped.scene.leaves.size() == 2 && sceneOrExit(writeScene("unclipped", ballText("ball(1);\n").c_str())).shot.clip.duration == 0, "clip with statements stays a join, and a file without a clip block is a still");

	const std::string falling = writeScene("falling", ballText("clip {\n\tduration = 4;\n\tcuts     = [3, 2];\n}\n\nball(1);\n").c_str());
	check(!loadScene(falling, 0).error.empty(), "a clip's cuts rise");
}

void checkStops() {
	const std::string line = "[[0, 0], [2, 4], [3, 4], [3, 10]]";
	check(placedX("before", line, "linear(s, -1)") == 0 && placedX("between", line, "linear(s, 1)") == 2 && placedX("past", line, "linear(s, 9)") == 10, "linear joins the stops by straight lines, the first before them and the last past them");
	check(placedX("leading", line, "linear(s, 2.99)") == 4 && placedX("jumped", line, "linear(s, 3)") == 10, "two stops of one time are a jump");
	const std::string curve = "[[0, 0], [1, 1], [2, 2], [3, 0]]";
	check(placedX("through", curve, "smooth(s, 1)") == 1 && std::abs(placedX("straight", "[[0, 0], [1, 1], [2, 2]]", "smooth(s, 0.5)") - 0.5) < 1e-12, "smooth passes through every stop and follows a straight run");
	check(placedX("bends", curve, "smooth(s, 2.5)") > placedX("cornered", curve, "linear(s, 2.5)"), "smooth turns no corner at a stop");
	check(placedX("vector", "[[0, 1, 2], [1, 3, 6]]", "linear(s, 0.5)[1]") == 4, "a stop of several values gives a vector");
}

void checkCompared() {
	const std::string text = ballText("light point {\n\tat    = [0, 0, 3];\n\tpower = [1, 1, 1] * (1 + time);\n}\n\npaint(matte([0.5, 0.5, 0.5] * (1 + select(time > 2, 1, 0)))) at([0, 0, select(time > 4, 1, 0)]) ball(1);\n");
	const std::string path = writeScene("compared", text.c_str());
	const SceneFile start  = sceneOrExit(path);
	const SceneFile lit    = sceneAtOrExit(path, 1);
	const SceneFile looked = sceneAtOrExit(path, 3);
	const SceneFile moved  = sceneAtOrExit(path, 5);
	check(sameShapes(start.scene, lit.scene) && sameLooks(start.looks, lit.looks) && !sameLighting(start.lighting, lit.lighting), "a light that changes leaves the shapes and the looks the same");
	check(sameShapes(start.scene, looked.scene) && !sameLooks(start.looks, looked.looks), "a material that changes leaves the shapes the same");
	check(!sameShapes(start.scene, moved.scene) && sameShapes(moved.scene, sceneAtOrExit(path, 6).scene), "a shape that moves changes the shapes, and the same evaluation twice compares equal");
}

void checkLanguage() {
	checkCompiledFn();
	checkPlacement();
	checkJoins();
	checkRepeat();
	checkUnionParts();
	checkRules();
	checkAbsoluteImport();
	checkRenderCaps();
	checkLoadErrors();
	checkTime();
	checkClip();
	checkStops();
	checkCompared();
}

}
