Audio Tests/
============

Speaker / amp / sound-system check tones for the field kit.
All files: 16-bit stereo PCM WAV, 44.1 kHz, 30 seconds.

WARNING
-------
  Start the source volume LOW. Full-scale (0 dBFS) and pink noise
  can damage tweeters if cranked. Turn off EQ, loudness, and bass boost
  when setting amp gains.


Level guide
-----------
  -6 dBFS   Safe listening / general checks (most files here)
  -10 dBFS  Hotter everyday listening when setting gains
  -5 dBFS   Middle ground for gain staging
  0 dBFS    Max digital level — gain setup only, not casual play


01_Channel/  — wiring & speaker ID
----------------------------------

left_only_440Hz_-6dBFS_30s.wav
  What: 440 Hz tone in the LEFT channel only; right is silent.
  Use:  Play it. You should hear only the left speaker(s).
        If the right side talks, or nothing plays, L/R wiring or
        balance is wrong. Fix balance first, then check speaker wires.

right_only_440Hz_-6dBFS_30s.wav
  What: 440 Hz tone in the RIGHT channel only; left is silent.
  Use:  Same as above for the right side. Confirm each speaker
        matches the file name (left vs right).

polarity_in_phase_200Hz_-6dBFS_30s.wav
  What: Same 200 Hz tone in BOTH channels, same polarity (in phase).
  Use:  Play at a moderate level. Correct wiring should sound solid,
        centered, and fuller in the bass. This is your “good” reference.
        Compare with the out-of-phase file below.

polarity_out_of_phase_200Hz_-6dBFS_30s.wav
  What: 200 Hz in both channels, but RIGHT is inverted (out of phase).
  Use:  On a correctly wired system this should sound thin, hollow, or
        “wide” with weak bass. If THIS file sounds better/fuller than
        the in-phase file, a speaker (or whole channel) is wired
        backwards — swap +/− on that speaker and re-check.


02_Tones/  — steady reference sines (−6 dBFS)
---------------------------------------------

50Hz_sine_-6dBFS_30s.wav
  What: Steady 50 Hz bass tone (both channels).
  Use:  Check sub / midbass output and hear rattles in cabinets,
        stands, or nearby furniture. Good “is the sub alive?” listen.
        Not for max gain (use 05_Gain for that).

100Hz_sine_-6dBFS_30s.wav
  What: Steady 100 Hz tone.
  Use:  Midbass / crossover area. Hear if that band is present and
        whether cabinets or rooms buzz around that frequency.

440Hz_sine_-6dBFS_30s.wav
  What: Concert A (440 Hz).
  Use:  Quick “anything playing?” check and midrange presence.
        Also fine for a rough left/right balance by ear.

1kHz_sine_-6dBFS_30s.wav
  What: 1 kHz — the classic engineering reference tone.
  Use:  Midrange speaker check. Level matching between sides by ear
        or SPL meter. Not the full-scale gain file (see 05_Gain).

10kHz_sine_-6dBFS_30s.wav
  What: Steady 10 kHz high tone.
  Use:  Tweeter / high-frequency path check. Confirm both sides have
        sparkle. Missing on one side = blown or disconnected tweeter,
        or a bad crossover / HF path.


03_Sweeps/  — rising tones (find rattles & dropouts)
----------------------------------------------------

sweep_20Hz-20kHz_log_-6dBFS_30s.wav
  What: Log sweep from 20 Hz up to 20 kHz over 30 seconds.
  Use:  Play once through the system. Listen for buzzes, rattles,
        and dead spots (where the tone vanishes). Note roughly when
        in the clip the problem happens (early = bass, late = treble).
        Best first pass for “what’s wrong with this system?”

sweep_20Hz-200Hz_log_-6dBFS_30s.wav
  What: Slow bass-only log sweep, 20 Hz to 200 Hz.
  Use:  Hunt cabinet buzz, room modes, loose hardware, and sub
        enclosure issues. Stays in the bass so tweeters stay safer.


04_Noise/  — broadband signals (balance & EQ)
---------------------------------------------

pink_noise_correlated_-6dBFS_30s.wav
  What: Same pink noise in L and R (mono / correlated). Equal energy
        per octave — how we hear and how RTAs usually display.
  Use:  Balance left vs right so the noise image sits in the center
        between the speakers (phantom center). Also good for rough EQ
        by ear or with an RTA. Loop it if your player supports repeat.

pink_noise_uncorrelated_-6dBFS_30s.wav
  What: Independent pink noise in each channel (stereo / uncorrelated).
  Use:  Sounds wider than correlated. Use for RTA measurements when
        you want both sides excited separately, or to spot a polarity
        problem (image collapses or gets weird). Prefer correlated
        for simple L/R balance by ear.

white_noise_-6dBFS_30s.wav
  What: White noise — equal energy per hertz (brighter / harsher).
  Use:  Quick high-frequency / filter sanity check. Not the first
        choice for EQ (pink is). Keep volume lower — it is hissy.


05_Gain/  — amp gain setting (use with care)
--------------------------------------------
  Prep: Source EQ / loudness / bass boost OFF. Start volume low, then
        set source volume to the level you use for “loud but clean”
        program. Measure amp output with a DMM or scope while you turn
        the gain up until you hit your target (or just before clipping).
        Do NOT use these for casual listening.

50Hz_sine_0dBFS_30s.wav
  What: Full-scale 50 Hz sine (max digital level).
  Use:  Subwoofer amp gain for max clean dynamic range / SQ style.
        Conservative loudness on some program. Loud — protect hearing
        and the sub.

50Hz_sine_-5dBFS_30s.wav
  What: 50 Hz sine at −5 dBFS.
  Use:  Common “everyday bass” sub gain. Louder program headroom than
        0 dBFS settings; slight clip risk on very hot material.

50Hz_sine_-10dBFS_30s.wav
  What: 50 Hz sine at −10 dBFS.
  Use:  Louder bass-oriented / SPL-leaning sub gains. More output,
        less dynamic range, higher chance of clipping on hot program.

1kHz_sine_0dBFS_30s.wav
  What: Full-scale 1 kHz sine.
  Use:  Midrange / full-range amp gains for clean SQ / max headroom.
        Same caution as other 0 dBFS files.

1kHz_sine_-5dBFS_30s.wav
  What: 1 kHz sine at −5 dBFS.
  Use:  Typical mid amp gain for mixed daily listening.

1kHz_sine_-10dBFS_30s.wav
  What: 1 kHz sine at −10 dBFS.
  Use:  Aggressive mid gains for loud systems. Higher blow-up risk —
        set carefully and verify with real program after.


Suggested order on any system
-----------------------------
  1. 01_Channel left/right — confirm wiring and balance.
  2. 01_Channel polarity — confirm no speaker is backwards.
  3. 03_Sweeps — find rattles and dead zones.
  4. 02_Tones — spot-check sub, mids, tweeters.
  5. 04_Noise correlated pink — balance / rough EQ.
  6. 05_Gain — only when you are actually setting amp gains.
