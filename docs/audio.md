# Audio {#audio_guide}

[TOC]

The @ref audio "audio" module plays sound effects and music through @ref astra2d::AudioEngine.
It is a thin layer over [miniaudio](https://miniaud.io) (vendored in `external/miniaudio/`), which
decodes the files, mixes the sounds on its own thread and talks to the output device (PulseAudio,
ALSA or JACK, loaded at run time).

<div class="astra-summary"><b>In short</b>
<ul>
<li>`sound(path)` decodes a file once; `play()` starts a new voice, so sounds can overlap; `playMusic()` loops one track.</li>
<li>Call `update()` once per frame.</li>
<li>Without a sound device the engine runs silently, and tests mix the output themselves with `readFrames()`.</li>
</ul></div>

## Use

```cpp
astra2d::AudioEngine m_audio({}, assetsDirectory);          // opens the default device

const astra2d::Sound& jump = m_audio.sound("sounds/jump.wav"); // decoded once, kept in memory
m_audio.playMusic(m_audio.sound("sounds/music.wav"), 0.5F);    // loops by default

m_audio.play(jump, {.volume = 0.6F, .pitch = 1.05F});          // when the player jumps
m_audio.update();                                              // once per frame
```

- **Sounds** are decoded once into 32-bit float samples (WAV, FLAC and MP3) and cached by path.
  A game can also add sounds it synthesizes itself with `addSound()`.
- **Voices.** Every `play()` gets its own voice, so the same sound can overlap itself (three coins
  collected in quick succession). A voice reads the shared samples through its own cursor
  (`ma_audio_buffer_ref`) and is resampled to the output rate, which is also how `pitch` works.
  `play()` returns a @ref astra2d::VoiceId with a generation, like an Entity: it goes stale when the
  sound ends, so it is safe to keep.
- **Voice limit.** At most `AudioSettings::maxVoices` sounds play at once. One more stops the oldest
  effect ("voice stealing"), so a burst of effects can never pile up.
- **Buses.** Effects and music go through two sound groups with their own volume (two sliders in an
  options menu), under a master volume and a mute switch.
- **Music** is one looping track on the music bus; `playMusic()` with the track that already plays
  does not restart it, so each level can call it when it starts.

## Threads and the frame

miniaudio mixes on its audio thread, pulling samples whenever the device needs them. The engine's
methods are called from the main thread; starting, stopping and changing a voice are thread-safe in
miniaudio. A finished voice is only released in `update()`, on the main thread, never from the
audio thread, so no lock is held while the game runs.

## Without a device

If no output device can be opened (a server, a CI runner, a container), the engine logs a warning
and continues without sound: the game still runs. `AudioSettings::enableDevice = false` asks for
this mode on purpose. Then nothing pulls the audio, and `readFrames()` mixes it on request, which is
how the unit tests check what would be heard:

```cpp
astra2d::AudioEngine audio({.enableDevice = false, .sampleRate = 48000});
const auto& beep = audio.addSound("beep", std::vector<float>(4800, 0.5F), 1, 48000);
audio.play(beep);
std::vector<float> out(1024 * audio.channels());
audio.readFrames(out); // the mixed samples a device would get
```

`tests/audio/test_audio.cpp` uses this to test mixing, volumes, pitch, looping, the voice limit, the music
bus and WAV decoding, all without sound hardware, also under Valgrind.

## Example

`examples/platformer` plays a jump, coin, stomp, hurt, checkpoint, win and game-over sound and a
music loop, all made by `scripts/generate_platformer_assets.py`; `M` mutes. Each effect is played
with a slightly random pitch, so repeated sounds do not get tiring. See @ref platformer_guide.

<div class="astra-pager">
<span class="astra-pager-prev">← @ref physics_guide "Physics"</span>
<span class="astra-pager-next">Next: @ref debug_guide "Debug tools" →</span>
</div>
