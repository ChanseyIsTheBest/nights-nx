#pragma once
#include "profiler/banjo_pc_profiler.h"
#include "banjo_pgo.h"
#include <rex/filesystem.h>
#include <rex/platform.h>
#include <rex/rex_app.h>
#include "nights_language.h"  // nights-nx (2026-10-29): renut.toml "language"
#if defined(NIGHTS_BUILTIN_CONFIG)
#include "nights_builtin_config.h"  // generated from app/nights_switch_defaults.toml (CMake)
#endif
#include "renut_engine/overlays/fps_overlay_dialog.h"
#include "renut_engine/renut_logging.h"
#include "renut_engine/overlays/renut_logging_overlay.h"
#if !REX_PLATFORM_SWITCH
#include "renut_engine/overlays/path_setup_wizard.h"
#include "renut_engine/overlays/mnk_controls_dialog.h"
#endif
#include "renut_engine/mnk_controls.h"
#include "renut_engine/Timer.h"
#include "renut_engine/Fps.h"
#include "renut_engine/hooks.h"
#include "profiler/profiler.h"
#include "profiler/profiler_overlay.h"
// nights-nx: banjo-nx's PM4-ring renderer (src/me) is N&B-only and not built (app/CMakeLists.txt NIGHTS_NB_SOURCES).
// banjo-nx (2026-10-16): Renut's Direct3D-level renderer, the boot choice banjo_renderer = "renut"
// (app/src/renut_engine/gpu, tools/renut_gpu_import.py, docs/renut-renderer-port.md).
#if BANJO_RENUT_RENDERER
#include "renut_engine/gpu/banjo_renut_select.h"
#include "renut_engine/gpu/overlay/app_integration.h"
#include "renut_engine/gpu/banjo_renut_capture.h"
#endif
#include "cpu/banjo_enhancements.h"
#include "native/nfsmw_nativo_captura.h"
#if !REX_PLATFORM_SWITCH
#include "profiler/nfsmw_perfil_pc.h"
#endif
#include <rex/ui/window.h>
//#include <rex/discord_rpc.h>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <string>

REXCVAR_DECLARE(bool, banjo_show_fps);  // banjo-nx (2026-10-21): profiler/profiler.cpp

REXCVAR_DECLARE(bool, banjo_mesa_disk_cache_off);
REXCVAR_DECLARE(bool, masseffect_nvk_cache_per_stage);
REXCVAR_DECLARE(bool, banjo_gpu_boost);
#if BANJO_RENUT_RENDERER
REXCVAR_DECLARE(bool, banjo_renut_linked_stages);  // renut_engine/gpu/banjo_renut_tuning.cpp
#endif
REXCVAR_DECLARE(bool, banjo_gpu_zcull_greater);
#if REX_PLATFORM_SWITCH
extern "C" {
void RexSwitchApmRequestGpuMhz(int mhz);  // sdk/src/ui/switch_apm.cpp
void RexSwitchApmApply(void);
}
#endif

class RenutApp : public rex::ReXApp {
public:
    RenutApp(rex::ui::WindowedAppContext& ctx, std::string_view name, rex::PPCImageInfo info)
        : rex::ReXApp(ctx, name, info), app_name_(name) {}

    static std::unique_ptr<rex::ui::WindowedApp> Create(
        rex::ui::WindowedAppContext& ctx) {
        return std::unique_ptr<RenutApp>(new RenutApp(ctx, "renut", PPCImageConfig));
    }


    // void OnPostSetup() override {
    //     rex::discord_rpc::Presence rpc;

    //     rpc.details_ = "";
    //     rpc.state_ = "";
    //     rpc.large_image_key_ = "e242d6b6-c34e-47a1-8c2a-5297fe33bce7";
    //     rpc.large_image_text_ = "renut";

    //     //rex::discord_rpc::Start(Application ID, Settings);
    //     rex::discord_rpc::Start("1520303728047951892", rpc);

    //     rex::cvar::LoadConfig("renut.toml");
    // }

    void OnCreateDialogs(rex::ui::ImGuiDrawer* drawer) override {
        //drawer->AddDialog(new FpsOverlayDialog(drawer));
        fps_dialog_ = std::make_unique<FpsOverlayDialog>(drawer);
        fps_dialog_->fpsManager = &fpsManager;
        fpsManager.showFPS = REXCVAR_GET(banjo_show_fps);  // banjo-nx (2026-10-21)
        drawer->AddDialog(fps_dialog_.get());
        drawer->AddDialog(new RenuLogOverlayDialog(drawer));
        // CPU/GPU profiler window ported from UnleashedRecomp (F5 / banjo_show_profiler).
        profiler_dialog_ = std::make_unique<ProfilerOverlayDialog>(drawer);
        drawer->AddDialog(profiler_dialog_.get());
#if !REX_PLATFORM_SWITCH
        path_wizard_ = new PathSetupWizard(drawer);
        drawer->AddDialog(path_wizard_);

        // Keyboard & mouse: the window exists by the time dialogs are created,
        // so this is where the host-side listener gets attached.
        mnk_dialog_ = std::make_unique<MnkControlsDialog>(drawer);
        drawer->AddDialog(mnk_dialog_.get());
        renut::mnk::AttachWindow(window());
#endif
    }

