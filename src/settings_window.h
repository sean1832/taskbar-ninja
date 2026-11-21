#pragma once

namespace ninja {
    /**
     * Opens the settings window, or brings it to the front when it is already open.
     * @remarks One window at most. Runs on the calling thread, so call it from the app UI thread.
     */
    void open_settings_window();
} // namespace ninja