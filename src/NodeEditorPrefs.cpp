#include "NodeEditorPrefs.h"

#include <algorithm>

namespace rigkit {

std::vector<sProp> NodeEditorPrefs::GetProperties() {
	return {
		{0, "Inline Edit Min Zoom", EPT_FLOAT, &inlineEditMinZoom},
		{1, "Field Scale", EPT_FLOAT, &fieldScale},
		{2, "Loose Row Drop", EPT_FLOAT, &looseDrop},
		{3, "Zoom Min", EPT_FLOAT, &zoomMin},
		{4, "Zoom Max", EPT_FLOAT, &zoomMax},
	};
}

void NodeEditorPrefs::clamp() {
	inlineEditMinZoom = std::clamp(inlineEditMinZoom, 0.05f, 4.f);
	fieldScale = std::clamp(fieldScale, 0.4f, 1.f);
	looseDrop = std::clamp(looseDrop, 0.f, 2.f);
	zoomMin = std::clamp(zoomMin, 0.01f, 1.f);
	zoomMax = std::clamp(zoomMax, 1.f, 16.f);
	if (zoomMax <= zoomMin) {
		zoomMax = zoomMin + 1.f;
	}
}

NodeEditorPrefs& nodeEditorPrefs() {
	static NodeEditorPrefs prefs;
	return prefs;
}

} // namespace rigkit
