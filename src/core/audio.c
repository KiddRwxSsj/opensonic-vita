/*
 * audio.c - audio module
 * Copyright (C) 2008-2010  Alexandre Martins <alemartf(at)gmail(dot)com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * --- PS Vita port ---
 * Upstream mixes music + up to 32 simultaneous sample voices through
 * Allegro's DIGMID software mixer talking to the host's audio driver.
 * There's no equivalent OS-level mixer exposed to homebrew here, so this
 * file *is* the mixer: a dedicated thread pulls PCM from a fixed-size
 * voice pool (one-shot/looping samples) plus the current music stream,
 * sums them into a stereo 16-bit buffer, and hands it to a single
 * sceAudioOut port. sceAudioOutOutput() blocks until the previous buffer
 * has finished playing, which is what paces the mixer thread -- the
 * standard pattern for this API.
 *
 * Samples (config/samples.def -> samples/*.wav, plus two .ogg one-shots:
 * 1up.ogg and goal.ogg) are short, so they're fully decoded and
 * resampled to the mixer's native format (48000 Hz stereo S16) once,
 * at load time -- runtime mixing is then a plain additive sum with
 * per-voice volume/pan/playback-rate stepping, no runtime format
 * conversion.
 *
 * Music (musics/*.ogg) is streamed with libvorbisfile, a chunk at a
 * time, with the same linear-resample-on-the-fly step used for voices
 * with a non-1.0 playback rate, in case a track isn't authored at
 * 48000 Hz.
 */

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/audioout.h>
#include <vorbis/vorbisfile.h>
#include "global.h"
#include "audio.h"
#include "osspec.h"
#include "stringutil.h"
#include "resourcemanager.h"
#include "logfile.h"
#include "timer.h"
#include "util.h"

/* mixer format */
/* --- Port type: SCE_AUDIO_OUT_PORT_TYPE_BGM, not MAIN --- root cause
 * for the changelog:
 *
 * The mixer's own output (captured post-mix, pre-sceAudioOutOutput())
 * was verified bit-clean well before this was found: no clipping, no
 * underruns, correct PCM. The distortion was confirmed present with
 * headphones (rules out the built-in speaker) and unchanged across a
 * 50% system volume drop (rules out acoustic/DAC clipping from a
 * too-hot signal) -- so it had to be happening to the signal *after*
 * it left this mixer, inside the sceAudioOut* pipeline itself.
 *
 * SCE_AUDIO_OUT_PORT_TYPE_MAIN on real Vita hardware runs through
 * additional system-level audio processing that BGM does not (MAIN is
 * meant for foreground app audio and gets mixed with system sounds/
 * volume-key overlay audio by the OS; BGM is meant for exactly this
 * use case -- a single app owning continuous music/SFX playback with
 * no need for that extra system mixing stage). On this
 * console/firmware, whatever that MAIN-only processing stage does
 * introduced the residual distortion -- not a bug in this file, and
 * not something our own sceAudioOutSetVolume()/port-reopen checks
 * could have caught, since both were already correct. Switching the
 * port type sidesteps that stage entirely.
 *
 * MIXER_RATE must still be 48000: sceAudioOutOpenPort()'s doc comment
 * lists 8000/11025/12000/16000/22050/24000/32000/44100/48000 as valid
 * frequencies in general, and while BGM/VOICE accept the wider list
 * (unlike MAIN, which only takes 48000 on real hardware), every
 * consumer of MIXER_RATE (WAV/OGG loaders, mix_music()'s resampler)
 * already resamples to this #define, so there's no reason to change
 * it now that BGM is the fixed port type. */
#define MIXER_RATE          48000
#define MIXER_CHANNELS      2
#define MIXER_GRAIN         256 /* frames per sceAudioOutOutput() call -- was 512/1024; testing progressively smaller grains */
#define MAX_VOICES          32
#define SOUND_INVALID_VOICE -1

/* private structures */
struct sound_t {
    int16 *pcm; /* interleaved stereo S16 @ MIXER_RATE */
    int frames;
    int voice_id;
};

/* --- PS Vita port: music streaming architecture ---
 * Decoding (ov_read()/ov_pcm_seek(), both blocking disk I/O + Vorbis
 * decode) used to happen inline inside mix_music(), on the real-time
 * mixer thread, while holding voices_mutex. Any slow read (SD card
 * hiccup, seek, whatever) made the mixer miss its sceAudioOutOutput()
 * deadline -> audible glitches in music AND in every one-shot sample
 * mixed into that same buffer. Raising the mixer thread's priority
 * doesn't help: a thread blocked on I/O is blocked regardless of
 * priority.
 *
 * Fix: a separate, lower-priority "decoder" thread keeps a ring buffer
 * of already-decoded native-rate PCM topped up, with a large margin
 * (MUSIC_RING_FRAMES) against I/O stalls. mix_music(), on the
 * real-time mixer thread, only ever touches memory that's already
 * decoded: it snapshots a small chunk out of the ring under a lock
 * (music_ring_mutex), then resamples/mixes from that local snapshot
 * with no lock held and no I/O possible. End-of-stream/looping
 * (ov_pcm_seek(), also I/O) is likewise handled entirely by the
 * decoder thread, never by the mixer thread.
 */
