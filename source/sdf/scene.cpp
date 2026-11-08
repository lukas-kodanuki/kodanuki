#include "sdf/scene.h"
#include "sdf/numbers.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace kodanuki {

double smoothMin(double a, double b, double radius) {
	if (radius <= 0) {
		return std::min(a, b);
	}
	return math::smoothMinimum(a, b, radius);
}

double joinPair(const Node& node, double a, double b) {
	const double radius = node.radius;
	switch (node.join) {
		case Join::Merge: return smoothMin(a, b, radius);
		case Join::Cut: return -smoothMin(-a, b, radius);
		case Join::Clip: return -smoothMin(-a, -b, radius);
		case Join::Leaf: return a;
	}

	return a;
}

double leafDistance(const Scene& scene, const Leaf& leaf, const Probe& probe) {
	const double d[3] = { probe.p.x - leaf.placement.origin[0], probe.p.y - leaf.placement.origin[1], probe.p.z - leaf.placement.origin[2] };
	double local[3];
	for (int i = 0; i < 3; ++i) {
		local[i] = leaf.placement.linear[i][0] * d[0] + leaf.placement.linear[i][1] * d[1] + leaf.placement.linear[i][2] * d[2];
	}

	const Inputs<double> inputs = {
		.arguments = scene.arguments.data() + leaf.arguments,
		.p         = { local[0], local[1], local[2] },
		.n         = { 0, 0, 0 },
	};

	return evaluate(scene.programs[leaf.program], inputs, probe.registers) * leaf.placement.stretch;
}

double nodeDistance(const Scene& scene, std::uint32_t index, const Probe& probe) {
	const Node& node = scene.nodes[index];
	if (node.join == Join::Leaf) {
		return leafDistance(scene, scene.leaves[node.leaf], probe);
	}
	double d = nodeDistance(scene, node.children[0], probe);
	for (std::size_t i = 1; i < node.children.size(); ++i) {
		d = joinPair(node, d, nodeDistance(scene, node.children[i], probe));
	}
	return d;
}

double sceneDistance(const Scene& scene, const Probe& probe) {
	double d = 1e30;
	for (const std::uint32_t object : scene.objects) {
		d = std::min(d, nodeDistance(scene, object, probe));
	}
	return d;
}

struct Builder {
	Program program;
	std::uint32_t leaf;
	std::uint32_t point[3];
};

struct Inlining {
	const Scene& scene;
	const Leaf& leaf;
	std::uint32_t local[3];
	std::vector<std::uint32_t> map;
};

struct Pair {
	std::uint32_t a;
	std::uint32_t b;
};

Step binaryStep(Opcode opcode, std::uint32_t a, std::uint32_t b) {
	return Step {
		.opcode = opcode,
		.a      = a,
		.b      = b,
		.c      = 0,
		.value  = 0,
		.leaf   = noLeaf,
	};
}

Step valueStep(double value) {
	Step step  = binaryStep(Opcode::Constant, 0, 0);
	step.value = value;
	return step;
}

std::uint32_t emitStep(Builder& builder, const Step& step) {
	Step placed = step;
	placed.leaf = builder.leaf;
	builder.program.steps.push_back(placed);
	return std::uint32_t(builder.program.steps.size() - 1);
}

std::uint32_t emitValue(Builder& builder, double value) {
	return emitStep(builder, valueStep(value));
}

std::uint32_t emitNegate(Builder& builder, std::uint32_t a) {
	return emitStep(builder, binaryStep(Opcode::Negate, a, 0));
}

std::uint32_t offsetCoordinate(Builder& builder, const Placement& placement, int axis) {
	if (placement.origin[axis] == 0) {
		return builder.point[axis];
	}
	const std::uint32_t origin = emitValue(builder, placement.origin[axis]);
	return emitStep(builder, binaryStep(Opcode::Subtract, builder.point[axis], origin));
}

std::uint32_t weightedTerm(Builder& builder, double weight, std::uint32_t offset) {
	if (weight == 1) {
		return offset;
	}
	return emitStep(builder, binaryStep(Opcode::Multiply, emitValue(builder, weight), offset));
}

