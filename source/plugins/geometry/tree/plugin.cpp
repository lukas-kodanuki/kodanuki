#include "plugins/geometry/tree/plugin.h"

#include "core/block.h"
#include "core/render.h"
#include "core/step.h"
#include "plugins/geometry/tree/walk.h"
#include "plugins/geometry/tree/world.h"
#include "plugins/transport/paths/paths.h"

#include <memory>
#include <optional>
#include <type_traits>
#include <vector>

namespace kodanuki {

using TreeWorld = std::optional<BuiltGeometry>;

static_assert(std::is_trivially_copyable_v<PackedNode>, "a block's element is plain data");
static_assert(std::is_trivially_copyable_v<SurfaceLeaf>, "a block's element is plain data");
static_assert(std::is_trivially_copyable_v<SolidLeaf>, "a block's element is plain data");
static_assert(std::is_trivially_copyable_v<ProgramList>, "a block's element is plain data");
static_assert(std::is_trivially_copyable_v<ProgramReference>, "a block's element is plain data");
static_assert(std::is_trivially_copyable_v<ProgramCut>, "a block's element is plain data");

TreeWorld* treeWorld(const Renderer& renderer) {
	return static_cast<TreeWorld*>(stateOf(renderer, "geometry/tree"));
}

TreeWorld& openedTreeWorld(Renderer& renderer) {
	if (!treeWorld(renderer)) {
		keptState(renderer, "geometry/tree", std::make_shared<TreeWorld>());
	}
	return *treeWorld(renderer);
}

Geometry treeBuilt(Renderer& renderer) {
	TreeWorld& world = openedTreeWorld(renderer);
	world.reset();
	world.emplace(builtGeometry(renderer.file, renderer.options.choices, pathsWorld(stateOf(renderer, "transport/paths"))));
	return treeGeometry(renderer.file.scene, world->tree);
}

double treeBytes(const Tree& tree) {
	const double nodes   = double(tree.nodes.size() * sizeof(PackedNode) + tree.surfaces.size() * sizeof(SurfaceLeaf) + tree.solids.size() * sizeof(SolidLeaf));
	const double lists   = double(tree.runs.size() * sizeof(std::uint32_t) + tree.lists.size() * sizeof(ProgramList) + tree.references.size() * sizeof(ProgramReference));
	const double cutting = double(tree.cuts.size() * sizeof(ProgramCut) + tree.cutSteps.size() * sizeof(std::uint16_t));
	return nodes + lists + cutting;
}

std::vector<Counter> treePublished(const Renderer& renderer) {
	const TreeWorld* world = treeWorld(renderer);
	return {
		Counter { .name = "bytes.tree", .value = world && *world ? treeBytes((*world)->tree) : 0 },
	};
}

std::vector<Block> treeBlocksOf(const Tree& tree) {
	return {
		Block { .name = "geometry/tree.nodes", .data = tree.nodes.data(), .count = tree.nodes.size(), .size = sizeof(PackedNode) },
		Block { .name = "geometry/tree.surfaces", .data = tree.surfaces.data(), .count = tree.surfaces.size(), .size = sizeof(SurfaceLeaf) },
		Block { .name = "geometry/tree.solids", .data = tree.solids.data(), .count = tree.solids.size(), .size = sizeof(SolidLeaf) },
		Block { .name = "geometry/tree.runs", .data = tree.runs.data(), .count = tree.runs.size(), .size = sizeof(std::uint32_t) },
		Block { .name = "geometry/tree.lists", .data = tree.lists.data(), .count = tree.lists.size(), .size = sizeof(ProgramList) },
		Block { .name = "geometry/tree.references", .data = tree.references.data(), .count = tree.references.size(), .size = sizeof(ProgramReference) },
		Block { .name = "geometry/tree.cuts", .data = tree.cuts.data(), .count = tree.cuts.size(), .size = sizeof(ProgramCut) },
		Block { .name = "geometry/tree.cutSteps", .data = tree.cutSteps.data(), .count = tree.cutSteps.size(), .size = sizeof(std::uint16_t) },
	};
}

std::vector<Block> treeBlocks(const Renderer& renderer) {
	const TreeWorld* world = treeWorld(renderer);
	if (!world || !*world) {
		return {};
	}
	return treeBlocksOf((*world)->tree);
}

Plugin geometryTreePlugin() {
	Hooks hooks      = noHooks();
	hooks.buildWorld = treeBuilt;
	hooks.published  = treePublished;
	hooks.blocks     = treeBlocks;
	return Plugin {
		.name   = "geometry/tree",
		.point  = Point::Geometry,
		.effect = Effect::Exact,
		.hooks  = hooks,
		.reads  = Reads::Nothing,
		.after  = {},
	};
}

} // namespace kodanuki
