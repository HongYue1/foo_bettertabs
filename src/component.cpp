// Component identity and lifecycle. Exactly one DECLARE_COMPONENT_VERSION per DLL.
//
// SDK headers are included with angle brackets on purpose: the project marks angle includes as
// external and silences warnings in them, so /W4-as-errors applies to our code only.

#include <helpers/foobar2000+atl.h>

#include "platform/cover_hub.h"
#include "platform/graphics.h"
#include "version.h"

DECLARE_COMPONENT_VERSION(BETTERTABS_NAME, BETTERTABS_VERSION,
                          "A fast, modern tab container for Columns UI and the Default UI.\n"
                          "Columns UI: add it from the Layout page (Splitters > " BETTERTABS_NAME ").\n"
                          "Default UI: add it in layout editing mode (Containers > " BETTERTABS_NAME ").\n\n"
                          "No third-party component dependencies.");

// Stops users from renaming the DLL, which would confuse the troubleshooter.
VALIDATE_COMPONENT_FILENAME("foo_bettertabs.dll");

namespace bettertabs {
namespace {

class lifecycle : public initquit {
public:
    void on_quit() override {
        cover::shutdown();
        gfx::shutdown();
    }
};

FB2K_SERVICE_FACTORY(lifecycle);

// The earliest stage: warm DirectWrite's process-wide caches on a CPU worker while foobar2000
// reads its configuration, so the first container's font setup is not a cold ~150 ms on the main
// thread. Nothing else is touched; the main thread at worst waits for whatever is left.
class warm_up : public init_stage_callback {
public:
    void on_init_stage(t_uint32 stage) override {
        if (stage != init_stages::before_config_read) return;
        fb2k::inCpuWorkerThread([] { (void)gfx::warm_text(); });
    }
};

FB2K_SERVICE_FACTORY(warm_up);

} // namespace
} // namespace bettertabs
