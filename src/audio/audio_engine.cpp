#include "astra2d/audio/audio_engine.hpp"

#include "astra2d/core/assert.hpp"
#include "astra2d/core/log.hpp"

#include <miniaudio.h>

#include <algorithm>
#include <format>
#include <stdexcept>
#include <unordered_map>
#include <utility>

namespace astra2d {

namespace {

// A sound being played: miniaudio reads the Sound's samples through `buffer` (its own cursor, so
// many voices can play one Sound) into `sound`, which is a node in the engine's mixing graph.
// miniaudio keeps pointers to both, so a voice never moves (it lives in a unique_ptr).
struct Voice {
    ma_audio_buffer_ref buffer{};
    ma_sound sound{};
    std::uint32_t generation = 0;
    std::uint64_t started = 0; // play() counter when it started; the smallest is the oldest
    bool alive = false;
};

// Playing: started and not at the end (a sound that ran out stays "started" in miniaudio).
bool playing(const ma_sound& sound)
{
    return ma_sound_is_playing(&sound) == MA_TRUE && ma_sound_at_end(&sound) == MA_FALSE;
}

float clampVolume(float volume)
{
    return std::clamp(volume, 0.0F, 1.0F);
}

// Decoded audio: interleaved 32-bit floats at the file's own rate and channel count.
struct Decoded {
    std::vector<float> samples;
    std::uint32_t channels = 0;
    std::uint32_t sampleRate = 0;
};

Decoded decodeFile(const std::filesystem::path& file)
{
    const ma_decoder_config config = ma_decoder_config_init(ma_format_f32, 0, 0);
    ma_decoder decoder;
    const ma_result result = ma_decoder_init_file(file.c_str(), &config, &decoder);
    if (result != MA_SUCCESS) {
        throw std::runtime_error(std::format("Cannot load sound '{}': {}", file.string(),
                                             ma_result_description(result)));
    }
    Decoded decoded{
        .samples = {}, .channels = decoder.outputChannels, .sampleRate = decoder.outputSampleRate};
    ma_uint64 length = 0;
    if (ma_decoder_get_length_in_pcm_frames(&decoder, &length) == MA_SUCCESS && length > 0) {
        decoded.samples.reserve(static_cast<std::size_t>(length) * decoded.channels);
    }
    // Read in chunks until the end: the length is not known in advance for every format.
    constexpr ma_uint64 kChunk = 4096;
    std::vector<float> chunk(static_cast<std::size_t>(kChunk) * decoded.channels);
    for (;;) {
        ma_uint64 read = 0;
        const ma_result status = ma_decoder_read_pcm_frames(&decoder, chunk.data(), kChunk, &read);
        decoded.samples.insert(decoded.samples.end(), chunk.begin(),
                               chunk.begin() +
                                   static_cast<std::ptrdiff_t>(read * decoded.channels));
        if (status != MA_SUCCESS || read < kChunk) {
            break;
        }
    }
    ma_decoder_uninit(&decoder);
    if (decoded.samples.empty()) {
        throw std::runtime_error(std::format("Sound '{}' has no audio", file.string()));
    }
    return decoded;
}

} // namespace

Sound::Sound(Key /*key*/, std::vector<float> samples, std::uint32_t channels,
             std::uint32_t sampleRate)
    : m_samples(std::move(samples))
    , m_frames(m_samples.size() / channels)
    , m_channels(channels)
    , m_sampleRate(sampleRate)
{
}

struct AudioEngine::Impl {
    AudioSettings settings;
    std::filesystem::path root;
    ma_engine engine{};
    ma_sound_group effects{};
    ma_sound_group music{};
    bool device = false;
    std::unordered_map<std::string, std::unique_ptr<Sound>> sounds;
    std::vector<std::unique_ptr<Voice>> voices; // effect voices; slots are reused
    std::vector<std::uint32_t> freeVoices;
    int activeVoices = 0;
    std::uint64_t playCounter = 0;
    std::unique_ptr<Voice> musicVoice; // alive while a track plays
    const Sound* musicSound = nullptr;
    float masterVolume = 1.0F;
    float effectsVolume = 1.0F;
    float musicVolume = 1.0F;
    bool muted = false;

