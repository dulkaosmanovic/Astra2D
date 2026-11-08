// AudioEngine without an output device: loading, playing, mixing, voices, music and volumes. The
// mix is pulled with readFrames(), so the tests hear exactly what a device would get.

#include <astra2d/audio/audio_engine.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <numbers>
#include <stdexcept>
#include <string>
#include <vector>

using astra2d::AudioEngine;
using astra2d::AudioSettings;
using astra2d::Sound;

namespace {

constexpr std::uint32_t kRate = 48000;

AudioSettings headless(int maxVoices = 32)
{
    return {.enableDevice = false, .sampleRate = kRate, .channels = 2, .maxVoices = maxVoices};
}

// A constant mono signal of `value` lasting `seconds`.
std::vector<float> constant(float value, float seconds, std::uint32_t rate = kRate)
{
    return std::vector<float>(static_cast<std::size_t>(seconds * static_cast<float>(rate)), value);
}

// Largest absolute sample of the next `frames` frames of the mix.
float peak(AudioEngine& audio, std::size_t frames)
{
    std::vector<float> out(frames * audio.channels());
    EXPECT_EQ(audio.readFrames(out), frames);
    float result = 0.0F;
    for (const float s : out) {
        result = std::max(result, std::abs(s));
    }
    return result;
}

// Writes a 16-bit PCM WAV file with a sine wave.
void writeWav(const std::filesystem::path& path, std::uint32_t rate, std::uint16_t channels,
              float seconds)
{
    const auto frames = static_cast<std::uint32_t>(seconds * static_cast<float>(rate));
    std::vector<std::int16_t> data;
    for (std::uint32_t i = 0; i < frames; ++i) {
        const double t = static_cast<double>(i) / rate;
        const auto sample =
            static_cast<std::int16_t>(16000.0 * std::sin(2.0 * std::numbers::pi * 440.0 * t));
        data.insert(data.end(), channels, sample);
    }
    const auto bytes = static_cast<std::uint32_t>(data.size() * sizeof(std::int16_t));
    std::ofstream file(path, std::ios::binary);
    // Little-endian, byte by byte, as WAV stores numbers.
    const auto put = [&](std::uint32_t v, int size) {
        for (int i = 0; i < size; ++i) {
            file.put(static_cast<char>((v >> (8U * static_cast<unsigned>(i))) & 0xFFU));
        }
    };
    const auto u32 = [&](std::uint32_t v) { put(v, 4); };
    const auto u16 = [&](std::uint16_t v) { put(v, 2); };
    file.write("RIFF", 4);
    u32(36 + bytes);
    file.write("WAVEfmt ", 8);
    u32(16);
    u16(1); // PCM
    u16(channels);
    u32(rate);
    u32(rate * channels * 2);
    u16(static_cast<std::uint16_t>(channels * 2));
    u16(16);
    file.write("data", 4);
    u32(bytes);
    for (const std::int16_t sample : data) {
        u16(static_cast<std::uint16_t>(sample));
    }
}

class AudioFileTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        m_dir =
            std::filesystem::temp_directory_path() /
            ("astra2d_audio_" + std::to_string(::testing::UnitTest::GetInstance()->random_seed()) +
             "_" + ::testing::UnitTest::GetInstance()->current_test_info()->name());
        std::filesystem::create_directories(m_dir);
    }
    void TearDown() override { std::filesystem::remove_all(m_dir); }
    [[nodiscard]] const std::filesystem::path& dir() const { return m_dir; }

private:
    std::filesystem::path m_dir;
};

} // namespace

TEST(AudioEngineTests, RunsWithoutADevice)
{
    AudioEngine audio(headless());
    EXPECT_FALSE(audio.hasDevice());
    EXPECT_EQ(audio.sampleRate(), kRate);
    EXPECT_EQ(audio.channels(), 2U);
    EXPECT_EQ(peak(audio, 256), 0.0F); // silence
}

TEST(AudioEngineTests, RejectsBadSettings)
{
    EXPECT_THROW(AudioEngine({.enableDevice = false, .channels = 0}), std::invalid_argument);
    EXPECT_THROW(AudioEngine({.enableDevice = false, .sampleRate = 10}), std::invalid_argument);
    EXPECT_THROW(AudioEngine({.enableDevice = false, .maxVoices = 1}), std::invalid_argument);
}

