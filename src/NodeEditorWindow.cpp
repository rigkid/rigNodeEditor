#include "NodeEditorWindow.h"

#include "CNodeGraph.h"
#include "CProject.h"
#include "CSelection.h"
#include "EntityPick.h"
#include "FileDialogs.h"
#include "GraphEval.h"
#include "MWindow.h"
#include "NodeCatalog.h"
#include "NodeGraph.h"
#include "NodePatch.h"
#include "PropEditors.h"
#include "PropertiesWindow.h"
#include "SceneDragPayload.h"
#include "core/RigKitEngine.h"
#include "core/pack/MPack.h"
#include "ecs/PropertyReflection.h"
#include "rigProject.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <imgui_internal.h>
#include <spdlog/spdlog.h>
#include <string>
#include <string_view>

namespace rigkit {
namespace {

/// Below this zoom inline widgets are unreadable - edit in Properties instead.
constexpr float kInlineEditMinZoom = 0.7f;
/// Wheel zoom range. A tight clamp here is a near/far clip: the patch cannot
/// be framed, and a single pin cannot be opened up.
constexpr float kZoomMin = 0.05f;
constexpr float kZoomMax = 8.f;

/**
 * @brief Node canvas metrics derived from the current (DPI-scaled) font.
 * @details Values are graph units (pixels at zoom 1). Everything is expressed
 * in ems so layout tracks the theme font size and FontScaleDpi instead of
 * assuming the 13px ImGui default.
 */
struct Metrics {
	float em = 13.f; ///< ImGui::GetFontSize() at last refresh.
	float nodeW = 168.f;
	float refW = 112.f;
	float titleH = 22.f;
	float pinR = 5.f;
	float pinRow = 20.f;
	float pad = 8.f;
	float paramRow = 22.f;
	float paramGap = 6.f;