    // Native renderer: with masseffect_renderer_native = true the graphics system is the app's own
    // (src/me, from masseffect-nx) and the emulated Xenos GPU plugin is not loaded (ReXApp only loads it
    // when config.graphics is empty).
    void OnPreSetup(rex::RuntimeConfig& config) override {
        banjo::ApplyMasterSwitches();
#if REX_PLATFORM_SWITCH
        // Before the Vulkan device exists (the graphics system creates it after this). Mesa's own disk cache
        // (fossilize file on the SD card) duplicates the renderer's pipeline cache file, and on the console its
        // writer thread held the cache lock during slow SD writes while the ring thread, in the middle of a
        // vkCreateGraphicsPipelines, waited on that lock for its lookup (switch stall dumps). Same pipelines.
        if (banjo::EnhancementActive(banjo::EnhancementGroup::kNonCpu, REXCVAR_GET(banjo_mesa_disk_cache_off))) {
            setenv("MESA_SHADER_CACHE_DISABLE", "true", 1);
        }
        // banjo_gpu_zcull_greater: the driver reads NVK_ZCULL once, at its first render pass.
        if (banjo::EnhancementActive(banjo::EnhancementGroup::kNonCpu, REXCVAR_GET(banjo_gpu_zcull_greater))) {
            setenv("NVK_ZCULL", "greater", 1);
        }
        // Per-stage shader cache keys in NVK (the patched driver reads it at vkCreateDevice; an unpatched one ignores
        // it): a vertex or pixel shader already compiled for another pipeline is not compiled again. In this driver
        // the linked compile only drops vertex outputs the pixel shader does not read, so the image is the same.
        bool per_stage_cache = banjo::EnhancementActive(banjo::EnhancementGroup::kNonCpu,
                                                        REXCVAR_GET(masseffect_nvk_cache_per_stage));
#if BANJO_RENUT_RENDERER
        // banjo_renut_linked_stages (package 26): not with Renut's renderer. Per-stage keys compile each stage without
        // the other (disable_lto), which banjo-nx's translator allows for by pruning the vertex outputs itself; Renut's
        // vertex shaders write every output (zeros for the unused ones), and only the linked compile drops the ones the
        // pixel shader does not read.
        if (per_stage_cache && banjo::renut_gpu::Active() &&
            banjo::EnhancementActive(banjo::EnhancementGroup::kNonCpu, REXCVAR_GET(banjo_renut_linked_stages))) {
            per_stage_cache = false;
        }
#endif
        if (per_stage_cache) {
            setenv("NVK_SWITCH_STAGE_CACHE", "1", 1);
        }
        // banjo_gpu_boost (from sa2-nx): the official handheld GPU profile; switch_apm.cpp logs what it did on stderr
        // (rex_stderr.log) and requests nothing when docked.
        if (banjo::EnhancementActive(banjo::EnhancementGroup::kNonCpu, REXCVAR_GET(banjo_gpu_boost))) {
            RexSwitchApmRequestGpuMhz(460);
            RexSwitchApmApply();
        }
#endif
#if BANJO_RENUT_RENDERER
        if (banjo::renut_gpu::Active()) {
            // No SDK graphics system and no GPU plugin: the renderer is the app's (detached mode).
            renut::gpu::ConfigureRuntime(config);
            banjo::profiler::SetRendererInfo("Renut native (Direct3D level, plume)", "");
            return;
        }
#endif
        // nights-nx: without Renut's renderer the SDK's emulated Xenos GPU draws (the ring renderer is N&B-only).
    }

#if BANJO_RENUT_RENDERER
    // Renut's renderer draws the SDK's ImGui through its own plume drawer, and resizes its swapchain itself.
    std::unique_ptr<rex::ui::ImmediateDrawer> OnCreateImmediateDrawer() override {
        if (banjo::renut_gpu::Active()) return renut::gpu::CreateOverlayDrawer();
        return rex::ReXApp::OnCreateImmediateDrawer();
    }
    void OnWindowPixelSizeChanged(uint32_t pixel_width, uint32_t pixel_height) override {
        rex::ReXApp::OnWindowPixelSizeChanged(pixel_width, pixel_height);
        if (banjo::renut_gpu::Active()) renut::gpu::OnWindowResized();
    }
    // The window exists here (the path wizard is drawn before the game starts): Renut's renderer comes up first.
    void StartRenutRenderer() {
        if (banjo::renut_gpu::Active())
            renut::gpu::StartRenderer(app_context(), [this] { return imgui_drawer(); });
    }
#else
    void StartRenutRenderer() {}
#endif

