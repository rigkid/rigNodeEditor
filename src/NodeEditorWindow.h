#pragma once

#include <cstdint>
#include <glm/glm.hpp>
#include <imgui.h>
#include <imfilebrowser.h>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "ecs/MEcs.h"
#include "CNodeGraph.h"
#include "GraphEval.h"
#include "IWindow.h"

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
	void ensureDocument(MEcs& ecs, const std::string& pathHint);
	ecs::NodeGraphData* activeGraph(ecs::CNodeGraph& root);
	void drawCanvas(ecs::NodeGraphData& graph, const rig::node::EvalResult* ev);
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
