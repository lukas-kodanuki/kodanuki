#pragma once

#include "frame/shown.h"
#include "image/image.h"

namespace kodanuki {

/**
 * Return an image drawn at the drawn size stretched to the shown size:
 * each shown pixel is the bilinear blend of the four drawn pixels whose
 * centres surround its centre, clamped at the edges. At a factor of 1 the
 * image comes back as it is.
 *
 * @param drawn The image at the drawn size, as the members before show
 *        it.
 * @param frame The frame, for the shown size and the factor.
 */
Image upsampledImage(const Image& drawn, const ShownFrame& frame);

}