#define MUSIC_RING_FRAMES   16384 /* ~370ms of margin @ 44.1kHz against disk stalls */
#define MUSIC_LOCAL_CHUNK   2048  /* per-callback snapshot size, well above what one MIXER_GRAIN callback needs */
#define MUSIC_STALL_LIMIT   ((MIXER_RATE / MIXER_GRAIN) * 2) /* ~2s of consecutive
                                * near-empty mixer callbacks (not paused, not
                                * legitimately done) before we give up on this
                                * ring and let the caller's "not playing ->
                                * replay" fallback (level.c/options.c already
                                * have one) restart it, instead of staying
                                * silently wedged forever */

struct music_t {
    char path[1024];
    OggVorbis_File vf;
    int vf_open;
    int native_rate, native_channels;
    int16 decode_buf[MUSIC_RING_FRAMES * 2]; /* circular, interleaved, up to stereo */
    int rd_pos;              /* ring index of the next frame the mixer will consume */
    int wr_pos;               /* ring index of the next frame slot the decoder will fill */
    volatile int fill;        /* frames currently buffered and ready to consume */
    volatile int stream_done; /* set by the decoder thread: no more data will ever come */
    int stall_frames;         /* consecutive mixer callbacks with < 2 frames buffered,
                                * while supposedly still playing (not paused, not done) --
                                * used to detect a wedged ring (e.g. the decoder thread
                                * couldn't keep up with the disk seek at a loop boundary)
                                * so playback can be force-restarted instead of staying
                                * silent forever */
    double resample_frac;   /* fractional frame position for linear resampling */
    int total_frames;       /* native-rate frame count, for duration/looping */
    int frames_done;        /* native-rate frames delivered so far, this play-through */
    int loops_left;
    int is_paused;
};

typedef struct voice_t {
    sound_t *sample;
    double pos;   /* fractional frame index into sample->pcm */
    double step;  /* playback rate: 1.0 = native speed */
    float gain_l, gain_r;
    int looping;
    int active;
} voice_t;

/* private data */
static music_t *current_music;
static float music_volume = 1.0f;
static voice_t voices[MAX_VOICES];
static SceUID mixer_thread;
static SceUID music_decoder_thread;
static volatile int mixer_running;
static int audio_port = -1;
static SceUID voices_mutex;
static SceUID music_ring_mutex;
static SceUID music_vf_mutex; /* guards every call into libvorbisfile (ov_read(),
                                * ov_pcm_seek(), ov_clear()) for ANY music_t. libvorbisfile
                                * is not safe to call concurrently from two threads on the
                                * same OggVorbis_File -- and because music_t objects are
                                * cache-reused across level_unload()/level_load() pairs
                                * (see resourcemanager), the SAME OggVorbis_File can be
                                * targeted by the decoder thread (music_ring_topup(), an
                                * old play-through still draining) and by the main thread
                                * (music_play(), a new play-through starting) at the same
                                * time. This single global mutex (rather than one per
                                * music_t) is deliberate: these calls are already the slow,
                                * blocking-disk-I/O path and never run on the real-time
                                * mixer thread, so a coarse lock costs nothing audible and
                                * is much harder to get wrong than per-object locking. */

static int mixer_thread_entry(SceSize args, void *argp);
static int music_decoder_thread_entry(SceSize args, void *argp);
static void mix_buffer(int16 *out, int frames);
static void mix_music(int16 *out, int frames);
static void mix_voices(int16 *out, int frames);
static int16 *wav_load(const char *path, int *out_frames);
static int16 *ogg_load_sample(const char *path, int *out_frames);
static void music_ring_topup(music_t *m, int max_frames_hint);
static int next_free_voice();
static int has_extension(const char *path, const char *ext);

/*
 * music_load()
 * Loads a music from a file
 */
music_t *music_load(const char *path)
{
    char abs_path[1024];
    music_t *m;

    if(NULL == (m = resourcemanager_find_music(path))) {
        resource_filepath(abs_path, path, sizeof(abs_path), RESFP_READ);
        logfile_message("music_load('%s')", abs_path);

        m = mallocx(sizeof *m);
        memset(m, 0, sizeof *m);
        str_cpy(m->path, abs_path, sizeof(m->path));

        if(ov_fopen(abs_path, &m->vf) != 0) {
            logfile_message("music_load() error: can't open ogg stream");
            free(m);
            return NULL;
        }
        m->vf_open = TRUE;

        {
            vorbis_info *vi = ov_info(&m->vf, -1);
            m->native_rate = vi->rate;
            m->native_channels = vi->channels;
            m->total_frames = (int)ov_pcm_total(&m->vf, -1);
        }

        resourcemanager_add_music(path, m);
        resourcemanager_ref_music(path);

        logfile_message("music_load() ok");
    }
    else
        resourcemanager_ref_music(path);

    return m;
}


/*
 * music_unref()
 */
int music_unref(const char *path)
{
    return resourcemanager_unref_music(path);
}


/*
 * music_release_now()
 * Fully unrefs [path] (draining any leftover reference count -- e.g. a
 * jingle like "musics/invincible.ogg" that got music_load()'d more than
 * once in the same level) and, if that brings it to zero, removes and
 * destroys it immediately instead of waiting for the periodic garbage
 * collector. No-op if [path] was never loaded.
 */
void music_release_now(const char *path)
{
    while(resourcemanager_unref_music(path) > 0)
        ; /* drain: resourcemanager_unref_music() clamps at 0 and never
           * goes negative, so this always terminates */
    resourcemanager_remove_music(path); /* only actually removes if refcount is now <= 0,
                                          * which it always is at this point */
}


/*
 * music_destroy()
 */