    [[nodiscard]] int maxEffectVoices() const { return settings.maxVoices - 1; } // 1 for music

    // Starts `sound` on `voice` in `group`; false (and logged) if miniaudio refuses.
    bool start(Voice& voice, const Sound& sound, ma_sound_group& group, const PlayOptions& options)
    {
        ma_result result = ma_audio_buffer_ref_init(
            ma_format_f32, sound.channels(), sound.samples().data(), sound.frames(), &voice.buffer);
        if (result != MA_SUCCESS) {
            ASTRA_CORE_ERROR("Audio: cannot play a sound: {}", ma_result_description(result));
            return false;
        }
        // The data is resampled from its own rate (0, the default, would mean the engine's).
        voice.buffer.sampleRate = sound.sampleRate();
        result = ma_sound_init_from_data_source(
            &engine, &voice.buffer, MA_SOUND_FLAG_NO_SPATIALIZATION, &group, &voice.sound);
        if (result != MA_SUCCESS) {
            ma_audio_buffer_ref_uninit(&voice.buffer);
            ASTRA_CORE_ERROR("Audio: cannot play a sound: {}", ma_result_description(result));
            return false;
        }
        ma_sound_set_volume(&voice.sound, std::max(options.volume, 0.0F));
        ma_sound_set_pitch(&voice.sound, std::max(options.pitch, 0.01F));
        ma_sound_set_pan(&voice.sound, std::clamp(options.pan, -1.0F, 1.0F));
        ma_sound_set_looping(&voice.sound, options.loop ? MA_TRUE : MA_FALSE);
        ma_sound_start(&voice.sound);
        voice.alive = true;
        return true;
    }

    static void release(Voice& voice)
    {
        if (!voice.alive) {
            return;
        }
        // Detaches the node from the graph; safe while the audio thread mixes.
        ma_sound_uninit(&voice.sound);
        ma_audio_buffer_ref_uninit(&voice.buffer);
        voice.alive = false;
        ++voice.generation;
    }

    void releaseEffect(std::uint32_t index)
    {
        Voice& voice = *voices[index];
        if (voice.alive) {
            release(voice);
            freeVoices.push_back(index);
            --activeVoices;
        }
    }

    [[nodiscard]] const Voice* find(VoiceId id) const
    {
        if (id.index >= voices.size()) {
            return nullptr;
        }
        const Voice& voice = *voices[id.index];
        return voice.alive && voice.generation == id.generation ? &voice : nullptr;
    }

    [[nodiscard]] bool owns(const Sound& sound) const
    {
        return std::ranges::any_of(sounds,
                                   [&](const auto& entry) { return entry.second.get() == &sound; });
    }

