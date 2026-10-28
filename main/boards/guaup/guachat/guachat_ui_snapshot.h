#ifndef GUACHAT_UI_SNAPSHOT_H
#define GUACHAT_UI_SNAPSHOT_H

class Display;

// Development aid: waits for the UI to settle, seeds a few chat messages so the
// bubble layout is actually visible, then dumps a JPEG of the active screen to
// the serial port as base64.
//
// It exists because the panel is the one thing that cannot be checked by
// reading a log. Capture is done in-process with lv_snapshot_take(), so it does
// not need the device to be connected to a server.
//
// Gated by GUACHAT_UI_SNAPSHOT in config.h (0 = off).
void GuachatScheduleUiSnapshot(Display* display);

#endif  // GUACHAT_UI_SNAPSHOT_H
