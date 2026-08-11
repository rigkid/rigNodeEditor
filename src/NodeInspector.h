#pragma once

#include "CNodeGraph.h"
#include "ecs/MEcs.h"

namespace rig {
namespace node {

/**
 * @brief Draw the Node Editor selection in Properties (params / ref / outs).
 * @details Reads `CNodeGraph::editSelectedNode` + `editDivePath`. Mutates node params
 * and group dive/ungroup on the graph.
 */
void drawGraphNodeInspector(rigkit::MEcs& ecs, entt::entity graphEntity);

/**
 * @brief Properties extra: Modulate float fields on a selected scene entity into the graph.
 * @details Spawns `mod.lfo` → `ref.float` (entity + prop). No-op when @p entity is the graph.
 */
void drawSceneModulateDrawer(rigkit::MEcs& ecs, entt::entity entity);

} // namespace node
} // namespace rig
