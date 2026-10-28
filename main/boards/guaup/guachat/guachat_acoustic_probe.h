#ifndef GUACHAT_ACOUSTIC_PROBE_H
#define GUACHAT_ACOUSTIC_PROBE_H

class AudioCodec;

// One-shot acoustic echo-path measurement for the guachat board.
//
// The point is to answer two questions before writing a single line of AEC
// code, because both decide whether device-side AEC is worth building at all:
//
//   1. How much is the speaker signal attenuated by the time it reaches the
//      microphone? AEC can only subtract a linear echo; if the echo is buried
//      in structure-borne vibration the path is short, loud and non-linear, and
//      no amount of filtering will help.
//   2. How late does the echo arrive? The AEC reference has to be delayed by
//      exactly this much, or esp-sr's delay estimator never locks on. Almost
//      all of it is I2S DMA buffering, not air travel.
//
// Method: play a linear chirp, record the microphone at the same time, then
// cross-correlate. A linear chirp compresses to a sharp peak, so the delay
// falls out of the peak position and the gain out of the peak height.
//
// BLOCKING, takes about 5 seconds, and it makes noise. It must run before
// AudioService starts its input task, otherwise two readers race on the same
// I2S RX channel and the recording is interleaved garbage. In practice that
// means calling it from the board constructor.
void GuachatRunAcousticProbe(AudioCodec* codec);

#endif  // GUACHAT_ACOUSTIC_PROBE_H
