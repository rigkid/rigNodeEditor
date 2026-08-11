#include "app.h"

#include "core/RigKitEngine.h"
#include "core/pack/MPack.h"
#include "packs/rigComponent/src/CSelection.h"
#include "packs/rigComponent/src/rig/create.h"
#include "packs/rigComponent/src/rigComponent.h"
#include "packs/rigImGui/src/ExportPng.h"
#include "packs/rigImGui/src/ImGuiStyleKit.h"
#include "packs/rigImGui/src/Mui.h"
#include "packs/rigImGui/src/PropertiesWindow.h"
#include "packs/rigImGui/src/rigImGui.h"
#include "packs/rigNodeComponent/src/NodeCatalog.h"
#include "packs/rigNodeComponent/src/NodeGraph.h"
#include "packs/rigNodeComponent/src/rigNodeComponent.h"
#include "packs/rigNodeEditor/src/rigNodeEditor.h"
#include "packs/rigProject/src/CProject.h"
#include "packs/rigProject/src/rigProject.h"
#include "packs/rigSystems/src/rigSystems.h"

#include "imgui_internal.h"
#include "rendering/U_gladGlfw.h"

#include <filesystem>
#include <spdlog/spdlog.h>

NodesApp::NodesApp() {
	window().width = 1280;
	window().height = 720;
	window().title = "rigNodeEditor - nodes";
	settings().appName = "nodes";
}

void NodesApp::parseCommandLineArgs(const rigkit::CommandLineArgs& args) {
	IApp::parseCommandLineArgs(args);
	if (args.hasFlag("capture-preview")) {
		// Enough frames for dock layout + a visible LFO swing.
		m_captureFrames = 90;
	}
}

void NodesApp::seedShowcaseGraph(rigkit::ecs::CNodeGraph& graph) {
	graph = {};
	using rig::node::spawnCatalogNode;
	using rig::node::setParamFloat;
	using rig::node::setParamString;
	using rig::node::tryLinkByName;

	// Color wash: Color -> Brightness <- (LFO -> Map) -> Color Out + Color Ref(pulse.Fill)
	const uint32_t lfo = spawnCatalogNode(graph, "mod.lfo", {40.f, 60.f});
	const uint32_t map = spawnCatalogNode(graph, "float.map", {260.f, 60.f});
	const uint32_t color = spawnCatalogNode(graph, "color.value", {40.f, 260.f});
	const uint32_t bright = spawnCatalogNode(graph, "color.brightness", {480.f, 150.f});
	const uint32_t cout = spawnCatalogNode(graph, "color.out", {740.f, 80.f});
	const uint32_t cref = spawnCatalogNode(graph, "ref.color", {740.f, 250.f});

	if (auto* n = graph.findNode(lfo)) {
		n->title = "LFO";
		setParamFloat(*n, "freq", 0.45f);
		setParamFloat(*n, "amp", 1.f);
		setParamFloat(*n, "offset", 0.f);
	}
	if (auto* n = graph.findNode(map)) {
		n->title = "Map";
		setParamFloat(*n, "inMin", -1.f);
		setParamFloat(*n, "inMax", 1.f);
		setParamFloat(*n, "outMin", 0.35f);
		setParamFloat(*n, "outMax", 1.15f);
	}
	if (auto* n = graph.findNode(color)) {
		n->title = "Tint";
		setParamFloat(*n, "r", 1.f);
		setParamFloat(*n, "g", 0.38f);
		setParamFloat(*n, "b", 0.22f);
		setParamFloat(*n, "a", 1.f);
	}
	if (auto* n = graph.findNode(bright)) {
		n->title = "Brightness";
	}
	if (auto* n = graph.findNode(cout)) {
		n->title = "Color Out";
	}
	if (auto* n = graph.findNode(cref)) {
		n->title = "pulse.Fill";
		setParamString(*n, "entity", "pulse");
		setParamString(*n, "prop", "Fill"); // -> Fill R/G/B on CDrawStyle
	}

	tryLinkByName(graph, lfo, "out", map, "in");
	tryLinkByName(graph, map, "out", bright, "gain");
	tryLinkByName(graph, color, "out", bright, "in");
	tryLinkByName(graph, bright, "out", cout, "in");
	tryLinkByName(graph, bright, "out", cref, "in");
}

