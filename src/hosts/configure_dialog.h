#pragma once

// The Configure dialog (per container, modal or modeless) and the small Rename dialog. They work
// on copies; the container decides what to do with the result. Keeping the container out of here
// means the dialog cannot reach into a half-updated container, and the container cannot depend on
// dialog controls.

#include <windows.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "../model/codec.h"
#include "../model/settings.h"

namespace bettertabs {

struct TabEdit {
    //! The tab's position when the dialog opened; how the container finds it again.
    std::size_t id{0};
    std::wstring panel_name;
    TabExtra extra;
};

struct ConfigureState {
    Settings settings;
    //! In the order the dialog shows them.
    std::vector<TabEdit> tabs;
    //! "Columns UI" or "Default UI": the host's colours in the combo boxes.
    const wchar_t* ui_name{L"Columns UI"};
    //! That UI's name for its highlight colour ("highlight colour", "active item frame").
    const wchar_t* highlight_name{L"active item frame"};
    //! The host's font, for the Fonts page's "Default (...)" row and where its font dialog starts.
    std::wstring host_font_family;
    std::uint32_t host_font_tenths{90};
    //! The strip is dark: where the Look page's fill slider rests while Automatic is ticked.
    bool dark{true};
};

class ConfigureTarget {
public:
    //! Apply `state` to the real container now (live preview). Also used to restore on Cancel.
    virtual void preview(const ConfigureState& state) noexcept = 0;
    //! The modeless dialog closed: OK with `state` the result, or Cancel (also when it went
    //! with its owner). Not called for close_configure_dialog.
    virtual void configure_closed(bool ok, const ConfigureState& state) noexcept = 0;

protected:
    ~ConfigureTarget() = default;
};

//! Modal. With `live`, every change is previewed on the container as it is made; without it
//! (the container has no window, e.g. Columns UI's Layout page) nothing is previewed and tabs
//! cannot be reordered, since the Layout page's own tree holds the children's order.
//! Returns true for OK, with `state` holding the result.
bool run_configure_dialog(HWND parent, ConfigureState& state, ConfigureTarget& target, bool live);

//! Edits the tab's title (custom or not, title formatting or not). True for OK.
bool run_rename_dialog(HWND parent, const std::wstring& panel_name, TabExtra& extra);
//! Modeless and always live: the element stays usable (hover it, switch tabs) while the dialog is
//! open. Returns the dialog window, or null when it could not be created. `target` hears of the
//! end through configure_closed and must outlive the dialog (close_configure_dialog it first).
HWND open_configure_dialog(HWND owner, const ConfigureState& state, ConfigureTarget& target);

//! Closes a dialog from open_configure_dialog without telling its target.
void close_configure_dialog(HWND wnd);


} // namespace bettertabs