std::uint32_t localCoordinate(Builder& builder, const double row[3], const std::uint32_t offsets[3]) {
	std::optional<std::uint32_t> sum;
	for (int j = 0; j < 3; ++j) {
		if (row[j] == 0) {
			continue;
		}
		const std::uint32_t term = weightedTerm(builder, row[j], offsets[j]);
		sum                      = sum ? emitStep(builder, binaryStep(Opcode::Add, *sum, term)) : term;
	}
	return sum ? *sum : emitValue(builder, 0);
}

Inlining startInlining(Builder& builder, const Scene& scene, const Leaf& leaf) {
	const Placement& placement     = leaf.placement;
	const std::uint32_t offsets[3] = { offsetCoordinate(builder, placement, 0), offsetCoordinate(builder, placement, 1), offsetCoordinate(builder, placement, 2) };
	return Inlining {
		.scene = scene,
		.leaf  = leaf,
		.local = { localCoordinate(builder, placement.linear[0], offsets), localCoordinate(builder, placement.linear[1], offsets), localCoordinate(builder, placement.linear[2], offsets) },
		.map   = {},
	};
}

std::uint32_t inlineStep(Builder& builder, const Inlining& inlining, const Step& step) {
	switch (step.opcode) {
		case Opcode::Constant: return emitValue(builder, step.value);
		case Opcode::Argument: return emitValue(builder, inlining.scene.arguments[inlining.leaf.arguments + step.a]);
		case Opcode::PointX: return inlining.local[0];
		case Opcode::PointY: return inlining.local[1];
		case Opcode::PointZ: return inlining.local[2];
		case Opcode::NormalX:
		case Opcode::NormalY:
		case Opcode::NormalZ: return emitValue(builder, 0);
		default: return emitStep(builder, renumberStep(step, inlining.map));
	}
}

std::uint32_t inlineLeaf(Builder& builder, const Scene& scene, std::uint32_t index) {
	const Leaf& leaf       = scene.leaves[index];
	builder.leaf           = index;
	const Program& program = scene.programs[leaf.program];
	Inlining inlining      = startInlining(builder, scene, leaf);
	for (const Step& step : program.steps) {
		inlining.map.push_back(inlineStep(builder, inlining, step));
	}
	const std::uint32_t result = inlining.map[program.result];
	if (leaf.placement.stretch == 1) {
		return result;
	}
	return emitStep(builder, binaryStep(Opcode::Multiply, result, emitValue(builder, leaf.placement.stretch)));
}

std::uint32_t hardJoin(Builder& builder, Join join, const Pair& pair) {
	switch (join) {
		case Join::Merge: return emitStep(builder, binaryStep(Opcode::Minimum, pair.a, pair.b));
		case Join::Cut: return emitStep(builder, binaryStep(Opcode::Maximum, pair.a, emitNegate(builder, pair.b)));
		case Join::Clip: return emitStep(builder, binaryStep(Opcode::Maximum, pair.a, pair.b));
		case Join::Leaf: return pair.a;
	}

	return pair.a;
}

std::uint32_t smoothMinimum(Builder& builder, const Pair& pair, double radius) {
	Step step = binaryStep(Opcode::SmoothMinimum, pair.a, pair.b);
	step.c    = emitValue(builder, radius);
	return emitStep(builder, step);
}

Pair negatedPair(Builder& builder, Join join, const Pair& pair) {
	return Pair {
		.a = emitNegate(builder, pair.a),
		.b = join == Join::Clip ? emitNegate(builder, pair.b) : pair.b,
	};
}

std::uint32_t smoothJoin(Builder& builder, const Node& node, const Pair& pair) {
	if (node.join == Join::Merge) {
		return smoothMinimum(builder, pair, node.radius);
	}
	const Pair negated = negatedPair(builder, node.join, pair);
	return emitNegate(builder, smoothMinimum(builder, negated, node.radius));
}

std::uint32_t joinNode(Builder& builder, const Node& node, const Pair& pair) {
	builder.leaf = builder.program.steps[pair.a].leaf;
	if (node.radius <= 0) {
		return hardJoin(builder, node.join, pair);
	}
	return smoothJoin(builder, node, pair);
}