	void refresh() {
		em = ImGui::GetFontSize();
		nodeW = std::ceil(em * 13.f);
		refW = std::ceil(em * 8.75f);
		titleH = std::ceil(em * 1.6f);
		pinR = em * 0.38f;
		pinRow = std::ceil(em * 1.55f);
		pad = std::ceil(em * 0.6f);
		paramRow = std::ceil(em + ImGui::GetStyle().FramePadding.y * 2.f + em * 0.3f);
		paramGap = std::ceil(em * 0.45f);
	}
};

/// Refreshed once per frame in renderContents (UI thread only).
Metrics g_m;

bool isRefType(std::string_view typeId) {
	return typeId == "ref.float" || typeId == "ref.vec2" || typeId == "ref.color";
}

bool isSinkType(std::string_view typeId) {
	return typeId == "float.out" || typeId == "vec2.out" || typeId == "color.out" ||
		   isRefType(typeId);
}

/// Rows of inline (on-node) param widgets. String params (ref entity/prop)
/// stay in the Properties inspector.
int inlineParamRows(const ecs::GraphNode& n) {
	if (n.isGroup() || isRefType(n.typeId)) {
		return 0;
	}
	if (n.typeId == "color.value" || n.typeId == "vec2.value") {
		return 1;
	}
	const auto* entry = rig::node::findCatalogEntry(n.typeId);
	if (!entry) {
		return 0;
	}
	int rows = 0;
	for (const auto& p : entry->params) {
		if (std::string_view(p.type) != "string") {
			++rows;
		}
	}
	return rows;
}

float nodeWidth(const ecs::GraphNode& n) {
	if (!isRefType(n.typeId)) {
		return g_m.nodeW;
	}
	// Ref nodes carry long "entity.prop" titles - grow to fit, within reason.
	const float text = ImGui::CalcTextSize(n.title.c_str()).x;
	return std::clamp(text + 2.f * g_m.pad, g_m.refW, g_m.em * 22.f);
}

/// Pin rows = the taller of the In / Out columns (pins stack per side).
int pinRowCount(const ecs::GraphNode& n) {
	int ins = 0;
	int outs = 0;
	for (const auto& p : n.pins) {
		(p.kind == ecs::NodePinKind::In ? ins : outs) += 1;
	}
	return std::max({1, ins, outs});
}

float nodeHeight(const ecs::GraphNode& n) {
	const int rows = pinRowCount(n);
	const int paramRows = inlineParamRows(n);
	float h = g_m.titleH + g_m.pad + static_cast<float>(rows) * g_m.pinRow + g_m.pad;
	if (paramRows > 0) {
		h += g_m.paramGap + static_cast<float>(paramRows) * g_m.paramRow;
	}
	return h;
}

ImVec2 pinPos(const ecs::GraphNode& n, const ecs::NodePin& pin, const ImVec2& origin, float zoom,
			  const glm::vec2& pan) {
	int kindIdx = 0;
	int slot = 0;
	for (const auto& p : n.pins) {
		if (p.kind != pin.kind) {
			continue;
		}
		if (p.id == pin.id) {
			slot = kindIdx;
			break;
		}
		++kindIdx;
	}
	const float nw = nodeWidth(n);
	const float x = (pin.kind == ecs::NodePinKind::In) ? 0.f : nw;
	const float y =
		g_m.titleH + g_m.pad + static_cast<float>(slot) * g_m.pinRow + g_m.pinRow * 0.5f;
	return ImVec2(origin.x + (n.pos.x + pan.x + x) * zoom, origin.y + (n.pos.y + pan.y + y) * zoom);
}

ImVec2 nodeScreen(const ecs::GraphNode& n, const ImVec2& origin, float zoom, const glm::vec2& pan) {
	return ImVec2(origin.x + (n.pos.x + pan.x) * zoom, origin.y + (n.pos.y + pan.y) * zoom);
}

float segDistSq(const ImVec2& p, const ImVec2& a, const ImVec2& b) {
	const float abx = b.x - a.x;
	const float aby = b.y - a.y;
	const float len2 = abx * abx + aby * aby;
	float t = 0.f;
	if (len2 > 1e-6f) {
		t = std::clamp(((p.x - a.x) * abx + (p.y - a.y) * aby) / len2, 0.f, 1.f);
	}
	const float dx = p.x - (a.x + abx * t);
	const float dy = p.y - (a.y + aby * t);
	return dx * dx + dy * dy;
}

/// Squared distance from @p p to a cubic bezier, sampled - wire hit-testing.
float bezierDistSq(const ImVec2& p, const ImVec2& p0, const ImVec2& c0, const ImVec2& c1,
				   const ImVec2& p1) {
	constexpr int kSteps = 24;
	ImVec2 prev = p0;
	float best = segDistSq(p, p0, p0);
	for (int i = 1; i <= kSteps; ++i) {
		const float t = static_cast<float>(i) / kSteps;
		const float u = 1.f - t;
		const ImVec2 pt(
			u * u * u * p0.x + 3.f * u * u * t * c0.x + 3.f * u * t * t * c1.x + t * t * t * p1.x,
			u * u * u * p0.y + 3.f * u * u * t * c0.y + 3.f * u * t * t * c1.y + t * t * t * p1.y);
		best = std::min(best, segDistSq(p, prev, pt));
		prev = pt;
	}
	return best;
}

} // namespace

NodeEditorWindow::NodeEditorWindow(const std::string& title)
	: IWindow(title, ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoScrollbar |
						 ImGuiWindowFlags_NoScrollWithMouse) {
	setCategory("Edit");
	m_openRigDialog.SetTitle("Open Scene");
	setFileBrowserFilters(m_openRigDialog, {".rig"});
	m_saveRigDialog.SetTitle("Save Scene");
	setFileBrowserFilters(m_saveRigDialog, {".rig"});
	installFileBrowserQuickAccess(m_openRigDialog);
	installFileBrowserQuickAccess(m_saveRigDialog);
}

void NodeEditorWindow::setHostEval(const rig::node::EvalResult* result, const float* time,
								   std::unordered_map<uint32_t, float>* scratch) {
	m_hostEval = result;
	m_hostTime = time;
	m_hostScratch = scratch;
}

MEcs* NodeEditorWindow::resolveEcs() const {
	if (m_ecsOverride) {
		return m_ecsOverride;
	}
	auto* engine = getEngine();
	return engine ? engine->getECSManager() : nullptr;
}

entt::entity NodeEditorWindow::findGraphEntity(MEcs& ecs) const {
	auto selView = ecs.view<ecs::CSelection, ecs::CNodeGraph>();
	for (auto e : selView) {
		const auto& s = selView.get<ecs::CSelection>(e);
		if (s.isSelected || s.isMultiSelected) {
			return e;
		}
	}
	auto view = ecs.view<ecs::CNodeGraph>();
	for (auto e : view) {
		return e;
	}
	return entt::null;
}

entt::entity NodeEditorWindow::ensureGraph(MEcs& ecs) {
	entt::entity e = findGraphEntity(ecs);
	if (e != entt::null) {
		return e;
	}
	e = rig::node::makeGraph(ecs, "node-graph");
	ecs::CSelection sel;
	sel.isSelected = true;
	ecs.addComponent<ecs::CSelection>(e, sel);
	return e;
}

void NodeEditorWindow::ensureDocument(MEcs& ecs, const std::string& pathHint) {
	for (auto entity : ecs.view<ecs::CProject>()) {
		(void)entity;
		return;
	}
	auto e = ecs.createEntity("document");
	ecs::CProject doc;
	if (!pathHint.empty()) {
		const auto slash = pathHint.find_last_of("/\\");
		doc.title = (slash == std::string::npos) ? pathHint : pathHint.substr(slash + 1);
	} else {
		doc.title = "untitled";
	}
	doc.path = pathHint;
	doc.dirty = true;
	ecs.addComponent<ecs::CProject>(e, doc);
}

ecs::NodeGraphData* NodeEditorWindow::activeGraph(ecs::CNodeGraph& root) {
	ecs::NodeGraphData* g = &root;
	for (uint32_t id : m_divePath) {
		auto* n = g->findNode(id);
		if (!n || !n->nested) {
			m_divePath.clear();
			return &root;
		}
		g = n->nested.get();
	}
	return g;
}

void NodeEditorWindow::drawAddNodeMenu(ecs::NodeGraphData& graph) {
	if (!ImGui::BeginMenu("Add node")) {
		return;
	}
	ImGui::SetNextItemWidth(220.f);
	ImGui::InputTextWithHint("##filter", "Search...", m_addFilter, sizeof(m_addFilter));
	const std::string_view filter = m_addFilter;
	const glm::vec2 spawnPos{-m_pan.x + 80.f, -m_pan.y + 80.f};
	auto matches = [&](const rig::node::CatalogEntry& e) {
		if (filter.empty()) {
			return true;
		}
		std::string hay = std::string(e.title) + " " + e.typeId + " " + e.category;
		std::string needle(filter);
		for (char& c : hay) {
			c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
		}
		for (char& c : needle) {
			c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
		}
		return hay.find(needle) != std::string::npos;
	};
	if (!filter.empty()) {
		for (const auto& e : rig::node::builtinCatalog()) {
			if (!matches(e)) {
				continue;
			}
			char label[128];
			std::snprintf(label, sizeof(label), "%s/%s", e.category, e.title);
			if (ImGui::MenuItem(label)) {
				const uint32_t id = rig::node::spawnCatalogNode(graph, e.typeId, spawnPos);
				if (id != 0) {
					m_selectedNode = id;
					m_multi = {id};
				}
			}
		}
	} else {
		for (const char* cat : rig::node::catalogCategories()) {
			if (!ImGui::BeginMenu(cat)) {
				continue;
			}
			for (const auto& e : rig::node::builtinCatalog()) {
				if (std::string_view(e.category) != cat) {
					continue;
				}
				if (ImGui::MenuItem(e.title)) {
					const uint32_t id = rig::node::spawnCatalogNode(graph, e.typeId, spawnPos);
					if (id != 0) {
						m_selectedNode = id;
						m_multi = {id};
					}
				}
			}
			ImGui::EndMenu();
		}
	}
	ImGui::EndMenu();
}

void NodeEditorWindow::syncSelectionToGraph(ecs::CNodeGraph& root, entt::entity graphEntity,
											MEcs& ecs) {
	// Properties may Dive / clear selection on the graph component.
	if (root.editDivePath != m_divePath) {
		m_divePath = root.editDivePath;
		m_selectedNode = root.editSelectedNode;
		m_selectedPin = 0;
		m_multi.clear();
		m_pan = {0.f, 0.f};
	}
	root.editDivePath = m_divePath;
	root.editSelectedNode = m_selectedNode;

	if (m_selectedNode != 0 && !m_ecsOverride) {
		selectEntityOnly(ecs, graphEntity);
		if (auto* ui = getEngine() ? getEngine()->getUiManager() : nullptr) {
			if (auto* wm = ui->getWindowManager()) {
				if (auto props = wm->getWindow<PropertiesWindow>("Properties")) {
					props->setSelectedEntity(entt::to_integral(graphEntity));
				}
			}
		}
	}
}

void NodeEditorWindow::drawGroupMenuItems(ecs::CNodeGraph& root, ecs::NodeGraphData& active) {
	if (!m_divePath.empty()) {
		if (ImGui::MenuItem("Up")) {
			m_divePath.pop_back();
			m_selectedNode = 0;
			m_selectedPin = 0;
			m_multi.clear();
			m_pan = {0.f, 0.f};
		}
	}
	if (ImGui::MenuItem("Empty group")) {
		ecs::NodeGraphData* g = activeGraph(root);
		const uint32_t id =
			rig::node::createEmptyGroup(*g, {-m_pan.x + 80.f, -m_pan.y + 80.f}, "Group");
		m_selectedNode = id;
		m_multi = {id};
	}
	const bool canGroup = !m_multi.empty() || m_selectedNode != 0;
	auto collapseSelection = [&](std::string_view title) {
		ecs::NodeGraphData* g = activeGraph(root);
		std::vector<uint32_t> ids(m_multi.begin(), m_multi.end());
		if (ids.empty() && m_selectedNode != 0) {
			ids.push_back(m_selectedNode);
		}
		const uint32_t gid =
			rig::node::createGroup(*g, ids, {-m_pan.x + 120.f, -m_pan.y + 80.f}, title);
		m_selectedNode = gid;
		m_multi = {gid};
	};
	if (ImGui::MenuItem("Group selection", nullptr, false, canGroup)) {
		collapseSelection("Group");
	}
	if (ImGui::MenuItem("Collapse to node", nullptr, false, canGroup)) {
		// Export In / Export Out nodes in the selection define the interface.
		collapseSelection("Node");
	}
	if (ImGui::IsItemHovered()) {
		ImGui::SetTooltip("Wrap the selection in a single node.\n"
						  "Add Export In / Export Out nodes to define its pins.");
	}
	if (auto* sel = active.findNode(m_selectedNode); sel && sel->isGroup()) {
		if (ImGui::MenuItem("Ungroup")) {
			const uint32_t gid = sel->id;
			if (rig::node::ungroup(active, gid)) {
				m_multi.erase(gid);
				m_selectedNode = 0;
				m_selectedPin = 0;
				m_linking = false;
			}
		}
	}
	const bool canPublish = m_selectedNode != 0 && m_selectedPin != 0 && !m_divePath.empty();
	if (ImGui::MenuItem("Publish pin", nullptr, false, canPublish)) {
		ecs::NodeGraphData* parent = &root;
		for (size_t i = 0; i + 1 < m_divePath.size(); ++i) {
			auto* n = parent->findNode(m_divePath[i]);
			if (!n || !n->nested) {
				parent = nullptr;
				break;
			}
			parent = n->nested.get();
		}
		if (parent) {
			auto* groupNode = parent->findNode(m_divePath.back());
			if (groupNode && groupNode->isGroup()) {
				rig::node::publishInnerPin(*groupNode, m_selectedNode, m_selectedPin, {});
			}
		}
	}
}

void NodeEditorWindow::drawMenuBar(std::shared_ptr<rigProject> document, entt::entity graphEntity,
								   ecs::CNodeGraph* root, ecs::NodeGraphData* active) {
	if (!ImGui::BeginMenuBar()) {
		return;
	}
	if (ImGui::BeginMenu("File")) {
		if (ImGui::MenuItem("Open Scene...", nullptr, false, document != nullptr)) {
			applyFileBrowserLayout(m_openRigDialog);
			m_openRigDialog.Open();
		}
		if (ImGui::MenuItem("Save Scene...", nullptr, false, document != nullptr)) {
			applyFileBrowserLayout(m_saveRigDialog);
			m_saveRigDialog.Open();
		}
		if (!document) {
			ImGui::TextDisabled("Scene IO needs rigProject");
		}
		ImGui::Separator();
		const bool canSeed = root != nullptr && m_divePath.empty();
		if (ImGui::MenuItem("Seed demo", nullptr, false, canSeed)) {
			rig::node::seedDemoGraph(*root);
			m_selectedNode = 0;
			m_linking = false;
			m_evalScratch.clear();
			m_multi.clear();
		}
		if (ImGui::MenuItem("Create demo graph", nullptr, false, graphEntity == entt::null)) {
			if (auto* ecs = resolveEcs()) {
				const auto e = rig::node::makeGraph(*ecs, "demo-graph");
				rig::node::seedDemoGraph(ecs->getComponent<ecs::CNodeGraph>(e));
				ecs::CSelection sel;
				sel.isSelected = true;
				ecs->addComponent<ecs::CSelection>(e, sel);
			}
		}
		if (active) {
			ImGui::Separator();
			drawAddNodeMenu(*active);
			if (ImGui::MenuItem("Delete node", "Del", false, m_selectedNode != 0)) {
				rig::node::removeNode(*active, m_selectedNode);
				m_multi.erase(m_selectedNode);
				m_selectedNode = 0;
			}
			if (root) {
				ImGui::Separator();
				drawGroupMenuItems(*root, *active);
			}
		}
		ImGui::EndMenu();
	}
	ImGui::EndMenuBar();
}

void NodeEditorWindow::drawStatusBar(const ecs::NodeGraphData* active,
									 const rig::node::EvalResult* ev) {
	ImGui::PushStyleColor(ImGuiCol_ChildBg, ImGui::GetStyleColorVec4(ImGuiCol_MenuBarBg));
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.f, 4.f));
	ImGui::BeginChild("##node_status", ImVec2(0.f, 0.f), ImGuiChildFlags_AlwaysUseWindowPadding,
					  ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
	if (!active) {
		ImGui::TextDisabled("No CNodeGraph in scene");
		ImGui::EndChild();
		ImGui::PopStyleVar();
		ImGui::PopStyleColor();
		return;
	}
	ImGui::Text("Nodes %zu  Links %zu  t=%.2f  zoom %.0f%%", active->nodes.size(),
				active->links.size(), m_time, m_zoom * 100.f);
	if (!m_divePath.empty()) {
		ImGui::SameLine();
		ImGui::TextDisabled(" | dive %zu", m_divePath.size());
	}
	if (ev) {
		if (!ev->ok) {
			ImGui::SameLine();
			ImGui::TextColored(ImVec4(1.f, 0.4f, 0.3f, 1.f), " | eval: %s", ev->error.c_str());
		} else {
			size_t fi = 0, vi = 0, ci = 0;
			for (const auto& n : active->nodes) {
				if (n.typeId == "float.out" && fi < ev->outputs.size()) {
					ImGui::SameLine();
					ImGui::TextColored(ImVec4(0.55f, 0.9f, 0.65f, 1.f), " | %s=%.3f",
									   n.title.c_str(), ev->outputs[fi++]);
				} else if (n.typeId == "vec2.out" && vi < ev->vec2Outputs.size()) {
					const auto& v = ev->vec2Outputs[vi++];
					ImGui::SameLine();
					ImGui::TextColored(ImVec4(0.55f, 0.75f, 1.f, 1.f), " | %s=%.2f,%.2f",
									   n.title.c_str(), v.x, v.y);
				} else if (n.typeId == "color.out" && ci < ev->colorOutputs.size()) {
					const auto& c = ev->colorOutputs[ci++];
					ImGui::SameLine();
					ImGui::TextColored(ImVec4(1.f, 0.65f, 0.8f, 1.f), " | %s", n.title.c_str());
					ImGui::SameLine(0.f, 4.f);
					const ImVec2 p0 = ImGui::GetCursorScreenPos();
					const float sw = ImGui::GetTextLineHeight();
					const ImVec2 p1(p0.x + sw, p0.y + sw);
					ImGui::GetWindowDrawList()->AddRectFilled(
						p0, p1, ImGui::ColorConvertFloat4ToU32(ImVec4(c.r, c.g, c.b, c.a)));
					ImGui::GetWindowDrawList()->AddRect(p0, p1,
														ImGui::GetColorU32(ImGuiCol_Border));
					ImGui::Dummy(ImVec2(sw, sw));
				} else if (isRefType(n.typeId)) {
					ImGui::SameLine();
					const std::string prop = rig::node::getParamString(n, "prop");
					ImGui::TextColored(ImVec4(0.45f, 0.85f, 0.9f, 1.f), " | %s->%s",
									   n.title.c_str(), prop.empty() ? "?" : prop.c_str());
				}
			}
		}
	}
	ImGui::EndChild();
	ImGui::PopStyleVar();
	ImGui::PopStyleColor();
}

