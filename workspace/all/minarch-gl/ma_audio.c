#include <stdbool.h>
#include <SDL2/SDL.h>
#include <msettings.h>
#include "ma_internal.h"
#include "ma_audio.h"

static bool resetAudio = false;

volatile int ma_audio_wrote_frame = 0;

/* ---------------------------------------------------------------------------
 * Asynchronous audio: RETRO_ENVIRONMENT_SET_AUDIO_CALLBACK (env 22).
 *
 * The protocol is a pull: the frontend keeps calling the core's callback and
 * the core writes samples from inside that call with the ordinary batch
 * callback (libretro.h:1122-1156).  RA runs that loop on its own audio thread
 * (audio_thread_wrapper.c:236-270) and parks 1 ms when a pull produced
 * nothing (:49); this is the same shape, over our SDL ring instead of a
 * device write.
 *
 * Backpressure is the pull rate: the write blocks on a full ring exactly like
 * RA's device write does on its audio thread, so an async core is held to
 * what the user actually hears.  Frame Pacing does not apply here -- it
 * decides the synchronous path -- which is also why the pull never sets
 * ma_audio_wrote_frame: the frame loop keeps its own timer, and audio is
 * decoupled from it by design (RA short-circuits audio_driver_frame_end() for
 * callback cores, audio_driver.c:7620-7635).
 *
 * Parking: the emulation loop is the liveness signal (MA_audio_async_tick()
 * is called once per iteration).  When it stops -- menu, sleep, a mode that
 * would not consume audio -- the pull parks instead of rendering audio for a
 * game that is not advancing, like RA's core_paused gate
 * (audio_driver.c:9350-9396).  RA does not send set_state(false) for a pause;
 * neither do we: set_state marks the frontend's audio path being up, and it
 * is delivered true on the first real pull and false at shutdown.
 * ------------------------------------------------------------------------ */
static struct retro_audio_callback async_cb;
static pthread_t async_thread;
static pthread_mutex_t async_lock = PTHREAD_MUTEX_INITIALIZER;
static int async_thread_live = 0;
static int async_state_sent = 0;
static volatile int async_stop = 0;
static volatile int async_pulling = 0;
static volatile uint32_t async_tick_ms = 0;

void MA_audio_async_tick(void) {
	async_tick_ms = SDL_GetTicks();
}

static int MA_audio_async_should_park(void) {
	if (!SND_isInitialized()) return 1;
	if (fast_forward && !ff_audio) return 1;
	/* Any rewind: the rewind path drives the core from the frame thread and
	 * plays its own audio, so a concurrent pull would double it. */
	if (rewinding) return 1;

	/* Three frame periods of silence from the loop: parked, not slow.  The
	 * floor keeps a very fast core from parking on ordinary jitter. */
	double fps = core.fps > 0.0 ? core.fps : 60.0;
	uint32_t limit = (uint32_t)(3000.0 / fps);
	if (limit < 100) limit = 100;
	if (limit > 1000) limit = 1000;
	return (uint32_t)(SDL_GetTicks() - async_tick_ms) > limit;
}

static void *MA_audio_async_main(void *unused) {
	(void)unused;
	for (;;) {
		struct retro_audio_callback cb;
		int stop;

		pthread_mutex_lock(&async_lock);
		cb = async_cb;
		stop = async_stop;
		pthread_mutex_unlock(&async_lock);
		if (stop || !cb.callback) break;

		if (MA_audio_async_should_park()) {
			usleep(2000);
			continue;
		}

		if (!async_state_sent) {
			if (cb.set_state) cb.set_state(true);
			async_state_sent = 1;
			LOG_info("minarch: async audio: pull thread running\n");
		}

		/* An async core has no other clock to be held to. */
		SND_setBlockOnFull(1);

		unsigned before = SND_batchCount();
		async_pulling = 1;
		cb.callback();
		async_pulling = 0;

		/* Nothing written: silent scene, or a core that fills a buffer only
		 * every few pulls.  Park like RA instead of spinning on it. */
		if (SND_batchCount() == before) usleep(1000);
	}

	pthread_mutex_lock(&async_lock);
	async_thread_live = 0;
	pthread_mutex_unlock(&async_lock);
	return NULL;
}

