#include "text_to_speech.h"
#include <atomic>
#include <cmath>
#include <godot_cpp/classes/audio_server.hpp>
#include <godot_cpp/classes/project_settings.hpp>
#include <godot_cpp/classes/time.hpp>
#include <godot_cpp/core/error_macros.hpp>
#include <godot_cpp/core/memory.hpp>
#include <godot_cpp/variant/packed_vector2_array.hpp>
#include <godot_cpp/variant/utility_functions.hpp>
#include <string>
#include <vector>

void _int_array_to_float_array(const uint32_t &p_mix_frame_count,
		const int16_t *p_process_buffer_in,
		float *p_process_buffer_out) {
	for (size_t i = 0; i < p_mix_frame_count; i++) {
		p_process_buffer_out[i] = (p_process_buffer_in[i]);
	}
}

// from audio_effect_pitch_shift.cpp
void SMBPitchShift::PitchShift(float pitchShift, long numSampsToProcess, long fftFrameSize, long osamp, float sampleRate, float *indata, float *outdata, int stride) {
	/*
		Routine smbPitchShift(). See top of file for explanation
		Purpose: doing pitch shifting while maintaining duration using the Short
		Time Fourier Transform.
		Author: (c)1999-2015 Stephan M. Bernsee <s.bernsee [AT] zynaptiq [DOT] com>
	*/

	double magn, phase, tmp, window, real, imag;
	double freqPerBin, expct;
	long i, k, qpd, index, inFifoLatency, stepSize, fftFrameSize2;

	/* set up some handy variables */
	fftFrameSize2 = fftFrameSize / 2;
	stepSize = fftFrameSize / osamp;
	freqPerBin = sampleRate / (double)fftFrameSize;
	expct = 2. * Math_PI * (double)stepSize / (double)fftFrameSize;
	inFifoLatency = fftFrameSize - stepSize;
	if (gRover == 0) {
		gRover = inFifoLatency;
	}
	/* initialize our static arrays */

	/* main processing loop */
	for (i = 0; i < numSampsToProcess; i++) {
		/* As long as we have not yet collected enough data just read in */
		gInFIFO[gRover] = indata[i * stride];
		outdata[i * stride] = gOutFIFO[gRover - inFifoLatency];
		gRover++;

		/* now we have enough data for processing */
		if (gRover >= fftFrameSize) {
			gRover = inFifoLatency;

			/* do windowing and re,im interleave */
			for (k = 0; k < fftFrameSize; k++) {
				window = -.5 * cos(2. * Math_PI * (double)k / (double)fftFrameSize) + .5;
				gFFTworksp[2 * k] = gInFIFO[k] * window;
				gFFTworksp[2 * k + 1] = 0.;
			}

			/* ***************** ANALYSIS ******************* */
			/* do transform */
			smbFft(gFFTworksp, fftFrameSize, -1);

			/* this is the analysis step */
			for (k = 0; k <= fftFrameSize2; k++) {
				/* de-interlace FFT buffer */
				real = gFFTworksp[2 * k];
				imag = gFFTworksp[2 * k + 1];

				/* compute magnitude and phase */
				magn = 2. * sqrt(real * real + imag * imag);
				phase = atan2(imag, real);

				/* compute phase difference */
				tmp = phase - gLastPhase[k];
				gLastPhase[k] = phase;

				/* subtract expected phase difference */
				tmp -= (double)k * expct;

				/* map delta phase into +/- Pi interval */
				qpd = tmp / Math_PI;
				if (qpd >= 0) {
					qpd += qpd & 1;
				} else {
					qpd -= qpd & 1;
				}
				tmp -= Math_PI * (double)qpd;

				/* get deviation from bin frequency from the +/- Pi interval */
				tmp = osamp * tmp / (2. * Math_PI);

				/* compute the k-th partials' true frequency */
				tmp = (double)k * freqPerBin + tmp * freqPerBin;

				/* store magnitude and true frequency in analysis arrays */
				gAnaMagn[k] = magn;
				gAnaFreq[k] = tmp;
			}

			/* ***************** PROCESSING ******************* */
			/* this does the actual pitch shifting */
			memset(gSynMagn, 0, fftFrameSize * sizeof(float));
			memset(gSynFreq, 0, fftFrameSize * sizeof(float));
			for (k = 0; k <= fftFrameSize2; k++) {
				index = k * pitchShift;
				if (index <= fftFrameSize2) {
					gSynMagn[index] += gAnaMagn[k];
					gSynFreq[index] = gAnaFreq[k] * pitchShift;
				}
			}

			/* ***************** SYNTHESIS ******************* */
			/* this is the synthesis step */
			for (k = 0; k <= fftFrameSize2; k++) {
				/* get magnitude and true frequency from synthesis arrays */
				magn = gSynMagn[k];
				tmp = gSynFreq[k];

				/* subtract bin mid frequency */
				tmp -= (double)k * freqPerBin;

				/* get bin deviation from freq deviation */
				tmp /= freqPerBin;

				/* take osamp into account */
				tmp = 2. * Math_PI * tmp / osamp;

				/* add the overlap phase advance back in */
				tmp += (double)k * expct;

				/* accumulate delta phase to get bin phase */
				gSumPhase[k] += tmp;
				phase = gSumPhase[k];

				/* get real and imag part and re-interleave */
				gFFTworksp[2 * k] = magn * cos(phase);
				gFFTworksp[2 * k + 1] = magn * sin(phase);
			}

			/* zero negative frequencies */
			for (k = fftFrameSize + 2; k < 2 * fftFrameSize; k++) {
				gFFTworksp[k] = 0.;
			}

			/* do inverse transform */
			smbFft(gFFTworksp, fftFrameSize, 1);

			/* do windowing and add to output accumulator */
			for (k = 0; k < fftFrameSize; k++) {
				window = -.5 * cos(2. * Math_PI * (double)k / (double)fftFrameSize) + .5;
				gOutputAccum[k] += 2. * window * gFFTworksp[2 * k] / (fftFrameSize2 * osamp);
			}
			for (k = 0; k < stepSize; k++) {
				gOutFIFO[k] = gOutputAccum[k];
			}

			/* shift accumulator */
			memmove(gOutputAccum, gOutputAccum + stepSize, fftFrameSize * sizeof(float));

			/* move input FIFO */
			for (k = 0; k < inFifoLatency; k++) {
				gInFIFO[k] = gInFIFO[k + stepSize];
			}
		}
	}
}