void music_destroy(music_t *music)
{
    if(music != NULL) {
        if(current_music == music)
            music_stop();

        /* the decoder thread may already have snapshotted this exact
         * music_t as current_music a moment before music_stop() above
         * cleared it, and could still be blocked inside ov_read() or
         * ov_pcm_seek() on music->vf right now. Take the same lock those
         * calls use so we only ov_clear() once any such in-flight call
         * has returned -- ov_clear()'ing (or freeing) a vf that another
         * thread is still reading from is a use-after-free. */
        sceKernelLockMutex(music_vf_mutex, 1, NULL);
        if(music->vf_open)
            ov_clear(&music->vf);
        sceKernelUnlockMutex(music_vf_mutex, 1);

        free(music);
    }
}


/*
 * music_play()
 * Plays the given music and loops [loop] times.
 * Set loop equal to INFINITY to make it loop forever.
 */
void music_play(music_t *music, int loop)
{
    logfile_message("music_play('%s', loop=%d) [prev current_music=%p]", music ? music->path : "(null)", loop, (void*)current_music);

    music_stop();

    if(music != NULL) {
        /* music_stop() above only clears current_music -- it does NOT
         * wait for the decoder thread to actually notice. If [music]
         * is a cache-reused music_t (see resourcemanager: level_unload()
         * unrefs but historically didn't force an eviction, so the very
         * same object could still be the decoder thread's in-flight
         * "old" current_music at this exact moment), an unguarded
         * ov_pcm_seek() here would run concurrently with the decoder
         * thread's own ov_read()/ov_pcm_seek() in music_ring_topup() on
         * the very same OggVorbis_File. libvorbisfile isn't safe to call
         * from two threads on the same handle at once, so serialize on
         * music_vf_mutex here too. */
        sceKernelLockMutex(music_vf_mutex, 1, NULL);
        ov_pcm_seek(&music->vf, 0);
        sceKernelUnlockMutex(music_vf_mutex, 1);

        /* these fields are also read/written by the mixer and decoder
         * threads (under music_ring_mutex) whenever this music_t is
         * current_music -- take the same lock here instead of writing
         * them unguarded, or a restart racing with either thread can
         * desync fill/rd_pos/wr_pos and wedge the ring permanently */
        sceKernelLockMutex(music_ring_mutex, 1, NULL);
        music->rd_pos = 0;
        music->wr_pos = 0;
        music->fill = 0;
        music->stream_done = FALSE;
        music->stall_frames = 0;
        sceKernelUnlockMutex(music_ring_mutex, 1);

        music->resample_frac = 0.0;
        music->frames_done = 0;
        music->loops_left = loop;
        music->is_paused = FALSE;

        /* prime the ring synchronously (this runs on the main thread,
         * blocking briefly here is fine) so playback doesn't start with
         * an audible gap while the decoder thread catches up */
        music_ring_topup(music, MUSIC_RING_FRAMES);
        logfile_message("music_play('%s'): primed ring, fill=%d/%d frames", music->path, music->fill, MUSIC_RING_FRAMES);
    }

    sceKernelLockMutex(voices_mutex, 1, NULL);
    current_music = music;
    sceKernelUnlockMutex(voices_mutex, 1);

    logfile_message("music_play('%s'): current_music now set, done", music ? music->path : "(null)");
}


/*
 * music_stop()
 */
void music_stop()
{
    sceKernelLockMutex(voices_mutex, 1, NULL);
    current_music = NULL;
    sceKernelUnlockMutex(voices_mutex, 1);
}


/*
 * music_pause()
 */
void music_pause()
{
    if(current_music != NULL)
        current_music->is_paused = TRUE;
}


/*
 * music_resume()
 */
void music_resume()
{
    if(current_music != NULL)
        current_music->is_paused = FALSE;
}


/*
 * music_set_volume()
 * 0.0 <= volume <= 1.0
 */
void music_set_volume(float volume)
{
    music_volume = clip(volume, 0.0f, 1.0f);
}


/*
 * music_get_volume()
 */
float music_get_volume()
{
    return music_volume;
}


/*
 * music_is_playing()
 */
int music_is_playing()
{
    return (current_music != NULL) && !(current_music->is_paused);
}


/* sound management */

/*
 * sound_load()
 */
sound_t *sound_load(const char *path)
{
    char abs_path[1024];
    sound_t *s;

    if(NULL == (s = resourcemanager_find_sample(path))) {
        int frames;
        int16 *pcm;

        resource_filepath(abs_path, path, sizeof(abs_path), RESFP_READ);
        logfile_message("sound_load('%s')", abs_path);

        pcm = has_extension(abs_path, ".ogg") ? ogg_load_sample(abs_path, &frames) : wav_load(abs_path, &frames);
        if(pcm == NULL) {
            logfile_message("sound_load() error: can't load %s", abs_path);
            return NULL;
        }

        s = mallocx(sizeof *s);
        s->pcm = pcm;
        s->frames = frames;
        s->voice_id = SOUND_INVALID_VOICE;

        resourcemanager_add_sample(path, s);
        resourcemanager_ref_sample(path);

        logfile_message("sound_load() ok");
    }
    else
        resourcemanager_ref_sample(path);

    return s;
}

/*
 * sound_unref()
 */
int sound_unref(const char *path)
{
    return resourcemanager_unref_sample(path);
}


/*
 * sound_destroy()
 */
