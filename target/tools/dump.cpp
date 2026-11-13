#include "core/options.h"
#include "sdf/scene.h"
#include "syntax/evaluator.h"

#include <cstdint>
#include <cstdio>

using namespace kodanuki;

const char* joinName(Join join) {
	switch (join) {
		case Join::Leaf: return "leaf";
		case Join::Merge: return "merge";
		case Join::Cut: return "cut";
		case Join::Clip: return "clip";
	}

	return "?";
}

void dumpNode(const Scene& scene, std::uint32_t index, int depth) {
	const Node& node = scene.nodes[index];
	std::printf("%*s%s", depth * 2, "", joinName(node.join));
	if (node.join == Join::Leaf) {
		const Leaf& leaf           = scene.leaves[node.leaf];
		const Placement& placement = leaf.placement;
		std::printf(" %s at %g %g %g stretch %g material %u\n", scene.programs[leaf.program].name.c_str(), placement.origin[0], placement.origin[1], placement.origin[2], placement.stretch, leaf.material);
		return;
	}
	std::printf(" radius %g\n", node.radius);
	for (const std::uint32_t child : node.children) {
		dumpNode(scene, child, depth + 1);
	}
}

int main(int argc, char** argv) {
	if (argc != 2) {
		std::fprintf(stderr, "usage: kodanuki_dump scene.sdf\n");
		return 1;
	}
	const SceneFile file = sceneOrExit(argv[1]);
	const Scene& scene   = file.scene;
	std::printf("%zu programs, %zu leaves, %zu nodes, %zu objects, %zu materials, %zu lights\n", scene.programs.size(), scene.leaves.size(), scene.nodes.size(), scene.objects.size(), file.looks.materials.size(), file.lighting.lights.size());
	for (const Program& program : scene.programs) {
		std::printf("program %s: %zu steps, %u arguments\n", program.name.c_str(), program.steps.size(), program.arguments);
	}
	for (const std::uint32_t object : scene.objects) {
		dumpNode(scene, object, 0);
	}
	return 0;
}