void SMBPitchShift::smbFft(float *fftBuffer, long fftFrameSize, long sign)
/*
	FFT routine, (C)1996 S.M.Bernsee. Sign = -1 is FFT, 1 is iFFT (inverse)
	Fills fftBuffer[0...2*fftFrameSize-1] with the Fourier transform of the
	time domain data in fftBuffer[0...2*fftFrameSize-1]. The FFT array takes
	and returns the cosine and sine parts in an interleaved manner, ie.
	fftBuffer[0] = cosPart[0], fftBuffer[1] = sinPart[0], asf. fftFrameSize
	must be a power of 2. It expects a complex input signal (see footnote 2),
	ie. when working with 'common' audio signals our input signal has to be
	passed as {in[0],0.,in[1],0.,in[2],0.,...} asf. In that case, the transform
	of the frequencies of interest is in fftBuffer[0...fftFrameSize].
*/
{
	float wr, wi, arg, *p1, *p2, temp;
	float tr, ti, ur, ui, *p1r, *p1i, *p2r, *p2i;
	long i, bitm, j, le, le2, k;

	for (i = 2; i < 2 * fftFrameSize - 2; i += 2) {
		for (bitm = 2, j = 0; bitm < 2 * fftFrameSize; bitm <<= 1) {
			if (i & bitm) {
				j++;
			}
			j <<= 1;
		}
		if (i < j) {
			p1 = fftBuffer + i;
			p2 = fftBuffer + j;
			temp = *p1;
			*(p1++) = *p2;
			*(p2++) = temp;
			temp = *p1;
			*p1 = *p2;
			*p2 = temp;
		}
	}
	for (k = 0, le = 2; k < (long)(log((double)fftFrameSize) / log(2.) + .5); k++) {
		le <<= 1;
		le2 = le >> 1;
		ur = 1.0;
		ui = 0.0;
		arg = Math_PI / (le2 >> 1);
		wr = cos(arg);
		wi = sign * sin(arg);
		for (j = 0; j < le2; j += 2) {
			p1r = fftBuffer + j;
			p1i = p1r + 1;
			p2r = p1r + le2;
			p2i = p2r + 1;
			for (i = j; i < 2 * fftFrameSize; i += le) {
				tr = *p2r * ur - *p2i * ui;
				ti = *p2r * ui + *p2i * ur;
				*p2r = *p1r - tr;
				*p2i = *p1i - ti;
				*p1r += tr;
				*p1i += ti;
				p1r += le;
				p1i += le;
				p2r += le;
				p2i += le;
			}
			tr = ur * wr - ui * wi;
			ui = ur * wi + ui * wr;
			ur = tr;
		}
	}
}

bool TTSSpeaker::is_init() {
	return (this->status & SPEAKER_STATE_INIT) != 0;
}
bool TTSSpeaker::is_infering() {
	return (this->status & SPEAKER_STATE_INFERING) != 0;
}

bool TTSSpeaker::is_playing() {
	return (this->status & SPEAKER_STATE_PLAYING) != 0;
}

bool TTSSpeaker::is_pause() {
	return (this->status & SPEAKER_STATE_PAUSE) != 0;
}

bool TTSSpeaker::is_mute() {
	return (this->status & SPEAKER_STATE_MUTE) != 0;
}

void TTSSpeaker::tts_initialize() {
	synthesizer_ptr = new SynthesizerTrn(model_ptr, model_size);
	audio_stream_player = memnew(AudioStreamPlayer);
	TextToSpeech::get_singleton()->add_child(audio_stream_player);

	audio_stream = memnew(AudioStreamPolyphonic);
	audio_stream_player->set_stream(Ref<AudioStream>(audio_stream));
	this->status = this->status | SPEAKER_STATE_INIT;
}

void TTSSpeaker::tts_destroy() {
	if (audio_stream_player) {
		TextToSpeech::get_singleton()->remove_child(audio_stream_player);
		audio_stream_player->queue_free();
		audio_stream_player = nullptr;
	}
	//TODO: 你应该销毁这个指针，当时当前加上这一行会编译后在运行是崩溃
	// delete synthesizer_ptr;

	this->status = this->status | (speaker_state_flags & ~SPEAKER_STATE_INIT);
}

void TTSSpeaker::infer() {
	infer_need_stop = false;
	if (infer_thread.is_started() == false) {
		this->status = this->status | SPEAKER_STATE_INFERING;
		infer_thread.start(callable_mp(this, &TTSSpeaker::process_message_infer), Thread::Priority::PRIORITY_NORMAL);
	} else {
		if (infer_is_running == false) {
			infer_semaphore.post();
		}
	}
}

void TTSSpeaker::uninfer() {
	infer_need_stop = true;
	if (infer_thread.is_started() == true) {
		this->status = this->status | (speaker_state_flags & ~SPEAKER_STATE_INFERING);
		infer_semaphore.post();
		infer_thread.wait_to_finish();
	}
}

void TTSSpeaker::pause() {
	if (audio_stream_player) {
		this->status = this->status | SPEAKER_STATE_PAUSE;
		audio_stream_player->set_stream_paused(true);
	}
}

void TTSSpeaker::resume() {
	if (audio_stream_player) {
		this->status = this->status | (speaker_state_flags & ~SPEAKER_STATE_PAUSE);
		audio_stream_player->set_stream_paused(false);
	}
}

void TTSSpeaker::play() {
	queue_need_stop = false;
	this->status = this->status | SPEAKER_STATE_PLAYING;
	if (audio_stream_player != nullptr) {
		if (audio_stream_player->is_inside_tree() == false) {
			TextToSpeech::get_singleton()->add_to_tree();
		}
		audio_stream_player->play();
	}

	if (queue_thread.is_started() == false) {
		queue_thread.start(callable_mp(this, &TTSSpeaker::process_message_queue), Thread::Priority::PRIORITY_NORMAL);
	} else {
		if (queue_is_running == false) {
			queue_semaphore.post();
		}
	}
}

void TTSSpeaker::stop() {
	queue_need_stop = true;
	this->status = this->status | (speaker_state_flags & ~SPEAKER_STATE_INFERING);
	if (audio_stream_player) {
		audio_stream_player->stop();
	}
	if (queue_thread.is_started() == true) {
		queue_semaphore.post();
		queue_thread.wait_to_finish();
	}
}

void TTSSpeaker::mute() {
	if (audio_stream_player) {
		this->status = this->status | SPEAKER_STATE_MUTE;
		audio_stream_player->set_volume_db(0);
	}
}

void TTSSpeaker::unmute() {
	if (audio_stream_player) {
		this->status = this->status | (speaker_state_flags & ~SPEAKER_STATE_MUTE);
		audio_stream_player->set_volume_db(-80);
	}
}

void TTSSpeaker::append_infer_message(TTSUtterance *message) {
	need_process_message_queue.push_back(message);
}

