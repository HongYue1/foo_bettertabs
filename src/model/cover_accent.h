#pragma once

// The colours of a cover come from the shared fb2k-common library (..\fb2k-common):
// fbc::cover_colours runs on the worker that just decoded the cover, once per cover. Its raw
// primary colour is not legible as is; the host passes it through
// colour::accent_for_background against the strip background.

#include "fbc/cover_accent.h"

namespace bettertabs {
using fbc::DecodedImage;
} // namespace bettertabs
