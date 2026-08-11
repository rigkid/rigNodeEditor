#include "NodeInspector.h"

#include "NodeCatalog.h"
#include "NodeGraph.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

#include <imgui.h>

namespace rig {
namespace node {
namespace {

bool isRefType(std::string_view typeId) {
	return typeId == "ref.float" || typeId == "ref.vec2" || typeId == "ref.color";
}

} // namespace

namespace {

rigkit::ecs::NodeGraphData* graphAtDive(rigkit::ecs::CNodeGraph& root) {
	rigkit::ecs::NodeGraphData* g = &root;
	for (uint32_t id : root.editDivePath) {
		auto* n = g->findNode(id);
		if (!n || !n->nested) {
			root.editDivePath.clear();
			return &root;
		}
		g = n->nested.get();
	}
	return g;
}

} // namespace

void drawSceneModulateDrawer(rigkit::MEcs& ecs, entt::entity entity) {
	if (entity == entt::null || ecs.hasComponent<rigkit::ecs::CNodeGraph>(entity)) {
		return;
	}
	entt::entity graphEntity = entt::null;
	for (auto e : ecs.view<rigkit::ecs::CNodeGraph>()) {
		graphEntity = e;
		break;
	}
	if (graphEntity == entt::null) {
		return;
	}

	std::string entName = ecs.entityName(entity);
	if (entName.empty()) {
		entName = "entity";
	}

	std::vector<std::string> floatProps;
	for (const auto& typeInfo : ecs.componentTypes()) {
		if (!ecs.hasRegisteredComponent(typeInfo, entity)) {
			continue;
		}
		for (const auto& p : ecs.registeredProperties(typeInfo, entity)) {
			if (p.type == EPT_FLOAT || p.type == EPT_DOUBLE || p.type == EPT_INT ||
				p.type == EPT_UINT) {
				floatProps.push_back(p.name);
			}
		}
	}
	if (floatProps.empty()) {
		return;
	}
	if (!ImGui::CollapsingHeader("Modulate", ImGuiTreeNodeFlags_DefaultOpen)) {
		return;
	}
	ImGui::TextDisabled("LFO -> Ref into the Node Editor graph");
	auto& root = ecs.getComponent<rigkit::ecs::CNodeGraph>(graphEntity);
	rigkit::ecs::NodeGraphData* graph = graphAtDive(root);
	for (const auto& name : floatProps) {
		ImGui::PushID(name.c_str());
		ImGui::TextUnformatted(name.c_str());
		ImGui::SameLine();
		if (ImGui::SmallButton("Modulate")) {
			const glm::vec2 pos{40.f + static_cast<float>(graph->nodes.size()) * 12.f, 40.f};
			const uint32_t refId = spawnCatalogNode(*graph, "ref.float", pos + glm::vec2{180.f, 0.f});
			const uint32_t lfoId = spawnCatalogNode(*graph, "mod.lfo", pos);
			if (refId != 0) {
				if (auto* n = graph->findNode(refId)) {
					setParamString(*n, "entity", entName);
					setParamString(*n, "prop", name);
					n->title = entName + "." + name;
				}
				if (lfoId != 0) {
					tryLinkByName(*graph, lfoId, "out", refId, "in");
				}
				root.editSelectedNode = refId;
			}
		}
		ImGui::PopID();
	}
}

void drawGraphNodeInspector(rigkit::MEcs& ecs, entt::entity graphEntity) {
	if (!ecs.hasComponent<rigkit::ecs::CNodeGraph>(graphEntity)) {
		return;
	}
	auto& root = ecs.getComponent<rigkit::ecs::CNodeGraph>(graphEntity);
	rigkit::ecs::NodeGraphData* graph = graphAtDive(root);
	auto* sel = graph->findNode(root.editSelectedNode);
	if (!sel) {
		return;
	}

	if (!ImGui::CollapsingHeader("Node", ImGuiTreeNodeFlags_DefaultOpen)) {
		return;
	}

	ImGui::TextDisabled("%s", sel->typeId.c_str());
	char titleBuf[128];
	std::snprintf(titleBuf, sizeof(titleBuf), "%s", sel->title.c_str());
	if (ImGui::InputText("Title", titleBuf, sizeof(titleBuf))) {
		sel->title = titleBuf;
	}

	if (sel->isGroup()) {
		ImGui::Text("%zu inner nodes / %zu publishes", sel->nested->nodes.size(),
					sel->publishes.size());
		if (ImGui::Button("Dive into group")) {
			root.editDivePath.push_back(sel->id);
			root.editSelectedNode = 0;
		}
		ImGui::SameLine();
		if (ImGui::Button("Ungroup")) {
			const uint32_t gid = sel->id;
			if (ungroup(*graph, gid)) {
				root.editSelectedNode = 0;
			}
		}
		return;
	}

	if (sel->typeId == "float.out" || sel->typeId == "vec2.out" || sel->typeId == "color.out") {
		ImGui::TextColored(ImVec4(0.55f, 0.85f, 1.f, 1.f), "App drive sink");
		ImGui::TextDisabled("Rename Title to label what this out feeds.");
		return;
	}

	if (isRefType(sel->typeId)) {
		ImGui::TextColored(ImVec4(0.55f, 0.85f, 1.f, 1.f), "Scene ref");
		char entityBuf[128];
		char propBuf[128];
		char propYBuf[128];
		const std::string ent = getParamString(*sel, "entity");
		const std::string prop = getParamString(*sel, "prop");
		const std::string propY = getParamString(*sel, "propY");
		std::snprintf(entityBuf, sizeof(entityBuf), "%s", ent.c_str());
		std::snprintf(propBuf, sizeof(propBuf), "%s", prop.c_str());
		std::snprintf(propYBuf, sizeof(propYBuf), "%s", propY.c_str());
		if (ImGui::InputText("Entity", entityBuf, sizeof(entityBuf))) {
			setParamString(*sel, "entity", entityBuf);
			sel->title = entityBuf[0] ? entityBuf : "Ref";
		}
		entt::entity target = entt::null;
		if (entityBuf[0]) {
			target = ecs.findEntity(entityBuf);
		}
		if (target != entt::null) {
			std::vector<std::string> floatProps;
			std::vector<std::string> vec2Props;
			for (const auto& typeInfo : ecs.componentTypes()) {
				if (!ecs.hasRegisteredComponent(typeInfo, target)) {
					continue;
				}
				for (const auto& p : ecs.registeredProperties(typeInfo, target)) {
					if (p.type == EPT_FLOAT || p.type == EPT_DOUBLE || p.type == EPT_INT ||
						p.type == EPT_UINT) {
						floatProps.push_back(p.name);
					} else if (p.type == EPT_VEC2 || p.type == EPT_IMVEC2) {
						vec2Props.push_back(p.name);
					}
				}
			}
			auto comboProp = [&](const char* label, char* buf, size_t bufSize,
								 const std::vector<std::string>& names, const char* paramKey) {
				if (ImGui::BeginCombo(label, buf[0] ? buf : "(pick property)")) {
					for (const auto& name : names) {
						const bool selected = name == buf;
						if (ImGui::Selectable(name.c_str(), selected)) {
							std::snprintf(buf, bufSize, "%s", name.c_str());
							setParamString(*sel, paramKey, name);
						}
						if (selected) {
							ImGui::SetItemDefaultFocus();
						}
					}
					ImGui::EndCombo();
				}
			};
			if (sel->typeId == "ref.float") {
				comboProp("Property", propBuf, sizeof(propBuf), floatProps, "prop");
				if (ImGui::Button("Drive with LFO")) {
					const uint32_t lfoId = rig::node::spawnCatalogNode(
						*graph, "mod.lfo", sel->pos + glm::vec2{-180.f, 0.f});
					if (lfoId != 0) {
						rig::node::tryLinkByName(*graph, lfoId, "out", sel->id, "in");
						root.editSelectedNode = lfoId;
					}
				}
				ImGui::SameLine();
				ImGui::TextDisabled("Modulate");
			} else if (sel->typeId == "ref.vec2") {
				if (!vec2Props.empty()) {
					comboProp("Vec2 property", propBuf, sizeof(propBuf), vec2Props, "prop");
					ImGui::TextDisabled("Or two float props:");
				}
				comboProp("Property X", propBuf, sizeof(propBuf), floatProps, "prop");
				comboProp("Property Y", propYBuf, sizeof(propYBuf), floatProps, "propY");
			} else {
				ImGui::TextDisabled("Empty prefix writes Color R/G/B on the entity.");
				if (ImGui::InputText("Prop prefix", propBuf, sizeof(propBuf))) {
					setParamString(*sel, "prop", propBuf);
				}
			}
		} else {
			if (ImGui::InputText("Property", propBuf, sizeof(propBuf))) {
				setParamString(*sel, "prop", propBuf);
			}
			if (sel->typeId == "ref.vec2") {
				if (ImGui::InputText("Property Y", propYBuf, sizeof(propYBuf))) {
					setParamString(*sel, "propY", propYBuf);
				}
			}
			ImGui::TextDisabled("Drag an entity from Scene onto the Node Editor to bind.");
		}
		return;
	}

	const auto* entry = findCatalogEntry(sel->typeId);
	if (!entry || entry->params.empty()) {
		ImGui::TextDisabled("No editable params.");
		return;
	}

	if (sel->typeId == "color.value") {
		float rgba[4] = {getParamFloat(*sel, "r", 1.f), getParamFloat(*sel, "g", 1.f),
						 getParamFloat(*sel, "b", 1.f), getParamFloat(*sel, "a", 1.f)};
		if (ImGui::ColorEdit4("Color", rgba)) {
			setParamFloat(*sel, "r", rgba[0]);
			setParamFloat(*sel, "g", rgba[1]);
			setParamFloat(*sel, "b", rgba[2]);
			setParamFloat(*sel, "a", rgba[3]);
		}
		return;
	}
	if (sel->typeId == "vec2.value") {
		float xy[2] = {getParamFloat(*sel, "x", 0.f), getParamFloat(*sel, "y", 0.f)};
		if (ImGui::DragFloat2("XY", xy, 0.01f)) {
			setParamFloat(*sel, "x", xy[0]);
			setParamFloat(*sel, "y", xy[1]);
		}
		return;
	}
	for (const auto& p : entry->params) {
		float v = getParamFloat(*sel, p.key, p.def);
		if (p.ui == 1 && p.comboLabels) {
			int idx = static_cast<int>(std::lround(v));
			const int lo = static_cast<int>(p.min);
			const int hi = static_cast<int>(p.max);
			idx = std::clamp(idx, lo, hi);
			if (ImGui::Combo(p.label, &idx, p.comboLabels, hi - lo + 1)) {
				setParamFloat(*sel, p.key, static_cast<float>(idx));
			}
		} else if (ImGui::DragFloat(p.label, &v, p.speed, p.min, p.max)) {
			setParamFloat(*sel, p.key, v);
		}
	}
}

} // namespace node
} // namespace rig
