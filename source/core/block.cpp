#include "core/block.h"

#include "core/render.h"

namespace kodanuki {

void pluginBlocks(const Renderer& renderer, const Plugin& plugin, std::vector<Block>& blocks) {
	if (!plugin.hooks.blocks) {
		return;
	}
	for (const Block& block : plugin.hooks.blocks(renderer)) {
		blocks.push_back(block);
	}
}

std::vector<Block> frameBlocks(const Renderer& renderer) {
	std::vector<Block> blocks;
	pluginBlocks(renderer, renderer.geometry, blocks);
	pluginBlocks(renderer, renderer.transport, blocks);
	for (const Plugin& plugin : renderer.preparations) {
		pluginBlocks(renderer, plugin, blocks);
	}
	return blocks;
}

} // namespace kodanuki
