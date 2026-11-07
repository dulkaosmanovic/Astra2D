#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <memory>
#include <span>
#include <string>
#include <vector>

/// @addtogroup audio
/// @{

namespace astra2d {

/// Settings used when creating an AudioEngine.
struct AudioSettings {
    /// Open the default output device. Without one (false, or when no device can be opened)
    /// nothing is heard, but everything else works and AudioEngine::readFrames() mixes the sound
    /// on request: used by the tests and on machines without sound hardware.
    bool enableDevice = true;
    /// Mixing rate in Hz without a device; with one, the device's own rate is used.
    std::uint32_t sampleRate = 48000;
    /// Output channels (1 = mono, 2 = stereo).
    std::uint32_t channels = 2;
    /// Most sounds playing at once (music included). Playing one more stops the oldest sound
    /// effect, so a burst of effects can never pile up.
    int maxVoices = 32;
};

/// Decoded audio in memory: a sound effect or a music track. Made by AudioEngine::sound() or
/// AudioEngine::addSound(), owned by the engine and alive as long as it is.
class Sound {
public:
    /// Lets only AudioEngine make sounds (a "passkey": its constructor is private to it).
    class Key {
        friend class AudioEngine;
        Key() = default;
    };

    /// Used by AudioEngine; games get sounds from AudioEngine::sound() and addSound().
    Sound(Key key, std::vector<float> samples, std::uint32_t channels, std::uint32_t sampleRate);

    /// Sample frames (one sample per channel each).
    [[nodiscard]] std::uint64_t frames() const { return m_frames; }
    /// Channels of the data (1 = mono, 2 = stereo).
    [[nodiscard]] std::uint32_t channels() const { return m_channels; }
    /// Rate of the data in Hz; it is resampled to the engine's rate when played.
    [[nodiscard]] std::uint32_t sampleRate() const { return m_sampleRate; }
    /// Length in seconds.
    [[nodiscard]] float duration() const
    {
        return static_cast<float>(static_cast<double>(m_frames) / m_sampleRate);
    }
    /// The interleaved 32-bit float samples, frames() * channels() of them.
    [[nodiscard]] std::span<const float> samples() const { return m_samples; }

private:
    std::vector<float> m_samples;
    std::uint64_t m_frames = 0;
    std::uint32_t m_channels = 0;
    std::uint32_t m_sampleRate = 0;
};

/// How AudioEngine::play() plays a sound.
struct PlayOptions {
    float volume = 1.0F; ///< Linear gain: 0 silent, 1 as recorded.
    float pitch = 1.0F;  ///< Playback speed and pitch: 2 is an octave up and twice as fast.
    float pan = 0.0F;    ///< -1 left, 0 centre, 1 right.
    bool loop = false;   ///< Start over at the end until stopped.
};

/// A playing sound, returned by AudioEngine::play(). It goes stale when the sound ends or is
/// stopped (like Entity, it has a generation), so it is safe to keep after that.
struct VoiceId {
    std::uint32_t index = std::numeric_limits<std::uint32_t>::max(); ///< Voice slot.
    std::uint32_t generation = 0;                                    ///< Use count of the slot.

    /// True only if default-constructed (a non-null id may still refer to a finished voice).
    [[nodiscard]] bool isNull() const { return index == std::numeric_limits<std::uint32_t>::max(); }
    /// Equal if all fields are equal.
    friend bool operator==(VoiceId, VoiceId) = default;
};

/// Plays sound effects and music, built on miniaudio.
///
/// Sounds are decoded once into memory (WAV, FLAC and MP3) and can then be played any number of
/// times, also overlapping: each play() gets its own voice. Sound effects and music go through two
/// separate buses with their own volume, so a game can offer two sliders; there is at most one
/// music track at a time.
///
/// @code
/// astra2d::AudioEngine m_audio({}, assetsDir);
/// const astra2d::Sound& m_jump = m_audio.sound("sounds/jump.wav");
/// m_audio.playMusic(m_audio.sound("sounds/music.wav"));
/// m_audio.play(m_jump, {.pitch = 1.1F});   // when the player jumps
/// m_audio.update();                         // once per frame: frees finished voices
/// @endcode
///
/// Mixing happens on miniaudio's audio thread; all methods are called from the main thread.
/// If no output device can be opened, the engine logs a warning and continues silently, so a
/// game still runs on a machine without sound (e.g. CI). Not copyable or movable: the voices
/// refer back to it.
class AudioEngine {
public:
    /// Starts the engine. @p root is the directory that relative sound paths are resolved
    /// against; empty means the working directory.
    /// @throws std::invalid_argument if the settings are out of range.
    /// @throws std::runtime_error if the mixer itself cannot be created (a missing device is not
    ///         an error).
    explicit AudioEngine(const AudioSettings& settings = {}, std::filesystem::path root = {});
    ~AudioEngine();
    AudioEngine(const AudioEngine&) = delete;
    AudioEngine& operator=(const AudioEngine&) = delete;
    AudioEngine(AudioEngine&&) = delete;
    AudioEngine& operator=(AudioEngine&&) = delete;

