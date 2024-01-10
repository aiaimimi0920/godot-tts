extends VBoxContainer


@export var model_res:VITSResource

func _ready():
	pass

func _on_test_button_pressed():
	var _text_to_speech_singleton = Engine.get_singleton("TextToSpeech")
	#_text_to_speech_singleton.setup_model(model_res)
	#_text_to_speech_singleton.tts_play_from_vits_res(model_res)
	var cur_text:String = %Label.text
	var cur_vits_model:VITSResource = model_res
	var cur_speaker_index:int = 0
	var cur_volume:int = 100
	var cur_pitch:float = 1.0
	var cur_rate:float = 1.0
	var cur_interrupt:bool = false
	var cur_auto_play:bool = true
	var cur_immediately:bool = false
	var cur_wait_utterance_id:int = -1
	var cur_wait_event:int = 0
	var cur_wait_time:float = 0.0
	var cur_create_file:bool = true
	var cur_file_path:String = ""
	var message_id = _text_to_speech_singleton.tts_infer_from_vits_res(
		cur_text,cur_vits_model,cur_speaker_index,cur_volume,
		cur_pitch,cur_rate,cur_interrupt,cur_auto_play,
		cur_immediately,cur_wait_utterance_id,cur_wait_event,cur_wait_time,
		cur_create_file,cur_file_path
	)
	printt("message_id",message_id)


func _on_test_button_2_pressed():
	var _text_to_speech_singleton = Engine.get_singleton("TextToSpeech")
	_text_to_speech_singleton.setup_model(model_res)
	await get_tree().create_timer(5).timeout
	_text_to_speech_singleton.tts_play_from_vits_res(model_res, 0)