int MA_audio_async_register(const struct retro_audio_callback *cb) {
	if (!cb) {
		LOG_info("minarch: SET_AUDIO_CALLBACK probe -> supported\n");
		return 1; /* data == NULL asks whether the call is available */
	}

	pthread_mutex_lock(&async_lock);
	if (!cb->callback) {
		/* Unregister: both members NULL (libretro.h:6460-6461). */
		pthread_mutex_unlock(&async_lock);
		MA_audio_async_shutdown();
		return 1;
	}

	async_cb = *cb;
	if (!async_thread_live) {
		/* Before the pull thread exists: SND_init (load-time registration)
		 * reads the flag and sizes the ring; a late registration resizes the
		 * existing ring here, which is only safe with no concurrent writer
		 * (api.c). */
		SND_setAsyncCore(1);
		SND_enableAsyncRing();
		async_stop = 0;
		async_state_sent = 0;
		async_tick_ms = SDL_GetTicks();
		if (pthread_create(&async_thread, NULL, MA_audio_async_main, NULL) != 0) {
			LOG_error("minarch: async audio: pthread_create failed\n");
			memset(&async_cb, 0, sizeof(async_cb));
			pthread_mutex_unlock(&async_lock);
			return 0;
		}
		async_thread_live = 1;
		LOG_info("minarch: async audio callback registered\n");
	}
	else LOG_info("minarch: async audio callback updated\n");
	pthread_mutex_unlock(&async_lock);
	return 1;
}

/* RETRO_ENVIRONMENT_SET_MINIMUM_AUDIO_LATENCY (env 63) rebuilds the audio
 * system.  The async pull thread writes into the ring SND_quit frees, so it
 * must be stopped around the rebuild; the core's registration is kept (same
 * struct) and the thread restarted afterwards.  For a synchronous core this
 * is just SND_resetAudio(). */
void MA_audio_reinit(double sample_rate, double frame_rate) {
	struct retro_audio_callback saved;
	int was_live;

	pthread_mutex_lock(&async_lock);
	was_live = async_thread_live;
	saved = async_cb;
	pthread_mutex_unlock(&async_lock);

	if (was_live) MA_audio_async_shutdown();
	SND_resetAudio(sample_rate, frame_rate);
	if (was_live && saved.callback) MA_audio_async_register(&saved);
}

void MA_audio_async_shutdown(void) {
	async_stop = 1;

	/* Unregistering from inside the pull callback must not join ourselves:
	 * clearing the callback is enough, the loop drops out when the current
	 * pull returns and clears the live flag there. */
	int self = 0;
	pthread_mutex_lock(&async_lock);
	self = async_thread_live && pthread_equal(pthread_self(), async_thread);
	if (self) {
		memset(&async_cb, 0, sizeof(async_cb));
		async_state_sent = 0;
	}
	pthread_mutex_unlock(&async_lock);
	if (self) return;

	if (!async_thread_live) {
		pthread_mutex_lock(&async_lock);
		memset(&async_cb, 0, sizeof(async_cb));
		pthread_mutex_unlock(&async_lock);
		return;
	}

	/* A writer blocked on a full ring re-checks this flag inside its wait
	 * loop; clearing it is what lets the pull thread leave SND_batchSamples
	 * (within one 20 ms timeout) and see async_stop.  SND_abortBlockingWrite
	 * covers the case where nobody drains the ring at all (dead sink). */
	SND_setBlockOnFull(0);
	SND_abortBlockingWrite(1);
	pthread_join(async_thread, NULL);
	SND_abortBlockingWrite(0);

	retro_audio_set_state_callback_t set_state = NULL;
	pthread_mutex_lock(&async_lock);
	async_thread_live = 0;
	if (async_state_sent) set_state = async_cb.set_state;
	async_state_sent = 0;
	memset(&async_cb, 0, sizeof(async_cb));
	pthread_mutex_unlock(&async_lock);

	if (set_state) set_state(false);
	if (!SND_isInitialized()) SND_setAsyncCore(0);
	LOG_info("minarch: async audio: pull thread stopped\n");
}