void sound_destroy(sound_t *sample)
{
    int i;

    if(sample != NULL) {
        sceKernelLockMutex(voices_mutex, 1, NULL);
        for(i = 0; i < MAX_VOICES; i++) {
            if(voices[i].active && voices[i].sample == sample)
                voices[i].active = FALSE;
        }
        sceKernelUnlockMutex(voices_mutex, 1);

        free(sample->pcm);
        free(sample);
    }
}


/*
 * sound_play()
 */
void sound_play(sound_t *sample)
{
    sound_play_ex(sample, 1.0, 0.0, 1.0, 0);
}


/*
 * sound_play_ex()
 * 0.0 <= volume <= 1.0
 * (left speaker) -1.0 <= pan <= 1.0 (right speaker)
 * 1.0 = default frequency
 * 0 = no loops
 */
void sound_play_ex(sound_t *sample, float vol, float pan, float freq, int loop)
{
    int idx;

    if(!sample)
        return;

    vol = clip(vol, 0.0f, 1.0f);
    pan = clip(pan, -1.0f, 1.0f);
    freq = max(freq, 0.0f);

    sceKernelLockMutex(voices_mutex, 1, NULL);
    idx = next_free_voice();
    if(idx >= 0) {
        voices[idx].sample = sample;
        voices[idx].pos = 0.0;
        voices[idx].step = freq;
        voices[idx].gain_l = vol * (pan <= 0.0f ? 1.0f : 1.0f - pan);
        voices[idx].gain_r = vol * (pan >= 0.0f ? 1.0f : 1.0f + pan);
        voices[idx].looping = (loop != 0);
        voices[idx].active = TRUE;
        sample->voice_id = idx;
    }
    sceKernelUnlockMutex(voices_mutex, 1);
}


/*
 * sound_stop()
 */
void sound_stop(sound_t *sample)
{
    if(!sample)
        return;

    sceKernelLockMutex(voices_mutex, 1, NULL);
    if(sample->voice_id != SOUND_INVALID_VOICE && sample->voice_id < MAX_VOICES)
        voices[sample->voice_id].active = FALSE;
    sceKernelUnlockMutex(voices_mutex, 1);
}


/*
 * sound_is_playing()
 */
int sound_is_playing(sound_t *sample)
{
    if(sample && sample->voice_id != SOUND_INVALID_VOICE)
        return voices[sample->voice_id].active && voices[sample->voice_id].sample == sample;
    return FALSE;
}


/* audio manager */

/*
 * audio_init()
 * upstream's header declares this with an empty parameter list while
 * its definition takes an (unused) int; that mismatch is harmless in C
 * and is kept as-is rather than "fixed", since the parameter was never
 * read by the original implementation either.
 */
void audio_init(int nomusic)
{
    int i;
    (void)nomusic;

    logfile_message("audio_init()");

    /* --- double-init guard (kept permanently, not diagnostic-only) ---
     * If audio_init() is ever called again while a port from a
     * previous call is still open, two ports would write to the
     * hardware simultaneously -- phase cancellation / doubling. This
     * can't silently happen: it's logged loudly and the second call is
     * refused instead of quietly leaking a second port. */
    if(audio_port >= 0) {
        logfile_message("audio_init(): CALLED AGAIN while audio_port=%d is still open -- refusing to open a second port (this would double-mix on real hardware)", audio_port);
        return;
    }

    current_music = NULL;
    for(i = 0; i < MAX_VOICES; i++)
        voices[i].active = FALSE;

    voices_mutex = sceKernelCreateMutex("audio_mixer_mutex", 0, 0, NULL);
    music_ring_mutex = sceKernelCreateMutex("audio_music_ring_mutex", 0, 0, NULL);
    music_vf_mutex = sceKernelCreateMutex("audio_music_vf_mutex", 0, 0, NULL);

    logfile_message("audio_init(): sceAudioOutOpenPort(BGM, grain=%d, freq=%d, STEREO)", MIXER_GRAIN, MIXER_RATE);
    audio_port = sceAudioOutOpenPort(SCE_AUDIO_OUT_PORT_TYPE_BGM, MIXER_GRAIN, MIXER_RATE, SCE_AUDIO_OUT_MODE_STEREO);
    if(audio_port < 0) {
        logfile_message("audio_init(): sceAudioOutOpenPort failed (0x%08X)", audio_port);
        return;
    }
    logfile_message("audio_init(): sceAudioOutOpenPort ok, port=%d", audio_port);

    /* explicit 0dB volume on both channels, instead of relying on
     * whatever the port's undocumented-in-practice default turns out
     * to be on this firmware/CFW -- rules out a non-unity default
     * gain as the source of the distortion. */
    {
        int vol[2];
        vol[0] = SCE_AUDIO_VOLUME_0DB;
        vol[1] = SCE_AUDIO_VOLUME_0DB;
        if(sceAudioOutSetVolume(audio_port, SCE_AUDIO_VOLUME_FLAG_L_CH | SCE_AUDIO_VOLUME_FLAG_R_CH, vol) < 0)
            logfile_message("audio_init(): sceAudioOutSetVolume() failed -- port left at its own default volume");
        else
            logfile_message("audio_init(): sceAudioOutSetVolume() set to SCE_AUDIO_VOLUME_0DB on both channels");
    }

    mixer_running = TRUE;
    /* priority raised slightly above the main thread (0x10000100) --
     * lower numeric value = higher priority on the Vita, that part was
     * correct. But a big jump down to 0x10000064 fell outside the
     * priority range the kernel accepts for user threads on real
     * hardware: sceKernelCreateThread() failed outright (returned a
     * negative UID) instead of just running at a bad priority, which is
     * why audio went from "distorted" to "completely silent". Using a
     * small, conservative bump from the known-working baseline instead. */
    mixer_thread = sceKernelCreateThread("audio_mixer_thread", mixer_thread_entry, 0x100000F0, 0x10000, 0, 0, NULL);
    if(mixer_thread >= 0)
        sceKernelStartThread(mixer_thread, 0, NULL);
    else
        logfile_message("audio_init(): couldn't start the mixer thread (sceKernelCreateThread returned 0x%08X)", mixer_thread);

    /* lower priority (higher numeric value) than both the main thread
     * and the mixer thread: this thread only does disk I/O + Vorbis
     * decode into the ring buffer, never anything real-time-critical,
     * so it shouldn't compete for CPU against either of them. */
    music_decoder_thread = sceKernelCreateThread("audio_music_decoder_thread", music_decoder_thread_entry, 0x10000110, 0x10000, 0, 0, NULL);
    if(music_decoder_thread >= 0)
        sceKernelStartThread(music_decoder_thread, 0, NULL);
    else
        logfile_message("audio_init(): couldn't start the music decoder thread (sceKernelCreateThread returned 0x%08X)", music_decoder_thread);

    logfile_message("audio_init() ok");
}


