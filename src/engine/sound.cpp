#include "sound.h"
#include <array>
#include "error.h"
#include "resource_manager.h"
#include "save.h"

#if defined(TA_DISABLE_AUDIO)

void TA::sound::init() {}
void TA::sound::quit() {}
MIX_Mixer* TA::sound::getMixer() { return nullptr; }
void TA::sound::playMusic(const std::string&, int) {}
void TA::sound::update() {}
bool TA::sound::isPlaying(TA_SoundChannel) { return false; }
bool TA::sound::isMusicPlaying() { return false; }
void TA::sound::fadeOut(int) {}
void TA::sound::fadeOutMusic(int) {}
void TA::sound::fadeOutChannel(TA_SoundChannel, int) {}

void TA_Sound::load(const std::string&, TA_SoundChannel newChannel, bool newLoop) {
    channel = newChannel;
    loop = newLoop;
    chunk = nullptr;
}

void TA_Sound::play() {}
void TA_Sound::fadeOut(int) {}

#else

namespace {
    MIX_Mixer* mixer = nullptr;
    MIX_Track* musicTrack = nullptr;
    std::array<MIX_Track*, TA_SOUND_CHANNEL_MAX> channels{};
    bool audioAvailable = false;

    float getVolumeGain(const std::string& parameter) {
        return static_cast<float>(TA::save::getParameter(parameter)) / 8.0F;
    }

    int64_t fadeFrames(MIX_Track* track, int time) {
        int64_t frames = MIX_TrackMSToFrames(track, static_cast<int64_t>(time) * 1000 / 60);
        return frames < 0 ? 0 : frames;
    }

    SDL_PropertiesID createLoopOptions(int loops) {
        if(loops == 0) {
            return 0;
        }

        SDL_PropertiesID options = SDL_CreateProperties();
        if(options == 0) {
            TA::handleSDLError("%s", "failed to create SDL_mixer playback options");
        }
        SDL_SetNumberProperty(options, MIX_PROP_PLAY_LOOPS_NUMBER, loops);
        return options;
    }

    MIX_Track* getChannel(TA_SoundChannel channel) {
        return channels.at(channel);
    }
} // namespace

void TA::sound::init() {
    if(!MIX_Init()) {
        SDL_Log("audio disabled: SDL_mixer init failed: %s", SDL_GetError());
        return;
    }

    SDL_AudioSpec audioSpec;
    audioSpec.format = SDL_AUDIO_S16;
    audioSpec.channels = 2;
    audioSpec.freq = 44100;

    mixer = MIX_CreateMixerDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &audioSpec);
    if(mixer == nullptr) {
        SDL_Log("audio disabled: MIX_CreateMixerDevice failed: %s", SDL_GetError());
        MIX_Quit();
        return;
    }

    musicTrack = MIX_CreateTrack(mixer);
    if(musicTrack == nullptr) {
        SDL_Log("audio disabled: failed to create music track: %s", SDL_GetError());
        MIX_DestroyMixer(mixer);
        mixer = nullptr;
        MIX_Quit();
        return;
    }

    for(MIX_Track*& channel : channels) {
        channel = MIX_CreateTrack(mixer);
        if(channel == nullptr) {
            SDL_Log("audio disabled: failed to create sound track: %s", SDL_GetError());
            MIX_DestroyMixer(mixer);
            mixer = nullptr;
            musicTrack = nullptr;
            channels.fill(nullptr);
            MIX_Quit();
            return;
        }
    }

    audioAvailable = true;
}

void TA::sound::quit() {
    if(!audioAvailable) {
        return;
    }
    MIX_DestroyMixer(mixer);
    mixer = nullptr;
    musicTrack = nullptr;
    channels.fill(nullptr);
    audioAvailable = false;
    MIX_Quit();
}

MIX_Mixer* TA::sound::getMixer() {
    return mixer;
}

void TA::sound::playMusic(const std::string& filename, int repeat) {
    if(!audioAvailable || mixer == nullptr || musicTrack == nullptr) {
        return;
    }
    MIX_Audio* music = TA::resmgr::loadMusic(filename);
    if(music == nullptr) {
        return;
    }
    if(!MIX_SetTrackAudio(musicTrack, music)) {
        SDL_Log("audio warning: MIX_SetTrackAudio(music) failed: %s", SDL_GetError());
        return;
    }

    SDL_PropertiesID options = createLoopOptions(repeat);
    if(!MIX_PlayTrack(musicTrack, options)) {
        if(options != 0) {
            SDL_DestroyProperties(options);
        }
        SDL_Log("audio warning: MIX_PlayTrack(music) failed: %s", SDL_GetError());
        return;
    }
    if(options != 0) {
        SDL_DestroyProperties(options);
    }
}

void TA::sound::update() {
    if(!audioAvailable || mixer == nullptr || musicTrack == nullptr) {
        return;
    }
    MIX_SetMixerGain(mixer, getVolumeGain("main_volume"));
    MIX_SetTrackGain(musicTrack, getVolumeGain("music_volume") * 0.6F);
    for(MIX_Track* channel : channels) {
        MIX_SetTrackGain(channel, getVolumeGain("sfx_volume"));
    }
}

bool TA::sound::isPlaying(TA_SoundChannel channel) {
    if(!audioAvailable) {
        return false;
    }
    return MIX_TrackPlaying(getChannel(channel));
}

bool TA::sound::isMusicPlaying() {
    if(!audioAvailable || musicTrack == nullptr) {
        return false;
    }
    return MIX_TrackPlaying(musicTrack);
}

void TA::sound::fadeOut(int time) {
    fadeOutMusic(time);
    for(int channel = 0; channel < TA_SOUND_CHANNEL_MAX; channel++) {
        fadeOutChannel(TA_SoundChannel(channel), time);
    }
}

void TA::sound::fadeOutMusic(int time) {
    if(!audioAvailable || musicTrack == nullptr) {
        return;
    }
    MIX_StopTrack(musicTrack, fadeFrames(musicTrack, time));
}

void TA::sound::fadeOutChannel(TA_SoundChannel channel, int time) {
    if(!audioAvailable) {
        return;
    }
    MIX_StopTrack(getChannel(channel), fadeFrames(getChannel(channel), time));
}

void TA_Sound::load(const std::string& filename, TA_SoundChannel newChannel, bool newLoop) {
    chunk = TA::resmgr::loadChunk(filename);
    channel = newChannel;
    loop = newLoop;
}

void TA_Sound::play() {
    if(chunk == nullptr) {
        return;
    }
    MIX_Track* track = getChannel(channel);
    if(track == nullptr) {
        return;
    }
    if(!MIX_SetTrackAudio(track, chunk)) {
        SDL_Log("audio warning: MIX_SetTrackAudio(sfx) failed: %s", SDL_GetError());
        return;
    }

    SDL_PropertiesID options = createLoopOptions(loop ? -1 : 0);
    if(!MIX_PlayTrack(track, options)) {
        if(options != 0) {
            SDL_DestroyProperties(options);
        }
        SDL_Log("audio warning: MIX_PlayTrack(sfx) failed: %s", SDL_GetError());
        return;
    }
    if(options != 0) {
        SDL_DestroyProperties(options);
    }
}

void TA_Sound::fadeOut(int time) {
    MIX_Track* track = getChannel(channel);
    if(track == nullptr) {
        return;
    }
    MIX_StopTrack(track, fadeFrames(track, time));
}

#endif