void TTSSpeaker::play_message(TTSUtterance *message) {
	AudioStreamPlayer *cur_player;
	if (custom_audio_stream_player != nullptr) {
		cur_player = custom_audio_stream_player;
	} else if (audio_stream_player != nullptr) {
		cur_player = audio_stream_player;
	} else {
		return;
	}
	playing_message.push_back(message);

	Ref<AudioStreamPlaybackPolyphonic> cur_audio_stream_playback_polyphonic = cur_player->get_stream_playback();

	message->polyphonic_stream_id = cur_audio_stream_playback_polyphonic->play_stream(message->audio_stream_wav, 0, 0, 1.0);
	message->start_time = Time::get_singleton()->get_ticks_msec();

	List<TTSUtterance *> target_need_update_messages = message->need_update_messages;
	for (int32_t i = target_need_update_messages.size() - 1; i >= 0; i--) {
		if (target_need_update_messages[i]->wait_event == TTS_UTTERANCE_STARTED) {
			target_need_update_messages[i]->target_start_time = playing_message[i]->start_time + (target_need_update_messages[i]->wait_time) * 1000;
		}
		target_need_update_messages.erase(target_need_update_messages[i]);
	}
}

bool TTSSpeaker::is_message_playing(TTSUtterance *message) {
	AudioStreamPlayer *cur_player;
	if (custom_audio_stream_player != nullptr) {
		cur_player = custom_audio_stream_player;
	} else if (audio_stream_player != nullptr) {
		cur_player = audio_stream_player;
	} else {
		return false;
	}
	Ref<AudioStreamPlaybackPolyphonic> cur_audio_stream_playback_polyphonic = cur_player->get_stream_playback();

	return cur_audio_stream_playback_polyphonic->is_stream_playing(message->polyphonic_stream_id);
}

void TTSSpeaker::stop_message(TTSUtterance *message) {
	AudioStreamPlayer *cur_player;
	if (custom_audio_stream_player != nullptr) {
		cur_player = custom_audio_stream_player;
	} else if (audio_stream_player != nullptr) {
		cur_player = audio_stream_player;
	} else {
		return;
	}
	Ref<AudioStreamPlaybackPolyphonic> cur_audio_stream_playback_polyphonic = cur_player->get_stream_playback();

	cur_audio_stream_playback_polyphonic->stop_stream(message->polyphonic_stream_id);
	playing_message.erase(message);
}

uint32_t TTSSpeaker::_resample_audio_buffer(
		const float *p_src, const uint32_t p_src_frame_count,
		const uint32_t p_src_samplerate, const uint32_t p_target_samplerate,
		float *p_dst) {
	if (p_src_samplerate != p_target_samplerate) {
		SRC_DATA src_data;

		src_data.data_in = p_src;
		src_data.data_out = p_dst;

		src_data.input_frames = p_src_frame_count;
		src_data.src_ratio = (double)p_target_samplerate / (double)p_src_samplerate;
		src_data.output_frames = p_src_frame_count * src_data.src_ratio;

		src_data.end_of_input = 0;
		int error = src_simple(&src_data, SRC_SINC_BEST_QUALITY, 1);
		if (error != 0) {
			ERR_PRINT(String(src_strerror(error)));
			return 0;
		}
		return src_data.output_frames_gen;
	} else {
		memcpy(p_dst, p_src,
				static_cast<size_t>(p_src_frame_count) * sizeof(float));
		return p_src_frame_count;
	}
}

static inline unsigned int encode_uint16(uint16_t p_uint, uint8_t *p_arr) {
	for (int i = 0; i < 2; i++) {
		*p_arr = p_uint & 0xFF;
		p_arr++;
		p_uint >>= 8;
	}

	return sizeof(uint16_t);
}

// 处理文本推理
bool TTSSpeaker::process_message_infer() {
	// 对于一个speaker每次只进行一次推理
	while (true) {
		infer_is_running = true;

		while (is_init() && is_playing() && need_process_message_queue.size() > 0) {
			TTSUtterance *message = need_process_message_queue.front()->get();
			int32_t retLen = 0;
			message->generate_time = Time::get_singleton()->get_ticks_msec();
			int16_t *wavData = synthesizer_ptr->infer(message->text.utf8().get_data(), speaker_index, message->rate, retLen);

			int buffer_len = retLen;
			float *buffer_float = (float *)memalloc(sizeof(float) * buffer_len);
			float *resampled_float = (float *)memalloc(sizeof(float) * buffer_len * AudioServer::get_singleton()->get_mix_rate() / 16000);
			_int_array_to_float_array(buffer_len, wavData, buffer_float);

			int result_size = _resample_audio_buffer(
					buffer_float, // Pointer to source buffer
					buffer_len, // Size of source buffer * sizeof(float)
					16000, // Source sample rate
					AudioServer::get_singleton()->get_mix_rate(), // Target sample rate
					// 16000, // Target sample rate
					resampled_float);

			retLen = result_size;
			Vector<AudioFrame> target_audio;

			for (int32_t i = 0; i < retLen; i++) {
				int16_t target_one_buffer = (int16_t)(resampled_float[i] * (message->volume / 100.0));
				AudioFrame cur_audio_frame;
				cur_audio_frame.l = target_one_buffer / 32768.f;
				cur_audio_frame.r = target_one_buffer / 32768.f;
				target_audio.push_back(cur_audio_frame);
			}
			float sample_rate = AudioServer::get_singleton()->get_mix_rate();
			float *in_l = (float *)target_audio.ptrw();
			float *in_r = in_l + 1;

			Vector<AudioFrame> p_dst_frames;
			p_dst_frames.resize(retLen);

			float *out_l = (float *)p_dst_frames.ptrw();
			float *out_r = out_l + 1;

			SMBPitchShift shift_l;
			SMBPitchShift shift_r;

			shift_l.PitchShift(message->pitch, retLen, 2048, 4, sample_rate, in_l, out_l, 2);
			shift_r.PitchShift(message->pitch, retLen, 2048, 4, sample_rate, in_r, out_r, 2);

			PackedByteArray p_data;
			p_data.resize(retLen * 2 * 2);
			uint8_t *w = p_data.ptrw();

			for (int32_t i = 0; i < retLen; i++) {
				int16_t l_v = CLAMP(p_dst_frames[i].l * 32768, -32768, 32767);
				encode_uint16(l_v, &w[(i * 2) * 2]);

				int16_t r_v = CLAMP(p_dst_frames[i].r * 32768, -32768, 32767);
				encode_uint16(r_v, &w[(i * 2 + 1) * 2]);
			}
			// 保存文件
			Ref<AudioStreamWAV> audio_stream_wav_ins;
			audio_stream_wav_ins.instantiate();
			audio_stream_wav_ins->set_data(p_data);
			audio_stream_wav_ins->set_stereo(true);
			audio_stream_wav_ins->set_mix_rate(AudioServer::get_singleton()->get_mix_rate());
			audio_stream_wav_ins->set_format(AudioStreamWAV::FORMAT_16_BITS);
			message->audio_stream_wav = audio_stream_wav_ins;

			if (message->create_file) {
				audio_stream_wav_ins->save_to_wav(message->file_path);
			}
			message->generated_time = Time::get_singleton()->get_ticks_msec();
			TextToSpeech::get_singleton()->call_deferred("emit_signal", "generated_audio_buffer", message->id, p_data, message->create_file ? message->file_path : "");
			if (message->auto_play) {
				// 这里的立刻不是推理完成后立刻，而是不加入等待序列
				if (message->immediately) {
					if (message->wait_utterance_id != -1) {
						need_play_message_pool.push_back(message);
					} else {
						play_message(message);
					}
				} else {
					need_play_message_queue.push_back(message);
				}
			}
			need_process_message_queue.pop_front();
		}
		// callable_mp(this, &TTSSpeaker::uninfer).call_deferred();

		infer_is_running = false;
		infer_semaphore.wait();
		if (infer_need_stop) {
			break;
		}
	}
	return true;
}

