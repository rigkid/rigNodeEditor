#pragma once

#include "core/pack/IPack.h"
namespace rigkit {

/**
 * @brief Code pack - ImGui Node Editor window over `CNodeGraph`.
 */
class rigNodeEditor : public IPack {
  public:
	rigNodeEditor();
	bool init() override;
	void setup() override;
};

} // namespace rigkit