/*
 * audio_release()
 */
void audio_release()
{
    logfile_message("audio_release()");

    mixer_running = FALSE;
    if(music_decoder_thread >= 0) {
        sceKernelWaitThreadEnd(music_decoder_thread, NULL, NULL);
        sceKernelDeleteThread(music_decoder_thread);
    }
    if(mixer_thread >= 0) {
        sceKernelWaitThreadEnd(mixer_thread, NULL, NULL);
        sceKernelDeleteThread(mixer_thread);
    }

    /* mixer thread is fully stopped and joined at this point */
    if(audio_port >= 0)
        sceAudioOutReleasePort(audio_port);
    audio_port = -1; /* so a legitimate later audio_init() isn't blocked
                       * by the double-init guard added above */

    sceKernelDeleteMutex(music_vf_mutex);
    sceKernelDeleteMutex(music_ring_mutex);
    sceKernelDeleteMutex(voices_mutex);

    logfile_message("audio_release() ok");
}


/*
 * audio_update()
 * The mixer runs on its own thread (see mixer_thread_entry()), so
 * there's nothing to pump from the main loop; kept for API
 * compatibility with the rest of the engine, which calls this once
 * per frame.
 */
void audio_update()
{
}


/* private stuff */

/* mixer thread: continuously builds MIXER_GRAIN-frame buffers and
 * blocks on sceAudioOutOutput(), which paces this loop to real time */
static int mixer_thread_entry(SceSize args, void *argp)
{
    static int16 buffer[MIXER_GRAIN * MIXER_CHANNELS];
    int heartbeat = 0;
    (void)args;
    (void)argp;

    logfile_message("mixer_thread_entry(): started");

    while(mixer_running) {
        mix_buffer(buffer, MIXER_GRAIN);
        sceAudioOutOutput(audio_port, buffer);

        /* ~1x/second heartbeat (48000/256 ~= 187.5 calls/s). If this
         * stops advancing, or the gap between two heartbeats is much
         * bigger than ~1s, the mixer thread itself is stalling at/after
         * sceAudioOutOutput() -- a level this code's own STALL_LIMIT
         * check can never see, since that check only runs when this
         * thread is actually being scheduled. */
        if(++heartbeat >= 188) {
            heartbeat = 0;
            logfile_message("mixer_thread_entry(): heartbeat, t=%d ms, current_music=%p, is_paused=%d",
                (int)timer_get_ticks(), (void*)current_music, current_music ? current_music->is_paused : -1);
        }
    }

    return 0;
}

static void mix_buffer(int16 *out, int frames)
{
    memset(out, 0, sizeof(int16) * frames * MIXER_CHANNELS);

    sceKernelLockMutex(voices_mutex, 1, NULL);
    mix_music(out, frames);
    mix_voices(out, frames);
    sceKernelUnlockMutex(voices_mutex, 1);
}

static inline int16 clamp_s16(int v)
{
    if(v > 32767) return 32767;
    if(v < -32768) return -32768;
    return (int16)v;
}

/* streams and mixes the current music, resampling from its native rate
 * to MIXER_RATE with linear interpolation. Reads only from the ring
 * buffer that music_decoder_thread_entry() keeps topped up -- never
 * touches the file or the Vorbis decoder itself, so this function can
 * never block on disk I/O. */