bool TTSSpeaker::process_message_queue() {
	UtilityFunctions::print("process_message_queue is_begin");
	while (true) {
		queue_is_running = true;

		while (is_playing()) {
			{
				AudioStreamPlayer *cur_player;
				if (custom_audio_stream_player != nullptr) {
					cur_player = custom_audio_stream_player;
				} else if (audio_stream_player != nullptr) {
					cur_player = audio_stream_player;
				} else {
					return false;
				}
				Ref<AudioStreamPlaybackPolyphonic> cur_audio_stream_playback_polyphonic = cur_player->get_stream_playback();
				// 处理所有的音频播放状态
				bool have_audio_playing = false;

				for (int i = playing_message.size() - 1; i >= 0; i--) {
					if (cur_audio_stream_playback_polyphonic->is_stream_playing(playing_message[i]->polyphonic_stream_id)) {
						if (playing_message[i]->immediately) {
							// 如果是立即播放则不会管
						} else {
							// 如果不是立即播放则说明当前还存在播放内容，那么音频应该还在等待序列中
							have_audio_playing = true;
						}

					} else {
						// 如果已经播放完毕则触发信号并记录时间
						playing_message[i]->finish_time = Time::get_singleton()->get_ticks_msec();
						TextToSpeech::get_singleton()->call_deferred("emit_signal", "finish_audio", playing_message[i]->id, playing_message[i]->finish_time);
						// 注意因为wait_time 是全局性质的，所以应该在TextToSpeech触发是否开启等待的audio
						List<TTSUtterance *> target_need_update_messages = playing_message[i]->need_update_messages;
						for (int32_t i = target_need_update_messages.size() - 1; i >= 0; i--) {
							if (target_need_update_messages[i]->wait_event == TTS_UTTERANCE_ENDED) {
								target_need_update_messages[i]->target_start_time = playing_message[i]->finish_time + (target_need_update_messages[i]->wait_time) * 1000;
							}
							target_need_update_messages.erase(target_need_update_messages[i]);
						}
						// 将这个message 从播放列表中移除
						playing_message.erase(playing_message[i]);
					}
				}
				if (have_audio_playing) {
				} else {
					if (need_play_message_queue.size() > 0) {
						TTSUtterance *message = need_play_message_queue.front()->get();
						if (message->target_start_time == -1) {
							if (message->wait_utterance_id == -1) {
								if (message->wait_time <= 0) {
									// 立刻播放即可
									play_message(message);
									need_play_message_queue.pop_front();
								} else {
									// 等待时间
									message->target_start_time = Time::get_singleton()->get_ticks_msec() + (message->wait_time) * 1000;
								}
							} else {
								if (TextToSpeech::get_singleton()->message_map.has(message->wait_utterance_id)) {
									if (message->wait_event == TTS_UTTERANCE_STARTED) {
										// 如果是开始播放
										TTSUtterance *wait_message = TextToSpeech::get_singleton()->message_map[message->wait_utterance_id];
										if (wait_message->start_time != -1) {
											// 如果已经开始播放了
											if (message->wait_time <= 0) {
												// 立刻播放即可
												play_message(message);
												need_play_message_queue.pop_front();
											} else {
												// 等待时间
												message->target_start_time = TextToSpeech::get_singleton()->message_map[message->wait_utterance_id]->start_time + (message->wait_time) * 1000;
												if (message->target_start_time <= Time::get_singleton()->get_ticks_msec()) {
													play_message(message);
													need_play_message_queue.pop_front();
												}
											}
										} else {
											// 如果没有开始播放那么就等待
											wait_message->need_update_messages.push_back(message);
											// 这里将时间设置为接近无穷大的数值，防止被自动启动
											message->target_start_time = 9223372036854775807;
										}
									} else if (message->wait_event == TTS_UTTERANCE_ENDED) {
										// 如果是开始播放
										TTSUtterance *wait_message = TextToSpeech::get_singleton()->message_map[message->wait_utterance_id];
										if (wait_message->finish_time != -1) {
											// 如果已经结束播放了
											if (message->wait_time <= 0) {
												// 立刻播放即可
												play_message(message);
												need_play_message_queue.pop_front();
											} else {
												// 等待时间
												message->target_start_time = TextToSpeech::get_singleton()->message_map[message->wait_utterance_id]->finish_time + (message->wait_time) * 1000;
												if (message->target_start_time <= Time::get_singleton()->get_ticks_msec()) {
													play_message(message);
													need_play_message_queue.pop_front();
												}
											}
										} else {
											// 如果没有开始播放那么就等待
											wait_message->need_update_messages.push_back(message);
											// 这里将时间设置为接近无穷大的数值，防止被自动启动
											message->target_start_time = 9223372036854775807;
										}
									}
								} else {
									// 如果没有id那么就当作wait_utterance_id=-1进行处理
									if (message->wait_time <= 0) {
										// 立刻播放即可
										play_message(message);
										need_play_message_queue.pop_front();
									} else {
										// 等待时间
										message->target_start_time = Time::get_singleton()->get_ticks_msec() + (message->wait_time) * 1000;
									}
								}
							}
						} else {
							if (Time::get_singleton()->get_ticks_msec() > message->target_start_time) {
								play_message(message);
								need_play_message_queue.pop_front();
							}
						}
					}
				}

				for (int32_t i = need_play_message_pool.size() - 1; i >= 0; i--) {
					TTSUtterance *message = need_play_message_pool[i];
					if (message->target_start_time == -1) {
						if (message->wait_utterance_id == -1) {
							if (message->wait_time <= 0) {
								// 立刻播放即可
								play_message(message);
								need_play_message_pool.erase(message);
							} else {
								// 等待时间
								message->target_start_time = Time::get_singleton()->get_ticks_msec() + (message->wait_time) * 1000;
							}
						} else {
							if (TextToSpeech::get_singleton()->message_map.has(message->wait_utterance_id)) {
								if (message->wait_event == TTS_UTTERANCE_STARTED) {
									// 如果是开始播放
									TTSUtterance *wait_message = TextToSpeech::get_singleton()->message_map[message->wait_utterance_id];
									if (wait_message->start_time != -1) {
										// 如果已经开始播放了
										if (message->wait_time <= 0) {
											// 立刻播放即可
											play_message(message);
											need_play_message_pool.erase(message);
										} else {
											// 等待时间
											message->target_start_time = TextToSpeech::get_singleton()->message_map[message->wait_utterance_id]->start_time + (message->wait_time) * 1000;
											if (message->target_start_time <= Time::get_singleton()->get_ticks_msec()) {
												play_message(message);
												need_play_message_pool.erase(message);
											}
										}
									} else {
										// 如果没有开始播放那么就等待
										wait_message->need_update_messages.push_back(message);
										// 这里将时间设置为接近无穷大的数值，防止被自动启动
										message->target_start_time = 9223372036854775807;
									}
								} else if (message->wait_event == TTS_UTTERANCE_ENDED) {
									// 如果是开始播放
									TTSUtterance *wait_message = TextToSpeech::get_singleton()->message_map[message->wait_utterance_id];
									if (wait_message->finish_time != -1) {
										// 如果已经结束播放了
										if (message->wait_time <= 0) {
											// 立刻播放即可
											play_message(message);
											need_play_message_pool.erase(message);
										} else {
											// 等待时间
											message->target_start_time = TextToSpeech::get_singleton()->message_map[message->wait_utterance_id]->finish_time + (message->wait_time) * 1000;
											if (message->target_start_time <= Time::get_singleton()->get_ticks_msec()) {
												play_message(message);
												need_play_message_pool.erase(message);
											}
										}
									} else {
										// 如果没有开始播放那么就等待
										wait_message->need_update_messages.push_back(message);
										// 这里将时间设置为接近无穷大的数值，防止被自动启动
										message->target_start_time = 9223372036854775807;
									}
								}
							} else {
								// 如果没有id那么就当作wait_utterance_id=-1进行处理
								if (message->wait_time <= 0) {
									// 立刻播放即可
									play_message(message);
									need_play_message_pool.erase(message);
								} else {
									// 等待时间
									message->target_start_time = Time::get_singleton()->get_ticks_msec() + (message->wait_time) * 1000;
								}
							}
						}
					} else {
						if (Time::get_singleton()->get_ticks_msec() > message->target_start_time) {
							play_message(message);
							need_play_message_pool.erase(message);
						}
					}
				}
				OS::get_singleton()->delay_msec(10);
			}
		}

		queue_is_running = false;
		queue_semaphore.wait();
		if (queue_need_stop) {
			break;
		}
	}
	return true;
}

