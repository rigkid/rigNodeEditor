#pragma once

#include "core/pack/IPack.h"

#include <memory>
#include <string>

namespace rigkit {

class MEcs;
class NodeEditorWindow;

/**
 * @brief Code pack - ImGui Node Editor window over `CNodeGraph`.
 */
class rigNodeEditor : public IPack {
  public:
	/**
	 * @brief Bootstrap.
	 * @details `registerWindows` opens the default Node Editor and Patch bay on the
	 * engine scene. A host with its own registries sets it false and calls `makeEditor`.
	 */
	struct Options {
		bool registerWindows = true;
	};

	rigNodeEditor();
	explicit rigNodeEditor(Options opts);

	bool init() override;
	void setup() override;

	const Options& options() const { return m_opts; }

	/**
	 * @brief Open an editor bound to @p ecs.
	 * @details Null uses the engine registry. The window is created already bound;
	 * `setEcsOverride` is for a later swap, such as a deck reload.
	 */
	std::shared_ptr<NodeEditorWindow> makeEditor(MEcs* ecs, const std::string& title);

  private:
	void wireProperties();

	Options m_opts;
};

} // namespace rigkit
