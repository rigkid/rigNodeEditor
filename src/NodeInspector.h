#pragma once

#include "CNodeGraph.h"
#include "NodeCatalog.h"
#include "ecs/MEcs.h"

namespace rig {
namespace node {

/**
 * @brief Display label for a catalog param (e.g. LFO freq becomes "Per beat" when sync).
 */
const char* catalogParamLabel(const rigkit::ecs::GraphNode& node, const CatalogParam& p);

/**
 * @brief Draw one non-string catalog param (bool / combo / float).
 * @param linked When true, show a disabled "linked" row instead of the editor.
 * @param compact When true, float uses an anonymous id and optional value-in-format (canvas).
 */
void drawCatalogParam(rigkit::ecs::GraphNode& node, const CatalogParam& p, bool linked,
					  bool compact);

/**
 * @brief Draw the Node Editor selection in Properties (params / ref / outs).
 * @details Reads `CNodeGraph::editSelectedNode` + `editDivePath`. Mutates node params
 * and group dive/ungroup on the graph.
 */
void drawGraphNodeInspector(rigkit::MEcs& ecs, entt::entity graphEntity);

/**
 * @brief Properties extra: Modulate float fields on a selected scene entity into the graph.
 * @details Spawns `mod.lfo` to `ref.float` (entity + prop). No-op when @p entity is the graph.
 */
void drawSceneModulateDrawer(rigkit::MEcs& ecs, entt::entity entity);

} // namespace node
} // namespace rig