void TTSSpeaker::interrupt() {
	need_process_message_queue.clear();
	need_play_message_queue.clear();
	need_play_message_pool.clear();
	// 循环遍历playing_message
	for (uint32_t i = need_play_message_pool.size() - 1; i >= 0; i--) {
		TTSUtterance *cur_playing_message = need_play_message_pool[i];
		stop_message(cur_playing_message);
	}
	playing_message.clear();
}

void TTSSpeaker::setup(const Ref<VITSResource> &vits_model, float *model_ptr, int model_size, int speaker_index, String speaker_id) {
	this->is_empty = false;
	this->vits_model = vits_model;
	this->model_ptr = model_ptr;
	this->model_size = model_size;
	this->speaker_index = speaker_index;
	this->speaker_id = speaker_id;

	this->status = 0;

	this->synthesizer_ptr = nullptr;
	this->audio_stream_player = nullptr;
	this->custom_audio_stream_player = nullptr;
}

TTSSpeaker::~TTSSpeaker() {
	if (is_init()) {
		tts_destroy();
	}
}

void TTSSpeaker::_bind_methods() {
	ClassDB::bind_method(D_METHOD("process_message_infer"), &TTSSpeaker::process_message_infer);
	ClassDB::bind_method(D_METHOD("process_message_queue"), &TTSSpeaker::process_message_queue);
	ClassDB::bind_method(D_METHOD("uninfer"), &TTSSpeaker::uninfer);
	ClassDB::bind_method(D_METHOD("stop"), &TTSSpeaker::stop);
}

int TextToSpeech::last_utterance_id = 0;
int TextToSpeech::_get_new_utterance_id() {
	last_utterance_id = last_utterance_id + 1;
	return last_utterance_id;
}

Ref<VITSResource> &TextToSpeech::_get_res_from_path(String vits_model_path) {
	Ref<VITSResource> ret = ResourceLoader::get_singleton()->load(vits_model_path, "VITSResource");
	return ret;
}

TTSSpeaker *TextToSpeech::_get_speaker_from_speaker_uuid(String speaker_uuid) {
	if (speaker_uuid.is_empty()) {
		// TTSSpeaker _empty_speaker = TTSSpeaker();
		return nullptr;
	}
	if (speaker_map.has(speaker_uuid)) {
		return speaker_map[speaker_uuid];
	}
	// TTSSpeaker _empty_speaker = TTSSpeaker();
	return nullptr;
}

String TextToSpeech::_create_speaker(const Ref<VITSResource> &vits_model, int speaker_index) {
	String speaker_uuid = _get_speaker_uuid(vits_model, speaker_index);
	TTSSpeaker *cur_speaker = new TTSSpeaker();
	cur_speaker->setup(vits_model, models_ptr[vits_model], models_size[vits_model], speaker_index, speaker_uuid);

	speaker_map[speaker_uuid] = cur_speaker;
	return speaker_uuid;
}

String TextToSpeech::_get_speaker_uuid(const Ref<VITSResource> &vits_model, int speaker_index) {
	String speaker_uuid = vits_model->get_path();
	speaker_uuid = speaker_uuid + "_" + std::to_string(speaker_index).c_str();
	return speaker_uuid;
}