uint32_t NodeEditorWindow::spawnPropRef(ecs::NodeGraphData& graph, MEcs& ecs, uint32_t entityId,
										const char* propName, int propType, glm::vec2 pos,
										bool withLfo, const char* sourceType, const char* sourcePin,
										bool hasMapRange, float mapOutMin, float mapOutMax) {
	const uint32_t nid =
		node_ui::spawnPropRef(graph, ecs, entityId, propName, propType, pos, withLfo, sourceType,
							  sourcePin, hasMapRange, mapOutMin, mapOutMax);
	if (nid != 0) {
		m_selectedNode = nid;
		m_multi = {nid};
	}
	return nid;
}

void NodeEditorWindow::applyPendingPatchRequests() {
	if (ImGui::GetCurrentContext() == nullptr) {
		return;
	}
	auto* ecs = resolveEcs();
	if (!ecs) {
		return;
	}
	const bool hadGraph = findGraphEntity(*ecs) != entt::null;
	ecs::NodeGraphData* active = nullptr;
	auto requeue = [](const ScenePropPatchRequest& req) {
		requestScenePropPatch(req.entity, req.name.c_str(), req.propType, req.withLfo, req.ecsTag,
							  req.sourceType.c_str(), req.sourcePin.c_str(), req.hasMapRange,
							  req.mapOutMin, req.mapOutMax, req.unpatch);
	};
	int spawned = 0;
	for (const auto& req : takeScenePropPatchRequests(ecs)) {
		if (req.unpatch) {
			if (!ecs->registry().valid(static_cast<entt::entity>(req.entity))) {
				continue;
			}
			const entt::entity ge = findGraphEntity(*ecs);
			if (ge == entt::null || !ecs->hasComponent<ecs::CNodeGraph>(ge)) {
				continue;
			}
			node_ui::unpatchProp(ecs->getComponent<ecs::CNodeGraph>(ge), *ecs, req.entity,
								 req.name.c_str(), req.propType);
			continue;
		}
		if (!ecs->registry().valid(static_cast<entt::entity>(req.entity))) {
			requeue(req);
			continue;
		}
		if (!active) {
			const entt::entity ge = ensureGraph(*ecs);
			if (ge == entt::null || !ecs->hasComponent<ecs::CNodeGraph>(ge)) {
				requeue(req);
				continue;
			}
			active = activeGraph(ecs->getComponent<ecs::CNodeGraph>(ge));
		}
		if (!active) {
			requeue(req);
			continue;
		}
		glm::vec2 pos = node_ui::nextSpawnPos(*active);
		if (isVisible() && hadGraph) {
			pos = m_canvasCenter - glm::vec2(g_m.nodeW * 0.5f, 0.f);
		}
		pos += glm::vec2(24.f, 24.f) * static_cast<float>(spawned);
		spawnPropRef(*active, *ecs, req.entity, req.name.c_str(), req.propType, pos, req.withLfo,
					 req.sourceType.c_str(), req.sourcePin.c_str(), req.hasMapRange, req.mapOutMin,
					 req.mapOutMax);
		++spawned;
	}
}