void Audio_onSinkChanged(int device, int watch_event) {
	switch (watch_event) {
	case DIRWATCH_CREATE:      LOG_info("callback reason: DIRWATCH_CREATE\n");      break;
	case DIRWATCH_DELETE:      LOG_info("callback reason: DIRWATCH_DELETE\n");      break;
	case FILEWATCH_MODIFY:     LOG_info("callback reason: FILEWATCH_MODIFY\n");     break;
	case FILEWATCH_DELETE:     LOG_info("callback reason: FILEWATCH_DELETE\n");     break;
	case FILEWATCH_CLOSE_WRITE:LOG_info("callback reason: FILEWATCH_CLOSE_WRITE\n");break;
	}

	resetAudio = true;

	// FIXME: This shouldnt be necessary, alsa should just read .asoundrc for the changed default device.
	if (device == AUDIO_SINK_BLUETOOTH)
		SDL_setenv("AUDIODEV", "bluealsa", 1);
	else
		SDL_setenv("AUDIODEV", "default", 1);
}

void Audio_checkAndResetIfNeeded(void) {
	if (!resetAudio) return;
	resetAudio = false;
	LOG_info("Resetting audio device config! (new state: %s)\n", SDL_getenv("AUDIODEV"));
	SND_resetAudio(core.sample_rate, core.fps);
}

void audio_sample_callback(int16_t left, int16_t right) {
	if (rewinding && !rewind_ctx.audio) return;
	if (!fast_forward || ff_audio) {
		if (use_core_fps || fast_forward) {
			SND_batchSamples_fixed_rate(&(const SND_Frame){left,right}, 1);
		}
		else {
			SND_batchSamples(&(const SND_Frame){left,right}, 1);
		}
	}
}

/* Per-call cost of the audio path (this is inside retro_run; a headless
 * harness has no resampler and no ring, so this is another piece of the
 * frontend-vs-harness gap).  MINARCH_VIDEO_PROFILE=1 logs every 5 s. */