TEST(AudioEngineTests, PlayedSoundIsMixedAndEnds)
{
    AudioEngine audio(headless());
    const Sound& beep = audio.addSound("beep", constant(0.5F, 0.1F), 1, kRate);
    EXPECT_EQ(beep.frames(), 4800U);
    EXPECT_NEAR(beep.duration(), 0.1F, 1e-6F);
    EXPECT_EQ(&audio.sound("beep"), &beep); // found by name

    const astra2d::VoiceId voice = audio.play(beep);
    EXPECT_TRUE(audio.isPlaying(voice));
    EXPECT_EQ(audio.activeVoices(), 1);
    EXPECT_GT(peak(audio, 1024), 0.3F); // mono to stereo, panned to the centre
    peak(audio, 8000);                  // past the end
    EXPECT_FALSE(audio.isPlaying(voice));
    audio.update();
    EXPECT_EQ(audio.activeVoices(), 0);
    EXPECT_EQ(peak(audio, 1024), 0.0F);
}

TEST(AudioEngineTests, OverlappingVoicesAddUp)
{
    AudioEngine audio(headless());
    const Sound& tone = audio.addSound("tone", constant(0.2F, 0.5F), 1, kRate);
    audio.play(tone);
    const float one = peak(audio, 512);
    audio.play(tone);
    EXPECT_NEAR(peak(audio, 512), 2.0F * one, 0.02F);
    EXPECT_EQ(audio.activeVoices(), 2);
    audio.stopAll();
    EXPECT_EQ(audio.activeVoices(), 0);
    EXPECT_EQ(peak(audio, 512), 0.0F);
}

TEST(AudioEngineTests, VolumeAndMute)
{
    AudioEngine audio(headless());
    const Sound& tone = audio.addSound("tone", constant(0.4F, 1.0F), 1, kRate);
    audio.play(tone, {.volume = 0.5F});
    const float half = peak(audio, 512);
    EXPECT_GT(half, 0.05F);
    audio.setEffectsVolume(0.5F);
    EXPECT_NEAR(peak(audio, 512), half * 0.5F, 0.01F);
    audio.setMuted(true);
    EXPECT_TRUE(audio.muted());
    EXPECT_EQ(peak(audio, 512), 0.0F);
    audio.setMuted(false);
    EXPECT_NEAR(peak(audio, 512), half * 0.5F, 0.01F);
    audio.setMasterVolume(3.0F); // clamped
    EXPECT_EQ(audio.masterVolume(), 1.0F);
}

TEST(AudioEngineTests, StoppedVoiceIdGoesStale)
{
    AudioEngine audio(headless());
    const Sound& tone = audio.addSound("tone", constant(0.3F, 1.0F), 1, kRate);
    const astra2d::VoiceId first = audio.play(tone, {.loop = true});
    audio.stop(first);
    EXPECT_FALSE(audio.isPlaying(first));
    const astra2d::VoiceId second = audio.play(tone);
    EXPECT_EQ(second.index, first.index); // the slot is reused
    EXPECT_NE(second, first);             // with a new generation
    audio.stop(first);                    // stale: does not stop the new sound
    EXPECT_TRUE(audio.isPlaying(second));
    EXPECT_FALSE(audio.isPlaying(astra2d::VoiceId{}));
}

TEST(AudioEngineTests, LoopingSoundKeepsPlaying)
{
    AudioEngine audio(headless());
    const Sound& tone = audio.addSound("tone", constant(0.3F, 0.01F), 1, kRate);
    const astra2d::VoiceId voice = audio.play(tone, {.loop = true});
    peak(audio, 4800); // ten times its length
    audio.update();
    EXPECT_TRUE(audio.isPlaying(voice));
    EXPECT_GT(peak(audio, 256), 0.1F);
}

TEST(AudioEngineTests, OldestVoiceIsStolenAtTheLimit)
{
    AudioEngine audio(headless(4)); // 3 effects + music
    const Sound& tone = audio.addSound("tone", constant(0.1F, 1.0F), 1, kRate);
    const astra2d::VoiceId oldest = audio.play(tone);
    audio.play(tone);
    audio.play(tone);
    const astra2d::VoiceId newest = audio.play(tone);
    EXPECT_EQ(audio.activeVoices(), 3);
    EXPECT_FALSE(audio.isPlaying(oldest));
    EXPECT_TRUE(audio.isPlaying(newest));
}

