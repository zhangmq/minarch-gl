// ma_pace -- the run loop's clock and its measurement, out of minarch.c.
//
// Two things that are decisions of the run loop rather than of any one
// subsystem, kept together and out of the overridden minarch.c so that override
// stays hook-shaped:
//
//   MA_pace_frame()    composes the frame's pace (RetroArch runloop PACE_*):
//                      audio backpressure, or else the timer on an absolute
//                      timeline.  The display is not an exclusive source: a
//                      blocking swap only quantizes the present, so a core
//                      slower than the panel is still timed here.
//                      fast-forward and rewind own their own cadence and are
//                      left alone.
//   MA_telemetry_tick() logs the frame-time distribution and (when the hw path
//                      is active) the video_cb/draw counters.  Inert unless
//                      MINARCH_FRAME_LOG is set or /tmp/minarch_frame_log
//                      exists; it is what settled the present investigation.
#ifndef MA_PACE_H
#define MA_PACE_H

void MA_pace_frame(void);
void MA_telemetry_tick(void);

#endif