static int audprof_on = -1;
static uint64_t audprof_ns;
static unsigned audprof_n;
static uint32_t audprof_last_ms;
static uint64_t audprof_now(void) {
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

size_t audio_sample_batch_callback(const int16_t *data, size_t frames) {
	if (audprof_on < 0)
		audprof_on = getenv("MINARCH_VIDEO_PROFILE") ? 1 : 0;
	uint64_t ap_t0 = audprof_on ? audprof_now() : 0;
	if (rewinding && !rewind_ctx.audio) return frames;
	// Frame pacing source (Frontend > Frame Pacing):
	//   Audio   -- the write blocks on a full ring buffer (RA's blocking-write
	//              convention): the producer is held to the drain rate the user
	//              actually hears, and pace_frame() adds no timer on top.
	//   Classic -- upstream minarch's model: the write drops the samples that
	//              do not fit (api.c break-on-full; its 60%/99% pause
	//              thresholds stay active) and pace_frame()'s timer sets the
	//              rate.
	// Config_syncFrontend pushes the same value into SND_setBlockOnFull the
	// moment the menu changes it, so a writer already blocked on a full ring
	// exits at the audio callback's next broadcast. Re-applying it per batch
	// keeps the two in step after an audio re-init (SND_resetAudio).
	// The env-22 pull thread owns this decision while it is pulling (it always
	// blocks), so a core writing from inside its pull callback cannot flip the
	// flag under it.
	if (!async_pulling)
		SND_setBlockOnFull(frame_pacing == 1);
	if (!fast_forward || ff_audio) {
		size_t written;
		if (use_core_fps || fast_forward) {
			/* Fixed-rate path: resamples to the core's fps and DROPS when
			 * the ring is full (api.c, "should never happen"). It never
			 * holds the producer, so it is not a pace: leave
			 * ma_audio_wrote_frame alone and let pace_frame's timer be the
			 * clock for this frame (sync_ref = core fps means exactly that
			 * -- the timer is the reference, audio follows it).
			 * Marking this as an audio-paced frame is what left
			 * sync_ref=core unpaced altogether: no block, and the timer
			 * skipped as "already paced". */
			written = SND_batchSamples_fixed_rate((const SND_Frame*)data, frames);
		}
		else {
			/* SND_batchSamples: blocks when the pacing is Audio, drops when it
			 * is Classic. Only the blocking case holds this frame to the
			 * sound card, so only then may pace_frame skip its timer. */
			written = SND_batchSamples((const SND_Frame*)data, frames);
			if (frame_pacing == 1 && !async_pulling)
				ma_audio_wrote_frame = 1;
		}
		if (audprof_on) {
			uint64_t t = audprof_now();
			audprof_ns += t - ap_t0;
			audprof_n++;
			uint32_t now_ms = (uint32_t)(t / 1000000ull);
			if (!audprof_last_ms) audprof_last_ms = now_ms;
			if (now_ms - audprof_last_ms >= 5000) {
				LOG_info("minarch: audio batch %.2f ms avg over %u calls\n",
						(double)audprof_ns / 1e6 / (audprof_n ? audprof_n : 1), audprof_n);
				fflush(stdout);
				audprof_ns = 0; audprof_n = 0; audprof_last_ms = now_ms;
			}
		}
		return written;
	}
	else return frames;
}

/* RETRO_ENVIRONMENT_SET_AUDIO_BUFFER_STATUS_CALLBACK (62): once per frame,
 * from the main loop, after core.run().  RetroArch's shape (runloop.c:8029-
 * 8065): snapshot the ring, translate it to "percent occupied", call the core
 * with no audio lock held.  When there is no usable ring (no audio device,
 * teardown in progress) the core is told inactive instead of being fed
 * nonsense -- and it must not be called at all after it unregistered. */
void MA_Audio_buffer_status_tick(void)
{
	unsigned total = 0, free_frames = 0, underruns = 0;
	bool active = false;
	unsigned occupancy = 0;
	bool underrun = false;

	if (!core.audio_buffer_status) return;

	/* Optional latch: keep reporting "underrun" for N frames after the driver
	 * actually ran dry.  A single-frame pulse is enough for a core that skips
	 * one frame, but not for one that needs several consecutive skips to climb
	 * back (Yabadabadoo's auto mode caps consecutive skips at Frameskip
	 * Interval).  Off by default; MINARCH_AUDIO_UNDERRUN_LATCH=<frames>. */
	static int latch_enabled = -1, latch_frames = 0, latch_left = 0;
	if (latch_enabled < 0) {
		const char *v = getenv("MINARCH_AUDIO_UNDERRUN_LATCH");
		latch_frames = (v && v[0]) ? atoi(v) : 0;
		if (latch_frames < 0) latch_frames = 0;
		latch_enabled = 1;
	}

	if (SND_getBufferStats(&total, &free_frames, &underruns)) {
		if (total)
			occupancy = (unsigned)(100 - (free_frames * 100) / total);
		/* RA's low-water mark is 25% occupancy (runloop.c:8045-8060).  The
		 * ring is written under backpressure, so a core that cannot keep up
		 * also shows up in the driver's "silence for want of audio" counter;
		 * report that as an underrun too, so the core skips a frame before
		 * the user hears the gap. */
		int fresh = (underruns != core.audio_underruns_last);
		if (fresh) latch_left = latch_frames;
		else if (latch_left > 0) latch_left--;
		underrun = (occupancy < 25) || fresh || (latch_left > 0);
		active = true;
	}
	core.audio_underruns_last = underruns;
	core.audio_buffer_status(active, occupancy, underrun);

	/* Optional instrumentation (MINARCH_AUDIO_STATUS_LOG=1): what the core is
	 * being told, and what the ring really looks like, once every 5 s.  This
	 * is how the 25% low-water mark gets checked on a device rather than
	 * argued about: frequencies of active/underrun and the occupancy range. */
	{
		static int log_enabled = -1;
		if (log_enabled < 0) {
			const char *v = getenv("MINARCH_AUDIO_STATUS_LOG");
			log_enabled = (v && v[0] && v[0] != '0');
		}
		if (log_enabled) {
			static unsigned n, occ_sum, occ_min = 999, occ_max, und_n, act_n;
			static Uint32 last;
			static unsigned ur_last_seen;
			Uint32 now = SDL_GetTicks();
			n++; act_n += active ? 1 : 0; und_n += underrun ? 1 : 0;
			occ_sum += occupancy;
			if (occupancy < occ_min) occ_min = occupancy;
			if (occupancy > occ_max) occ_max = occupancy;
			if (!last) last = now;
			if (now - last >= 5000) {
				unsigned new_und = underruns - ur_last_seen;
				ur_last_seen = underruns;
				LOG_info("minarch: audio status %us: %u calls, active %u, underrun-flag %u, "
						"occupancy avg %u min %u max %u, %u new underrun periods\n",
						(now - last) / 1000, n, act_n, und_n,
						n ? occ_sum / n : 0, occ_min == 999 ? 0 : occ_min, occ_max, new_und);
				fflush(stdout);
				n = act_n = und_n = occ_sum = occ_max = 0; occ_min = 999; last = now;
			}
		}
	}
}