TTSSpeaker *TextToSpeech::_get_speaker(const Ref<VITSResource> &vits_model, int speaker_index) {
	String speaker_uuid = _get_speaker_uuid(vits_model, speaker_index);
	if (speaker_map.has(speaker_uuid) == false) {
		setup_model(vits_model);
	}
	return speaker_map[speaker_uuid];
}

bool TextToSpeech::setup_model(const Ref<VITSResource> &vits_model) {
	if (vits_model.is_null()) {
		return false;
	}
	String file_path = vits_model->get_file();
	if (file_path.is_empty()) {
		return false;
	}
	CharString cur_file_path = ProjectSettings::get_singleton()->globalize_path(file_path).utf8();
	float *model_instance = NULL;
	int cur_model_size = ttsLoadModel(cur_file_path.ptrw(), &model_instance);

	models_size[vits_model] = cur_model_size;
	models_ptr[vits_model] = model_instance;

	SynthesizerTrn *synthesizer = new SynthesizerTrn(model_instance, cur_model_size);

	models_speaker_num[vits_model] = synthesizer->getSpeakerNum();

	for (int i = 0; i < models_speaker_num[vits_model]; i++) {
		_create_speaker(vits_model, i);
	}
	return true;
}

bool TextToSpeech::unsetup_model(const Ref<VITSResource> &vits_model) {
	if (vits_model.is_null()) {
		return false;
	}
	for (int i = 0; i < models_speaker_num[vits_model]; i++) {
		delete speaker_map[_get_speaker_uuid(vits_model, i)];
	}
	float *model_instance = models_ptr[vits_model];
	tts_free_data(model_instance);
	models_ptr.erase(vits_model);
	models_size.erase(vits_model);
	models_speaker_num.erase(vits_model);
	return true;
}

bool TextToSpeech::tts_is_speaking_from_vits_res(const Ref<VITSResource> &vits_model, int speaker_index) {
	return _tts_is_speaking(_get_speaker(vits_model, speaker_index));
}

bool TextToSpeech::tts_is_speaking_from_vits_path(String vits_model_path, int speaker_index) {
	Ref<VITSResource> vits_model = _get_res_from_path(vits_model_path);
	return _tts_is_speaking(_get_speaker(vits_model, speaker_index));
}

bool TextToSpeech::tts_is_speaking_from_speaker_uuid(String speaker_uuid) {
	return _tts_is_speaking(speaker_map[speaker_uuid]);
}

bool TextToSpeech::_tts_is_speaking(TTSSpeaker *speaker_ins) {
	return speaker_ins->is_playing();
}

bool TextToSpeech::tts_is_paused_from_vits_res(const Ref<VITSResource> &vits_model, int speaker_index) {
	return _tts_is_paused(_get_speaker(vits_model, speaker_index));
}

bool TextToSpeech::tts_is_paused_from_vits_path(String vits_model_path, int speaker_index) {
	Ref<VITSResource> vits_model = _get_res_from_path(vits_model_path);
	return _tts_is_paused(_get_speaker(vits_model, speaker_index));
}

bool TextToSpeech::tts_is_paused_from_speaker_uuid(String speaker_uuid) {
	return _tts_is_paused(speaker_map[speaker_uuid]);
}

bool TextToSpeech::_tts_is_paused(TTSSpeaker *speaker_ins) {
	return speaker_ins->is_pause();
}

Array TextToSpeech::tts_get_voices() {
	Array ret = Array();
	for (const KeyValue<const Ref<VITSResource> &, float *> &E : models_ptr) {
		ret.append_array(_tts_get_voices(E.key));
	}
	return ret;
}

Array TextToSpeech::tts_get_voices_from_vits_res(const Ref<VITSResource> &vits_model) {
	return _tts_get_voices(vits_model);
}

Array TextToSpeech::tts_get_voices_from_vits_path(String vits_model_path) {
	Ref<VITSResource> vits_model = _get_res_from_path(vits_model_path);
	return _tts_get_voices(vits_model);
}

Array TextToSpeech::_tts_get_voices(const Ref<VITSResource> &vits_model) {
	Array ret;
	int cur_models_speaker_num = models_speaker_num[vits_model];
	for (int i = 0; i < cur_models_speaker_num; i++) {
		TTSSpeaker *cur_speaker = _get_speaker(vits_model, i);
		// TODO:
		// ret.append(cur_speaker);
	}
	return ret;
}

int TextToSpeech::tts_infer_from_vits_res(String p_text, const Ref<VITSResource> &vits_model, int speaker_index, int p_volume, float p_pitch, float p_rate, bool p_interrupt, bool auto_play, bool immediately, int wait_utterance_id, int wait_event, float wait_time, bool create_file, String file_path) {
	TTSSpeaker *cur_speaker = _get_speaker(vits_model, speaker_index);
	return _tts_infer(p_text, cur_speaker, p_volume, p_pitch, p_rate, p_interrupt, auto_play, immediately, wait_utterance_id, TTSUtteranceEvent(wait_event), wait_time, create_file, file_path);
}

int TextToSpeech::tts_infer_from_vits_path(String p_text, const String &vits_model_path, int speaker_index, int p_volume, float p_pitch, float p_rate, bool p_interrupt, bool auto_play, bool immediately, int wait_utterance_id, int wait_event, float wait_time, bool create_file, String file_path) {
	Ref<VITSResource> vits_model = _get_res_from_path(vits_model_path);
	TTSSpeaker *cur_speaker = _get_speaker(vits_model, speaker_index);
	return _tts_infer(p_text, cur_speaker, p_volume, p_pitch, p_rate, p_interrupt, auto_play, immediately, wait_utterance_id, TTSUtteranceEvent(wait_event), wait_time, create_file, file_path);
}

int TextToSpeech::tts_infer_from_speaker_uuid(String p_text, String speaker_uuid, int p_volume, float p_pitch, float p_rate, bool p_interrupt, bool auto_play, bool immediately, int wait_utterance_id, int wait_event, float wait_time, bool create_file, String file_path) {
	TTSSpeaker *speaker = speaker_map[speaker_uuid];
	return _tts_infer(p_text, speaker, p_volume, p_pitch, p_rate, p_interrupt, auto_play, immediately, wait_utterance_id, TTSUtteranceEvent(wait_event), wait_time, create_file, file_path);
}

