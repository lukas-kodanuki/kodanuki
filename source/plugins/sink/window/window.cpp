#include "plugins/sink/window/window.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>

namespace kodanuki::viewer {

struct WindowSize {
	int width;
	int height;
};

void exitWithSdlError(const char* what) {
	std::fprintf(stderr, "%s: %s\n", what, SDL_GetError());
	std::exit(1);
}

void checkedSdl(bool succeeded, const char* what) {
	if (!succeeded) {
		exitWithSdlError(what);
	}
}

WindowSize fittedSize(int width, int height) {
	SDL_Rect usable = {
		.x = 0,
		.y = 0,
		.w = width,
		.h = height,
	};

	SDL_GetDisplayUsableBounds(SDL_GetPrimaryDisplay(), &usable);
	const double scale = std::min({ 1.0, double(usable.w) / width, double(usable.h) / height });

	const WindowSize size = {
		.width  = std::max(1, int(width * scale)),
		.height = std::max(1, int(height * scale)),
	};

	return size;
}

SDL_Window* createdSdlWindow(const std::string& title, int width, int height) {
	const WindowSize size = fittedSize(width, height);
	SDL_Window* window    = SDL_CreateWindow(title.c_str(), size.width, size.height, SDL_WINDOW_RESIZABLE);
	if (window == nullptr) {
		exitWithSdlError("cannot open a window");
	}
	return window;
}

SDL_Renderer* createdRenderer(SDL_Window* window, int width, int height) {
	SDL_Renderer* renderer = SDL_CreateRenderer(window, nullptr);
	if (renderer == nullptr) {
		exitWithSdlError("cannot create a renderer");
	}
	checkedSdl(SDL_SetRenderLogicalPresentation(renderer, width, height, SDL_LOGICAL_PRESENTATION_LETTERBOX), "cannot fit the frame");
	return renderer;
}

SDL_Texture* createdTexture(SDL_Renderer* renderer, int width, int height) {
	SDL_Texture* texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGB24, SDL_TEXTUREACCESS_STREAMING, width, height);
	if (texture == nullptr) {
		exitWithSdlError("cannot create a texture");
	}
	return texture;
}

Window openedWindow(const std::string& title, const WindowShape& shape) {
	checkedSdl(SDL_Init(SDL_INIT_VIDEO), "cannot start SDL");
	SDL_Window* window     = createdSdlWindow(title, shape.width, shape.height);
	SDL_Renderer* renderer = createdRenderer(window, shape.frameWidth, shape.frameHeight);

	const Window opened = {
		.window   = window,
		.renderer = renderer,
		.texture  = createdTexture(renderer, shape.frameWidth, shape.frameHeight),
		.width    = shape.frameWidth,
		.height   = shape.frameHeight,
	};

	return opened;
}

void closedWindow(const Window& window) {
	SDL_DestroyTexture(window.texture);
	SDL_DestroyRenderer(window.renderer);
	SDL_DestroyWindow(window.window);
	SDL_Quit();
}

WindowInput mouseButtonInput(const Window& window, const SDL_MouseButtonEvent& button, const WindowInput& input) {
	if (button.button == SDL_BUTTON_RIGHT) {
		SDL_SetWindowRelativeMouseMode(window.window, button.down);
	}
	return input;
}

WindowInput mouseMotionInput(const SDL_MouseMotionEvent& motion, const WindowInput& input) {
	if ((motion.state & SDL_BUTTON_RMASK) == 0) {
		return input;
	}

	WindowInput moved = input;
	moved.steering.turnX += motion.xrel;
	moved.steering.turnY += motion.yrel;
	return moved;
}

WindowInput wheelInput(const SDL_MouseWheelEvent& wheel, const WindowInput& input) {
	const float direction = wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -1.0f : 1.0f;
	WindowInput turned    = input;
	turned.steering.wheel += wheel.y * direction;
	return turned;
}

WindowInput keyInput(const SDL_KeyboardEvent& key, const WindowInput& input) {
	WindowInput pressed = input;
	pressed.closed      = input.closed || key.key == SDLK_ESCAPE;
	pressed.reload      = input.reload || (key.key == SDLK_R && !key.repeat);
	pressed.pause       = input.pause != (key.key == SDLK_P && !key.repeat);
	pressed.step        = input.step + (key.key == SDLK_RIGHT ? 1 : 0) - (key.key == SDLK_LEFT ? 1 : 0);
	return pressed;
}

WindowInput closedInput(const WindowInput& input) {
	WindowInput closed = input;
	closed.closed      = true;
	return closed;
}

WindowInput eventInput(const Window& window, const SDL_Event& event, const WindowInput& input) {
	switch (event.type) {
		case SDL_EVENT_QUIT: return closedInput(input);
		case SDL_EVENT_WINDOW_CLOSE_REQUESTED: return closedInput(input);
		case SDL_EVENT_KEY_DOWN: return keyInput(event.key, input);
		case SDL_EVENT_MOUSE_BUTTON_DOWN: return mouseButtonInput(window, event.button, input);
		case SDL_EVENT_MOUSE_BUTTON_UP: return mouseButtonInput(window, event.button, input);
		case SDL_EVENT_MOUSE_MOTION: return mouseMotionInput(event.motion, input);
		case SDL_EVENT_MOUSE_WHEEL: return wheelInput(event.wheel, input);
	}

	return input;
}

bool heldKey(const bool* keys, SDL_Scancode first, SDL_Scancode second) {
	return keys[first] || keys[second];
}

Steering heldSteering(const Steering& steering) {
	const bool* keys = SDL_GetKeyboardState(nullptr);
	Steering held    = steering;
	held.forward     = keys[SDL_SCANCODE_W];
	held.back        = keys[SDL_SCANCODE_S];
	held.left        = keys[SDL_SCANCODE_A];
	held.right       = keys[SDL_SCANCODE_D];
	held.rise        = keys[SDL_SCANCODE_SPACE];
	held.sink        = heldKey(keys, SDL_SCANCODE_LCTRL, SDL_SCANCODE_RCTRL);
	held.fast        = heldKey(keys, SDL_SCANCODE_LSHIFT, SDL_SCANCODE_RSHIFT);
	return held;
}

WindowInput quietInput() {
	const WindowInput input = {
		.closed   = false,
		.reload   = false,
		.pause    = false,
		.step     = 0,
		.steering = stillSteering(),
	};

	return input;
}

WindowInput polledInput(const Window& window, int milliseconds) {
	WindowInput input = quietInput();
	SDL_Event event;
	bool waiting = SDL_WaitEventTimeout(&event, milliseconds);
	while (waiting) {
		input   = eventInput(window, event, input);
		waiting = SDL_PollEvent(&event);
	}
	input.steering = heldSteering(input.steering);
	return input;
}

void shownFrame(const Window& window, const std::vector<unsigned char>& rgb) {
	SDL_UpdateTexture(window.texture, nullptr, rgb.data(), window.width * 3);
	SDL_SetRenderDrawColor(window.renderer, 0, 0, 0, SDL_ALPHA_OPAQUE);
	SDL_RenderClear(window.renderer);
	SDL_RenderTexture(window.renderer, window.texture, nullptr, nullptr);
	SDL_RenderPresent(window.renderer);
}

void titledWindow(const Window& window, const std::string& title) {
	SDL_SetWindowTitle(window.window, title.c_str());
}

}
