// Component identity and lifecycle. Exactly one DECLARE_COMPONENT_VERSION per DLL.
//
// SDK headers are included with angle brackets on purpose: the project marks angle includes as
// external and silences warnings in them, so /W4-as-errors applies to our code only.

#include <helpers/foobar2000+atl.h>

#include "platform/cover_hub.h"
#include "platform/graphics.h"
#include "version.h"

DECLARE_COMPONENT_VERSION(BETTERTABS_NAME, BETTERTABS_VERSION,
                          "A fast, modern tab container for Columns UI.\n"
                          "Add it from the Layout page (Splitters > " BETTERTABS_NAME ").\n\n"
                          "No third-party component dependencies.");

// Stops users from renaming the DLL, which would confuse the troubleshooter.
VALIDATE_COMPONENT_FILENAME("foo_bettertabs.dll");

namespace bettertabs {
namespace {

// Nothing at startup: containers set themselves up when Columns UI creates them.
class lifecycle : public initquit {
public:
    void on_quit() override {
        cover::shutdown();
        gfx::shutdown();
    }
};

FB2K_SERVICE_FACTORY(lifecycle);

} // namespace
} // namespace bettertabs