TEST(AudioEngineTests, MusicHasItsOwnBus)
{
    AudioEngine audio(headless());
    const Sound& song = audio.addSound("song", constant(0.25F, 0.05F), 1, kRate);
    audio.playMusic(song);
    EXPECT_TRUE(audio.isMusicPlaying());
    EXPECT_EQ(audio.activeVoices(), 0); // not an effect
    const float full = peak(audio, 512);
    EXPECT_GT(full, 0.1F);
    audio.setEffectsVolume(0.0F); // does not touch the music
    EXPECT_NEAR(peak(audio, 512), full, 0.01F);
    audio.setMusicVolume(0.0F);
    EXPECT_EQ(peak(audio, 512), 0.0F);
    audio.setMusicVolume(1.0F);
    peak(audio, 48000); // loops by default
    audio.update();
    EXPECT_TRUE(audio.isMusicPlaying());
    audio.playMusic(song); // same track: keeps playing
    EXPECT_TRUE(audio.isMusicPlaying());
    audio.stopMusic();
    EXPECT_FALSE(audio.isMusicPlaying());
    EXPECT_EQ(peak(audio, 512), 0.0F);
}

TEST(AudioEngineTests, MusicThatDoesNotLoopEnds)
{
    AudioEngine audio(headless());
    const Sound& jingle = audio.addSound("jingle", constant(0.25F, 0.05F), 1, kRate);
    audio.playMusic(jingle, 1.0F, false);
    peak(audio, 4800);
    audio.update();
    EXPECT_FALSE(audio.isMusicPlaying());
}

TEST(AudioEngineTests, PitchChangesTheLength)
{
    AudioEngine audio(headless());
    const Sound& tone = audio.addSound("tone", constant(0.3F, 0.1F), 1, kRate);
    const astra2d::VoiceId fast = audio.play(tone, {.pitch = 2.0F});
    peak(audio, 2600); // a bit more than half of the 4800 frames
    EXPECT_FALSE(audio.isPlaying(fast));
    const astra2d::VoiceId normal = audio.play(tone);
    peak(audio, 2600);
    EXPECT_TRUE(audio.isPlaying(normal));
}

TEST(AudioEngineTests, AddSoundValidates)
{
    AudioEngine audio(headless());
    EXPECT_THROW(audio.addSound("a", {0.0F, 0.0F, 0.0F}, 2, kRate), std::invalid_argument);
    EXPECT_THROW(audio.addSound("b", {}, 0, kRate), std::invalid_argument);
    EXPECT_THROW(audio.addSound("c", {}, 1, 0), std::invalid_argument);
    const Sound& empty = audio.addSound("empty", {}, 1, kRate);
    EXPECT_TRUE(audio.play(empty).isNull()); // nothing to play
    EXPECT_THROW(audio.addSound("empty", {0.5F}, 1, kRate), std::invalid_argument);
    EXPECT_EQ(&audio.sound("empty"), &empty); // the first one is kept
}

TEST_F(AudioFileTest, LoadsWavFilesOnce)
{
    writeWav(dir() / "beep.wav", 22050, 1, 0.25F);
    AudioEngine audio(headless(), dir());
    const Sound& beep = audio.sound("beep.wav");
    EXPECT_EQ(beep.sampleRate(), 22050U);
    EXPECT_EQ(beep.channels(), 1U);
    EXPECT_EQ(beep.frames(), 5512U);
    EXPECT_EQ(&audio.sound("beep.wav"), &beep);
    EXPECT_EQ(&audio.sound(dir() / "beep.wav"), &beep); // same file, absolute path
    EXPECT_EQ(audio.soundCount(), 1U);

    // Resampled to the engine's rate: plays for the same time.
    const astra2d::VoiceId voice = audio.play(beep);
    EXPECT_GT(peak(audio, 2048), 0.3F);
    peak(audio, 9000); // 0.23 s in total
    EXPECT_TRUE(audio.isPlaying(voice));
    peak(audio, 2000);
    EXPECT_FALSE(audio.isPlaying(voice));
}

TEST_F(AudioFileTest, StereoFile)
{
    writeWav(dir() / "stereo.wav", kRate, 2, 0.1F);
    AudioEngine audio(headless(), dir());
    const Sound& sound = audio.sound("stereo.wav");
    EXPECT_EQ(sound.channels(), 2U);
    EXPECT_EQ(sound.samples().size(), sound.frames() * 2);
}

TEST_F(AudioFileTest, MissingOrBrokenFilesThrow)
{
    AudioEngine audio(headless(), dir());
    EXPECT_THROW((void)audio.sound("missing.wav"), std::runtime_error);
    std::ofstream(dir() / "junk.wav") << "not a sound file at all";
    EXPECT_THROW((void)audio.sound("junk.wav"), std::runtime_error);
    EXPECT_EQ(audio.soundCount(), 0U);
}