static void mix_music(int16 *out, int frames)
{
    music_t *m = current_music;
    static int16 local[MUSIC_LOCAL_CHUNK * 2]; /* interleaved, up to stereo */
    int local_count, channels, idx, i, consumed;
    double step, frac;

    if(m == NULL || m->is_paused)
        return;

    channels = m->native_channels;
    step = (double)m->native_rate / (double)MIXER_RATE;

    /* snapshot a chunk of already-decoded PCM out of the ring; this is
     * the only lock held here, and it guards plain memory, never I/O */
    sceKernelLockMutex(music_ring_mutex, 1, NULL);
    local_count = min(m->fill, MUSIC_LOCAL_CHUNK);
    for(i = 0; i < local_count; i++) {
        int slot = (m->rd_pos + i) % MUSIC_RING_FRAMES;
        int c;
        for(c = 0; c < channels; c++)
            local[i * channels + c] = m->decode_buf[slot * channels + c];
    }
    sceKernelUnlockMutex(music_ring_mutex, 1);

    if(local_count >= 2)
        m->stall_frames = 0;
    else {
        if(m->stream_done) {
            current_music = NULL; /* truly nothing left, ever: stop cleanly */
            return;
        }

        /* the decoder thread hasn't caught up yet this callback --
         * output silence for this buffer instead of blocking on disk
         * I/O. With MUSIC_RING_FRAMES's margin this should be rare; if
         * PROFILE-style checks ever show it firing often, grow the
         * margin or the decoder's chunk size, don't add a wait here.
         *
         * but if it keeps happening for MUSIC_STALL_LIMIT consecutive
         * callbacks (e.g. the decoder thread got starved of CPU right
         * as it needed to do the disk seek for a loop-back, under a
         * heavier per-frame workload than an idle menu/options screen),
         * the ring is wedged: nothing will ever refill it on its own,
         * yet current_music stays non-NULL, so music_is_playing()
         * keeps reporting "playing" and callers' own restart fallback
         * (level.c/options.c already have one) never fires. Give up on
         * this play-through so that fallback can kick in instead of
         * staying silent for the rest of the level. */
        if(++(m->stall_frames) >= MUSIC_STALL_LIMIT) {
            logfile_message("mix_music(): STALL LIMIT reached for '%s' (fill=%d, stream_done=%d) -- giving up on this play-through, current_music=NULL", m->path, m->fill, m->stream_done);
            current_music = NULL;
        }

        return;
    }

    frac = m->resample_frac;
    idx = 0; /* index into local[]; local[0] == the ring's current rd_pos */
    for(i = 0; i < frames; i++) {
        int i0, i1;
        int16 l0, r0, l1, r1, l, r;

        if(idx + 1 >= local_count)
            break; /* ran out of snapshot mid-callback -- silence the rest */

        i0 = idx;
        i1 = idx + 1;
        if(channels >= 2) {
            l0 = local[i0 * channels + 0];
            r0 = local[i0 * channels + 1];
            l1 = local[i1 * channels + 0];
            r1 = local[i1 * channels + 1];
        }
        else {
            l0 = r0 = local[i0];
            l1 = r1 = local[i1];
        }

        l = (int16)(l0 + (l1 - l0) * frac);
        r = (int16)(r0 + (r1 - r0) * frac);

        out[i * 2 + 0] = clamp_s16(out[i * 2 + 0] + (int)(l * music_volume));
        out[i * 2 + 1] = clamp_s16(out[i * 2 + 1] + (int)(r * music_volume));

        frac += step;
        while(frac >= 1.0) {
            frac -= 1.0;
            idx++;
        }
    }
    m->resample_frac = frac;

    consumed = idx; /* whole native frames advanced past; safe to drop from ring */
    if(consumed > 0) {
        sceKernelLockMutex(music_ring_mutex, 1, NULL);
        m->rd_pos = (m->rd_pos + consumed) % MUSIC_RING_FRAMES;
        m->fill -= consumed;
        sceKernelUnlockMutex(music_ring_mutex, 1);
    }
}

/* background thread: keeps every active music's ring buffer topped up.
 * All disk I/O (ov_read()) and looping (ov_pcm_seek()) happen here,
 * off the real-time mixer thread. Runs at lower priority than both the
 * main thread and the mixer thread. */
static int music_decoder_thread_entry(SceSize args, void *argp)
{
    (void)args;
    (void)argp;

    while(mixer_running) {
        music_t *m;

        sceKernelLockMutex(voices_mutex, 1, NULL);
        m = current_music;
        sceKernelUnlockMutex(voices_mutex, 1);

        if(m != NULL && m->vf_open && !m->is_paused && !m->stream_done)
            music_ring_topup(m, MUSIC_RING_FRAMES);

        /* small yield either way: keeps this thread from spinning
         * tightly once the ring is full (music_ring_topup() then
         * returns immediately), while still refilling comfortably
         * faster than real-time playback drains the ring */
        sceKernelDelayThread(2000);
    }

    return 0;
}

/* decodes up to max_frames_hint more native-rate PCM frames into m's
 * ring buffer (bounded by however much free space the ring actually
 * has), and handles end-of-stream/looping. The ov_read()/ov_pcm_seek()
 * calls here are the only place this file still does blocking disk
 * I/O -- called only from music_decoder_thread_entry(), and once
 * synchronously from music_play() to prime playback, never from the
 * real-time mixer thread. */
