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
 * @brief Spawn a `ref.*` bound to @p propName. Optional source wired into it.
 * @details @p withLfo spawns `mod.lfo`. @p sourceType / @p sourcePin spawn that
 * catalog node instead (Sound is `mod.audio` + `level` / `bass` / ...). Sound
 * goes through a `float.map` (input 0..1, output the knob range when
 * @p hasMapRange). An existing `mod.audio` and that Map are reused.
 * @return Ref node id (0 on failure).
 */
uint32_t spawnPropRef(ecs::NodeGraphData& graph, MEcs& ecs, uint32_t entityId, const char* propName,
					  int propType, glm::vec2 pos, bool withLfo, const char* sourceType = nullptr,
					  const char* sourcePin = nullptr, bool hasMapRange = false,
					  float mapOutMin = 0.f, float mapOutMax = 1.f);

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
