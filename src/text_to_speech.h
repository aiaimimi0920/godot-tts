#ifndef SPEECH_TO_TEXT_H
#define SPEECH_TO_TEXT_H

#include "resource_vits.h"

#include <libsamplerate/src/samplerate.h>
#include <godot_cpp/classes/audio_effect_pitch_shift.hpp>
#include <godot_cpp/classes/audio_frame.hpp>
#include <godot_cpp/classes/audio_stream_playback_polyphonic.hpp>
#include <godot_cpp/classes/audio_stream_player.hpp>
#include <godot_cpp/classes/audio_stream_polyphonic.hpp>
#include <godot_cpp/classes/audio_stream_wav.hpp>
#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/mutex.hpp>
#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/classes/os.hpp>
#include <godot_cpp/classes/resource_uid.hpp>
#include <godot_cpp/classes/scene_tree.hpp>
#include <godot_cpp/classes/semaphore.hpp>
#include <godot_cpp/classes/thread.hpp>
#include <godot_cpp/classes/window.hpp>
#include <godot_cpp/core/math.hpp>
#include <godot_cpp/core/memory.hpp>
#include <godot_cpp/core/mutex_lock.hpp>
#include <godot_cpp/templates/hash_map.hpp>
#include <godot_cpp/templates/list.hpp>
#include <godot_cpp/templates/vector.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/callable.hpp>
#include <godot_cpp/variant/callable_method_pointer.hpp>

#include "SynthesizerTrn.h"
#include "stdio.h"
#include "string.h"
#include "utils.h"

#include "resource_loader_vits.h"
#include <atomic>
#include <string>
#include <vector>

using namespace godot;

// from audio_effect_pitch_shift.h
class SMBPitchShift {
	enum {
		MAX_FRAME_LENGTH = 8192
	};

	float gInFIFO[MAX_FRAME_LENGTH];
	float gOutFIFO[MAX_FRAME_LENGTH];
	float gFFTworksp[2 * MAX_FRAME_LENGTH];
	float gLastPhase[MAX_FRAME_LENGTH / 2 + 1];
	float gSumPhase[MAX_FRAME_LENGTH / 2 + 1];
	float gOutputAccum[2 * MAX_FRAME_LENGTH];
	float gAnaFreq[MAX_FRAME_LENGTH];
	float gAnaMagn[MAX_FRAME_LENGTH];
	float gSynFreq[MAX_FRAME_LENGTH];
	float gSynMagn[MAX_FRAME_LENGTH];
	long gRover;

	void smbFft(float *fftBuffer, long fftFrameSize, long sign);

public:
	void PitchShift(float pitchShift, long numSampsToProcess, long fftFrameSize, long osamp, float sampleRate, float *indata, float *outdata, int stride);

	SMBPitchShift() {
		gRover = 0;
		memset(gInFIFO, 0, MAX_FRAME_LENGTH * sizeof(float));
		memset(gOutFIFO, 0, MAX_FRAME_LENGTH * sizeof(float));
		memset(gFFTworksp, 0, 2 * MAX_FRAME_LENGTH * sizeof(float));
		memset(gLastPhase, 0, (MAX_FRAME_LENGTH / 2 + 1) * sizeof(float));
		memset(gSumPhase, 0, (MAX_FRAME_LENGTH / 2 + 1) * sizeof(float));
		memset(gOutputAccum, 0, 2 * MAX_FRAME_LENGTH * sizeof(float));
		memset(gAnaFreq, 0, MAX_FRAME_LENGTH * sizeof(float));
		memset(gAnaMagn, 0, MAX_FRAME_LENGTH * sizeof(float));
	}
};

enum TTSUtteranceEvent {
	TTS_UTTERANCE_NONE = 0,
	TTS_UTTERANCE_STARTED = 1,
	TTS_UTTERANCE_ENDED = 2,
	TTS_UTTERANCE_MAX = 3,
};

struct TTSUtterance {
	String text;
	int volume = 50;
	float pitch = 1.f;
	float rate = 1.f;
	int id = 0;
	bool auto_play = true;
	bool immediately = false;
	int wait_utterance_id = -1;
	TTSUtteranceEvent wait_event;
	float wait_time = 0;
	bool create_file = false;
	String file_path = "";

