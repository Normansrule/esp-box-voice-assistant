The buddy's music.
==================

Two pieces ship here. Both are public domain compositions, and both recordings
were made by this project's own synthesiser — scripts/make_music.py — from a
score written out in that file. Nothing is sampled and nothing was downloaded.

    ode-to-joy.mp3    Beethoven, Symphony No. 9 in D minor, Op. 125, fourth
                      movement (1824). Transposed to F major.

    fur-elise.mp3     Beethoven, Bagatelle No. 25 in A minor, WoO 59
                      (composed 1810, published 1867). Opening section.

WHY BOTH HALVES MATTER
----------------------
A composition falling out of copyright does not make a RECORDING of it free.
Performances carry their own rights, which is the trap behind most "royalty
free classical" downloads: the notes are free, the audio file is not.

Here both halves are settled. The compositions have been public domain
everywhere for more than a century, and the recordings were generated from
scratch by a script in this repository, so they are covered by the same
Unlicense/CC0 as the rest of the project. You can ship them, sell them, or put
them in something else without asking anybody.

To re-render them, or to change the arrangement:

    pip install numpy
    sudo apt-get install ffmpeg
    python3 scripts/make_music.py

ADDING YOUR OWN
---------------
Drop any .mp3 into this folder and rebuild. The playlist is whatever is here at
build time — "play some music" picks at random, "play the next song" moves on.

Watch the size. This folder shares the 4056 KB storage partition with the
offline voice, which is about 1.8 MB of it. Roughly 1 MB of music is a
comfortable ceiling; past that, take more space from ota_0 in partitions.csv,
where there is plenty spare.

Mono or stereo both work, but keep the sample rate sensible — 44.1 kHz is fine,
and the firmware converts to the codec's 16 kHz on the fly so that the wake
word keeps working while music plays. See main/audio/audio_resampler.h for why
that conversion has to happen at all.
