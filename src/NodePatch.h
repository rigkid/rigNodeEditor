#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include <glm/glm.hpp>

#include "CNodeGraph.h"
#include "ecs/MEcs.h"

namespace rigkit {
namespace node_ui {

entt::entity findGraphEntity(MEcs& ecs);
entt::entity ensureGraph(MEcs& ecs);
ecs::NodeGraphData* activeGraph(ecs::CNodeGraph& root);

uint32_t firstPin(const ecs::GraphNode& node, ecs::NodePinKind kind);
int pinCount(const ecs::GraphNode& node, ecs::NodePinKind kind);
std::string pinLabel(const ecs::GraphNode& node, const ecs::NodePin& pin);

bool isRefType(std::string_view typeId);
const char* refTypeIdForProp(int propType);

/**
 * @brief Existing `ref.*` bound to this entity + property, or 0.
 */
uint32_t findRefNode(const ecs::NodeGraphData& graph, std::string_view entityName,
					 std::string_view propName, int propType);

uint32_t findRefForEntity(const ecs::NodeGraphData& graph, std::string_view entityName);

/**
 * @brief Options for `spawnPropRef` (one POD instead of a bool/float soup).
 */
struct SpawnPropRefOpts {
	bool withLfo = false;
	const char* sourceType = nullptr;
	const char* sourcePin = nullptr;
	bool hasMapRange = false;
	float mapOutMin = 0.f;
	float mapOutMax = 1.f;
	uint32_t reuseLfo = 0;
};

/**
 * @brief Spawn a `ref.*` bound to @p propName. Optional source wired into it.
 * @details `opts.withLfo` spawns `mod.lfo`, or wires `opts.reuseLfo` when that
 * node is already a `mod.lfo`. The LFO goes through a new `float.map`: input is
 * the LFO span (offset plus or minus amplitude; -1..1 at the defaults), output
 * is the knob range when `opts.hasMapRange` and 0..1 otherwise.
 * `opts.sourceType` / `opts.sourcePin` spawn that catalog node instead (Sound
 * is `mod.audio` + `level` / `bass` / ...). Sound goes through a `float.map`
 * too. An existing `mod.audio` is reused; each LFO patch gets its own Map so
 * slider ranges stay independent.
 * @return Ref node id (0 on failure).
 */
uint32_t spawnPropRef(ecs::NodeGraphData& graph, MEcs& ecs, uint32_t entityId, const char* propName,
					  int propType, glm::vec2 pos, const SpawnPropRefOpts& opts = {});

/**
 * @brief Remove every ref bound to this property, plus a Map or LFO that only fed it.
 * @details A shared `mod.audio` stays while another patch still uses it, and is
 * removed once nothing links out of it. The knob keeps its last value.
 */
void unpatchProp(ecs::NodeGraphData& graph, MEcs& ecs, uint32_t entityId, const char* propName,
				 int propType);

glm::vec2 nextSpawnPos(const ecs::NodeGraphData& graph);

} // namespace node_ui
} // namespace rigkit