void NodeEditorWindow::drawCanvas(ecs::NodeGraphData& graph, const rig::node::EvalResult* ev) {
	const ImVec2 canvasSize = ImGui::GetContentRegionAvail();
	const ImVec2 origin = ImGui::GetCursorScreenPos();
	// AllowOverlap lets the inline param widgets drawn on node bodies take hover.
	ImGui::SetNextItemAllowOverlap();
	ImGui::InvisibleButton("##node_canvas", canvasSize,
						   ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonMiddle |
							   ImGuiButtonFlags_MouseButtonRight);
	const bool hovered = ImGui::IsItemHovered();
	const ImVec2 mouse = ImGui::GetIO().MousePos;
	auto* dl = ImGui::GetWindowDrawList();

	auto toGraph = [&](ImVec2 screen) {
		return glm::vec2((screen.x - origin.x) / m_zoom - m_pan.x,
						 (screen.y - origin.y) / m_zoom - m_pan.y);
	};
	// Pan/zoom before hit-testing. Zoom stays under the cursor. The child must
	// not also scroll: inline widgets used to grow the scroll range, and the
	// window clip then ate the graph while the wheel both zoomed and panned.
	ImGuiWindow* canvasWin = ImGui::GetCurrentWindow();
	const ImVec2 canvasCursorMax = canvasWin->DC.CursorMaxPos;
	if (hovered && (ImGui::IsMouseDragging(ImGuiMouseButton_Middle) ||
					(ImGui::GetIO().KeyAlt && ImGui::IsMouseDragging(ImGuiMouseButton_Left)))) {
		m_pan.x += ImGui::GetIO().MouseDelta.x / std::max(m_zoom, 0.001f);
		m_pan.y += ImGui::GetIO().MouseDelta.y / std::max(m_zoom, 0.001f);
	}
	if (hovered && ImGui::GetIO().MouseWheel != 0.f && !ImGui::IsAnyItemActive()) {
		const glm::vec2 before = toGraph(mouse);
		m_zoom = std::clamp(m_zoom * std::pow(1.1f, ImGui::GetIO().MouseWheel), kZoomMin, kZoomMax);
		m_pan.x = (mouse.x - origin.x) / m_zoom - before.x;
		m_pan.y = (mouse.y - origin.y) / m_zoom - before.y;
	}
	m_canvasCenter =
		toGraph(ImVec2(origin.x + canvasSize.x * 0.5f, origin.y + canvasSize.y * 0.5f));

	if (ImGui::BeginDragDropTarget()) {
		if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kRigSceneEntityPayload)) {
			const uint32_t id = *static_cast<const uint32_t*>(payload->Data);
			m_pendingRefEntity = id;
			m_pendingRefPos = toGraph(mouse);
			m_openRefPopup = true;
		}
		if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kRigScenePropPayload)) {
			const auto* prop = static_cast<const RigScenePropPayload*>(payload->Data);
			auto* ecs = resolveEcs();
			if (ecs && prop && prop->name[0]) {
				if (prop->ecsTag && prop->ecsTag != ecs) {
					spdlog::warn("[Node Editor] '{}' belongs to another scene/deck - "
								 "drop it on that deck's patch editor",
								 prop->name);
				} else {
					// Alt+drop: Modulate - spawn LFO and wire into the Ref (same
					// addressing as rig.mod.binding: entity + property).
					spawnPropRef(graph, *ecs, prop->entity, prop->name, prop->propType,
								 toGraph(mouse), ImGui::GetIO().KeyAlt);
				}
			}
		}
		ImGui::EndDragDropTarget();
	}

	const ImGuiStyle& style = ImGui::GetStyle();
	const float round = style.FrameRounding * m_zoom;
	const ImU32 colCanvas = ImGui::GetColorU32(ImGuiCol_FrameBg);
	const ImU32 colLink = ImGui::GetColorU32(ImGuiCol_TextDisabled);
	const ImU32 colLinkActive = ImGui::GetColorU32(ImGuiCol_SliderGrabActive);
	const ImU32 colNode = ImGui::GetColorU32(ImGuiCol_ChildBg, 1.f);
	const ImU32 colNodeSolid = ImGui::GetColorU32(ImGuiCol_PopupBg);
	const ImU32 colRef = ImGui::GetColorU32(ImGuiCol_FrameBgHovered);
	const ImU32 colGroup = ImGui::GetColorU32(ImGuiCol_TitleBgActive);
	const ImU32 colBorder = ImGui::GetColorU32(ImGuiCol_Border);
	const ImU32 colSelect = ImGui::GetColorU32(ImGuiCol_SliderGrabActive);
	const ImU32 colTitle = ImGui::GetColorU32(ImGuiCol_Header);
	const ImU32 colTitleRef = ImGui::GetColorU32(ImGuiCol_HeaderHovered);
	const ImU32 colTitleGroup = ImGui::GetColorU32(ImGuiCol_HeaderActive);
	const ImU32 colText = ImGui::GetColorU32(ImGuiCol_Text);
	const ImU32 colTextDim = ImGui::GetColorU32(ImGuiCol_TextDisabled);
	const ImU32 colPinF = ImGui::GetColorU32(ImGuiCol_CheckMark);
	const ImU32 colPinV = ImGui::GetColorU32(ImGuiCol_SliderGrab);
	const ImU32 colPinC = ImGui::GetColorU32(ImGuiCol_ButtonActive);
	const ImU32 colValue = ImGui::GetColorU32(ImGuiCol_SliderGrabActive);
	// ChildBg is often transparent in themes - fall back to PopupBg for node bodies.
	const ImU32 colBody = (colNode & IM_COL32_A_MASK) ? colNode : colNodeSolid;

	const ImVec2 canvasMax(origin.x + canvasSize.x, origin.y + canvasSize.y);
	dl->AddRectFilled(origin, canvasMax, colCanvas);
	// Window clip (not only the draw list) so inline widgets cannot spill into
	// the menu or status bar, and cannot extend the scrollable range.
	ImGui::PushClipRect(origin, canvasMax, true);

	// Wires: hovered wires highlight (click selects, Del / right-click removes).
	uint32_t hoveredLink = 0;
	const float wireHitR = std::max(6.f, 5.f * m_zoom);
	float wireHitBest = wireHitR * wireHitR;
	for (const auto& link : graph.links) {
		const auto* a = graph.findNode(link.fromNode);
		const auto* b = graph.findNode(link.toNode);
		const auto* pa = graph.findPin(link.fromNode, link.fromPin);
		const auto* pb = graph.findPin(link.toNode, link.toPin);
		if (!a || !b || !pa || !pb) {
			continue;
		}
		const ImVec2 p0 = pinPos(*a, *pa, origin, m_zoom, m_pan);
		const ImVec2 p1 = pinPos(*b, *pb, origin, m_zoom, m_pan);
		const float dx = std::max(40.f * m_zoom, std::fabs(p1.x - p0.x) * 0.5f);
		if (hovered && !m_linking) {
			const float d2 =
				bezierDistSq(mouse, p0, ImVec2(p0.x + dx, p0.y), ImVec2(p1.x - dx, p1.y), p1);
			if (d2 <= wireHitBest) {
				wireHitBest = d2;
				hoveredLink = link.id;
			}
		}
	}
	for (const auto& link : graph.links) {
		const auto* a = graph.findNode(link.fromNode);
		const auto* b = graph.findNode(link.toNode);
		const auto* pa = graph.findPin(link.fromNode, link.fromPin);
		const auto* pb = graph.findPin(link.toNode, link.toPin);
		if (!a || !b || !pa || !pb) {
			continue;
		}
		const ImVec2 p0 = pinPos(*a, *pa, origin, m_zoom, m_pan);
		const ImVec2 p1 = pinPos(*b, *pb, origin, m_zoom, m_pan);
		const float dx = std::max(40.f * m_zoom, std::fabs(p1.x - p0.x) * 0.5f);
		const bool hot = link.id == m_selectedLink || link.id == hoveredLink;
		dl->AddBezierCubic(p0, ImVec2(p0.x + dx, p0.y), ImVec2(p1.x - dx, p1.y), p1,
						   hot ? colLinkActive : colLink, (hot ? 3.f : 2.f) * m_zoom);
	}

	if (m_linking) {
		const auto* a = graph.findNode(m_linkFromNode);
		const auto* pa = graph.findPin(m_linkFromNode, m_linkFromPin);
		if (a && pa) {
			const ImVec2 p0 = pinPos(*a, *pa, origin, m_zoom, m_pan);
			const float dx = 40.f * m_zoom;
			dl->AddBezierCubic(p0, ImVec2(p0.x + dx, p0.y), ImVec2(mouse.x - dx, mouse.y), mouse,
							   colLinkActive, 2.0f * m_zoom);
		}
	}

	for (auto& node : graph.nodes) {
		const ImVec2 tl = nodeScreen(node, origin, m_zoom, m_pan);
		const float w = nodeWidth(node) * m_zoom;
		const float h = nodeHeight(node) * m_zoom;
		const ImVec2 br(tl.x + w, tl.y + h);
		const bool selected = node.id == m_selectedNode || m_multi.count(node.id);
		const bool refNode = isRefType(node.typeId);
		const ImU32 fill = node.isGroup() ? colGroup : (refNode ? colRef : colBody);
		const ImU32 titleFill = node.isGroup() ? colTitleGroup : (refNode ? colTitleRef : colTitle);
		// Node text is drawn at em * zoom so it tracks the scaled node body.
		const float fontSz = g_m.em * m_zoom;
		ImFont* font = ImGui::GetFont();
		dl->AddRectFilled(tl, br, fill, round);
		dl->AddRect(tl, br, selected ? colSelect : colBorder, round, 0, selected ? 2.f : 1.f);
		dl->AddRectFilled(tl, ImVec2(br.x, tl.y + g_m.titleH * m_zoom), titleFill, round);
		const ImVec2 titlePos(tl.x + g_m.pad * 0.75f * m_zoom,
							  tl.y + (g_m.titleH - g_m.em) * 0.5f * m_zoom);
		dl->PushClipRect(tl, ImVec2(br.x - 3.f * m_zoom, tl.y + g_m.titleH * m_zoom), true);
		dl->AddText(font, fontSz, titlePos, colText, node.title.c_str());
		dl->PopClipRect();
		if (hovered && mouse.x >= tl.x && mouse.x <= br.x && mouse.y >= tl.y &&
			mouse.y <= tl.y + g_m.titleH * m_zoom &&
			ImGui::CalcTextSize(node.title.c_str()).x * m_zoom > w - g_m.pad * m_zoom) {
			ImGui::SetTooltip("%s", node.title.c_str());
		}

		for (const auto& pin : node.pins) {
			const ImVec2 pp = pinPos(node, pin, origin, m_zoom, m_pan);
			ImU32 col = colPinF;
			if (pin.type == "vec2") {
				col = colPinV;
			} else if (pin.type == "vec4") {
				col = colPinC;
			}
			dl->AddCircleFilled(pp, g_m.pinR * m_zoom, col);
			const char* label = pin.name.c_str();
			const float labelY = pp.y - g_m.em * 0.5f * m_zoom;
			const float labelGap = (g_m.pinR + 3.f) * m_zoom;
			if (pin.kind == ecs::NodePinKind::In) {
				dl->AddText(font, fontSz, ImVec2(pp.x + labelGap, labelY), colTextDim, label);
			} else {
				const float tw = ImGui::CalcTextSize(label).x * m_zoom;
				dl->AddText(font, fontSz, ImVec2(pp.x - labelGap - tw, labelY), colTextDim, label);
			}
			if (ev && ev->ok) {
				const bool sinkOut = isSinkType(node.typeId);
				const bool showVal = pin.kind == ecs::NodePinKind::Out ||
									 (sinkOut && pin.kind == ecs::NodePinKind::In);
				if (showVal) {
					const auto it = ev->pinValue.find(rig::node::pinKey(node.id, pin.id));
					if (it != ev->pinValue.end()) {
						char buf[48];
						const glm::vec4& v = it->second;
						std::string ty = pin.type;
						if (sinkOut && pin.kind == ecs::NodePinKind::In) {
							if (node.typeId == "vec2.out" || node.typeId == "ref.vec2") {
								ty = "vec2";
							} else if (node.typeId == "color.out" || node.typeId == "ref.color") {
								ty = "vec4";
							} else {
								ty = "float";
							}
						}
						// Values sit outside the node (right of Out pins, left of
						// sink In pins) so they never collide with pin labels.
						const bool rightSide = pin.kind == ecs::NodePinKind::Out;
						if (ty == "vec2") {
							std::snprintf(buf, sizeof(buf), "%.2f,%.2f", v.x, v.y);
						} else if (ty == "vec4") {
							const float sw = g_m.em * 1.3f * m_zoom;
							const float sh = g_m.em * 0.85f * m_zoom;
							const float sx = rightSide ? pp.x + labelGap : pp.x - labelGap - sw;
							dl->AddRectFilled(ImVec2(sx, pp.y - sh * 0.5f),
											  ImVec2(sx + sw, pp.y + sh * 0.5f),
											  IM_COL32(static_cast<int>(v.r * 255.f),
													   static_cast<int>(v.g * 255.f),
													   static_cast<int>(v.b * 255.f), 255));
							buf[0] = 0;
						} else {
							std::snprintf(buf, sizeof(buf), "%.2f", v.x);
						}
						if (buf[0]) {
							const float tw = ImGui::CalcTextSize(buf).x * m_zoom;
							const float vx = rightSide ? pp.x + labelGap : pp.x - labelGap - tw;
							dl->AddText(font, fontSz, ImVec2(vx, labelY), colValue, buf);
						}
					}
				}
			}
		}

		// Inline param editing on the node body (catalog-driven, matches the
		// Properties inspector). String params (ref entity/prop) stay in Properties.
		const int paramRows = inlineParamRows(node);
		if (paramRows > 0 && m_zoom >= kInlineEditMinZoom) {
			const int pinRows = pinRowCount(node);
			const float y0 = tl.y + (g_m.titleH + g_m.pad +
									 static_cast<float>(pinRows) * g_m.pinRow + g_m.paramGap) *
										m_zoom;
			const float x0 = tl.x + g_m.pad * m_zoom;
			const float itemW = std::max(24.f, w - 2.f * g_m.pad * m_zoom);
			// Scale font + frame padding so widget rows track the zoomed node body.
			// PushFont takes the *unscaled* base size (FontScaleMain/Dpi are
			// applied on top) - passing GetFontSize() here would double-scale.
			ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * m_zoom);
			const ImVec2 basePad = ImGui::GetStyle().FramePadding;
			ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,
								ImVec2(basePad.x * m_zoom, basePad.y * m_zoom));
			ImGui::PushID(static_cast<int>(node.id));
			auto paramRowPos = [&](int row) {
				return ImVec2(x0, y0 + static_cast<float>(row) * g_m.paramRow * m_zoom);
			};
			auto linkedIn = [&](const char* key) {
				for (const auto& pin : node.pins) {
					if (pin.kind != ecs::NodePinKind::In || pin.name != key) {
						continue;
					}
					for (const auto& l : graph.links) {
						if (l.toNode == node.id && l.toPin == pin.id) {
							return true;
						}
					}
				}
				return false;
			};
			if (node.typeId == "color.value") {
				ImGui::SetCursorScreenPos(paramRowPos(0));
				float rgba[4] = {rig::node::getParamFloat(node, "r", 1.f),
								 rig::node::getParamFloat(node, "g", 1.f),
								 rig::node::getParamFloat(node, "b", 1.f),
								 rig::node::getParamFloat(node, "a", 1.f)};
				if (ImGui::ColorEdit4("##color", rgba,
									  ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel)) {
					rig::node::setParamFloat(node, "r", rgba[0]);
					rig::node::setParamFloat(node, "g", rgba[1]);
					rig::node::setParamFloat(node, "b", rgba[2]);
					rig::node::setParamFloat(node, "a", rgba[3]);
				}
			} else if (node.typeId == "vec2.value") {
				ImGui::SetCursorScreenPos(paramRowPos(0));
				ImGui::SetNextItemWidth(itemW);
				float xy[2] = {rig::node::getParamFloat(node, "x", 0.f),
							   rig::node::getParamFloat(node, "y", 0.f)};
				if (ImGui::DragFloat2("##xy", xy, 0.01f)) {
					rig::node::setParamFloat(node, "x", xy[0]);
					rig::node::setParamFloat(node, "y", xy[1]);
				}
			} else if (const auto* entry = rig::node::findCatalogEntry(node.typeId)) {
				int row = 0;
				for (const auto& p : entry->params) {
					if (std::string_view(p.type) == "string") {
						continue;
					}
					ImGui::SetCursorScreenPos(paramRowPos(row));
					ImGui::PushID(p.key);
					if (linkedIn(p.key)) {
						ImGui::TextDisabled("%s: linked", p.label);
					} else {
						ImGui::SetNextItemWidth(itemW);
						float v = rig::node::getParamFloat(node, p.key, p.def);
						if (p.ui == 1 && p.comboLabels) {
							int idx = static_cast<int>(std::lround(v));
							const int lo = static_cast<int>(p.min);
							const int hi = static_cast<int>(p.max);
							idx = std::clamp(idx, lo, hi);
							if (ImGui::Combo("##combo", &idx, p.comboLabels, hi - lo + 1)) {
								rig::node::setParamFloat(node, p.key, static_cast<float>(idx));
							}
						} else {
							char fmt[64];
							std::snprintf(fmt, sizeof(fmt), "%s: %%.2f", p.label);
							if (ImGui::DragFloat("##v", &v, p.speed, p.min, p.max, fmt)) {
								rig::node::setParamFloat(node, p.key, v);
							}
						}
					}
					ImGui::PopID();
					++row;
				}
			}
			ImGui::PopID();
			ImGui::PopStyleVar();
			ImGui::PopFont();
		}
	}

	if (hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
		const glm::vec2 gp = toGraph(mouse);
		for (auto it = graph.nodes.rbegin(); it != graph.nodes.rend(); ++it) {
			const float h = nodeHeight(*it);
			const float w = nodeWidth(*it);
			if (gp.x >= it->pos.x && gp.x <= it->pos.x + w && gp.y >= it->pos.y &&
				gp.y <= it->pos.y + h && it->isGroup()) {
				m_divePath.push_back(it->id);
				m_selectedNode = 0;
				m_selectedPin = 0;
				m_multi.clear();
				m_pan = {0.f, 0.f};
				break;
			}
		}
	}

	if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !ImGui::GetIO().KeyAlt) {
		const glm::vec2 gp = toGraph(mouse);
		bool hitPin = false;
		bool hitNode = false;
		for (auto it = graph.nodes.rbegin(); it != graph.nodes.rend(); ++it) {
			auto& node = *it;
			const float h = nodeHeight(node);
			const float w = nodeWidth(node);
			for (const auto& pin : node.pins) {
				const ImVec2 pp = pinPos(node, pin, origin, m_zoom, m_pan);
				const float dx = mouse.x - pp.x;
				const float dy = mouse.y - pp.y;
				if (dx * dx + dy * dy <= (g_m.pinR * m_zoom + 4.f) * (g_m.pinR * m_zoom + 4.f)) {
					hitPin = true;
					m_selectedNode = node.id;
					m_selectedPin = pin.id;
					m_selectedLink = 0;
					if (!m_linking) {
						if (pin.kind == ecs::NodePinKind::Out) {
							m_linking = true;
							m_linkFromNode = node.id;
							m_linkFromPin = pin.id;
						} else {
							// Pick up an existing wire: detach it and keep
							// linking from its source (drop elsewhere to rewire,
							// release on empty canvas to disconnect).
							uint32_t pickId = 0;
							for (const auto& l : graph.links) {
								if (l.toNode == node.id && l.toPin == pin.id) {
									pickId = l.id;
									m_linkFromNode = l.fromNode;
									m_linkFromPin = l.fromPin;
									break;
								}
							}
							if (pickId != 0) {
								rig::node::removeLink(graph, pickId);
								m_linking = true;
							}
						}
					} else if (pin.kind == ecs::NodePinKind::In) {
						if (!rig::node::tryLink(graph, m_linkFromNode, m_linkFromPin, node.id,
												pin.id)) {
							spdlog::warn("[Node Editor] link rejected");
						}
						m_linking = false;
					}
					break;
				}
			}
			if (hitPin) {
				break;
			}
			if (gp.x >= node.pos.x && gp.x <= node.pos.x + w && gp.y >= node.pos.y &&
				gp.y <= node.pos.y + h) {
				hitNode = true;
				m_selectedNode = node.id;
				m_selectedPin = 0;
				m_selectedLink = 0;
				if (ImGui::GetIO().KeyCtrl) {
					if (m_multi.count(node.id)) {
						m_multi.erase(node.id);
					} else {
						m_multi.insert(node.id);
					}
				} else {
					m_multi = {node.id};
				}
				m_dragNode = node.id;
				m_dragOffset = gp - node.pos;
				break;
			}
		}
		if (!hitPin && !hitNode) {
			m_selectedNode = 0;
			m_selectedPin = 0;
			if (!ImGui::GetIO().KeyCtrl) {
				m_multi.clear();
			}
			m_linking = false;
			m_dragNode = 0;
			if (hoveredLink != 0) {
				// Click a wire to select it (Del disconnects).
				m_selectedLink = hoveredLink;
			} else {
				m_selectedLink = 0;
				// Begin rubber-band selection from empty canvas.
				m_boxSelecting = true;
				m_boxAdditive = ImGui::GetIO().KeyCtrl;
				m_boxStart = mouse;
			}
		}
	}

	// Right-click a wire: context disconnect.
	if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right) && hoveredLink != 0) {
		m_selectedLink = hoveredLink;
		ImGui::OpenPopup("##link_ctx");
	}
	if (ImGui::BeginPopup("##link_ctx")) {
		if (ImGui::MenuItem("Disconnect")) {
			rig::node::removeLink(graph, m_selectedLink);
			m_selectedLink = 0;
		}
		ImGui::EndPopup();
	}

	if (m_boxSelecting) {
		const ImVec2 mn(std::min(m_boxStart.x, mouse.x), std::min(m_boxStart.y, mouse.y));
		const ImVec2 mx(std::max(m_boxStart.x, mouse.x), std::max(m_boxStart.y, mouse.y));
		const float dxs = mouse.x - m_boxStart.x;
		const float dys = mouse.y - m_boxStart.y;
		const bool dragged = dxs * dxs + dys * dys > 16.f;
		if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
			if (dragged) {
				dl->AddRectFilled(mn, mx, ImGui::GetColorU32(ImGuiCol_SliderGrabActive, 0.15f));
				dl->AddRect(mn, mx, colSelect, 0.f, 0, 1.f);
			}
		} else {
			if (dragged) {
				const glm::vec2 g0 = toGraph(mn);
				const glm::vec2 g1 = toGraph(mx);
				if (!m_boxAdditive) {
					m_multi.clear();
				}
				for (const auto& node : graph.nodes) {
					const float w = nodeWidth(node);
					const float h = nodeHeight(node);
					const bool overlap = node.pos.x <= g1.x && node.pos.x + w >= g0.x &&
										 node.pos.y <= g1.y && node.pos.y + h >= g0.y;
					if (overlap) {
						m_multi.insert(node.id);
					}
				}
				if (!m_multi.empty()) {
					m_selectedNode = *m_multi.begin();
				}
			}
			m_boxSelecting = false;
		}
	}

	if (m_dragNode != 0 && ImGui::IsMouseDragging(ImGuiMouseButton_Left) &&
		!ImGui::GetIO().KeyAlt) {
		if (auto* n = graph.findNode(m_dragNode)) {
			n->pos = toGraph(mouse) - m_dragOffset;
		}
	}
	if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
		if (m_linking) {
			bool completed = false;
			for (auto it = graph.nodes.rbegin(); it != graph.nodes.rend(); ++it) {
				const auto& node = *it;
				for (const auto& pin : node.pins) {
					if (pin.kind != ecs::NodePinKind::In) {
						continue;
					}
					const ImVec2 pp = pinPos(node, pin, origin, m_zoom, m_pan);
					const float dx = mouse.x - pp.x;
					const float dy = mouse.y - pp.y;
					if (dx * dx + dy * dy <=
						(g_m.pinR * m_zoom + 4.f) * (g_m.pinR * m_zoom + 4.f)) {
						if (!rig::node::tryLink(graph, m_linkFromNode, m_linkFromPin, node.id,
												pin.id)) {
							spdlog::warn("[Node Editor] link rejected");
						}
						completed = true;
						break;
					}
				}
				if (completed) {
					break;
				}
			}
			// Drag-to-connect: releasing on an In completes. Click-click: a tiny
			// click on Out (release with no In hit) keeps m_linking for the next In click.
			const ImVec2 delta = ImGui::GetMouseDragDelta(ImGuiMouseButton_Left);
			const float dragDist2 = delta.x * delta.x + delta.y * delta.y;
			if (completed || dragDist2 > 16.f) {
				m_linking = false;
			}
			ImGui::ResetMouseDragDelta(ImGuiMouseButton_Left);
		}
		m_dragNode = 0;
	}
	if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
		m_linking = false;
		m_boxSelecting = false;
		m_selectedLink = 0;
	}
	// Delete the selected wire, else every selected node (guard: not while
	// typing in an inline widget).
	if (ImGui::IsKeyPressed(ImGuiKey_Delete) && !ImGui::IsAnyItemActive()) {
		if (m_selectedLink != 0) {
			rig::node::removeLink(graph, m_selectedLink);
			m_selectedLink = 0;
		} else if (m_selectedNode != 0 || !m_multi.empty()) {
			std::vector<uint32_t> doomed(m_multi.begin(), m_multi.end());
			if (m_selectedNode != 0 && !m_multi.count(m_selectedNode)) {
				doomed.push_back(m_selectedNode);
			}
			for (uint32_t id : doomed) {
				rig::node::removeNode(graph, id);
			}
			m_multi.clear();
			m_selectedNode = 0;
			m_selectedPin = 0;
		}
	}

	canvasWin->DC.CursorMaxPos = canvasCursorMax;
	ImGui::SetScrollX(0.f);
	ImGui::SetScrollY(0.f);
	ImGui::PopClipRect();
	ImGui::SetCursorScreenPos(ImVec2(origin.x, origin.y + canvasSize.y));
}

