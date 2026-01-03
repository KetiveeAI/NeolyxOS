/**
 * @file nxvideo_cpp.hpp
 * @brief C++ wrapper for NXVideo C library
 * 
 * Provides RAII-based C++ interface to NXVideo.
 * Use this for browser integration.
 */

#pragma once

extern "C" {
#include "nxvideo/nxvideo.h"
}

#include <string>
#include <functional>
#include <memory>
#include <stdexcept>

namespace nxvideo {

// =============================================================================
// Enums (C++ style)
// =============================================================================

enum class Codec {
    Unknown = NXVIDEO_CODEC_UNKNOWN,
    H264    = NXVIDEO_CODEC_H264,
    H265    = NXVIDEO_CODEC_H265,
    VP8     = NXVIDEO_CODEC_VP8,
    VP9     = NXVIDEO_CODEC_VP9,
    AV1     = NXVIDEO_CODEC_AV1,
    MPEG4   = NXVIDEO_CODEC_MPEG4,
    MPEG2   = NXVIDEO_CODEC_MPEG2,
};

enum class PixelFormat {
    RGB24   = NXVIDEO_PIXEL_RGB24,
    RGBA32  = NXVIDEO_PIXEL_RGBA32,
    BGR24   = NXVIDEO_PIXEL_BGR24,
    BGRA32  = NXVIDEO_PIXEL_BGRA32,
    YUV420P = NXVIDEO_PIXEL_YUV420P,
    NV12    = NXVIDEO_PIXEL_NV12,
    P010    = NXVIDEO_PIXEL_P010,
};

enum class HardwareType {
    None        = NXVIDEO_HW_NONE,
    VAAPI       = NXVIDEO_HW_VAAPI,
    NVDEC       = NXVIDEO_HW_NVDEC,
    VDPAU       = NXVIDEO_HW_VDPAU,
    VideoToolbox = NXVIDEO_HW_VIDEOTOOLBOX,
    D3D11VA     = NXVIDEO_HW_D3D11VA,
    Vulkan      = NXVIDEO_HW_VULKAN,
};

enum class State {
    Stopped   = NXVIDEO_STATE_STOPPED,
    Playing   = NXVIDEO_STATE_PLAYING,
    Paused    = NXVIDEO_STATE_PAUSED,
    Buffering = NXVIDEO_STATE_BUFFERING,
    Seeking   = NXVIDEO_STATE_SEEKING,
    Ended     = NXVIDEO_STATE_ENDED,
    Error     = NXVIDEO_STATE_ERROR,
};

// =============================================================================
// Structs (C++ style)
// =============================================================================

struct VideoInfo {
    Codec codec;
    int width;
    int height;
    double fps;
    double duration;
    int64_t bitrate;
    int bitDepth;
    PixelFormat pixelFormat;
    std::string codecName;
    std::string profile;
    
    static VideoInfo fromC(const nxvideo_stream_info_t& c) {
        VideoInfo v;
        v.codec = static_cast<Codec>(c.codec);
        v.width = c.width;
        v.height = c.height;
        v.fps = c.fps;
        v.duration = c.duration;
        v.bitrate = c.bitrate;
        v.bitDepth = c.bit_depth;
        v.pixelFormat = static_cast<PixelFormat>(c.pixel_format);
        v.codecName = c.codec_name;
        v.profile = c.profile;
        return v;
    }
};

struct AudioInfo {
    int sampleRate;
    int channels;
    int64_t bitrate;
    double duration;
    std::string codecName;
    
    static AudioInfo fromC(const nxvideo_audio_info_t& c) {
        AudioInfo a;
        a.sampleRate = c.sample_rate;
        a.channels = c.channels;
        a.bitrate = c.bitrate;
        a.duration = c.duration;
        a.codecName = c.codec_name;
        return a;
    }
};

struct MediaInfo {
    bool hasVideo;
    bool hasAudio;
    VideoInfo video;
    AudioInfo audio;
    double duration;
    int64_t sizeBytes;
    std::string container;
    std::string title;
    
    static MediaInfo fromC(const nxvideo_media_info_t& c) {
        MediaInfo m;
        m.hasVideo = c.has_video;
        m.hasAudio = c.has_audio;
        m.video = VideoInfo::fromC(c.video);
        m.audio = AudioInfo::fromC(c.audio);
        m.duration = c.duration;
        m.sizeBytes = c.size_bytes;
        m.container = c.container;
        m.title = c.title;
        return m;
    }
};

struct Frame {
    uint8_t* data[4];
    int linesize[4];
    int width;
    int height;
    PixelFormat format;
    double pts;
    double duration;
    bool keyframe;
    uint32_t hwTexture;
    