	String speaker_id = "";
	int polyphonic_stream_id = -1;
	Ref<AudioStreamWAV> audio_stream_wav;

	List<TTSUtterance *> need_update_messages;

	int generate_time = -1;
	int generated_time = -1;
	int target_start_time = -1;
	int start_time = -1;
	int finish_time = -1;
};

class TTSSpeaker : public Node {
private:
	GDCLASS(TTSSpeaker, Node);
	friend class TextToSpeech;

protected:
	static void _bind_methods();

public:
	enum SpeakerState {
		SPEAKER_STATE_INIT = (1L << 0),
		SPEAKER_STATE_INFERING = (1L << 1),
		SPEAKER_STATE_PLAYING = (1L << 3),
		SPEAKER_STATE_PAUSE = (1L << 4),
		SPEAKER_STATE_MUTE = (1L << 5),
	};

	int speaker_state_flags = (1L << 5) - 1;
	// 需要处理的队列
	List<TTSUtterance *> need_process_message_queue;
	// 待播放的队列，需要注意：如果你通过wait_time,
	List<TTSUtterance *> need_play_message_queue;
	// 待播放的池子，无序列结构，类似延迟机制
	List<TTSUtterance *> need_play_message_pool;

	List<TTSUtterance *> playing_message;
	Ref<VITSResource> vits_model;
	float *model_ptr;
	int model_size;
	// 对应的在模型中的id
	int speaker_index;
	// 全局唯一的id
	String speaker_id;

	SynthesizerTrn *synthesizer_ptr;
	int status;

	Mutex infer_mutex;
	Thread *infer_thread = nullptr;
	Semaphore infer_semaphore;
	std::atomic<bool> infer_need_stop = false;
	std::atomic<bool> infer_is_running = false;

	Mutex queue_mutex;
	Thread *queue_thread = nullptr;
	Semaphore queue_semaphore;
	std::atomic<bool> queue_need_stop = false;
	std::atomic<bool> queue_is_running = false;

	AudioStreamPlayer *audio_stream_player = nullptr;
	AudioStreamPlayer *custom_audio_stream_player = nullptr;

	AudioStreamPolyphonic *audio_stream;

	bool is_empty;

	bool is_init();
	bool is_infering();
	bool is_playing();
	bool is_pause();
	bool is_mute();

	void tts_initialize();
	void tts_destroy();

	void infer();
	void uninfer();
	void pause();
	void resume();
	void play();
	void stop();
	void mute();
	void unmute();

	void append_infer_message(TTSUtterance *message);

	void play_message(TTSUtterance *message);

	bool is_message_playing(TTSUtterance *message);
	void stop_message(TTSUtterance *message);

	uint32_t _resample_audio_buffer(const float *p_src, const uint32_t p_src_frame_count,
			const uint32_t p_src_samplerate, const uint32_t p_target_samplerate,
			float *p_dst);

	bool process_message_infer();

	bool process_message_queue();

	void interrupt();

	void setup(const Ref<VITSResource> &vits_model, float *model_ptr, int model_size, int speaker_index, String speaker_id);
	TTSSpeaker() {
		is_empty = true;
	};

	~TTSSpeaker();
};

class TextToSpeech : public Node {
	GDCLASS(TextToSpeech, Node);
	friend class TTSSpeaker;

public:
	static TextToSpeech *singleton;

protected:
	static void _bind_methods();

public:
	static int last_utterance_id;
	static int _get_new_utterance_id();

	HashMap<int, TTSUtterance *> message_map;
	HashMap<String, TTSSpeaker *> speaker_map;
	// Dictionary speaker_map;

	const Ref<VITSResource> _get_res_from_path(String vits_model_path);

	TTSSpeaker *_get_speaker_from_speaker_uuid(String speaker_uuid);

	String _create_speaker(const Ref<VITSResource> &vits_model, int speaker_index);

	String _get_speaker_uuid(const Ref<VITSResource> &vits_model, int speaker_index);
	TTSSpeaker *_get_speaker(const Ref<VITSResource> &vits_model, int speaker_index);

	HashMap<const Ref<VITSResource> &, float *> models_ptr;

	HashMap<const Ref<VITSResource> &, int> models_size;

	HashMap<const Ref<VITSResource> &, int> models_speaker_num;

