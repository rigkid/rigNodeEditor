#pragma once

#include <imgui.h> // imfilebrowser.h refuses to compile without imgui.h first

#include <imfilebrowser.h>

#include <glm/glm.hpp>
#include <cstdint>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "CNodeGraph.h"
#include "GraphEval.h"
#include "IWindow.h"
#include "ecs/MEcs.h"

namespace rigkit {

class rigProject;

/**
 * @brief Edit a `CNodeGraph` (with dive into nestable groups).
 */
class NodeEditorWindow : public IWindow {
  public:
	NodeEditorWindow();

  protected:
	void renderContents() override;

  private:
	entt::entity findGraphEntity(MEcs& ecs) const;
	/// Create an empty `CNodeGraph` if the scene has none (drop / patch path).
	entt::entity ensureGraph(MEcs& ecs);
	void ensureDocument(MEcs& ecs, const std::string& pathHint);
	ecs::NodeGraphData* activeGraph(ecs::CNodeGraph& root);
	void drawCanvas(ecs::NodeGraphData& graph, const rig::node::EvalResult* ev);
	/// Spawn a `ref.*` node bound to @p propName on @p entityId (shared by
	/// drag-drop and right-click "Patch to Node Editor" requests).
	uint32_t spawnPropRef(ecs::NodeGraphData& graph, MEcs& ecs, uint32_t entityId,
						  const char* propName, int propType, glm::vec2 pos, bool withLfo);
	void drawMenuBar(std::shared_ptr<rigProject> document, entt::entity graphEntity,
					 ecs::CNodeGraph* root, ecs::NodeGraphData* active);
	void drawAddNodeMenu(ecs::NodeGraphData& graph);
	void drawGroupMenuItems(ecs::CNodeGraph& root, ecs::NodeGraphData& active);
	void drawStatusBar(const ecs::NodeGraphData* active, const rig::node::EvalResult* ev);
	void syncSelectionToGraph(ecs::CNodeGraph& root, entt::entity graphEntity, MEcs& ecs);

	glm::vec2 m_pan{0.f, 0.f};
	float m_zoom = 1.f;
	float m_time = 0.f;
	char m_addFilter[64] = {};
	uint32_t m_selectedNode = 0;
	uint32_t m_selectedPin = 0;
	std::unordered_set<uint32_t> m_multi;
	std::vector<uint32_t> m_divePath;
	uint32_t m_dragNode = 0;
	glm::vec2 m_dragOffset{0.f, 0.f};
	uint32_t m_linkFromNode = 0;
	uint32_t m_linkFromPin = 0;
	bool m_linking = false;
	/// Rubber-band (marquee) selection.
	bool m_boxSelecting = false;
	bool m_boxAdditive = false;
	ImVec2 m_boxStart{0.f, 0.f};
	/// Graph-space centre of the canvas last frame (spawn anchor for patch requests).
	glm::vec2 m_canvasCenter{0.f, 0.f};
	std::unordered_map<uint32_t, float> m_evalScratch;

	/// Pending Scene entity drop - spawn ref node popup.
	uint32_t m_pendingRefEntity = 0;
	glm::vec2 m_pendingRefPos{0.f, 0.f};
	bool m_openRefPopup = false;

	ImGui::FileBrowser m_openRigDialog{ImGuiFileBrowserFlags_CloseOnEsc};
	ImGui::FileBrowser m_saveRigDialog{ImGuiFileBrowserFlags_EnterNewFilename |
									   ImGuiFileBrowserFlags_CreateNewDir |
									   ImGuiFileBrowserFlags_CloseOnEsc};
};

} // namespace rigkit
