#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace kodanuki {

struct Renderer;

/**
 * A flat array a frame hands the kernels: the tree, the materials, the
 * lights, a plugin's prepared table.
 *
 * Its elements are plain data, without a pointer or a container inside
 * (each is checked trivially copyable where the block is made), so a
 * backend may copy the bytes into a buffer of its own. The block only
 * names the array; the plugin that made it keeps it, as long as the
 * frame it was read from is drawn.
 */
struct Block {
	/**
	 * The plugin's name, a dot and the array's, as
	 * `geometry/tree.nodes`.
	 */
	std::string name;
	/**
	 * The first element.
	 */
	const void* data;
	/**
	 * How many elements.
	 */
	std::size_t count;
	/**
	 * The size of an element in bytes.
	 */
	std::size_t size;
};

/**
 * Return the blocks of the frame being drawn: those of the Geometry
 * plugin, then the Transport's, then each Preparation's, as their
 * `blocks` hooks name them.
 *
 * @param renderer The loop's state, its world built and its frame
 * prepared.
 */
std::vector<Block> frameBlocks(const Renderer& renderer);

} // namespace kodanuki