	bool setup_model(const Ref<VITSResource> &vits_model);
	bool unsetup_model(const Ref<VITSResource> &vits_model);

	bool tts_is_speaking_from_vits_res(const Ref<VITSResource> &vits_model, int speaker_index);
	bool tts_is_speaking_from_vits_path(String vits_model_path, int speaker_index);
	bool tts_is_speaking_from_speaker_uuid(String speaker_uuid);
	bool _tts_is_speaking(TTSSpeaker *speaker_ins);

	bool tts_is_paused_from_vits_res(const Ref<VITSResource> &vits_model, int speaker_index);
	bool tts_is_paused_from_vits_path(String vits_model_path, int speaker_index);
	bool tts_is_paused_from_speaker_uuid(String speaker_uuid);
	bool _tts_is_paused(TTSSpeaker *speaker_ins);

	Array tts_get_voices();
	Array tts_get_voices_from_vits_res(const Ref<VITSResource> &vits_model);
	Array tts_get_voices_from_vits_path(String vits_model_path);
	Array _tts_get_voices(const Ref<VITSResource> &vits_model);

	int tts_infer_from_vits_res(String p_text, const Ref<VITSResource> &vits_model, int speaker_index, int p_volume = 50, float p_pitch = 1.f, float p_rate = 1.f, bool p_interrupt = false, bool auto_play = true, bool immediately = false, int wait_utterance_id = -1, int wait_event = 0, float wait_time = 0.f, bool create_file = false, String file_path = "");

	int tts_infer_from_vits_path(String p_text, const String &vits_model_path, int speaker_index, int p_volume = 50, float p_pitch = 1.f, float p_rate = 1.f, bool p_interrupt = false, bool auto_play = true, bool immediately = false, int wait_utterance_id = -1, int wait_event = 0, float wait_time = 0.f, bool create_file = false, String file_path = "");

	int tts_infer_from_speaker_uuid(String p_text, String speaker_uuid, int p_volume = 50, float p_pitch = 1.f, float p_rate = 1.f, bool p_interrupt = false, bool auto_play = true, bool immediately = false, int wait_utterance_id = -1, int wait_event = 0, float wait_time = 0.f, bool create_file = false, String file_path = "");

	int _tts_infer(String p_text, TTSSpeaker *speaker_ins, int p_volume = 50, float p_pitch = 1.f, float p_rate = 1.f, bool p_interrupt = false, bool auto_play = true, bool immediately = false, int wait_utterance_id = -1, TTSUtteranceEvent wait_event = TTS_UTTERANCE_NONE, float wait_time = 0.f, bool create_file = false, String file_path = "");

	void tts_pause();
	void tts_pause_from_vits_res(const Ref<VITSResource> &vits_model, int speaker_index);
	void tts_pause_from_vits_path(const String &vits_model_path, int speaker_index);
	void tts_pause_from_speaker_uuid(String speaker_uuid);
	void _tts_pause(TTSSpeaker *speaker);

	void tts_resume();
	void tts_resume_from_vits_res(const Ref<VITSResource> &vits_model, int speaker_index);
	void tts_resume_from_vits_path(const String &vits_model_path, int speaker_index);
	void tts_resume_from_speaker_uuid(String speaker_uuid);
	void _tts_resume(TTSSpeaker *speaker);

	void tts_play();
	void tts_play_from_utterance_id(int utterance_id);
	void tts_play_from_vits_res(const Ref<VITSResource> &vits_model, int speaker_index);
	void tts_play_from_vits_path(const String &vits_model_path, int speaker_index);
	void tts_play_from_speaker_uuid(String speaker_uuid);
	void _tts_play(TTSSpeaker *speaker);

	void tts_stop();
	void tts_stop_from_utterance_id(int utterance_id);
	void tts_stop_from_vits_res(const Ref<VITSResource> &vits_model, int speaker_index);
	void tts_stop_from_vits_path(const String &vits_model_path, int speaker_index);
	void tts_stop_from_speaker_uuid(String speaker_uuid);
	void _tts_stop(TTSSpeaker *speaker);

	void add_to_tree();

	static TextToSpeech *get_singleton();
	TextToSpeech();
	~TextToSpeech();
};

#endif // TEXT_TO_SPEECH_H
