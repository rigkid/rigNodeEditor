#pragma once

#include "core/IApp.h"
#include "core/util/CommandLineArgs.h"

namespace rigkit {
namespace ecs {
struct CNodeGraph;
}
} // namespace rigkit

class NodesApp : public rigkit::IApp {
  public:
	NodesApp();

	void parseCommandLineArgs(const rigkit::CommandLineArgs& args) override;

	void setup() override;
	void update(float dt) override;
	void draw() override {}
	void exit() override {}

  private:
	void seedShowcaseGraph(rigkit::ecs::CNodeGraph& graph);
	void setupAuthorUi();

	float m_time = 0.f;
	int m_captureFrames = -1; ///< >=0: countdown then export preview.png and quit
};
