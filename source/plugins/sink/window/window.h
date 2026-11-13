#pragma once

#include "plugins/sink/window/fly.h"

#include <SDL3/SDL.h>

#include <string>
#include <vector>

namespace kodanuki::viewer {

/**
 * What the person did in the window since the last poll.
 */
struct WindowInput {
	/**
	 * The window was closed or Escape pressed.
	 */
	bool closed;
	/**
	 * R was pressed: load the scene file again.
	 */
	bool reload;
	/**
	 * P was pressed: pause the clip's time, or let it run again.
	 */
	bool pause;
	/**
	 * How many clip frames the arrow keys stepped: the right one forward,
	 * the left one back.
	 */
	int step;
	/**
	 * The keys held and the mouse and wheel moved.
	 */
	Steering steering;
};

/**
 * How large a window opens and how large the frames it shows are.
 */
struct WindowShape {
	/**
	 * The window's width, in pixels, before it shrinks to fit the
	 * display.
	 */
	int width;
	/**
	 * The window's height, in pixels.
	 */
	int height;
	/**
	 * The frames' width, in pixels; the window scales them to fit.
	 */
	int frameWidth;
	/**
	 * The frames' height, in pixels.
	 */
	int frameHeight;
};

/**
 * An open window that shows frames of a fixed size, scaled to fit it.
 */
struct Window {
	/**
	 * The SDL window.
	 */
	SDL_Window* window;
	/**
	 * The renderer that draws into it.
	 */
	SDL_Renderer* renderer;
	/**
	 * The texture a frame is uploaded into, of the frame's size.
	 */
	SDL_Texture* texture;
	/**
	 * The frame's width, in pixels.
	 */
	int width;
	/**
	 * The frame's height, in pixels.
	 */
	int height;
};

/**
 * Open a resizable window for frames of a size, or print SDL's error and
 * exit 1.
 *
 * The window starts at the shape's window size, shrunk to fit the
 * display; a frame is shown scaled to fit it, linearly, keeping its
 * aspect, so frames smaller than the window are scaled up.
 *
 * @param title The window's title.
 * @param shape The window's size and the frames' size.
 */
Window openedWindow(const std::string& title, const WindowShape& shape);

/**
 * Close a window and its texture and renderer.
 *
 * @param window The window.
 */
void closedWindow(const Window& window);

/**
 * Wait for input at most a time, then return what the person did since
 * the last poll.
 *
 * The mouse is held in relative mode while its right button is down, so
 * the view turns without the pointer leaving the window.
 *
 * @param window The window.
 * @param milliseconds How long to wait at most for an event.
 */
WindowInput polledInput(const Window& window, int milliseconds);

/**
 * Show a frame: bytes in RGB order, by rows from the top left, of the
 * window's frame size.
 *
 * @param window The window.
 * @param rgb The frame's bytes, three a pixel.
 */
void shownFrame(const Window& window, const std::vector<unsigned char>& rgb);

/**
 * Set a window's title.
 *
 * @param window The window.
 * @param title The title.
 */
void titledWindow(const Window& window, const std::string& title);

}