    static Frame fromC(const nxvideo_frame_info_t& c) {
        Frame f;
        for (int i = 0; i < 4; i++) {
            f.data[i] = c.data[i];
            f.linesize[i] = c.linesize[i];
        }
        f.width = c.width;
        f.height = c.height;
        f.format = static_cast<PixelFormat>(c.format);
        f.pts = c.pts;
        f.duration = c.duration;
        f.keyframe = c.keyframe;
        f.hwTexture = c.hw_texture;
        return f;
    }
};

struct HardwareCaps {
    HardwareType type;
    std::string name;
    bool supportsH264;
    bool supportsH265;
    bool supportsVP9;
    bool supportsAV1;
    bool supports10bit;
    int maxWidth;
    int maxHeight;
    
    static HardwareCaps fromC(const nxvideo_hw_caps_t& c) {
        HardwareCaps h;
        h.type = static_cast<HardwareType>(c.type);
        h.name = c.name;
        h.supportsH264 = c.supports_h264;
        h.supportsH265 = c.supports_h265;
        h.supportsVP9 = c.supports_vp9;
        h.supportsAV1 = c.supports_av1;
        h.supports10bit = c.supports_10bit;
        h.maxWidth = c.max_width;
        h.maxHeight = c.max_height;
        return h;
    }
};

// =============================================================================
// Exception
// =============================================================================

class VideoException : public std::runtime_error {
public:
    VideoException(nxvideo_error_t error)
        : std::runtime_error(nxvideo_error_string(error))
        , error_(error) {}
    
    nxvideo_error_t error() const { return error_; }
    
private:
    nxvideo_error_t error_;
};

// =============================================================================
// System (RAII initialization)
// =============================================================================

class System {
public:
    System() {
        nxvideo_error_t err = nxvideo_init();
        if (err != NXVIDEO_SUCCESS) {
            throw VideoException(err);
        }
    }
    
    ~System() {
        nxvideo_shutdown();
    }
    
    static const char* version() {
        return nxvideo_version();
    }
    
    static int hardwareCount() {
        return nxvideo_hw_count();
    }
    
    static HardwareCaps getHardwareCaps(int index) {
        nxvideo_hw_caps_t caps;
        nxvideo_error_t err = nxvideo_hw_caps(index, &caps);
        if (err != NXVIDEO_SUCCESS) {
            throw VideoException(err);
        }
        return HardwareCaps::fromC(caps);
    }
    
    static HardwareType bestHardwareFor(Codec codec) {
        return static_cast<HardwareType>(
            nxvideo_hw_best(static_cast<nxvideo_codec_t>(codec)));
    }
    
    static bool isHardwareAvailable(Codec codec) {
        return nxvideo_hw_available(static_cast<nxvideo_codec_t>(codec));
    }
    
    // Non-copyable
    System(const System&) = delete;
    System& operator=(const System&) = delete;
};

// =============================================================================
// Player (RAII player)
// =============================================================================

class Player {
public:
    using FrameCallback = std::function<void(const Frame&)>;
    using StateCallback = std::function<void(State)>;
    using ErrorCallback = std::function<void(nxvideo_error_t, const std::string&)>;
    using ProgressCallback = std::function<void(double, double)>;
    
    struct Config {
        bool hwDecode = true;
        bool loop = false;
        float volume = 1.0f;
        float playbackRate = 1.0f;
        bool muted = false;
    };
    
    Player() : Player(Config{}) {}
    
    explicit Player(const Config& config) {
        nxvideo_player_config_t c;
        c.hw_decode = config.hwDecode ? 1 : 0;
        c.loop = config.loop ? 1 : 0;
        c.volume = config.volume;
        c.playback_rate = config.playbackRate;
        c.muted = config.muted ? 1 : 0;
        
        handle_ = nxvideo_player_create(&c);
        if (handle_ == NXVIDEO_INVALID_HANDLE) {
            throw VideoException(NXVIDEO_ERROR_NO_MEMORY);
        }
        
        // Set up callbacks to forward to C++ lambdas
        nxvideo_player_on_state(handle_, stateCallbackC, this);
        nxvideo_player_on_progress(handle_, progressCallbackC, this);
    }
    
    ~Player() {
        if (handle_ != NXVIDEO_INVALID_HANDLE) {
            nxvideo_player_destroy(handle_);
        }
    }
    
    // Non-copyable
    Player(const Player&) = delete;
    Player& operator=(const Player&) = delete;
    
    // Move-able
    Player(Player&& other) noexcept : handle_(other.handle_) {
        other.handle_ = NXVIDEO_INVALID_HANDLE;
    }
    
    Player& operator=(Player&& other) noexcept {
        if (this != &other) {
            if (handle_ != NXVIDEO_INVALID_HANDLE) {
                nxvideo_player_destroy(handle_);
            }
            handle_ = other.handle_;
            other.handle_ = NXVIDEO_INVALID_HANDLE;
        }
        return *this;
    }
    