void NodesApp::setupAuthorUi() {
	auto* mui = dynamic_cast<rigkit::Mui*>(m_engine->getUiManager());
	if (!mui) {
		return;
	}
	mui->setImGuiTheme(rigkit::ImGuiTheme::Dark);
	mui->uiPrefs().showStatusBar = true;
	mui->setDockPassthroughCentral(false);

	mui->addHostPanel(rigkit::HostPanel::Scene);
	mui->addHostPanel(rigkit::HostPanel::Properties);
	mui->addHostPanel(rigkit::HostPanel::Log);

	if (auto* wm = mui->getWindowManager()) {
		wm->hideWindow("Log");
		wm->showWindow("Scene");
		wm->showWindow("Properties");
		wm->showWindow("Node Editor");
	}

	// First-run: Node Editor fills the central dock (same idea as glEditor preview).
	mui->setDockLayoutBuilder([mui](ImGuiID dockspaceId) {
		ImGuiDockNode* node = ImGui::DockBuilderGetNode(dockspaceId);
		if (!node) {
			return;
		}
		if (node->IsSplitNode()) {
			mui->setDockLayoutBuilder(nullptr);
			return;
		}
		const ImGuiViewport* vp = ImGui::GetMainViewport();
		ImGui::DockBuilderRemoveNode(dockspaceId);
		ImGui::DockBuilderAddNode(dockspaceId, ImGuiDockNodeFlags_DockSpace);
		ImGui::DockBuilderSetNodeSize(dockspaceId, vp->WorkSize);

		ImGuiID left = 0, center = 0, right = 0;
		ImGui::DockBuilderSplitNode(dockspaceId, ImGuiDir_Left, 0.18f, &left, &center);
		ImGui::DockBuilderSplitNode(center, ImGuiDir_Right, 0.28f, &right, &center);

		ImGui::DockBuilderDockWindow("Scene", left);
		ImGui::DockBuilderDockWindow("Node Editor", center);
		ImGui::DockBuilderDockWindow("Properties", right);
		ImGui::DockBuilderFinish(dockspaceId);
		mui->setDockLayoutBuilder(nullptr);
	});
}

void NodesApp::setup() {
	spdlog::info("nodes - CNodeGraph showcase (color wash -> pulse fill)");
	m_engine->setClearColor(0.10f, 0.11f, 0.14f, 1.0f);

	auto* packs = m_engine->getPackManager();
	if (!packs) {
		return;
	}
	packs->registerPack(std::make_shared<rigkit::rigComponent>());
	packs->registerPack(std::make_shared<rigkit::rigSystems>());
	packs->registerPack(std::make_shared<rigkit::rigProject>());
	packs->registerPack(std::make_shared<rigkit::rigImGui>());
	packs->registerPack(std::make_shared<rigkit::rigNodeComponent>());
	packs->registerPack(std::make_shared<rigkit::rigNodeEditor>());
	packs->initAll();
	packs->setupAll();

	auto* ecs = m_engine->getECSManager();
	if (!ecs) {
		return;
	}

	{
		const auto doc = ecs->createEntity("document");
		rigkit::ecs::CProject d;
		d.title = "nodes-demo";
		d.dirty = true;
		ecs->addComponent<rigkit::ecs::CProject>(doc, d);
	}

	// Scene entity driven by Color Ref (Fill R/G/B) - watch Fill in Properties.
	const auto pulse =
		rig::makeCircle(*ecs, 200.f, 200.f, 80.f, rig::fill(1.f, 0.38f, 0.22f), "pulse");

	const auto graph = rig::node::makeGraph(*ecs, "demo-graph");
	seedShowcaseGraph(ecs->getComponent<rigkit::ecs::CNodeGraph>(graph));
	rigkit::ecs::CSelection sel;
	sel.isSelected = true;
	ecs->addComponent<rigkit::ecs::CSelection>(graph, sel);

	setupAuthorUi();

	if (auto* mui = dynamic_cast<rigkit::Mui*>(m_engine->getUiManager())) {
		if (auto* wm = mui->getWindowManager()) {
			if (auto props = wm->getWindow<rigkit::PropertiesWindow>("Properties")) {
				if (pulse != entt::null) {
					props->setSelectedEntity(static_cast<uint32_t>(pulse));
				}
			}
		}
	}

	spdlog::info("nodes - LFO -> Map -> Brightness(Tint) -> Color Out + pulse.Fill");
	if (m_captureFrames >= 0) {
		spdlog::info("nodes - --capture-preview armed ({} frames)", m_captureFrames);
	}
}

void NodesApp::update(float dt) {
	m_time += dt;
	if (m_captureFrames < 0) {
		return;
	}
	if (m_captureFrames > 0) {
		--m_captureFrames;
		return;
	}
	m_captureFrames = -1;

	auto* win = m_engine->getWindow();
	int w = 0, h = 0;
	if (win) {
		glfwGetFramebufferSize(win, &w, &h);
	}
	const std::string written = rigkit::exportFramebufferPng(w, h, "preview_capture");
	if (!written.empty()) {
		namespace fs = std::filesystem;
		const fs::path srcPreview = fs::path(__FILE__).parent_path() / "img" / "preview.png";
		std::error_code ec;
		fs::create_directories(srcPreview.parent_path(), ec);
		fs::copy_file(written, srcPreview, fs::copy_options::overwrite_existing, ec);
		if (ec) {
			spdlog::warn("nodes - could not copy preview to {}: {}", srcPreview.string(),
						 ec.message());
			spdlog::info("nodes - capture left at {}", written);
		} else {
			spdlog::info("nodes - wrote {}", srcPreview.string());
		}
	}
	if (win) {
		glfwSetWindowShouldClose(win, GLFW_TRUE);
	}
}
