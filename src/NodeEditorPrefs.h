#pragma once

#include "ecs/PropertyReflection.h"

#include <vector>

namespace rigkit {

/**
 * @brief Node Editor user preferences (plain POD, edited in Preferences).
 * @details Registered on `MSettings` as `rigNodeEditor.editor` ("Node Editor")
 * by `rigNodeEditor::setup()` and persisted to `rigkit_settings.json`.
 * Editor windows read the live instance at draw time, so an edit applies on
 * the next frame. Holds no graph/document state - that stays in `CNodeGraph`.
 */
struct NodeEditorPrefs {
	/// Fields stay on the node while zoomed out. Below this they are too small
	/// and the values are edited in Properties instead.
	float inlineEditMinZoom = 0.35f;
	/// On-node fields are smaller than a pin row so a slider can sit beside its pin.
	float fieldScale = 0.72f;
	/// Combo rows start this far (in pin rows) under the last pin.
	float looseDrop = 0.4f;
	/// Wheel zoom range. A tight clamp here is a near/far clip: the patch cannot
	/// be framed, and a single pin cannot be opened up.
	float zoomMin = 0.05f;
	float zoomMax = 8.f;

	std::vector<sProp> GetProperties();

	/// Clamp every field to the range the canvas can draw with.
	void clamp();
};

/**
 * @brief The one live Node Editor preferences instance.
 * @details Every editor window (pack-opened or host-opened via
 * `rigNodeEditor::makeEditor`) reads the same values here. `rigNodeEditor`
 * registers this instance on `MSettings`; the Preferences panel writes it
 * through `GetProperties()`.
 */
NodeEditorPrefs& nodeEditorPrefs();

} // namespace rigkit
