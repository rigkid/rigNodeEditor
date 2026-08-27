#include "rigNodeEditor.h"

#include "core/IMui.h"
#include "core/pack/PackRegistry.h"
#include "core/RigKitEngine.h"
#include "CNodeGraph.h"
#include "MWindow.h"
#include "NodeEditorWindow.h"
#include "NodeInspector.h"
#include "PropertiesWindow.h"

#include <spdlog/spdlog.h>

namespace rigkit {

rigNodeEditor::rigNodeEditor() : IPack("rigNodeEditor") {

}

bool rigNodeEditor::init() {
	spdlog::info("[rigNodeEditor] init");
	return true;
}

void rigNodeEditor::setup() {
	auto* engine = getEngine();
	if (!engine) {
		return;
	}
	auto* ui = engine->getUiManager();
	if (!ui) {
		spdlog::warn("[rigNodeEditor] no IMui - skip Node Editor (need rigImGui)");
		return;
	}
	auto* wm = ui->getWindowManager();
	if (!wm) {
		return;
	}
	wm->createWindow<NodeEditorWindow>();
	wm->showWindow("Node Editor");

	if (auto props = wm->getWindow<PropertiesWindow>("Properties")) {
		props->addExtraDrawer([](MEcs& ecs, entt::entity entity) {
			rig::node::drawGraphNodeInspector(ecs, entity);
			rig::node::drawSceneModulateDrawer(ecs, entity);
		});
	}

	spdlog::info("[rigNodeEditor] Node Editor window registered");
}

} // namespace rigkit

namespace {
struct rigNodeEditorRegistrar {
	rigNodeEditorRegistrar() {
		rigkit::PackRegistry::instance().addFactory("rigNodeEditor", []() {
			return std::shared_ptr<rigkit::IPack>(std::make_shared<rigkit::rigNodeEditor>());
		});
	}
};
static rigNodeEditorRegistrar rigNodeEditor_auto_reg;
} // namespace
