#pragma once

#include <cstdint>

#include "IWindow.h"
#include "ecs/MEcs.h"

namespace rigkit {

/**
 * @brief From / Destination list over the same `CNodeGraph` the Node Editor draws.
 * @details Ctrl-P toggles this window. It does not replace the canvas.
 */
class PatchBayWindow : public IWindow {
  public:
	explicit PatchBayWindow(const std::string& title = "Patch");

	void setEcsOverride(MEcs* ecs) { m_ecsOverride = ecs; }
	MEcs* ecsOverride() const { return m_ecsOverride; }

  protected:
	void renderContents() override;

  private:
	MEcs* resolveEcs() const;

	MEcs* m_ecsOverride = nullptr;
	uint32_t m_addFromNode = 0;
	uint32_t m_addFromPin = 0;
	uint32_t m_addToNode = 0;
	uint32_t m_addToPin = 0;
};

} // namespace rigkit
