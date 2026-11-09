#include "NodePatch.h"

#include "CSelection.h"
#include "NodeCatalog.h"
#include "NodeGraph.h"
#include "ecs/PropertyReflection.h"

#include <algorithm>
#include <cmath>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace rigkit {
namespace node_ui {
namespace {

bool colorPropKeepsEmptyKey(std::string_view propName) {
	return propName == "Color" || propName.find("Color ") == 0;
}

void bindRefParams(ecs::GraphNode& n, std::string_view typeId, std::string_view entName,
				   std::string_view propName) {
	rig::node::setParamString(n, "entity", entName);
	if (typeId == "ref.color") {
		if (!colorPropKeepsEmptyKey(propName)) {
			rig::node::setParamString(n, "prop", propName);
		}
	} else {
		rig::node::setParamString(n, "prop", propName);
	}
	n.title = std::string(entName) + "." + std::string(propName);
}

} // namespace

entt::entity findGraphEntity(MEcs& ecs) {
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

entt::entity ensureGraph(MEcs& ecs) {
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

ecs::NodeGraphData* activeGraph(ecs::CNodeGraph& root) {
	ecs::NodeGraphData* g = &root;
	for (uint32_t id : root.editDivePath) {
		auto* n = g->findNode(id);
		if (!n || !n->nested) {
			return &root;
		}
		g = n->nested.get();
	}
	return g;
}

uint32_t firstPin(const ecs::GraphNode& node, ecs::NodePinKind kind) {
	for (const auto& p : node.pins) {
		if (p.kind == kind) {
			return p.id;
		}
	}
	return 0;
}

int pinCount(const ecs::GraphNode& node, ecs::NodePinKind kind) {
	int n = 0;
	for (const auto& p : node.pins) {
		if (p.kind == kind) {
			++n;
		}
	}
	return n;
}

std::string pinLabel(const ecs::GraphNode& node, const ecs::NodePin& pin) {
	if (pinCount(node, pin.kind) <= 1) {
		return node.title;
	}
	return node.title + " / " + pin.name;
}

bool isRefType(std::string_view typeId) {
	return typeId == "ref.float" || typeId == "ref.vec2" || typeId == "ref.color";
}

const char* refTypeIdForProp(int propType) {
	if (propType == EPT_VEC2) {
		return "ref.vec2";
	}
	if (propType == EPT_COLOR || propType == EPT_VEC4) {
		return "ref.color";
	}
	return "ref.float";
}

uint32_t findRefNode(const ecs::NodeGraphData& graph, std::string_view entityName,
					 std::string_view propName, int propType) {
	const char* typeId = refTypeIdForProp(propType);
	for (const auto& n : graph.nodes) {
		if (n.typeId != typeId) {
			continue;
		}
		if (rig::node::getParamString(n, "entity") != entityName) {
			continue;
		}
		const std::string stored = rig::node::getParamString(n, "prop");
		if (std::string_view(typeId) == "ref.color" && colorPropKeepsEmptyKey(propName)) {
			if (stored.empty() || stored == propName) {
				return n.id;
			}
			continue;
		}
		if (stored == propName) {
			return n.id;
		}
	}
	return 0;
}

uint32_t findRefForEntity(const ecs::NodeGraphData& graph, std::string_view entityName) {
	for (const auto& n : graph.nodes) {
		if (!isRefType(n.typeId)) {
			continue;
		}
		if (rig::node::getParamString(n, "entity") == entityName) {
			return n.id;
		}
	}
	return 0;
}

namespace {

uint32_t findNodeOfType(const ecs::NodeGraphData& graph, std::string_view typeId) {
	for (const auto& n : graph.nodes) {
		if (n.typeId == typeId) {
			return n.id;
		}
	}
	return 0;
}

uint32_t linkedFrom(const ecs::NodeGraphData& graph, uint32_t toNode, uint32_t toPin) {
	for (const auto& link : graph.links) {
		if (link.toNode == toNode && link.toPin == toPin) {
			return link.fromNode;
		}
	}
	return 0;
}

uint32_t pinId(const ecs::GraphNode& node, std::string_view name, ecs::NodePinKind kind) {
	for (const auto& pin : node.pins) {
		if (pin.kind == kind && pin.name == name) {
			return pin.id;
		}
	}
	return 0;
}

void clearPinInputs(ecs::NodeGraphData& graph, uint32_t node, uint32_t pin) {
	std::vector<uint32_t> drop;
	for (const auto& link : graph.links) {
		if (link.toNode == node && link.toPin == pin) {
			drop.push_back(link.id);
		}
	}
	for (uint32_t id : drop) {
		rig::node::removeLink(graph, id);
	}
}

/// Wire @p sourceType.@p sourcePin into the ref. Sound sits behind a Map so
/// 0..1 audio can land on the knob's real range. One shared Audio node.
void wireSourceIntoRef(ecs::NodeGraphData& graph, uint32_t refId, std::string_view sourceType,
					   std::string_view sourcePin, glm::vec2 pos, bool hasMapRange, float mapOutMin,
					   float mapOutMax) {
	const auto* ref = graph.findNode(refId);
	if (!ref || sourceType.empty()) {
		return;
	}
	uint32_t inPin = 0;
	for (const auto& pin : ref->pins) {
		if (pin.kind == ecs::NodePinKind::In && pin.name == "in") {
			inPin = pin.id;
			break;
		}
	}
	if (inPin == 0) {
		inPin = firstPin(*ref, ecs::NodePinKind::In);
	}
	if (inPin == 0) {
		return;
	}
	uint32_t src = 0;
	// Loudness and timbre share gain / attack / release, so one Audio node
	// fans out instead of stacking a copy per knob.
	if (sourceType == "mod.audio") {
		src = findNodeOfType(graph, sourceType);
	}
	glm::vec2 refPos = pos;
	if (const auto* n = graph.findNode(refId)) {
		refPos = n->pos;
	}
	if (src == 0) {
		// Map sits between Audio and the ref, so Audio is two steps to the left.
		const float left = (sourceType == "mod.audio") ? -360.f : -180.f;
		src = rig::node::spawnCatalogNode(graph, sourceType, refPos + glm::vec2{left, 0.f});
	}
	if (src == 0) {
		return;
	}
	if (sourcePin.empty()) {
		sourcePin = "out";
	}

	uint32_t mapId = 0;
	if (sourceType == "mod.audio") {
		if (const uint32_t fed = linkedFrom(graph, refId, inPin)) {
			if (const auto* fedNode = graph.findNode(fed)) {
				if (fedNode->typeId == "float.map") {
					mapId = fed;
				}
			}
		}
		if (mapId == 0) {
			mapId =
				rig::node::spawnCatalogNode(graph, "float.map", refPos + glm::vec2{-180.f, 0.f});
			if (auto* map = graph.findNode(mapId)) {
				std::string title = "Map";
				if (!sourcePin.empty()) {
					title.push_back(' ');
					title += sourcePin;
					if (title.size() > 4 && title[4] >= 'a' && title[4] <= 'z') {
						title[4] = static_cast<char>(title[4] - 'a' + 'A');
					}
				}
				map->title = std::move(title);
				if (hasMapRange) {
					rig::node::setParamFloat(*map, "outMin", mapOutMin);
					rig::node::setParamFloat(*map, "outMax", mapOutMax);
				}
			}
		} else if (auto* map = graph.findNode(mapId)) {
			if (!sourcePin.empty()) {
				std::string title = "Map ";
				title += sourcePin;
				if (title.size() > 4 && title[4] >= 'a' && title[4] <= 'z') {
					title[4] = static_cast<char>(title[4] - 'a' + 'A');
				}
				map->title = std::move(title);
			}
		}
	}

	if (mapId != 0) {
		if (auto* map = graph.findNode(mapId)) {
			if (const uint32_t mapIn = pinId(*map, "in", ecs::NodePinKind::In)) {
				clearPinInputs(graph, mapId, mapIn);
			}
		}
		clearPinInputs(graph, refId, inPin);
		rig::node::tryLinkByName(graph, src, sourcePin, mapId, "in");
		rig::node::tryLinkByName(graph, mapId, "out", refId, "in");
		return;
	}

	clearPinInputs(graph, refId, inPin);
	rig::node::tryLinkByName(graph, src, sourcePin, refId, "in");
}

/// LFO out is bipolar (offset plus or minus amplitude). A Map per patch lands that span on
/// the slider. @p reuseLfo wires an existing `mod.lfo` and leaves it where it
/// sits; 0 spawns one to the left of the Map.
void wireLfoIntoRef(ecs::NodeGraphData& graph, uint32_t refId, glm::vec2 pos, uint32_t reuseLfo,
					bool hasMapRange, float mapOutMin, float mapOutMax) {
	const auto* ref = graph.findNode(refId);
	if (!ref) {
		return;
	}
	uint32_t inPin = 0;
	for (const auto& pin : ref->pins) {
		if (pin.kind == ecs::NodePinKind::In && pin.name == "in") {
			inPin = pin.id;
			break;
		}
	}
	if (inPin == 0) {
		inPin = firstPin(*ref, ecs::NodePinKind::In);
	}
	if (inPin == 0) {
		return;
	}
	glm::vec2 refPos = pos;
	if (const auto* n = graph.findNode(refId)) {
		refPos = n->pos;
	}
	uint32_t lfoId = 0;
	if (const auto* existing = graph.findNode(reuseLfo)) {
		if (existing->typeId == "mod.lfo") {
			lfoId = reuseLfo;
		}
	}
	if (lfoId == 0) {
		lfoId = rig::node::spawnCatalogNode(graph, "mod.lfo", refPos + glm::vec2{-360.f, 0.f});
	}
	const auto* lfo = graph.findNode(lfoId);
	if (!lfo) {
		return;
	}
	const float amp = std::fabs(rig::node::getParamFloat(*lfo, "amp", 1.f));
	const float off = rig::node::getParamFloat(*lfo, "offset", 0.f);
	const float inMin = off - amp;
	const float inMax = off + amp;
	const float outLo = hasMapRange ? mapOutMin : 0.f;
	const float outHi = hasMapRange ? mapOutMax : 1.f;

	const uint32_t mapId =
		rig::node::spawnCatalogNode(graph, "float.map", refPos + glm::vec2{-180.f, 0.f});
	if (mapId == 0) {
		clearPinInputs(graph, refId, inPin);
		rig::node::tryLinkByName(graph, lfoId, "out", refId, "in");
		return;
	}
	if (auto* map = graph.findNode(mapId)) {
		map->title = "Map";
		rig::node::setParamFloat(*map, "inMin", inMin);
		rig::node::setParamFloat(*map, "inMax", inMax);
		rig::node::setParamFloat(*map, "outMin", outLo);
		rig::node::setParamFloat(*map, "outMax", outHi);
	}
	clearPinInputs(graph, refId, inPin);
	rig::node::tryLinkByName(graph, lfoId, "out", mapId, "in");
	rig::node::tryLinkByName(graph, mapId, "out", refId, "in");
}

} // namespace

uint32_t spawnPropRef(ecs::NodeGraphData& graph, MEcs& ecs, uint32_t entityId, const char* propName,
					  int propType, glm::vec2 pos, const SpawnPropRefOpts& opts) {
	if (!propName || propName[0] == '\0') {
		return 0;
	}
	const entt::entity e = static_cast<entt::entity>(entityId);
	if (!ecs.registry().valid(e)) {
		return 0;
	}
	std::string entName = ecs.entityName(e);
	if (entName.empty()) {
		entName = "entity";
	}
	const char* typeId = refTypeIdForProp(propType);
	const bool hasSource = opts.sourceType && opts.sourceType[0] != '\0';
	uint32_t nid = 0;
	if (hasSource) {
		nid = findRefNode(graph, entName, propName, propType);
	}
	if (nid == 0) {
		nid = rig::node::spawnCatalogNode(graph, typeId, pos);
		if (nid == 0) {
			return 0;
		}
		if (auto* n = graph.findNode(nid)) {
			bindRefParams(*n, typeId, entName, propName);
		}
	}
	if (std::string_view(typeId) != "ref.float") {
		return nid;
	}
	if (hasSource) {
		wireSourceIntoRef(graph, nid, opts.sourceType, opts.sourcePin ? opts.sourcePin : "", pos,
						  opts.hasMapRange, opts.mapOutMin, opts.mapOutMax);
	} else if (opts.withLfo) {
		wireLfoIntoRef(graph, nid, pos, opts.reuseLfo, opts.hasMapRange, opts.mapOutMin,
					   opts.mapOutMax);
	}
	return nid;
}

glm::vec2 nextSpawnPos(const ecs::NodeGraphData& graph) {
	if (graph.nodes.empty()) {
		return {80.f, 80.f};
	}
	float maxX = graph.nodes.front().pos.x;
	float y = graph.nodes.front().pos.y;
	for (const auto& n : graph.nodes) {
		if (n.pos.x > maxX) {
			maxX = n.pos.x;
			y = n.pos.y;
		}
	}
	return {maxX + 180.f, y};
}

void unpatchProp(ecs::NodeGraphData& graph, MEcs& ecs, uint32_t entityId, const char* propName,
				 int propType) {
	if (!propName || propName[0] == '\0') {
		return;
	}
	const entt::entity e = static_cast<entt::entity>(entityId);
	if (!ecs.registry().valid(e)) {
		return;
	}
	std::string entName = ecs.entityName(e);
	if (entName.empty()) {
		entName = "entity";
	}

	std::vector<ecs::NodeGraphData*> nested;
	for (const auto& n : graph.nodes) {
		if (n.nested) {
			nested.push_back(n.nested.get());
		}
	}
	for (ecs::NodeGraphData* child : nested) {
		unpatchProp(*child, ecs, entityId, propName, propType);
	}

	const char* typeId = refTypeIdForProp(propType);
	std::unordered_set<uint32_t> doomed;
	for (const auto& n : graph.nodes) {
		if (n.typeId != typeId) {
			continue;
		}
		if (rig::node::getParamString(n, "entity") != entName) {
			continue;
		}
		const std::string stored = rig::node::getParamString(n, "prop");
		bool match = stored == propName;
		if (std::string_view(typeId) == "ref.color" && colorPropKeepsEmptyKey(propName)) {
			match = stored.empty() || stored == propName;
		}
		if (match) {
			doomed.insert(n.id);
		}
	}
	if (doomed.empty()) {
		return;
	}

	// A Map or LFO that only drives this ref (or another node we're removing)
	// goes with it. Anything shared with another patch stays.
	bool grew = true;
	while (grew) {
		grew = false;
		for (const auto& n : graph.nodes) {
			if (doomed.count(n.id) != 0) {
				continue;
			}
			if (n.typeId != "float.map" && n.typeId != "mod.lfo") {
				continue;
			}
			bool any = false;
			bool exclusive = true;
			for (const auto& link : graph.links) {
				if (link.fromNode != n.id) {
					continue;
				}
				any = true;
				if (doomed.count(link.toNode) == 0) {
					exclusive = false;
					break;
				}
			}
			if (any && exclusive) {
				doomed.insert(n.id);
				grew = true;
			}
		}
	}

	std::unordered_set<uint32_t> audio;
	for (const auto& link : graph.links) {
		if (doomed.count(link.toNode) == 0) {
			continue;
		}
		const auto* from = graph.findNode(link.fromNode);
		if (from && from->typeId == "mod.audio") {
			audio.insert(from->id);
		}
	}

	const std::vector<uint32_t> ids(doomed.begin(), doomed.end());
	for (uint32_t id : ids) {
		rig::node::removeNode(graph, id);
	}
	for (uint32_t id : audio) {
		bool stillUsed = false;
		for (const auto& link : graph.links) {
			if (link.fromNode == id) {
				stillUsed = true;
				break;
			}
		}
		if (!stillUsed) {
			rig::node::removeNode(graph, id);
		}
	}
}

} // namespace node_ui
} // namespace rigkit
