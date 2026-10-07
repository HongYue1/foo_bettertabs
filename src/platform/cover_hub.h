#pragma once

// The now-playing cover's colours come from the shared fb2k-common library (..\fb2k-common):
// fbc::cover is the hub every container with a "cover" accent source subscribes to. Main thread
// only; costs nothing until the first subscriber.

#include "fbc/cover_hub.h"

namespace bettertabs {
namespace cover = fbc::cover;
} // namespace bettertabs
