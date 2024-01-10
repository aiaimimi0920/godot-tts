#ifndef VITS_RESOURCE_H
#define VITS_RESOURCE_H

#include <godot_cpp/classes/resource.hpp>

using namespace godot;

class VITSResource : public Resource {
	GDCLASS(VITSResource, Resource);

protected:
	static void _bind_methods() {}
	String file;

public:
	void set_file(const String &p_file) {
		file = p_file;
		emit_changed();
	}

	String get_file() {
		return file;
	}

	// PackedByteArray get_content();
	VITSResource() {}
	~VITSResource() {}
};
#endif // VITS_RESOURCE_H