    void OnPostSetup() override {
        // One log line with every enhancement and whether it is active (src/cpu/banjo_enhancements.h).
        banjo::LogEnhancements();
        // PC: function-level CPU profile (banjo_pc_profile_s = N seconds per report; 0 = off; from futari-nx).
        banjo::pc_profiler::Start();
        // Switch: per-thread CPU accounting, the optional sampler and the stall watchdog.
        banjo::profiler::Start();
        // PNG captures of the game image for testing (nfsmw_captura_cada_s = N seconds; 0 = off). Taken from
        // the presenter, so no overlay or window can cover them.
#if BANJO_RENUT_RENDERER
        // Renut's renderer has no SDK presenter: its own Present reads the frame back (banjo_renut_capture.h).
        if (banjo::renut_gpu::Active()) {
            nfsmw::captura::Arrancar(nfsmw::captura::Capturador(
                [](rex::ui::RawImage& image) { return renut::gpu::CaptureFrame(image); }));
        } else
#endif
        nfsmw::captura::Arrancar([this]() -> rex::ui::Presenter* {
            const auto* rt = runtime();
            const auto* graphics = rt ? rt->graphics_system() : nullptr;
            return graphics ? graphics->presenter() : nullptr;
        });
#if !REX_PLATFORM_SWITCH
        // Sampling CPU profiler for PC tests (nfsmw_perfil_pc_desde_s; off by default).
        nfsmw::perfil_pc::Arrancar();
#endif
        // The PGO-instrumented build (BANJO_PGO=generate) saves its counts to pgo/ every 2 minutes (nothing otherwise).
        banjo::pgo::Start();
    }

    // The PGO-instrumented build saves its counts when the game loses focus (HOME menu): it may be closed from there.
    void OnWindowFocusChanged(bool focused) override {
        if (!focused) banjo::pgo::DumpNow();
    }

    void OnShutdown() override {
#if BANJO_RENUT_RENDERER
        if (banjo::renut_gpu::Active()) renut::gpu::ShutdownRenderer();
#endif
        banjo::pc_profiler::Stop();
        nfsmw::captura::Parar();
#if !REX_PLATFORM_SWITCH
        nfsmw::perfil_pc::Parar();
#endif
        renut::mnk::DetachWindow();
        banjo::pgo::Stop();
    }

    // nights-nx (2026-10-29, the user: "the only thing I want in the .toml by default is" the controls, the language
    // and SMAA; sa2-nx's built-in config): the Switch build's release settings compiled in (app/nights_switch_
    // defaults.toml, written by tools/make_nights_tomls.py); renut.toml next to the NRO overrides them key by key.
    std::string_view BuiltInConfig() const override {
#if defined(NIGHTS_BUILTIN_CONFIG)
        return kNightsBuiltInConfig;
#else
        return {};
#endif
    }
    // Without a renut.toml next to the NRO, the player's default one is written there (app/nights_switch.toml).
    std::string_view DefaultConfigFile() const override {
#if defined(NIGHTS_BUILTIN_CONFIG)
        return kNightsPlayerConfig;
#else
        return {};
#endif
    }

    // The game's language (renut.toml "language"; auto = the system language): before the game asks for it.
    void OnPostInitLogging() override {
        nights::language::Apply();
    }

    // A game_root folder next to the executable is used when no path was given.
    // On the Switch that is sdmc:/switch/nights-nx/game_root (the extracted package).
    void OnConfigurePaths(rex::PathConfig& paths) override {
        if (!paths.game_data_root.empty()) {
            return;
        }
        const auto folder = rex::filesystem::GetExecutableFolder();
        std::error_code ec;
        const auto extracted = folder / "game_root";
        if (!folder.empty() && std::filesystem::is_directory(extracted, ec)) {
            paths.game_data_root = extracted;
        }
    }

#if !REX_PLATFORM_SWITCH
    std::optional<rex::PathConfig> OnFinalizePaths(
        const rex::PathConfig& defaults,
        std::function<void(rex::PathConfig)> resume) override
    {
        StartRenutRenderer();
        path_wizard_->Init(app_name_, defaults, [resume](rex::PathConfig resolved) {
            resume(resolved);
            });
        return std::nullopt;
    }
#else
    std::optional<rex::PathConfig> OnFinalizePaths(
        const rex::PathConfig& defaults,
        std::function<void(rex::PathConfig)> resume) override
    {
        StartRenutRenderer();
        return rex::ReXApp::OnFinalizePaths(defaults, std::move(resume));
    }
#endif

private:
    std::string      app_name_;
#if !REX_PLATFORM_SWITCH
    PathSetupWizard* path_wizard_ = nullptr;
    std::unique_ptr<MnkControlsDialog> mnk_dialog_;
#endif
    std::unique_ptr<FpsOverlayDialog> fps_dialog_;
    std::unique_ptr<ProfilerOverlayDialog> profiler_dialog_;
};