static void music_ring_topup(music_t *m, int max_frames_hint)
{
    int16 scratch[2048]; /* interleaved, up to stereo */
    int channels = m->native_channels;
    int free_frames, want_frames, bytes_read, bitstream;

    sceKernelLockMutex(music_ring_mutex, 1, NULL);
    free_frames = MUSIC_RING_FRAMES - m->fill;
    sceKernelUnlockMutex(music_ring_mutex, 1);

    if(free_frames <= 0)
        return;

    want_frames = min(free_frames, (int)(sizeof(scratch) / sizeof(int16)) / channels);
    want_frames = min(want_frames, max_frames_hint);
    if(want_frames <= 0)
        return;

    /* music_vf_mutex serializes every touch of m->vf against music_play()
     * (main thread) and music_destroy() (whichever thread releases this
     * music_t). Without this, a music_t reused across a level_unload()/
     * level_load() pair could have its vf seeked-and-reset by music_play()
     * on the main thread at the exact moment this thread is mid-ov_read()
     * on it -- libvorbisfile isn't safe to call concurrently like that,
     * and the corruption doesn't necessarily stay confined to this one
     * music_t: a wedged/crashed decoder thread takes every later track
     * (menu, options, the next level, ...) down with it. */
    sceKernelLockMutex(music_vf_mutex, 1, NULL);
    bytes_read = (int)ov_read(&m->vf, (char *)scratch,
        want_frames * channels * (int)sizeof(int16), 0, 2, 1, &bitstream);

    if(bytes_read > 0) {
        int frames_got = bytes_read / (channels * (int)sizeof(int16));
        int i;

        sceKernelUnlockMutex(music_vf_mutex, 1); /* done with m->vf; the ring-buffer
                                                    * copy below only needs music_ring_mutex */
        sceKernelLockMutex(music_ring_mutex, 1, NULL);
        for(i = 0; i < frames_got; i++) {
            int slot = (m->wr_pos + i) % MUSIC_RING_FRAMES;
            int c;
            for(c = 0; c < channels; c++)
                m->decode_buf[slot * channels + c] = scratch[i * channels + c];
        }
        m->wr_pos = (m->wr_pos + frames_got) % MUSIC_RING_FRAMES;
        m->fill += frames_got;
        sceKernelUnlockMutex(music_ring_mutex, 1);
    }
    else {
        /* end of stream */
        if(--(m->loops_left) >= 0)
            ov_pcm_seek(&m->vf, 0); /* I/O, but off the real-time thread */
        sceKernelUnlockMutex(music_vf_mutex, 1);

        if(m->loops_left < 0) {
            sceKernelLockMutex(music_ring_mutex, 1, NULL);
            m->stream_done = TRUE;
            sceKernelUnlockMutex(music_ring_mutex, 1);
        }
    }
}

/* additively mixes every active one-shot/looping voice into out */
static void mix_voices(int16 *out, int frames)
{
    int v, i;

    for(v = 0; v < MAX_VOICES; v++) {
        voice_t *voice = &voices[v];
        if(!voice->active)
            continue;

        for(i = 0; i < frames; i++) {
            int idx = (int)voice->pos;
            int16 s;

            if(idx >= voice->sample->frames) {
                if(voice->looping) {
                    voice->pos = 0.0;
                    idx = 0;
                }
                else {
                    voice->active = FALSE;
                    break;
                }
            }

            /* sample->pcm is already stereo; average channels down to mono
             * source amplitude, then apply the per-voice pan gains */
            s = (int16)(((int)voice->sample->pcm[idx * 2] + (int)voice->sample->pcm[idx * 2 + 1]) / 2);

            out[i * 2 + 0] = clamp_s16(out[i * 2 + 0] + (int)(s * voice->gain_l));
            out[i * 2 + 1] = clamp_s16(out[i * 2 + 1] + (int)(s * voice->gain_r));

            voice->pos += voice->step;
        }
    }
}

static int next_free_voice()
{
    int i;
    for(i = 0; i < MAX_VOICES; i++) {
        if(!voices[i].active)
            return i;
    }
    return -1; /* voice pool exhausted; the sample is silently dropped,
                * same as upstream running out of Allegro voices */
}

/*
 * wav_load()
 * Minimal RIFF/WAVE PCM reader (8/16-bit, mono/stereo, any sample
 * rate). Resamples to MIXER_RATE stereo S16 so the runtime mixer never
 * has to convert formats.
 */
