#include "NodeEditorWindow.h"

#include "CProject.h"
#include "CNodeGraph.h"
#include "CSelection.h"
#include "EntityPick.h"
#include "FileDialogs.h"
#include "GraphEval.h"
#include "NodeCatalog.h"
#include "NodeGraph.h"
#include "PropertiesWindow.h"
#include "SceneDragPayload.h"
#include "core/RigKitEngine.h"
#include "core/pack/MPack.h"
#include "ecs/PropertyReflection.h"
#include "MWindow.h"
#include "rigProject.h"
#include <spdlog/spdlog.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <string>
#include <string_view>

namespace rigkit {
namespace {

constexpr float kNodeW = 168.f;
constexpr float kRefNodeW = 112.f;
constexpr float kTitleH = 22.f;
constexpr float kPinR = 5.f;
constexpr float kPinRow = 20.f;
constexpr float kPad = 8.f;

bool isRefType(std::string_view typeId) {
	return typeId == "ref.float" || typeId == "ref.vec2" || typeId == "ref.color";
}

bool isSinkType(std::string_view typeId) {
	return typeId == "float.out" || typeId == "vec2.out" || typeId == "color.out" ||
		   isRefType(typeId);
}

float nodeWidth(const ecs::GraphNode& n) {
	return isRefType(n.typeId) ? kRefNodeW : kNodeW;
}

float nodeHeight(const ecs::GraphNode& n) {
	const int rows = std::max(1, static_cast<int>(n.pins.size()));
	return kTitleH + kPad + static_cast<float>(rows) * kPinRow + kPad;
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
	const float y = kTitleH + kPad + static_cast<float>(slot) * kPinRow + kPinRow * 0.5f;
	return ImVec2(origin.x + (n.pos.x + pan.x + x) * zoom,
				  origin.y + (n.pos.y + pan.y + y) * zoom);
}

ImVec2 nodeScreen(const ecs::GraphNode& n, const ImVec2& origin, float zoom, const glm::vec2& pan) {
	return ImVec2(origin.x + (n.pos.x + pan.x) * zoom, origin.y + (n.pos.y + pan.y) * zoom);
}

} // namespace

NodeEditorWindow::NodeEditorWindow() : IWindow("Node Editor", ImGuiWindowFlags_MenuBar) {
	setCategory("Edit");
	m_openRigDialog.SetTitle("Open Scene");
	setFileBrowserFilters(m_openRigDialog, {".rig"});
	m_saveRigDialog.SetTitle("Save Scene");
	setFileBrowserFilters(m_saveRigDialog, {".rig"});
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

	if (m_selectedNode != 0) {
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
	if (ImGui::MenuItem("Group selection", nullptr, false, canGroup)) {
		ecs::NodeGraphData* g = activeGraph(root);
		std::vector<uint32_t> ids(m_multi.begin(), m_multi.end());
		if (ids.empty() && m_selectedNode != 0) {
			ids.push_back(m_selectedNode);
		}
		const uint32_t gid =
			rig::node::createGroup(*g, ids, {-m_pan.x + 120.f, -m_pan.y + 80.f}, "Group");
		m_selectedNode = gid;
		m_multi = {gid};
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
			m_openRigDialog.Open();
		}
		if (ImGui::MenuItem("Save Scene...", nullptr, false, document != nullptr)) {
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
			if (auto* engine = getEngine()) {
				if (auto* ecs = engine->getECSManager()) {
					const auto e = rig::node::makeGraph(*ecs, "demo-graph");
					rig::node::seedDemoGraph(ecs->getComponent<ecs::CNodeGraph>(e));
					ecs::CSelection sel;
					sel.isSelected = true;
					ecs->addComponent<ecs::CSelection>(e, sel);
				}
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
	ImGui::Text("Nodes %zu  Links %zu  t=%.2f", active->nodes.size(), active->links.size(), m_time);
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

void NodeEditorWindow::drawCanvas(ecs::NodeGraphData& graph, const rig::node::EvalResult* ev) {
	const ImVec2 canvasSize = ImGui::GetContentRegionAvail();
	const ImVec2 origin = ImGui::GetCursorScreenPos();
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

	if (ImGui::BeginDragDropTarget()) {
		if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kRigSceneEntityPayload)) {
			const uint32_t id = *static_cast<const uint32_t*>(payload->Data);
			m_pendingRefEntity = id;
			m_pendingRefPos = toGraph(mouse);
			m_openRefPopup = true;
		}
		if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kRigScenePropPayload)) {
			const auto* prop = static_cast<const RigScenePropPayload*>(payload->Data);
			auto* engine = getEngine();
			auto* ecs = engine ? engine->getECSManager() : nullptr;
			if (ecs && prop && prop->name[0]) {
				const entt::entity e = static_cast<entt::entity>(prop->entity);
				std::string entName = ecs->entityName(e);
				if (entName.empty()) {
					entName = "entity";
				}
				const char* typeId = "ref.float";
				if (prop->propType == EPT_VEC2 || prop->propType == EPT_IMVEC2) {
					typeId = "ref.vec2";
				} else if (prop->propType == EPT_COLOR || prop->propType == EPT_VEC4 ||
						   prop->propType == EPT_IMVEC4) {
					typeId = "ref.color";
				}
				const glm::vec2 dropPos = toGraph(mouse);
				const uint32_t nid = rig::node::spawnCatalogNode(graph, typeId, dropPos);
				if (nid != 0) {
					if (auto* n = graph.findNode(nid)) {
						rig::node::setParamString(*n, "entity", entName);
						if (std::string_view(typeId) == "ref.color") {
							// Empty prefix -> Color R/G/B; named colour props keep prefix.
							if (std::string_view(prop->name) != "Color" &&
								std::string_view(prop->name).find("Color ") != 0) {
								rig::node::setParamString(*n, "prop", prop->name);
							}
						} else {
							rig::node::setParamString(*n, "prop", prop->name);
						}
						n->title = entName + "." + prop->name;
					}
					// Alt+drop: Modulate - spawn LFO and wire into the Ref (same addressing
					// as rig.mod.binding: entity + property).
					if (ImGui::GetIO().KeyAlt && std::string_view(typeId) == "ref.float") {
						const uint32_t lfoId = rig::node::spawnCatalogNode(
							graph, "mod.lfo", dropPos + glm::vec2{-180.f, 0.f});
						if (lfoId != 0) {
							rig::node::tryLinkByName(graph, lfoId, "out", nid, "in");
						}
					}
					m_selectedNode = nid;
					m_multi = {nid};
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

	dl->AddRectFilled(origin, ImVec2(origin.x + canvasSize.x, origin.y + canvasSize.y), colCanvas);
	dl->PushClipRect(origin, ImVec2(origin.x + canvasSize.x, origin.y + canvasSize.y), true);

	if (hovered && (ImGui::IsMouseDragging(ImGuiMouseButton_Middle) ||
					(ImGui::GetIO().KeyAlt && ImGui::IsMouseDragging(ImGuiMouseButton_Left)))) {
		m_pan.x += ImGui::GetIO().MouseDelta.x / m_zoom;
		m_pan.y += ImGui::GetIO().MouseDelta.y / m_zoom;
	}
	if (hovered && ImGui::GetIO().MouseWheel != 0.f) {
		m_zoom = std::clamp(m_zoom + ImGui::GetIO().MouseWheel * 0.1f, 0.4f, 2.5f);
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
		dl->AddBezierCubic(p0, ImVec2(p0.x + dx, p0.y), ImVec2(p1.x - dx, p1.y), p1, colLink,
						   2.0f * m_zoom);
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
		const ImU32 titleFill =
			node.isGroup() ? colTitleGroup : (refNode ? colTitleRef : colTitle);
		dl->AddRectFilled(tl, br, fill, round);
		dl->AddRect(tl, br, selected ? colSelect : colBorder, round, 0, selected ? 2.f : 1.f);
		dl->AddRectFilled(tl, ImVec2(br.x, tl.y + kTitleH * m_zoom), titleFill, round);
		dl->AddText(ImVec2(tl.x + 6.f * m_zoom, tl.y + 3.f * m_zoom), colText, node.title.c_str());

		for (const auto& pin : node.pins) {
			const ImVec2 pp = pinPos(node, pin, origin, m_zoom, m_pan);
			ImU32 col = colPinF;
			if (pin.type == "vec2") {
				col = colPinV;
			} else if (pin.type == "vec4") {
				col = colPinC;
			}
			dl->AddCircleFilled(pp, kPinR * m_zoom, col);
			const char* label = pin.name.c_str();
			if (pin.kind == ecs::NodePinKind::In) {
				dl->AddText(ImVec2(pp.x + 8.f * m_zoom, pp.y - 7.f * m_zoom), colTextDim, label);
			} else {
				const ImVec2 ts = ImGui::CalcTextSize(label);
				dl->AddText(ImVec2(pp.x - 8.f * m_zoom - ts.x, pp.y - 7.f * m_zoom), colTextDim,
							label);
			}
			if (ev && ev->ok) {
				const bool sinkOut = isSinkType(node.typeId);
				const bool showVal =
					pin.kind == ecs::NodePinKind::Out ||
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
						if (ty == "vec2") {
							std::snprintf(buf, sizeof(buf), "%.2f,%.2f", v.x, v.y);
						} else if (ty == "vec4") {
							dl->AddRectFilled(ImVec2(pp.x + 6.f * m_zoom, pp.y + 4.f * m_zoom),
											  ImVec2(pp.x + 22.f * m_zoom, pp.y + 16.f * m_zoom),
											  IM_COL32(static_cast<int>(v.r * 255.f),
													   static_cast<int>(v.g * 255.f),
													   static_cast<int>(v.b * 255.f), 255));
							buf[0] = 0;
						} else {
							std::snprintf(buf, sizeof(buf), "%.2f", v.x);
						}
						if (buf[0]) {
							dl->AddText(ImVec2(pp.x + 6.f * m_zoom, pp.y + 4.f * m_zoom), colValue,
										buf);
						}
					}
				}
			}
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
				if (dx * dx + dy * dy <= (kPinR * m_zoom + 4.f) * (kPinR * m_zoom + 4.f)) {
					hitPin = true;
					m_selectedNode = node.id;
					m_selectedPin = pin.id;
					if (!m_linking) {
						if (pin.kind == ecs::NodePinKind::Out) {
							m_linking = true;
							m_linkFromNode = node.id;
							m_linkFromPin = pin.id;
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
			m_multi.clear();
			m_linking = false;
			m_dragNode = 0;
		}
	}

	if (m_dragNode != 0 && ImGui::IsMouseDragging(ImGuiMouseButton_Left) && !ImGui::GetIO().KeyAlt) {
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
					if (dx * dx + dy * dy <= (kPinR * m_zoom + 4.f) * (kPinR * m_zoom + 4.f)) {
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
	}
	if (ImGui::IsKeyPressed(ImGuiKey_Delete) && m_selectedNode != 0) {
		rig::node::removeNode(graph, m_selectedNode);
		m_multi.erase(m_selectedNode);
		m_selectedNode = 0;
		m_selectedPin = 0;
	}

	dl->PopClipRect();
	ImGui::SetCursorScreenPos(ImVec2(origin.x, origin.y + canvasSize.y));
}

void NodeEditorWindow::renderContents() {
	auto* engine = getEngine();
	if (!engine) {
		return;
	}
	auto* ecs = engine->getECSManager();
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
					  ImGuiWindowFlags_NoScrollbar);

	if (graphEntity == entt::null || !root || !active) {
		ImGui::TextDisabled("No CNodeGraph in scene. File > Create demo graph.");
		ImGui::EndChild();
		drawStatusBar(nullptr, nullptr);
	} else {
		m_time += engine->getDeltaTime();

		rig::node::EvalContext ctx;
		ctx.time = m_time;
		ctx.dt = engine->getDeltaTime();
		ctx.scratch = &m_evalScratch;
		const auto ev = rig::node::evaluateAlongDive(*root, m_divePath, ctx);

		drawCanvas(*active, &ev);
		syncSelectionToGraph(*root, graphEntity, *ecs);

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
				const uint32_t id =
					rig::node::spawnCatalogNode(*active, typeId, m_pendingRefPos);
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
		drawStatusBar(active, &ev);
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
