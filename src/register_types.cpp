#include "register_types.h"

#include "resource_loader_vits.h"
#include "resource_vits.h"
#include "text_to_speech.h"

#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/os.hpp>
#include <godot_cpp/classes/resource_loader.hpp>
#include <godot_cpp/classes/scene_tree.hpp>
#include <godot_cpp/classes/window.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

static Ref<ResourceFormatLoaderVITS> vits_loader;

static TextToSpeech *TextToSpeechPtr;

void initialize_tts_module(ModuleInitializationLevel p_level) {
	if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE) {
		return;
	}
	GDREGISTER_CLASS(TTSSpeaker);
	GDREGISTER_CLASS(TextToSpeech);
	GDREGISTER_CLASS(VITSResource);
	GDREGISTER_CLASS(ResourceFormatLoaderVITS);
	vits_loader.instantiate();
	ResourceLoader::get_singleton()->add_resource_format_loader(vits_loader);

	TextToSpeechPtr = memnew(TextToSpeech);
	Engine::get_singleton()->register_singleton("TextToSpeech", TextToSpeech::get_singleton());
}

void uninitialize_tts_module(ModuleInitializationLevel p_level) {
	if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE) {
		return;
	}
	SceneTree *sml = Object::cast_to<SceneTree>(Engine::get_singleton()->get_main_loop());

	if (!sml) {
		return;
	}
	Window *root = sml->get_root();

	root->remove_child(TextToSpeech::get_singleton());

	Engine::get_singleton()->unregister_singleton("TextToSpeech");
	memdelete(TextToSpeechPtr);

	ResourceLoader::get_singleton()->remove_resource_format_loader(vits_loader);
	vits_loader.unref();
}

extern "C" {

GDExtensionBool GDE_EXPORT godot_tts_library_init(const GDExtensionInterfaceGetProcAddress p_get_proc_address, GDExtensionClassLibraryPtr p_library, GDExtensionInitialization *r_initialization) {
	godot::GDExtensionBinding::InitObject init_obj(p_get_proc_address, p_library, r_initialization);

	init_obj.register_initializer(initialize_tts_module);
	init_obj.register_terminator(uninitialize_tts_module);
	init_obj.set_minimum_library_initialization_level(MODULE_INITIALIZATION_LEVEL_SCENE);

	return init_obj.init();
}
}