    void applyMasterVolume() { ma_engine_set_volume(&engine, muted ? 0.0F : masterVolume); }
};

AudioEngine::AudioEngine(const AudioSettings& settings, std::filesystem::path root)
    : m_impl(std::make_unique<Impl>())
{
    if (settings.channels < 1 || settings.channels > 8) {
        throw std::invalid_argument("AudioSettings::channels must be 1 to 8");
    }
    if (settings.sampleRate < 8000 || settings.sampleRate > 384000) {
        throw std::invalid_argument("AudioSettings::sampleRate must be 8000 to 384000 Hz");
    }
    if (settings.maxVoices < 2) {
        throw std::invalid_argument("AudioSettings::maxVoices must be at least 2");
    }
    Impl& a = *m_impl;
    a.settings = settings;
    a.root = std::move(root);

    ma_engine_config config = ma_engine_config_init();
    config.channels = settings.channels;
    config.sampleRate = settings.sampleRate;
    ma_result result = MA_ERROR;
    if (settings.enableDevice) {
        result = ma_engine_init(&config, &a.engine);
        if (result == MA_SUCCESS) {
            a.device = true;
        } else {
            ASTRA_CORE_WARN("Audio: no output device ({}); continuing without sound",
                            ma_result_description(result));
        }
    }
    if (!a.device) {
        config.noDevice = MA_TRUE;
        result = ma_engine_init(&config, &a.engine);
        if (result != MA_SUCCESS) {
            throw std::runtime_error(
                std::format("Audio: cannot start the mixer: {}", ma_result_description(result)));
        }
    }
    if (ma_sound_group_init(&a.engine, 0, nullptr, &a.effects) != MA_SUCCESS ||
        ma_sound_group_init(&a.engine, 0, nullptr, &a.music) != MA_SUCCESS) {
        ma_engine_uninit(&a.engine);
        throw std::runtime_error("Audio: cannot create the mixing buses");
    }
    ASTRA_CORE_INFO("Audio: {} Hz, {} channels{}", ma_engine_get_sample_rate(&a.engine),
                    ma_engine_get_channels(&a.engine), a.device ? "" : ", no device");
}

AudioEngine::~AudioEngine()
{
    Impl& a = *m_impl;
    // Voices first: they are nodes attached to the buses, which are attached to the engine.
    for (const auto& voice : a.voices) {
        Impl::release(*voice);
    }
    if (a.musicVoice) {
        Impl::release(*a.musicVoice);
    }
    ma_sound_group_uninit(&a.effects);
    ma_sound_group_uninit(&a.music);
    ma_engine_uninit(&a.engine);
}

const Sound& AudioEngine::sound(const std::filesystem::path& path)
{
    Impl& a = *m_impl;
    if (const auto it = a.sounds.find(path.string()); it != a.sounds.end()) {
        return *it->second; // a name given to addSound()
    }
    const std::filesystem::path full = path.is_absolute() || a.root.empty() ? path : a.root / path;
    const std::string key = full.lexically_normal().string();
    if (const auto it = a.sounds.find(key); it != a.sounds.end()) {
        return *it->second;
    }

    Decoded decoded = decodeFile(full);
    auto& slot = a.sounds[key];
    slot = std::make_unique<Sound>(Sound::Key{}, std::move(decoded.samples), decoded.channels,
                                   decoded.sampleRate);
    ASTRA_CORE_TRACE("Audio: loaded '{}' ({:.2f} s, {} Hz, {} ch)", key, slot->duration(),
                     decoded.sampleRate, decoded.channels);
    return *slot;
}

const Sound& AudioEngine::addSound(const std::string& name, std::vector<float> samples,
                                   std::uint32_t channels, std::uint32_t sampleRate)
{
    if (channels == 0 || sampleRate == 0) {
        throw std::invalid_argument("Sound channels and sampleRate must be positive");
    }
    if (samples.size() % channels != 0) {
        throw std::invalid_argument("Sound sample count must be a multiple of the channels");
    }
    // Replacing a sound would free samples that voices may still be playing.
    const auto [it, added] = m_impl->sounds.try_emplace(name);
    if (!added) {
        throw std::invalid_argument(std::format("A sound named '{}' exists already", name));
    }
    it->second = std::make_unique<Sound>(Sound::Key{}, std::move(samples), channels, sampleRate);
    return *it->second;
}

std::size_t AudioEngine::soundCount() const
{
    return m_impl->sounds.size();
}

VoiceId AudioEngine::play(const Sound& sound, const PlayOptions& options)
{
    Impl& a = *m_impl;
    ASTRA_ASSERT(a.owns(sound), "the sound belongs to another AudioEngine");
    if (sound.frames() == 0) {
        return {};
    }
    if (a.activeVoices >= a.maxEffectVoices()) {
        // Voice stealing: the oldest effect has been heard the longest.
        std::uint32_t oldest = 0;
        for (std::uint32_t i = 1; i < a.voices.size(); ++i) {
            const Voice& v = *a.voices[i];
            const Voice& o = *a.voices[oldest];
            if (v.alive && (!o.alive || v.started < o.started)) {
                oldest = i;
            }
        }
        a.releaseEffect(oldest);
    }
    std::uint32_t index = 0;
    if (!a.freeVoices.empty()) {
        index = a.freeVoices.back();
        a.freeVoices.pop_back();
    } else {
        index = static_cast<std::uint32_t>(a.voices.size());
        a.voices.push_back(std::make_unique<Voice>());
    }
    Voice& voice = *a.voices[index];
    if (!a.start(voice, sound, a.effects, options)) {
        a.freeVoices.push_back(index);
        return {};
    }
    voice.started = a.playCounter++;
    ++a.activeVoices;
    return {.index = index, .generation = voice.generation};
}

void AudioEngine::stop(VoiceId voice)
{
    if (m_impl->find(voice) != nullptr) {
        m_impl->releaseEffect(voice.index);
    }
}

bool AudioEngine::isPlaying(VoiceId voice) const
{
    const Voice* v = m_impl->find(voice);
    return v != nullptr && playing(v->sound);
}

void AudioEngine::stopAll()
{
    for (std::uint32_t i = 0; i < m_impl->voices.size(); ++i) {
        m_impl->releaseEffect(i);
    }
}

int AudioEngine::activeVoices() const
{
    return m_impl->activeVoices;
}

void AudioEngine::playMusic(const Sound& sound, float volume, bool loop)
{
    Impl& a = *m_impl;
    ASTRA_ASSERT(a.owns(sound), "the sound belongs to another AudioEngine");
    if (a.musicSound == &sound && isMusicPlaying()) {
        ma_sound_set_volume(&a.musicVoice->sound, std::max(volume, 0.0F));
        return;
    }
    stopMusic();
    if (sound.frames() == 0) {
        return;
    }
    a.musicVoice = std::make_unique<Voice>();
    if (a.start(*a.musicVoice, sound, a.music, {.volume = volume, .loop = loop})) {
        a.musicSound = &sound;
    } else {
        a.musicVoice.reset();
    }
}

void AudioEngine::stopMusic()
{
    Impl& a = *m_impl;
    if (a.musicVoice) {
        Impl::release(*a.musicVoice);
        a.musicVoice.reset();
    }
    a.musicSound = nullptr;
}

bool AudioEngine::isMusicPlaying() const
{
    const Impl& a = *m_impl;
    return a.musicVoice && a.musicVoice->alive && playing(a.musicVoice->sound);
}

void AudioEngine::setMasterVolume(float volume)
{
    m_impl->masterVolume = clampVolume(volume);
    m_impl->applyMasterVolume();
}

float AudioEngine::masterVolume() const
{
    return m_impl->masterVolume;
}

void AudioEngine::setEffectsVolume(float volume)
{
    m_impl->effectsVolume = clampVolume(volume);
    ma_sound_group_set_volume(&m_impl->effects, m_impl->effectsVolume);
}

float AudioEngine::effectsVolume() const
{
    return m_impl->effectsVolume;
}

void AudioEngine::setMusicVolume(float volume)
{
    m_impl->musicVolume = clampVolume(volume);
    ma_sound_group_set_volume(&m_impl->music, m_impl->musicVolume);
}

float AudioEngine::musicVolume() const
{
    return m_impl->musicVolume;
}

void AudioEngine::setMuted(bool muted)
{
    m_impl->muted = muted;
    m_impl->applyMasterVolume();
}

bool AudioEngine::muted() const
{
    return m_impl->muted;
}

void AudioEngine::update()
{
    Impl& a = *m_impl;
    for (std::uint32_t i = 0; i < a.voices.size(); ++i) {
        const Voice& voice = *a.voices[i];
        if (voice.alive && !playing(voice.sound)) {
            a.releaseEffect(i); // ended
        }
    }
    if (a.musicVoice && !isMusicPlaying()) {
        stopMusic(); // a track that does not loop has ended
    }
}

bool AudioEngine::hasDevice() const
{
    return m_impl->device;
}

std::uint32_t AudioEngine::sampleRate() const
{
    return ma_engine_get_sample_rate(&m_impl->engine);
}

std::uint32_t AudioEngine::channels() const
{
    return ma_engine_get_channels(&m_impl->engine);
}

std::size_t AudioEngine::readFrames(std::span<float> out)
{
    if (m_impl->device) {
        return 0;
    }
    const ma_uint64 frames = out.size() / channels();
    ma_uint64 read = 0;
    ma_engine_read_pcm_frames(&m_impl->engine, out.data(), frames, &read);
    return static_cast<std::size_t>(read);
}

} // namespace astra2d