    // --- Sounds -----------------------------------------------------------------------------

    /// The sound in the file at @p path (relative to the root), decoded on first use.
    /// @throws std::runtime_error if the file cannot be read or decoded.
    const Sound& sound(const std::filesystem::path& path);
    /// Adds a sound made by the game, e.g. synthesized at run time: @p samples are interleaved
    /// 32-bit floats in [-1, 1]. Stored under @p name, which sound() then finds too.
    /// @throws std::invalid_argument if channels or sampleRate is 0, or the sample count is not a
    ///         multiple of @p channels.
    const Sound& addSound(const std::string& name, std::vector<float> samples,
                          std::uint32_t channels, std::uint32_t sampleRate);
    /// Number of sounds loaded or added.
    [[nodiscard]] std::size_t soundCount() const;

    // --- Sound effects ----------------------------------------------------------------------

    /// Starts @p sound on the effects bus. If maxVoices sounds already play, the oldest sound
    /// effect is stopped to make room. @pre @p sound comes from this engine.
    VoiceId play(const Sound& sound, const PlayOptions& options = {});
    /// Stops a voice; does nothing if it has already ended.
    void stop(VoiceId voice);
    /// True while @p voice plays (until it ends or is stopped).
    [[nodiscard]] bool isPlaying(VoiceId voice) const;
    /// Stops every sound effect (not the music).
    void stopAll();
    /// Sound effects playing now.
    [[nodiscard]] int activeVoices() const;

    // --- Music ------------------------------------------------------------------------------

    /// Plays @p sound as the music track on the music bus, looping by default; replaces the track
    /// that played before. Calling it again with the track that already plays changes nothing,
    /// so a level can call it on every start without restarting the music.
    void playMusic(const Sound& sound, float volume = 1.0F, bool loop = true);
    /// Stops the music.
    void stopMusic();
    /// True while a music track plays.
    [[nodiscard]] bool isMusicPlaying() const;

    // --- Volume -----------------------------------------------------------------------------

    /// Gain of everything (0 to 1, values outside are clamped).
    void setMasterVolume(float volume);
    /// @copydoc setMasterVolume()
    [[nodiscard]] float masterVolume() const;
    /// Gain of the sound effects bus.
    void setEffectsVolume(float volume);
    /// @copydoc setEffectsVolume()
    [[nodiscard]] float effectsVolume() const;
    /// Gain of the music bus.
    void setMusicVolume(float volume);
    /// @copydoc setMusicVolume()
    [[nodiscard]] float musicVolume() const;
    /// Silences everything without forgetting the volumes.
    void setMuted(bool muted);
    /// True while muted.
    [[nodiscard]] bool muted() const;

    // --- Frame update and output ------------------------------------------------------------

    /// Frees the voices of sounds that ended. Call once per frame.
    void update();
    /// True if sound goes to an output device; false if the engine runs without one.
    [[nodiscard]] bool hasDevice() const;
    /// Output rate in Hz.
    [[nodiscard]] std::uint32_t sampleRate() const;
    /// Output channels.
    [[nodiscard]] std::uint32_t channels() const;
    /// Without a device: mixes the next `out.size() / channels()` frames of everything that plays
    /// into @p out (interleaved floats) and returns the number of frames written. With a device the
    /// device pulls the audio itself and this returns 0.
    std::size_t readFrames(std::span<float> out);

    /// The directory relative sound paths are resolved against.
    [[nodiscard]] const std::filesystem::path& root() const;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace astra2d

/// @}
