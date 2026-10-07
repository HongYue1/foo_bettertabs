#pragma once

// Colour maths (OKLab, APCA and WCAG 2 contrast, cover colour -> accent) live in the shared
// fb2k-common library (sibling checkout, ..\fb2k-common), so a cover gives the same accent in
// every component. This header only brings it into the component's namespace.

#include "fbc/colour.h"

namespace bettertabs {
namespace colour = fbc::colour;
} // namespace bettertabs