int TextToSpeech::_tts_infer(String p_text, TTSSpeaker *speaker, int p_volume, float p_pitch, float p_rate, bool p_interrupt, bool auto_play, bool immediately, int wait_utterance_id, TTSUtteranceEvent wait_event, float wait_time, bool create_file, String file_path) {
	if (speaker->is_empty) {
		return -1;
	}
	if (p_text.is_empty()) {
		return -1;
	}
	if (p_volume < 0) {
		return -1;
	}
	if (p_pitch < 0) {
		return -1;
	}
	if (p_rate < 0) {
		return -1;
	}
	if (p_interrupt) {
		// 清空这个speaker的所有内容
		speaker->interrupt();
	}
	TTSUtterance *message = new TTSUtterance();
	message->text = p_text;
	message->volume = CLAMP(p_volume, 0, 100);
	message->pitch = CLAMP(p_pitch, 0.f, 2.f);
	message->rate = CLAMP(p_rate, 0.1f, 10.f);
	message->id = _get_new_utterance_id();
	message->auto_play = auto_play;
	message->immediately = immediately;
	message->wait_utterance_id = wait_utterance_id;
	message->wait_event = wait_event;
	message->wait_time = wait_time;
	message->create_file = create_file;

	if (create_file) {
		if (file_path.is_empty()) {
			String data_dir = OS::get_singleton()->get_user_data_dir();
			file_path = data_dir + "/tts_" + std::to_string(message->id).c_str() + ".wav";
		}
		if (!(file_path.substr(file_path.length() - 4, 4) == ".wav")) {
			file_path += ".wav";
		}
		message->file_path = file_path;
	}

	message_map[message->id] = message;
	speaker->append_infer_message(message);
	speaker->infer();
	return message->id;
}

void TextToSpeech::tts_pause() {
	for (KeyValue<String, TTSSpeaker *> &E : speaker_map) {
		_tts_pause(E.value);
	}
}

void TextToSpeech::tts_pause_from_vits_res(const Ref<VITSResource> &vits_model, int speaker_index) {
	TTSSpeaker *cur_speaker = _get_speaker(vits_model, speaker_index);
	_tts_pause(cur_speaker);
}

void TextToSpeech::tts_pause_from_vits_path(const String &vits_model_path, int speaker_index) {
	Ref<VITSResource> vits_model = _get_res_from_path(vits_model_path);
	TTSSpeaker *cur_speaker = _get_speaker(vits_model, speaker_index);
	_tts_pause(cur_speaker);
}

void TextToSpeech::tts_pause_from_speaker_uuid(String speaker_uuid) {
	TTSSpeaker *cur_speaker = speaker_map[speaker_uuid];
	_tts_pause(cur_speaker);
}

void TextToSpeech::_tts_pause(TTSSpeaker *speaker) {
	speaker->pause();
}

void TextToSpeech::tts_resume() {
	for (KeyValue<String, TTSSpeaker *> &E : speaker_map) {
		_tts_resume(E.value);
	}
}

void TextToSpeech::tts_resume_from_vits_res(const Ref<VITSResource> &vits_model, int speaker_index) {
	TTSSpeaker *cur_speaker = _get_speaker(vits_model, speaker_index);
	_tts_resume(cur_speaker);
}

void TextToSpeech::tts_resume_from_vits_path(const String &vits_model_path, int speaker_index) {
	Ref<VITSResource> vits_model = _get_res_from_path(vits_model_path);
	TTSSpeaker *cur_speaker = _get_speaker(vits_model, speaker_index);
	_tts_resume(cur_speaker);
}

void TextToSpeech::tts_resume_from_speaker_uuid(String speaker_uuid) {
	TTSSpeaker *cur_speaker = speaker_map[speaker_uuid];
	_tts_resume(cur_speaker);
}

void TextToSpeech::_tts_resume(TTSSpeaker *speaker) {
	speaker->resume();
}

void TextToSpeech::tts_play() {
	for (KeyValue<String, TTSSpeaker *> &E : speaker_map) {
		_tts_play(E.value);
	}
}

void TextToSpeech::tts_play_from_utterance_id(int utterance_id) {
	TTSUtterance *message = message_map[utterance_id];
	if (message->polyphonic_stream_id != -1) {
		if (message->speaker_id != -1) {
			TTSSpeaker *cur_speaker = speaker_map[message->speaker_id];
			cur_speaker->play_message(message);
		}
	}
}

void TextToSpeech::tts_play_from_vits_res(const Ref<VITSResource> &vits_model, int speaker_index) {
	TTSSpeaker *cur_speaker = _get_speaker(vits_model, speaker_index);
	_tts_play(cur_speaker);
}

void TextToSpeech::tts_play_from_vits_path(const String &vits_model_path, int speaker_index) {
	Ref<VITSResource> vits_model = _get_res_from_path(vits_model_path);
	TTSSpeaker *cur_speaker = _get_speaker(vits_model, speaker_index);
	_tts_play(cur_speaker);
}

void TextToSpeech::tts_play_from_speaker_uuid(String speaker_uuid) {
	TTSSpeaker *cur_speaker = speaker_map[speaker_uuid];
	_tts_play(cur_speaker);
}

void TextToSpeech::_tts_play(TTSSpeaker *speaker) {
	if (speaker->is_init() == false) {
		speaker->tts_initialize();
	}
	speaker->play();
}

void TextToSpeech::tts_stop() {
	for (KeyValue<String, TTSSpeaker *> &E : speaker_map) {
		_tts_stop(E.value);
	}
}

void TextToSpeech::tts_stop_from_utterance_id(int utterance_id) {
	TTSUtterance *message = message_map[utterance_id];
	if (message->polyphonic_stream_id != -1) {
		if (message->speaker_id != -1) {
			TTSSpeaker *cur_speaker = speaker_map[message->speaker_id];
			cur_speaker->stop_message(message);
		}
	}
}

void TextToSpeech::tts_stop_from_vits_res(const Ref<VITSResource> &vits_model, int speaker_index) {
	TTSSpeaker *cur_speaker = _get_speaker(vits_model, speaker_index);
	_tts_stop(cur_speaker);
}

void TextToSpeech::tts_stop_from_vits_path(const String &vits_model_path, int speaker_index) {
	Ref<VITSResource> vits_model = _get_res_from_path(vits_model_path);
	TTSSpeaker *cur_speaker = _get_speaker(vits_model, speaker_index);
	_tts_stop(cur_speaker);
}

void TextToSpeech::tts_stop_from_speaker_uuid(String speaker_uuid) {
	TTSSpeaker *cur_speaker = speaker_map[speaker_uuid];
	_tts_stop(cur_speaker);
}

void TextToSpeech::_tts_stop(TTSSpeaker *speaker) {
	speaker->stop();
}

TextToSpeech *TextToSpeech::singleton = nullptr;

TextToSpeech *TextToSpeech::get_singleton() {
	return TextToSpeech::singleton;
}