    // ==========================================================================
    // Media Control
    // ==========================================================================
    
    void open(const std::string& url) {
        nxvideo_error_t err = nxvideo_player_open(handle_, url.c_str());
        if (err != NXVIDEO_SUCCESS) {
            throw VideoException(err);
        }
    }
    
    void close() {
        nxvideo_player_close(handle_);
    }
    
    MediaInfo getInfo() const {
        nxvideo_media_info_t info;
        nxvideo_error_t err = nxvideo_player_info(handle_, &info);
        if (err != NXVIDEO_SUCCESS) {
            throw VideoException(err);
        }
        return MediaInfo::fromC(info);
    }
    
    // ==========================================================================
    // Playback
    // ==========================================================================
    
    void play() {
        nxvideo_error_t err = nxvideo_player_play(handle_);
        if (err != NXVIDEO_SUCCESS) {
            throw VideoException(err);
        }
    }
    
    void pause() {
        nxvideo_error_t err = nxvideo_player_pause(handle_);
        if (err != NXVIDEO_SUCCESS) {
            throw VideoException(err);
        }
    }
    
    void stop() {
        nxvideo_error_t err = nxvideo_player_stop(handle_);
        if (err != NXVIDEO_SUCCESS) {
            throw VideoException(err);
        }
    }
    
    void seek(double seconds) {
        nxvideo_error_t err = nxvideo_player_seek(handle_, seconds);
        if (err != NXVIDEO_SUCCESS) {
            throw VideoException(err);
        }
    }
    
    void togglePlayPause() {
        if (state() == State::Playing) {
            pause();
        } else {
            play();
        }
    }
    
    // ==========================================================================
    // State
    // ==========================================================================
    
    State state() const {
        return static_cast<State>(nxvideo_player_state(handle_));
    }
    
    double position() const {
        return nxvideo_player_position(handle_);
    }
    
    double duration() const {
        return nxvideo_player_duration(handle_);
    }
    
    bool isPlaying() const { return state() == State::Playing; }
    bool isPaused() const { return state() == State::Paused; }
    bool isStopped() const { return state() == State::Stopped; }
    bool isEnded() const { return state() == State::Ended; }
    
    // ==========================================================================
    // Settings
    // ==========================================================================
    
    void setVolume(float volume) {
        nxvideo_player_set_volume(handle_, volume);
    }
    
    void setMuted(bool muted) {
        nxvideo_player_set_muted(handle_, muted ? 1 : 0);
    }
    
    void setPlaybackRate(float rate) {
        nxvideo_player_set_rate(handle_, rate);
    }
    
    void setLoop(bool loop) {
        nxvideo_player_set_loop(handle_, loop ? 1 : 0);
    }
    
    // ==========================================================================
    // Frame
    // ==========================================================================
    
    Frame currentFrame() const {
        nxvideo_frame_info_t frame;
        nxvideo_player_current_frame(handle_, &frame);
        return Frame::fromC(frame);
    }
    
    void update() {
        nxvideo_player_update(handle_);
    }
    
    // ==========================================================================
    // Callbacks
    // ==========================================================================
    
    void onFrame(FrameCallback callback) {
        onFrame_ = std::move(callback);
    }
    
    void onState(StateCallback callback) {
        onState_ = std::move(callback);
    }
    
    void onError(ErrorCallback callback) {
        onError_ = std::move(callback);
    }
    
    void onProgress(ProgressCallback callback) {
        onProgress_ = std::move(callback);
    }
    
    // ==========================================================================
    // A/V Sync
    // ==========================================================================
    
    void setAudioClock(double audioTime) {
        nxvideo_player_set_audio_clock(handle_, audioTime);
    }
    
    double avOffset() const {
        return nxvideo_player_av_offset(handle_);
    }
    
    void setAVOffset(double offsetMs) {
        nxvideo_player_set_av_offset(handle_, offsetMs);
    }
    
    // Handle access
    nxvideo_player_t handle() const { return handle_; }
    
private:
    nxvideo_player_t handle_ = NXVIDEO_INVALID_HANDLE;
    
    FrameCallback onFrame_;
    StateCallback onState_;
    ErrorCallback onError_;
    ProgressCallback onProgress_;
    
    static void stateCallbackC(void* userdata, nxvideo_state_t state) {
        auto* self = static_cast<Player*>(userdata);
        if (self->onState_) {
            self->onState_(static_cast<State>(state));
        }
    }
    
    static void progressCallbackC(void* userdata, double current, double total) {
        auto* self = static_cast<Player*>(userdata);
        if (self->onProgress_) {
            self->onProgress_(current, total);
        }
    }
};

// =============================================================================
// Convenience: Global video system (singleton)
// =============================================================================

inline System& getVideoSystem() {
    static System instance;
    return instance;
}

} // namespace nxvideo
