#include "resource_loader_vits.h"
#include "resource_vits.h"

Variant ResourceFormatLoaderVITS::_load(const String &p_path, const String &original_path, bool use_sub_threads, int32_t cache_mode) const {
	Ref<VITSResource> vits_model = memnew(VITSResource);
	vits_model->set_file(p_path);
	return vits_model;
}
PackedStringArray ResourceFormatLoaderVITS::_get_recognized_extensions() const {
	PackedStringArray array;
	array.push_back("bin");
	return array;
}
bool ResourceFormatLoaderVITS::_handles_type(const StringName &type) const {
	return ClassDB::is_parent_class(type, "VITSResource");
}
String ResourceFormatLoaderVITS::_get_resource_type(const String &p_path) const {
	String el = p_path.get_extension().to_lower();
	if (el == "bin") {
		return "VITSResource";
	}
	return "";
}