TextToSpeech::TextToSpeech() {
	TextToSpeech::singleton = this;
}

TextToSpeech::~TextToSpeech() {
	TextToSpeech::singleton = nullptr;
}

void TextToSpeech::add_to_tree() {
	SceneTree *sml = Object::cast_to<SceneTree>(Engine::get_singleton()->get_main_loop());
	if (!sml) {
		return;
	}
	Window *root = sml->get_root();
	root->add_child(TextToSpeech::get_singleton());
}

void TextToSpeech::_bind_methods() {
	ClassDB::bind_method(D_METHOD("setup_model", "vits_model"), &TextToSpeech::setup_model);
	ClassDB::bind_method(D_METHOD("unsetup_model", "vits_model"), &TextToSpeech::unsetup_model);

	ClassDB::bind_method(D_METHOD("tts_is_speaking_from_vits_res", "vits_model", "speaker_index"), &TextToSpeech::tts_is_speaking_from_vits_res);
	ClassDB::bind_method(D_METHOD("tts_is_speaking_from_vits_path", "vits_model_path", "speaker_index"), &TextToSpeech::tts_is_speaking_from_vits_path);
	ClassDB::bind_method(D_METHOD("tts_is_speaking_from_speaker_uuid", "speaker_uuid"), &TextToSpeech::tts_is_speaking_from_speaker_uuid);

	ClassDB::bind_method(D_METHOD("tts_is_paused_from_vits_res", "vits_model", "speaker_index"), &TextToSpeech::tts_is_paused_from_vits_res);
	ClassDB::bind_method(D_METHOD("tts_is_paused_from_vits_path", "vits_model_path", "speaker_index"), &TextToSpeech::tts_is_paused_from_vits_path);
	ClassDB::bind_method(D_METHOD("tts_is_paused_from_speaker_uuid", "speaker_uuid"), &TextToSpeech::tts_is_paused_from_speaker_uuid);

	ClassDB::bind_method(D_METHOD("tts_get_voices"), &TextToSpeech::tts_get_voices);

	ClassDB::bind_method(D_METHOD("tts_get_voices_from_vits_res", "vits_model"), &TextToSpeech::tts_get_voices_from_vits_res);
	ClassDB::bind_method(D_METHOD("tts_get_voices_from_vits_path", "vits_model_path"), &TextToSpeech::tts_get_voices_from_vits_path);

	ClassDB::bind_method(D_METHOD("tts_infer_from_vits_res", "text", "vits_model", "speaker_index", "volume", "pitch", "rate", "interrupt", "auto_play", "immediately", "wait_utterance_id", "wait_event", "wait_time", "create_file", "file_path"), &TextToSpeech::tts_infer_from_vits_res);
	ClassDB::bind_method(D_METHOD("tts_infer_from_vits_path", "text", "vits_model_path", "speaker_index", "volume", "pitch", "rate", "interrupt", "auto_play", "immediately", "wait_utterance_id", "wait_event", "wait_time", "create_file", "file_path"), &TextToSpeech::tts_infer_from_vits_path);
	ClassDB::bind_method(D_METHOD("tts_infer_from_speaker_uuid", "text", "speaker_uuid", "volume", "pitch", "rate", "interrupt", "auto_play", "immediately", "wait_utterance_id", "wait_event", "wait_time", "create_file", "file_path"), &TextToSpeech::tts_infer_from_speaker_uuid);

	ClassDB::bind_method(D_METHOD("tts_pause"), &TextToSpeech::tts_pause);
	ClassDB::bind_method(D_METHOD("tts_pause_from_vits_res", "vits_model", "speaker_index"), &TextToSpeech::tts_pause_from_vits_res);
	ClassDB::bind_method(D_METHOD("tts_pause_from_vits_path", "vits_model_path", "speaker_index"), &TextToSpeech::tts_pause_from_vits_path);
	ClassDB::bind_method(D_METHOD("tts_pause_from_speaker_uuid", "speaker_uuid"), &TextToSpeech::tts_pause_from_speaker_uuid);

	ClassDB::bind_method(D_METHOD("tts_resume"), &TextToSpeech::tts_resume);
	ClassDB::bind_method(D_METHOD("tts_resume_from_vits_res", "vits_model", "speaker_index"), &TextToSpeech::tts_resume_from_vits_res);
	ClassDB::bind_method(D_METHOD("tts_resume_from_vits_path", "vits_model_path", "speaker_index"), &TextToSpeech::tts_resume_from_vits_path);
	ClassDB::bind_method(D_METHOD("tts_resume_from_speaker_uuid", "speaker_uuid"), &TextToSpeech::tts_resume_from_speaker_uuid);

	ClassDB::bind_method(D_METHOD("tts_play"), &TextToSpeech::tts_play);
	ClassDB::bind_method(D_METHOD("tts_play_from_utterance_id", "utterance_id"), &TextToSpeech::tts_play_from_utterance_id);
	ClassDB::bind_method(D_METHOD("tts_play_from_vits_res", "vits_model", "speaker_index"), &TextToSpeech::tts_play_from_vits_res);
	ClassDB::bind_method(D_METHOD("tts_play_from_vits_path", "vits_model_path", "speaker_index"), &TextToSpeech::tts_play_from_vits_path);
	ClassDB::bind_method(D_METHOD("tts_play_from_speaker_uuid", "speaker_uuid"), &TextToSpeech::tts_play_from_speaker_uuid);

	ClassDB::bind_method(D_METHOD("tts_stop"), &TextToSpeech::tts_stop);
	ClassDB::bind_method(D_METHOD("tts_stop_from_utterance_id", "utterance_id"), &TextToSpeech::tts_stop_from_utterance_id);
	ClassDB::bind_method(D_METHOD("tts_stop_from_vits_res", "vits_model", "speaker_index"), &TextToSpeech::tts_stop_from_vits_res);
	ClassDB::bind_method(D_METHOD("tts_stop_from_vits_path", "vits_model_path", "speaker_index"), &TextToSpeech::tts_stop_from_vits_path);
	ClassDB::bind_method(D_METHOD("tts_stop_from_speaker_uuid", "speaker_uuid"), &TextToSpeech::tts_stop_from_speaker_uuid);

	ADD_SIGNAL(MethodInfo("generated_audio_buffer", PropertyInfo(Variant::INT, "utterance_id"), PropertyInfo(Variant::ARRAY, "audio_buffer"), PropertyInfo(Variant::STRING, "file_path")));

	ADD_SIGNAL(MethodInfo("finish_audio", PropertyInfo(Variant::INT, "utterance_id"), PropertyInfo(Variant::INT, "finish_time")));
}