void NodeEditorWindow::renderContents() {
	auto* engine = getEngine();
	if (!engine) {
		return;
	}
	g_m.refresh();
	auto* ecs = resolveEcs();
	if (!ecs) {
		return;
	}

	auto* packs = engine->getPackManager();
	std::shared_ptr<rigProject> document;
	if (packs) {
		document = std::dynamic_pointer_cast<rigProject>(packs->getPack("rigProject"));
	}

	entt::entity graphEntity = findGraphEntity(*ecs);
	ecs::CNodeGraph* root = nullptr;
	ecs::NodeGraphData* active = nullptr;
	if (graphEntity != entt::null) {
		root = &ecs->getComponent<ecs::CNodeGraph>(graphEntity);
		active = activeGraph(*root);
	}

	drawMenuBar(document, graphEntity, root, active);

	const float statusH = ImGui::GetFrameHeight() + ImGui::GetStyle().WindowPadding.y * 2.f;
	ImGui::BeginChild("##node_body", ImVec2(0.f, -statusH), ImGuiChildFlags_None,
					  ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

	if (graphEntity == entt::null || !root || !active) {
		const ImVec2 avail = ImGui::GetContentRegionAvail();
		const ImVec2 origin = ImGui::GetCursorScreenPos();
		ImGui::InvisibleButton("##node_empty", avail);
		if (ImGui::BeginDragDropTarget()) {
			if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kRigScenePropPayload)) {
				const auto* prop = static_cast<const RigScenePropPayload*>(payload->Data);
				if (prop && prop->name[0] && (!prop->ecsTag || prop->ecsTag == ecs)) {
					const auto ge = ensureGraph(*ecs);
					auto& g = ecs->getComponent<ecs::CNodeGraph>(ge);
					spawnPropRef(g, *ecs, prop->entity, prop->name, prop->propType, {80.f, 80.f},
								 ImGui::GetIO().KeyAlt);
				}
			}
			if (const ImGuiPayload* payload =
					ImGui::AcceptDragDropPayload(kRigSceneEntityPayload)) {
				m_pendingRefEntity = *static_cast<const uint32_t*>(payload->Data);
				m_pendingRefPos = {80.f, 80.f};
				m_openRefPopup = true;
				ensureGraph(*ecs);
			}
			ImGui::EndDragDropTarget();
		}
		ImGui::GetWindowDrawList()->AddText(
			ImVec2(origin.x + 8.f, origin.y + 8.f), ImGui::GetColorU32(ImGuiCol_TextDisabled),
			"No graph yet. Drop a filter property here, or File > Create demo graph.");

		applyPendingPatchRequests();

		ImGui::EndChild();
		drawStatusBar(nullptr, nullptr);
	} else {
		if (m_hostTime) {
			m_time = *m_hostTime;
		} else {
			m_time += engine->getDeltaTime();
		}

		// Keep group outer pins in lockstep with Export In / Export Out nodes.
		rig::node::syncGroupInterfaces(*root);
		// Add param input pins missing on nodes from older saves.
		rig::node::syncCatalogPins(*root);

		rig::node::EvalContext ctx;
		ctx.time = m_time;
		ctx.dt = engine->getDeltaTime();
		ctx.scratch = m_hostScratch ? m_hostScratch : &m_evalScratch;
		rig::node::EvalResult localEv;
		const rig::node::EvalResult* ev = nullptr;
		if (m_hostEval && m_divePath.empty()) {
			ev = m_hostEval;
		} else {
			localEv = rig::node::evaluateAlongDive(*root, m_divePath, ctx);
			ev = &localEv;
		}

		drawCanvas(*active, ev);
		syncSelectionToGraph(*root, graphEntity, *ecs);

		// Right-click patches from prop pins (LFO, Sound, or a bare ref).
		applyPendingPatchRequests();

		if (m_openRefPopup) {
			ImGui::OpenPopup("##spawn_ref_from_scene");
			m_openRefPopup = false;
		}
		if (ImGui::BeginPopup("##spawn_ref_from_scene")) {
			const entt::entity e = static_cast<entt::entity>(m_pendingRefEntity);
			std::string name = ecs->entityName(e);
			if (name.empty()) {
				name = "entity";
			}
			ImGui::Text("Ref -> %s", name.c_str());
			auto spawnRef = [&](const char* typeId) {
				const uint32_t id = rig::node::spawnCatalogNode(*active, typeId, m_pendingRefPos);
				if (id != 0) {
					if (auto* n = active->findNode(id)) {
						rig::node::setParamString(*n, "entity", name);
						n->title = name;
					}
					m_selectedNode = id;
					m_multi = {id};
				}
				m_pendingRefEntity = 0;
				ImGui::CloseCurrentPopup();
			};
			if (ImGui::MenuItem("Float Ref")) {
				spawnRef("ref.float");
			}
			if (ImGui::MenuItem("Vec2 Ref")) {
				spawnRef("ref.vec2");
			}
			if (ImGui::MenuItem("Color Ref")) {
				spawnRef("ref.color");
			}
			if (ImGui::MenuItem("Cancel")) {
				m_pendingRefEntity = 0;
				ImGui::CloseCurrentPopup();
			}
			ImGui::EndPopup();
		}

		ImGui::EndChild();
		drawStatusBar(active, ev);
	}

	m_openRigDialog.Display();
	if (document && m_openRigDialog.HasSelected()) {
		const std::string path = m_openRigDialog.GetSelected().string();
		m_openRigDialog.ClearSelected();
		document->requestLoad(path);
		m_divePath.clear();
		spdlog::info("[Node Editor] queued scene load {}", path);
	}

	m_saveRigDialog.Display();
	if (document && m_saveRigDialog.HasSelected()) {
		std::string path = m_saveRigDialog.GetSelected().string();
		m_saveRigDialog.ClearSelected();
		path = document->documentPath(path);
		ensureDocument(*ecs, path);
		for (auto entity : ecs->view<ecs::CProject>()) {
			ecs->getComponent<ecs::CProject>(entity).path = path;
			ecs->getComponent<ecs::CProject>(entity).dirty = true;
			break;
		}
		document->requestSave(path);
		spdlog::info("[Node Editor] queued scene save {}", path);
	}
}

} // namespace rigkit
