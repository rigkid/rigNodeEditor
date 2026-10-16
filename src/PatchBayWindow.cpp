#include "PatchBayWindow.h"

#include "CNodeGraph.h"
#include "MWindow.h"
#include "NodeEditorWindow.h"
#include "NodeGraph.h"
#include "NodePatch.h"
#include "SceneDragPayload.h"
#include "core/RigKitEngine.h"

#include <imgui.h>
#include <spdlog/spdlog.h>
#include <string>
#include <vector>

namespace rigkit {
namespace {

struct PinChoice {
	uint32_t node = 0;
	uint32_t pin = 0;
	std::string label;
};

std::vector<PinChoice> listPins(const ecs::NodeGraphData& graph, ecs::NodePinKind kind,
								uint32_t fromNode, uint32_t fromPin, uint32_t ignoreLink) {
	std::vector<PinChoice> out;
	for (const auto& n : graph.nodes) {
		std::vector<const ecs::NodePin*> pins;
		for (const auto& p : n.pins) {
			if (p.kind != kind) {
				continue;
			}
			if (kind == ecs::NodePinKind::In && fromNode != 0 && fromPin != 0) {
				if (!rig::node::canLink(graph, fromNode, fromPin, n.id, p.id, ignoreLink)) {
					continue;
				}
			}
			pins.push_back(&p);
		}
		for (const auto* p : pins) {
			PinChoice c;
			c.node = n.id;
			c.pin = p->id;
			c.label = node_ui::pinLabel(n, *p);
			out.push_back(std::move(c));
		}
	}
	return out;
}

std::string previewLabel(const ecs::NodeGraphData& graph, uint32_t node, uint32_t pin,
						 const char* empty) {
	if (node == 0 || pin == 0) {
		return empty;
	}
	const auto* n = graph.findNode(node);
	const auto* p = graph.findPin(node, pin);
	if (!n || !p) {
		return empty;
	}
	return node_ui::pinLabel(*n, *p);
}

void offerGraphNodeDrag(uint32_t node, uint32_t pin, const void* ecsTag) {
	if (node == 0 || !ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
		return;
	}
	RigGraphNodePayload payload;
	payload.node = node;
	payload.pin = pin;
	payload.ecsTag = ecsTag;
	ImGui::SetDragDropPayload(kRigGraphNodePayload, &payload, sizeof(payload));
	ImGui::Text("Node %u", node);
	ImGui::EndDragDropSource();
}

bool comboPins(const char* id, const ecs::NodeGraphData& graph, uint32_t* node, uint32_t* pin,
			   ecs::NodePinKind kind, uint32_t fromNode, uint32_t fromPin, uint32_t ignoreLink,
			   const char* empty, const void* ecsTag) {
	const std::string preview = previewLabel(graph, *node, *pin, empty);
	bool changed = false;
	if (!ImGui::BeginCombo(id, preview.c_str())) {
		return false;
	}
	if (ImGui::Selectable(empty, *node == 0)) {
		*node = 0;
		*pin = 0;
		changed = true;
	}
	for (const auto& c : listPins(graph, kind, fromNode, fromPin, ignoreLink)) {
		ImGui::PushID(static_cast<int>(c.node));
		ImGui::PushID(static_cast<int>(c.pin));
		const bool selected = c.node == *node && c.pin == *pin;
		if (ImGui::Selectable(c.label.c_str(), selected)) {
			*node = c.node;
			*pin = c.pin;
			changed = true;
		}
		if (selected) {
			ImGui::SetItemDefaultFocus();
		}
		offerGraphNodeDrag(c.node, c.pin, ecsTag);
		ImGui::PopID();
		ImGui::PopID();
	}
	ImGui::EndCombo();
	return changed;
}

uint32_t pickInPin(const ecs::NodeGraphData& graph, uint32_t nodeId, uint32_t preferredPin,
				   uint32_t fromNode, uint32_t fromPin, uint32_t ignoreLink) {
	const auto* n = graph.findNode(nodeId);
	if (!n) {
		return 0;
	}
	if (preferredPin != 0) {
		const auto* p = graph.findPin(nodeId, preferredPin);
		if (p && p->kind == ecs::NodePinKind::In) {
			if (fromNode == 0 || fromPin == 0 ||
				rig::node::canLink(graph, fromNode, fromPin, nodeId, preferredPin, ignoreLink)) {
				return preferredPin;
			}
		}
	}
	for (const auto& p : n->pins) {
		if (p.kind != ecs::NodePinKind::In) {
			continue;
		}
		if (fromNode == 0 || fromPin == 0 ||
			rig::node::canLink(graph, fromNode, fromPin, nodeId, p.id, ignoreLink)) {
			return p.id;
		}
	}
	return node_ui::firstPin(*n, ecs::NodePinKind::In);
}

} // namespace

PatchBayWindow::PatchBayWindow(const std::string& title) : IWindow(title) {
	setCategory("Edit");
}

MEcs* PatchBayWindow::resolveEcs() const {
	if (m_ecsOverride) {
		return m_ecsOverride;
	}
	auto* engine = getEngine();
	if (!engine) {
		return nullptr;
	}
	if (auto* ui = engine->getUiManager()) {
		if (auto* wm = ui->getWindowManager()) {
			if (auto ed = wm->getWindow<NodeEditorWindow>("Node Editor")) {
				if (auto* o = ed->ecsOverride()) {
					return o;
				}
			}
		}
	}
	return engine->getECSManager();
}

void PatchBayWindow::renderContents() {
	auto* ecs = resolveEcs();
	if (!ecs) {
		ImGui::TextDisabled("No ECS");
		return;
	}

	entt::entity graphEntity = node_ui::findGraphEntity(*ecs);
	ecs::CNodeGraph* root = nullptr;
	ecs::NodeGraphData* graph = nullptr;
	if (graphEntity != entt::null) {
		root = &ecs->getComponent<ecs::CNodeGraph>(graphEntity);
		graph = node_ui::activeGraph(*root);
	}

	ImGui::TextDisabled("Same graph as the Node Editor. Drop a property on Destination.");

	auto applyEndpoints = [&](uint32_t linkId, uint32_t fromNode, uint32_t fromPin, uint32_t toNode,
							  uint32_t toPin) {
		if (!graph || fromNode == 0 || fromPin == 0 || toNode == 0 || toPin == 0) {
			return;
		}
		if (linkId != 0) {
			if (!rig::node::relink(*graph, linkId, fromNode, fromPin, toNode, toPin)) {
				spdlog::warn("[Patch] rewire rejected");
			}
			return;
		}
		if (!rig::node::tryLink(*graph, fromNode, fromPin, toNode, toPin)) {
			spdlog::warn("[Patch] link rejected");
			return;
		}
		m_addFromNode = 0;
		m_addFromPin = 0;
		m_addToNode = 0;
		m_addToPin = 0;
	};

	auto acceptDestDrop = [&](uint32_t* toNode, uint32_t* toPin, uint32_t* fromNode,
							  uint32_t* fromPin, uint32_t linkId) -> bool {
		if (!ImGui::BeginDragDropTarget()) {
			return false;
		}
		bool applied = false;
		if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kRigScenePropPayload)) {
			const auto* prop = static_cast<const RigScenePropPayload*>(payload->Data);
			if (prop && prop->name[0] && (!prop->ecsTag || prop->ecsTag == ecs)) {
				if (!graph) {
					const auto ge = node_ui::ensureGraph(*ecs);
					root = &ecs->getComponent<ecs::CNodeGraph>(ge);
					graph = node_ui::activeGraph(*root);
				}
				if (graph) {
					std::string entName = "entity";
					const entt::entity e = static_cast<entt::entity>(prop->entity);
					if (ecs->registry().valid(e)) {
						entName = ecs->entityName(e);
						if (entName.empty()) {
							entName = "entity";
						}
					}
					const bool withLfo = ImGui::GetIO().KeyAlt;
					uint32_t nid = 0;
					if (!withLfo) {
						nid = node_ui::findRefNode(*graph, entName, prop->name, prop->propType);
					}
					if (nid == 0) {
						nid = node_ui::spawnPropRef(*graph, *ecs, prop->entity, prop->name,
													prop->propType, node_ui::nextSpawnPos(*graph),
													withLfo);
					}
					if (auto* n = graph->findNode(nid)) {
						*toNode = nid;
						*toPin = node_ui::firstPin(*n, ecs::NodePinKind::In);
						if (withLfo) {
							for (const auto& l : graph->links) {
								if (l.toNode == nid && l.toPin == *toPin) {
									if (linkId != 0 && linkId != l.id) {
										rig::node::removeLink(*graph, linkId);
									}
									if (linkId == 0 && fromNode && fromPin) {
										*fromNode = 0;
										*fromPin = 0;
										*toNode = 0;
										*toPin = 0;
									}
									applied = true;
									break;
								}
							}
						}
						if (!applied) {
							if (linkId != 0 && fromNode && fromPin && *fromNode && *fromPin) {
								applyEndpoints(linkId, *fromNode, *fromPin, *toNode, *toPin);
							}
							applied = true;
						}
					}
				}
			}
		}
		if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kRigGraphNodePayload)) {
			const auto* drop = static_cast<const RigGraphNodePayload*>(payload->Data);
			if (drop && graph && drop->node != 0 && (!drop->ecsTag || drop->ecsTag == ecs)) {
				const uint32_t pin =
					pickInPin(*graph, drop->node, drop->pin, fromNode ? *fromNode : 0,
							  fromPin ? *fromPin : 0, linkId);
				if (pin != 0) {
					*toNode = drop->node;
					*toPin = pin;
					if (linkId != 0 && fromNode && fromPin && *fromNode && *fromPin) {
						applyEndpoints(linkId, *fromNode, *fromPin, *toNode, *toPin);
					}
					applied = true;
				}
			}
		}
		if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kRigSceneEntityPayload)) {
			const uint32_t id = *static_cast<const uint32_t*>(payload->Data);
			if (graph) {
				const entt::entity e = static_cast<entt::entity>(id);
				std::string name = ecs->registry().valid(e) ? ecs->entityName(e) : std::string{};
				const uint32_t nid = node_ui::findRefForEntity(*graph, name);
				if (auto* n = graph->findNode(nid)) {
					*toNode = nid;
					*toPin = pickInPin(*graph, nid, 0, fromNode ? *fromNode : 0,
									   fromPin ? *fromPin : 0, linkId);
					if (linkId != 0 && fromNode && fromPin && *fromNode && *fromPin) {
						applyEndpoints(linkId, *fromNode, *fromPin, *toNode, *toPin);
					}
					applied = true;
				}
			}
		}
		ImGui::EndDragDropTarget();
		return applied;
	};

	const ImGuiTableFlags flags =
		ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV;
	if (!ImGui::BeginTable("##patch_bay", 3, flags)) {
		return;
	}
	ImGui::TableSetupColumn("From", ImGuiTableColumnFlags_WidthStretch);
	ImGui::TableSetupColumn("Destination", ImGuiTableColumnFlags_WidthStretch);
	ImGui::TableSetupColumn("##act", ImGuiTableColumnFlags_WidthFixed, 72.f);
	ImGui::TableHeadersRow();

	std::vector<uint32_t> linkIds;
	if (graph) {
		linkIds.reserve(graph->links.size());
		for (const auto& l : graph->links) {
			linkIds.push_back(l.id);
		}
	}
	for (uint32_t linkId : linkIds) {
		auto* link = graph ? graph->findLink(linkId) : nullptr;
		if (!link) {
			continue;
		}
		ImGui::TableNextRow();
		ImGui::PushID(static_cast<int>(linkId));
		ImGui::TableNextColumn();
		uint32_t fromNode = link->fromNode;
		uint32_t fromPin = link->fromPin;
		if (comboPins("##from", *graph, &fromNode, &fromPin, ecs::NodePinKind::Out, 0, 0, linkId,
					  "From", ecs)) {
			applyEndpoints(linkId, fromNode, fromPin, link->toNode, link->toPin);
		}
		ImGui::TableNextColumn();
		uint32_t toNode = link->toNode;
		uint32_t toPin = link->toPin;
		if (comboPins("##dest", *graph, &toNode, &toPin, ecs::NodePinKind::In, fromNode, fromPin,
					  linkId, "Destination", ecs)) {
			applyEndpoints(linkId, link->fromNode, link->fromPin, toNode, toPin);
		}
		acceptDestDrop(&toNode, &toPin, &fromNode, &fromPin, linkId);
		ImGui::TableNextColumn();
		if (ImGui::Button("Remove")) {
			rig::node::removeLink(*graph, linkId);
		}
		ImGui::PopID();
	}

	ImGui::TableNextRow();
	ImGui::PushID("add");
	ImGui::TableNextColumn();
	if (graph) {
		comboPins("##from", *graph, &m_addFromNode, &m_addFromPin, ecs::NodePinKind::Out, 0, 0, 0,
				  "From", ecs);
	} else {
		ImGui::BeginDisabled();
		ImGui::BeginCombo("##from", "From");
		ImGui::EndCombo();
		ImGui::EndDisabled();
	}
	ImGui::TableNextColumn();
	if (graph) {
		comboPins("##dest", *graph, &m_addToNode, &m_addToPin, ecs::NodePinKind::In, m_addFromNode,
				  m_addFromPin, 0, "Destination", ecs);
	} else {
		if (ImGui::BeginCombo("##dest", "Destination")) {
			ImGui::TextDisabled("Drop a property");
			ImGui::EndCombo();
		}
	}
	acceptDestDrop(&m_addToNode, &m_addToPin, &m_addFromNode, &m_addFromPin, 0);
	ImGui::TableNextColumn();
	const bool canAdd =
		graph && rig::node::canLink(*graph, m_addFromNode, m_addFromPin, m_addToNode, m_addToPin);
	ImGui::BeginDisabled(!canAdd);
	if (ImGui::Button("Add")) {
		applyEndpoints(0, m_addFromNode, m_addFromPin, m_addToNode, m_addToPin);
	}
	ImGui::EndDisabled();
	ImGui::PopID();
	ImGui::EndTable();
}

} // namespace rigkit
