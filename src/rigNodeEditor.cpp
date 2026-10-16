#include "rigNodeEditor.h"

#include "CNodeGraph.h"
#include "MWindow.h"
#include "NodeCatalog.h"
#include "NodeEditorWindow.h"
#include "NodeInspector.h"
#include "PatchBayWindow.h"
#include "PropEditors.h"
#include "PropertiesWindow.h"
#include "core/IMui.h"
#include "core/RigKitEngine.h"
#include "core/pack/PackRegistry.h"

#include <spdlog/spdlog.h>
#include <string>
#include <string_view>
#include <vector>

namespace rigkit {
namespace {

std::string soundPinLabel(std::string_view name) {
	std::string label(name);
	if (!label.empty() && label[0] >= 'a' && label[0] <= 'z') {
		label[0] = static_cast<char>(label[0] - 'a' + 'A');
	}
	return label;
}

void registerSoundPatchSources() {
	const rig::node::CatalogEntry* audio = rig::node::findCatalogEntry("mod.audio");
	if (!audio) {
		return;
	}
	std::vector<ScenePropPatchSource> sources;
	for (const rig::node::PinDesc& pin : audio->pins) {
		if (pin.kind != ecs::NodePinKind::Out || pin.type != "float" || pin.name.empty()) {
			continue;
		}
		ScenePropPatchSource src;
		src.group = "Sound";
		src.typeId = "mod.audio";
		src.pin = pin.name;
		src.label = soundPinLabel(pin.name);
		sources.push_back(std::move(src));
	}
	if (!sources.empty()) {
		setScenePropPatchSources(std::move(sources));
	}
}

} // namespace

rigNodeEditor::rigNodeEditor() : IPack("rigNodeEditor") {}

bool rigNodeEditor::init() {
	spdlog::info("[rigNodeEditor] init");
	return true;
}

void rigNodeEditor::setup() {
	registerSoundPatchSources();
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
	wm->createWindow<PatchBayWindow>();
	wm->hideWindow("Patch");
	ui->registerShortcut("patch.toggle", "Patch", "Ctrl+P", [wm] { wm->toggleWindow("Patch"); });
	ui->registerViewAction("Patch", [wm] { wm->toggleWindow("Patch"); }, "Ctrl+P");

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
