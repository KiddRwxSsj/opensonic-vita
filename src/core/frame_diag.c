/*
 * frame_diag.c - see frame_diag.h for usage and rationale.
 *
 * Everything here is in-memory only: no logfile_message(), no fopen(),
 * no disk I/O of any kind, so it's safe to leave compiled into normal
 * builds -- when toggled off (the default), frame_diag_sample() still
 * runs (it's just a timestamp + array write, needed to keep the
 * window populated so the overlay is instant when toggled on) but
 * frame_diag_render() and the toggle check are single early-return
 * branches, and scePowerGet*Frequency() is only called once, not
 * every frame.
 */

#include "frame_diag.h"
#include <string.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/ctrl.h>
#include <psp2/power.h>
#include "../entities/font.h"
#include "v2d.h" /* ADAPT: if v2d_t lives in another header (e.g. util.h) */

#define FRAME_HISTORY 300 /* ~5s @ 60fps */

static uint64_t frame_times_us[FRAME_HISTORY];
static int frame_write_idx = 0;
static int frames_recorded = 0; /* caps at FRAME_HISTORY */
static uint64_t last_tick = 0;

static int overlay_active = FALSE;

/* toggle debounce: L+R must be held continuously for TOGGLE_HOLD_US
 * before it fires once, then requires both released before it can
 * fire again -- prevents a normal L+R gameplay combo (if your game
 * has one) from spuriously toggling this, and prevents one held press
 * from toggling on/off repeatedly. */
#define TOGGLE_HOLD_US 500000 /* 0.5s */
static uint64_t combo_held_since = 0;
static int toggle_armed = TRUE; /* becomes FALSE once fired, until both released */

/* cached clock readings -- these don't change frame to frame in normal
 * operation, so they're read once at init and once more each time the
 * overlay is toggled on (in case a plugin changed them mid-session),
 * never every frame. */
static int cpu_freq = 0, bus_freq = 0, gpu_freq_es4 = 0, gpu_freq_xbar = 0;

/* the overlay's own font object, created once in frame_diag_init() and
 * updated (never recreated) each time frame_diag_render() runs while
 * active -- font_set_text() just rewrites its internal string, so this
 * is cheap and allocation-free per frame. */
static font_t *diag_font = NULL;

static void refresh_clock_readings(void)
{
    cpu_freq = scePowerGetArmClockFrequency();
    bus_freq = scePowerGetBusClockFrequency();
    gpu_freq_es4 = scePowerGetGpuClockFrequency();
    gpu_freq_xbar = scePowerGetGpuXbarClockFrequency();
}

void frame_diag_init(void)
{
    memset(frame_times_us, 0, sizeof(frame_times_us));
    frame_write_idx = 0;
    frames_recorded = 0;
    last_tick = 0;
    overlay_active = FALSE; /* off by default, per spec */
    combo_held_since = 0;
    toggle_armed = TRUE;
    refresh_clock_readings();

    if(diag_font == NULL)
        diag_font = font_create(0); /* ADAPT: 0 = debug font type;
                                      * use whatever "type" you use for the HUD */
}

void frame_diag_sample(void)
{
    uint64_t now = sceKernelGetProcessTimeWide();

    if(last_tick != 0) {
        frame_times_us[frame_write_idx] = now - last_tick;
        frame_write_idx = (frame_write_idx + 1) % FRAME_HISTORY;
        if(frames_recorded < FRAME_HISTORY)
            frames_recorded++;
    }
    last_tick = now;
}

void frame_diag_update_toggle(void)
{
    SceCtrlData pad;
    int select_held, start_held;
    uint64_t now;

    /* SELECT+START, not L1+R1: L1+R1 collides with screenshot.c's
     * reserved screenshot combo (same physical triggers on Vita),
     * and screenshot_update() runs earlier in the loop and consumes
     * the combo first, so this toggle would never fire. SELECT+START
     * isn't used anywhere else in the engine. */
    sceCtrlPeekBufferPositive(0, &pad, 1);
    select_held = (pad.buttons & SCE_CTRL_SELECT) != 0;
    start_held = (pad.buttons & SCE_CTRL_START) != 0;

    now = sceKernelGetProcessTimeWide();

    if(select_held && start_held) {
        if(combo_held_since == 0)
            combo_held_since = now;

        if(toggle_armed && (now - combo_held_since) >= TOGGLE_HOLD_US) {
            overlay_active = !overlay_active;
            toggle_armed = FALSE; /* wait for release before firing again */
            if(overlay_active)
                refresh_clock_readings(); /* pick up any mid-session change */
        }
    }
    else {
        combo_held_since = 0;
        toggle_armed = TRUE;
    }
}

int frame_diag_is_active(void)
{
    return overlay_active;
}

void frame_diag_release(void)
{
    /* ADAPT: if your API exposes font_destroy(font_t*), call it here.
     * That signature wasn't confirmed -- if it doesn't exist, leaving
     * this as a no-op is safe (the font lives until the process ends). */
    diag_font = NULL;
}

void frame_diag_render(void)
{
    uint64_t sum_us = 0, max_us = 0;
    int i, n;
    double avg_ms, max_ms, fps;

    if(!overlay_active || diag_font == NULL)
        return;

    n = frames_recorded;
    for(i = 0; i < n; i++) {
        uint64_t t = frame_times_us[i];
        sum_us += t;
        if(t > max_us)
            max_us = t;
    }
    avg_ms = (n > 0) ? ((double)sum_us / n) / 1000.0 : 0.0;
    max_ms = (double)max_us / 1000.0;
    fps = (avg_ms > 0.0) ? 1000.0 / avg_ms : 0.0;

    /* one multi-line string, one font_set_text() call -- avoids
     * juggling several font_t objects for a 4-line overlay */
    font_set_text(diag_font,
        "CPU %dMHz  BUS %dMHz\n"
        "GPU-ES4 %dMHz  GPU-XBAR %dMHz\n"
        "frame avg %.2fms  max %.2fms (n=%d)\n"
        "~%.1f fps avg",
        cpu_freq, bus_freq, gpu_freq_es4, gpu_freq_xbar,
        avg_ms, max_ms, n, fps);

    /* ADAPT: v2d_new(0,0) assumes your font_render() treats the
     * camera position as a screen-space offset (HUD in screen space,
     * not world space) when the font was created as overlay/debug
     * text -- which is OpenSonic's typical pattern for the HUD.
     * If your debug-type font_t already self-positions differently,
     * adjust here. */
    font_render(diag_font, v2d_new(0, 0));
}
