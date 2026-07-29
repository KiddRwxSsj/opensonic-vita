/*
 * frame_diag.h - lightweight, in-memory frame timing + clock overlay
 *
 * PS Vita port diagnostic module. Confirms objectively whether the
 * console is running at nominal clocks and whether the main loop is
 * holding a stable frametime, WITHOUT touching the real-time audio
 * thread and WITHOUT any blocking I/O (no logfile_message(), no
 * fopen/fwrite) during gameplay.
 *
 * Disabled by default. Toggle at runtime with SELECT+START held
 * together for ~0.5s (debounced so a normal tap doesn't trip it).
 * NOT L1+R1: that combo is already reserved by screenshot.c for
 * screenshots on this port, and screenshot_update() runs earlier in
 * the main loop, so it would consume the combo before this toggle
 * ever saw it.
 *
 * Usage:
 *   1. Call frame_diag_init() once, after input and video are ready.
 *   2. Call frame_diag_sample() exactly once per iteration of the main
 *      loop, on the main/game thread (never from the audio thread).
 *   3. Call frame_diag_render() once per frame, after your normal scene
 *      render, right before the frame is presented -- it no-ops
 *      instantly when the overlay is toggled off.
 *   4. Call frame_diag_release() at shutdown (optional; frees nothing
 *      dynamic, just here for symmetry with audio_release()-style APIs).
 */

#ifndef _FRAME_DIAG_H
#define _FRAME_DIAG_H

/* Call once at startup, after input AND the font system are ready
 * (font_create() needs the font system initialized). */
void frame_diag_init(void);

/* Call once per main-loop iteration, on the main thread only.
 * Cheap: one timestamp read + one array write, no branches on I/O. */
void frame_diag_sample(void);

/* Call once per main-loop iteration, on the main thread only. Reads
 * SELECT+START directly via sceCtrlPeekBufferPositive() -- deliberately
 * bypasses your input_t gameplay layer, so this needs no input_t
 * instance and doesn't touch input.c at all. Safe to call alongside
 * your normal input_update()/input_button_down() calls; reading the
 * pad this way doesn't consume or interfere with that layer. */
void frame_diag_update_toggle(void);

/* Call once per frame, AFTER scn->render() and BEFORE video_render(),
 * so the overlay text is part of the frame that actually gets flipped
 * to the screen. No-op (single branch) when the overlay is off. */
void frame_diag_render(void);

/* Optional, symmetric with audio_release()-style shutdown. */
void frame_diag_release(void);

/* Returns TRUE/nonzero if the overlay is currently active -- exposed in
 * case other systems want to know (e.g. to also dump extra info). */
int frame_diag_is_active(void);

#endif