std::uint32_t compileNode(Builder& builder, const Scene& scene, std::uint32_t index) {
	const Node& node = scene.nodes[index];
	if (node.join == Join::Leaf) {
		return inlineLeaf(builder, scene, node.leaf);
	}
	std::uint32_t result = compileNode(builder, scene, node.children[0]);
	for (std::size_t i = 1; i < node.children.size(); ++i) {
		const Pair pair = {
			.a = result,
			.b = compileNode(builder, scene, node.children[i]),
		};

		result = joinNode(builder, node, pair);
	}
	return result;
}

std::uint32_t firstLeaf(const Scene& scene, std::uint32_t index) {
	const Node& node = scene.nodes[index];
	if (node.join == Join::Leaf) {
		return node.leaf;
	}
	return firstLeaf(scene, node.children[0]);
}

Builder namedBuilder(const std::string& name, std::uint32_t leaf) {
	const Program program = {
		.name      = name,
		.steps     = {},
		.result    = 0,
		.arguments = 0,
	};

	Builder builder = {
		.program = program,
		.leaf    = leaf,
		.point   = { 0, 1, 2 },
	};

	for (int i = 0; i < 3; ++i) {
		emitStep(builder, binaryStep(Opcode(int(Opcode::PointX) + i), 0, 0));
	}
	return builder;
}

Program namedNodeProgram(const Scene& scene, std::uint32_t node, const std::string& name) {
	Builder builder        = namedBuilder(name, firstLeaf(scene, node));
	builder.program.result = compileNode(builder, scene, node);
	return pruneProgram(builder.program);
}

Program objectProgram(const Scene& scene, std::uint32_t object) {
	return namedNodeProgram(scene, scene.objects[object], "object " + std::to_string(object));
}

Program leafProgram(const Scene& scene, std::uint32_t leaf) {
	Builder builder        = namedBuilder("leaf " + std::to_string(leaf), leaf);
	builder.program.result = inlineLeaf(builder, scene, leaf);
	return pruneProgram(builder.program);
}

std::vector<Program> objectPrograms(const Scene& scene) {
	std::vector<Program> programs;
	for (std::uint32_t i = 0; i < scene.objects.size(); ++i) {
		programs.push_back(objectProgram(scene, i));
	}
	return programs;
}

std::uint32_t firstSun(const Lighting& lighting) {
	for (std::uint32_t light = 0; light < lighting.lights.size(); ++light) {
		if (lighting.lights[light].kind == LightKind::Sun) {
			return light;
		}
	}
	return std::uint32_t(lighting.lights.size());
}

bool sceneRoughens(const Looks& looks) {
	for (const Coat& coat : looks.coats) {
		if (coat.effect == CoatEffect::Paint || coat.effect == CoatEffect::Rough || coat.effect == CoatEffect::Wet) {
			return true;
		}
	}
	return false;
}

bool hardUnion(const Node& node) {
	return node.join == Join::Merge && node.radius == 0;
}

void appendUnionParts(const Scene& scene, std::uint32_t node, std::vector<std::uint32_t>& parts) {
	const bool split = hardUnion(scene.nodes[node]) && namedNodeProgram(scene, node, "union").steps.size() >= fewestUnionSteps;
	if (!split) {
		parts.push_back(node);
		return;
	}
	for (const std::uint32_t child : scene.nodes[node].children) {
		appendUnionParts(scene, child, parts);
	}
}

std::vector<std::uint32_t> unionParts(const Scene& scene) {
	std::vector<std::uint32_t> parts;
	for (const std::uint32_t object : scene.objects) {
		appendUnionParts(scene, object, parts);
	}
	return parts;
}

double programDistance(const Program& program, const Probe& probe) {
	const Inputs<double> inputs = {
		.arguments = nullptr,
		.p         = { probe.p.x, probe.p.y, probe.p.z },
		.n         = { 0, 0, 0 },
	};

	return evaluate(program, inputs, probe.registers);
}

}
