#ifndef GUACHAT_TASK_REPORT_H
#define GUACHAT_TASK_REPORT_H

// Development aid: dumps every FreeRTOS task with its priority, core affinity
// and measured CPU share, so the scheduling layout can be checked against what
// the code claims instead of inferred from it.
//
// Two samples are taken five seconds apart and differenced, because the
// cumulative runtime counters still carry the boot storm and would otherwise
// show audio and Wi-Fi tasks eating the world for ever.
//
// Gated by GUACHAT_TASK_REPORT in config.h (0 = off).
void GuachatScheduleTaskReport();

#endif  // GUACHAT_TASK_REPORT_H