static int16 *wav_load(const char *path, int *out_frames)
{
    FILE *fp;
    char chunk_id[4];
    uint32 chunk_size;
    int channels = 0, bits_per_sample = 0, sample_rate = 0;
    long data_offset = 0, data_size = 0;
    int16 *raw = NULL, *out;
    int raw_frames, out_frames_count;
    int i;

    fp = fopen(path, "rb");
    if(fp == NULL)
        return NULL;

    if(fread(chunk_id, 1, 4, fp) != 4 || memcmp(chunk_id, "RIFF", 4) != 0) {
        fclose(fp);
        return NULL;
    }
    fseek(fp, 4, SEEK_CUR); /* RIFF size */
    if(fread(chunk_id, 1, 4, fp) != 4 || memcmp(chunk_id, "WAVE", 4) != 0) {
        fclose(fp);
        return NULL;
    }

    while(fread(chunk_id, 1, 4, fp) == 4) {
        if(fread(&chunk_size, 4, 1, fp) != 1)
            break;

        if(memcmp(chunk_id, "fmt ", 4) == 0) {
            uint16 format, ch, block_align, bps;
            uint32 rate, byte_rate;
            long chunk_start = ftell(fp);

            fread(&format, 2, 1, fp);
            fread(&ch, 2, 1, fp);
            fread(&rate, 4, 1, fp);
            fread(&byte_rate, 4, 1, fp);
            fread(&block_align, 2, 1, fp);
            fread(&bps, 2, 1, fp);
            (void)format; (void)byte_rate; (void)block_align;

            channels = ch;
            sample_rate = rate;
            bits_per_sample = bps;

            fseek(fp, chunk_start + chunk_size, SEEK_SET);
        }
        else if(memcmp(chunk_id, "data", 4) == 0) {
            data_offset = ftell(fp);
            data_size = chunk_size;
            fseek(fp, chunk_size, SEEK_CUR);
        }
        else {
            fseek(fp, chunk_size, SEEK_CUR);
        }

        if(chunk_size & 1) /* chunks are word-aligned */
            fseek(fp, 1, SEEK_CUR);
    }

    if(channels <= 0 || sample_rate <= 0 || data_offset == 0 || data_size <= 0) {
        fclose(fp);
        return NULL;
    }

    fseek(fp, data_offset, SEEK_SET);

    if(bits_per_sample == 16) {
        raw_frames = (int)(data_size / (channels * 2));
        raw = mallocx(sizeof(int16) * raw_frames * channels);
        fread(raw, sizeof(int16) * channels, raw_frames, fp);
    }
    else if(bits_per_sample == 8) {
        int total_samples = (int)(data_size / channels) * channels;
        uint8 *raw8 = mallocx(data_size);
        raw_frames = total_samples / channels;
        fread(raw8, 1, data_size, fp);
        raw = mallocx(sizeof(int16) * raw_frames * channels);
        for(i = 0; i < raw_frames * channels; i++)
            raw[i] = (int16)(((int)raw8[i] - 128) * 256);
        free(raw8);
    }
    else {
        logfile_message("wav_load(): unsupported bit depth (%d) in %s", bits_per_sample, path);
        fclose(fp);
        return NULL;
    }

    fclose(fp);

    /* resample to MIXER_RATE stereo */
    out_frames_count = (int)((double)raw_frames * MIXER_RATE / sample_rate);
    if(out_frames_count < 1)
        out_frames_count = 1;
    out = mallocx(sizeof(int16) * out_frames_count * 2);

    for(i = 0; i < out_frames_count; i++) {
        double src_pos = (double)i * sample_rate / MIXER_RATE;
        int idx = (int)src_pos;
        double frac = src_pos - idx;
        int idx1;
        int16 l0, r0, l1, r1;

        if(idx >= raw_frames)
            idx = raw_frames - 1;
        idx1 = (idx + 1 < raw_frames) ? idx + 1 : idx;

        if(channels >= 2) {
            l0 = raw[idx * channels + 0];
            r0 = raw[idx * channels + 1];
            l1 = raw[idx1 * channels + 0];
            r1 = raw[idx1 * channels + 1];
        }
        else {
            l0 = r0 = raw[idx];
            l1 = r1 = raw[idx1];
        }

        out[i * 2 + 0] = (int16)(l0 + (l1 - l0) * frac);
        out[i * 2 + 1] = (int16)(r0 + (r1 - r0) * frac);
    }

    free(raw);

    *out_frames = out_frames_count;
    return out;
}

static int has_extension(const char *path, const char *ext)
{
    size_t path_len = strlen(path), ext_len = strlen(ext);
    if(path_len < ext_len)
        return FALSE;
    return str_icmp(path + (path_len - ext_len), ext) == 0;
}

/*
 * ogg_load_sample()
 * Fully decodes a (short) Ogg Vorbis file into MIXER_RATE stereo S16,
 * for the handful of one-shot samples authored as .ogg instead of .wav
 * (see config/samples.def: 1up.ogg, goal.ogg). Shares the same linear
 * resampling approach as wav_load() and mix_music()'s streaming path.
 */
static int16 *ogg_load_sample(const char *path, int *out_frames)
{
    OggVorbis_File vf;
    vorbis_info *vi;
    int channels, native_rate, total_frames;
    int16 *raw, *out;
    int out_frames_count, i, pos, bitstream;

    if(ov_fopen(path, &vf) != 0)
        return NULL;

    vi = ov_info(&vf, -1);
    channels = vi->channels;
    native_rate = vi->rate;
    total_frames = (int)ov_pcm_total(&vf, -1);

    if(total_frames <= 0) {
        ov_clear(&vf);
        return NULL;
    }

    raw = mallocx(sizeof(int16) * total_frames * channels);
    pos = 0;
    while(pos < total_frames * channels) {
        int want = (total_frames * channels - pos) * (int)sizeof(int16);
        int got = (int)ov_read(&vf, (char *)(raw + pos), want, 0, 2, 1, &bitstream);
        if(got <= 0)
            break;
        pos += got / (int)sizeof(int16);
    }
    ov_clear(&vf);

    out_frames_count = (int)((double)total_frames * MIXER_RATE / native_rate);
    if(out_frames_count < 1)
        out_frames_count = 1;
    out = mallocx(sizeof(int16) * out_frames_count * 2);

    for(i = 0; i < out_frames_count; i++) {
        double src_pos = (double)i * native_rate / MIXER_RATE;
        int idx = (int)src_pos;
        double frac = src_pos - idx;
        int idx1;
        int16 l0, r0, l1, r1;

        if(idx >= total_frames)
            idx = total_frames - 1;
        idx1 = (idx + 1 < total_frames) ? idx + 1 : idx;

        if(channels >= 2) {
            l0 = raw[idx * channels + 0];
            r0 = raw[idx * channels + 1];
            l1 = raw[idx1 * channels + 0];
            r1 = raw[idx1 * channels + 1];
        }
        else {
            l0 = r0 = raw[idx];
            l1 = r1 = raw[idx1];
        }

        out[i * 2 + 0] = (int16)(l0 + (l1 - l0) * frac);
        out[i * 2 + 1] = (int16)(r0 + (r1 - r0) * frac);
    }

    free(raw);
    *out_frames = out_frames_count;
    return out;
}
