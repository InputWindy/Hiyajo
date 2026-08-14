# -*- coding: utf-8 -*-
"""Maho engine + Hiyajo-Project architecture diagrams — dark theme."""
import numpy as np
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.patches import FancyBboxPatch, FancyArrowPatch

plt.rcParams["font.family"]="Microsoft YaHei"; plt.rcParams["axes.unicode_minus"]=False
SURFACE="#1a1a19"; INK="#f0efe9"; INK2="#c3c2b7"; MUTED="#898781"; GRID="#2c2c2a"; BASE="#383835"; PANEL="#242422"
BLUE="#3987e5"; AQUA="#199e70"; YEL="#c98500"; GREEN="#33a833"; VIOLET="#9085e9"; RED="#e05a5a"; ORANGE="#d95926"
GOOD="#0ca30c"; WARN="#fab219"; CRIT="#e0574f"

def box(ax, x, y, w, h, fc, ec, txt="", tc=INK, fs=10, bold=False, pad=0.015):
    ax.add_patch(FancyBboxPatch((x, y), w, h,
        boxstyle=f"round,pad={pad},rounding_size=0.03",
        fc=fc, ec=ec, lw=1.7, zorder=2))
    if txt:
        ax.text(x + w/2, y + h/2, txt, ha="center", va="center",
                color=tc, fontsize=fs, fontweight="bold" if bold else "normal", zorder=3)

def multiline(ax, x, y, w, h, fc, ec, txt, tc=INK, fs=9.5, bold=False, pad=0.015):
    """Box with multi-line text."""
    ax.add_patch(FancyBboxPatch((x, y), w, h,
        boxstyle=f"round,pad={pad},rounding_size=0.03",
        fc=fc, ec=ec, lw=1.7, zorder=2))
    if txt:
        ax.text(x + w/2, y + h/2, txt, ha="center", va="center",
                color=tc, fontsize=fs, fontweight="bold" if bold else "normal", zorder=3)

def arrow(ax, x1, y1, x2, y2, color=MUTED, lw=1.5):
    ax.add_patch(FancyArrowPatch((x1, y1), (x2, y2),
        arrowstyle="-|>", mutation_scale=14, color=color, lw=lw, zorder=1))

def line(ax, x1, y1, x2, y2, color=MUTED, lw=1.3):
    ax.plot([x1, x2], [y1, y2], color=color, lw=lw, zorder=0)

def bracket(ax, x, y, w, h, label, color, fs=12):
    """A labeled bracket/region."""
    ax.add_patch(FancyBboxPatch((x, y), w, h,
        boxstyle="round,pad=0.02,rounding_size=0.04",
        fc="none", ec=color, lw=2.5, ls="--", zorder=0))
    ax.text(x + 0.15, y + h - 0.2, label, color=color, fontsize=fs,
            fontweight="bold", zorder=3)


# ════════════════════════════════════════════════════════════════════
#  Fig 1 — Engine Shell Architecture (for engine README)
# ════════════════════════════════════════════════════════════════════

def fig_engine_arch():
    fig, ax = plt.subplots(figsize=(16, 12), dpi=140)
    fig.patch.set_facecolor(SURFACE); ax.set_facecolor(SURFACE)
    ax.set_xlim(0, 16); ax.set_ylim(0, 12.5); ax.axis("off")
    ax.set_title("Maho Engine — Minimal Shell Architecture", fontsize=17,
                 fontweight="bold", color=INK, loc="center", pad=10)

    # ──  Project Layer  ──
    bracket(ax, 0.3, 8.6, 15.4, 3.5, "Game Project    (¥Hiyajo‑Project, Source/Game/ + Source/Render/)", YEL, 10)
    ax.text(0.6, 11.6, "← 引擎不感知 — codegen 自动扫描注册", color=INK2, fontsize=9.5)

    # Project systems row
    box(ax, 0.6, 10.4, 2.5, 1.1, "#16283f", BLUE, "GCSystem\nUObject 池 + GC", INK, 9, True)
    box(ax, 3.3, 10.4, 2.5, 1.1, "#152a1c", GREEN, "ResourceSystem\n资产加载 / IO", INK, 9, True)
    box(ax, 6.0, 10.4, 2.5, 1.1, "#2e2611", WARN, "WorkerPool\n异步 Job 调度", INK, 9, True)
    box(ax, 8.7, 10.4, 2.5, 1.1, "#231d34", VIOLET, "ScriptSystem\nLua VM / 绑定", INK, 9, True)
    box(ax, 11.4, 10.4, 2.5, 1.1, "#331a1a", RED, "EditorLayer\nImGui 编辑器", INK, 9, True)

    # Project render row
    box(ax, 0.6, 8.9, 6.3, 1.1, "#16283f", BLUE, "RenderFeatures (Source/Render/)\nTriangleBasePass · ShadowPass · PostProcess", INK, 9.5, True)
    box(ax, 7.1, 8.9, 6.8, 1.1, "#152a1c", GREEN, "ECS World + Level (Source/Game/World/)\nEntityManager · Archetype · SystemScheduler", INK, 9.5, True)

    # Arrows project systems → engine render
    arrow(ax, 7.6, 8.9, 7.6, 7.8, MUTED)  # down from RenderFeatures
    arrow(ax, 5.5, 8.9, 5.5, 7.8, MUTED)  # down from left

    # ──  Engine DLL Layer  ──
    bracket(ax, 0.3, 4.2, 15.4, 4.2, "Maho Engine DLL (Maho.dll)    Infrastructure — every game needs these", BLUE, 10)

    # Engine core row (top of engine block)
    box(ax, 0.6, 6.8, 3.0, 1.3, PANEL, BLUE, "FAppBase\nExtension Framework\nBoot / Stage 管线", INK, 9.5, True)
    box(ax, 3.8, 6.8, 3.0, 1.3, PANEL, GREEN, "FPlatformSystem\nWindow · Input · File\nTimer · Paths", INK, 9.5, True)
    box(ax, 7.0, 6.8, 3.0, 1.3, PANEL, VIOLET, "FRenderSystem\nRender Thread\nFRenderServer · Upload", INK, 9.5, True)
    box(ax, 10.2, 6.8, 3.0, 1.3, PANEL, ORANGE, "FImGuiSystem\nVulkan Backend\nDockSpace · Viewport", INK, 9.5, True)

    # RHI / RDG / Shader / Async row
    box(ax, 0.6, 4.5, 3.0, 2.0, "#16283f", BLUE, "RHI\nG/C/T 逻辑队列\nBuffer·Image·Sampler\nDynamic Rendering\nBindless Descriptors", INK, 9, True)
    box(ax, 3.8, 4.5, 3.0, 2.0, "#152a1c", GREEN, "RDG\nFRDGBuilder\n声明式 Pass 图\n资源 Transitions", INK, 9, True)
    box(ax, 7.0, 4.5, 3.0, 2.0, "#2e2611", WARN, "Shader Pipeline\n.shader → 语法解析\nGLSL → SPIR-V\n懒编译 + 反射", INK, 9, True)
    box(ax, 10.2, 4.5, 3.0, 2.0, "#231d34", VIOLET, "Async Infrastructure\nFThreadedServer\nTAsyncTransferServer", INK, 9, True)

    # ──  Hardware Layer  ──
    box(ax, 0.6, 2.2, 13.6, 1.8, "#331a1a", CRIT, "Vulkan Backend (Private/)\nFVulkanRHI + VMA · VK_KHR_dynamic_rendering · VK_EXT_descriptor_indexing\nSPIR‑V · GPU Queues · glslang Compiler", INK, 10, True)

    # ──  Legend  ──
    ax.text(0.6, 0.8, "● Infrastructure (引擎壳)",
            color=BLUE, fontsize=10, fontweight="bold")
    ax.text(6.0, 0.8, "● Application Layer (项目)",
            color=YEL, fontsize=10, fontweight="bold")
    ax.text(11.5, 0.8, "● Hardware Backend",
            color=CRIT, fontsize=10, fontweight="bold")

    fig.tight_layout()
    fig.savefig("engine_architecture.png", facecolor=SURFACE, bbox_inches="tight")
    plt.close(fig)
    print("[OK] engine_architecture.png")


# ════════════════════════════════════════════════════════════════════
#  Fig 2 — Project Building Blocks (for project README)
# ════════════════════════════════════════════════════════════════════

def fig_project_arch():
    fig, ax = plt.subplots(figsize=(16, 12.5), dpi=140)
    fig.patch.set_facecolor(SURFACE); ax.set_facecolor(SURFACE)
    ax.set_xlim(0, 16); ax.set_ylim(0, 12.5); ax.axis("off")
    ax.set_title("Hiyajo-Project — Building Blocks on Maho Shell", fontsize=17,
                 fontweight="bold", color=INK, loc="center", pad=10)

    # ═══ TOP: Game Thread ═══
    bracket(ax, 0.3, 8.0, 11.8, 4.2, "GAME THREAD    (主线程, FAppBase::Tick → ExecuteStage)", GREEN, 10)

    # Systems row
    box(ax, 0.6, 10.7, 2.5, 1.2, "#16283f", BLUE, "GCSystem\nUObject 池 + GC", INK, 9.5, True)
    box(ax, 3.3, 10.7, 2.5, 1.2, "#152a1c", GREEN, "ResourceSystem\n资产加载 / Export", INK, 9.5, True)
    box(ax, 6.0, 10.7, 2.5, 1.2, "#2e2611", WARN, "WorkerPoolSystem\n异步 Job 并行", INK, 9.5, True)
    box(ax, 8.7, 10.7, 2.5, 1.2, "#231d34", VIOLET, "ScriptSystem\nLua VM · 反射绑定", INK, 9.5, True)

    # ECS World
    box(ax, 0.6, 8.3, 11.6, 2.1, PANEL, AQUA,
        "ECS World (WorldLayer)\n"
        "EntityManager (entt ID)  ·  Archetype (SoA 存储)  ·  SystemScheduler (依赖排序)\n"
        "Components (POD, not UObject)\n"
        "FTransformComponent  ·  FStaticMeshComponent  ·  FSkeletonComponent  ·  FAnimationComponent\n"
        "FCameraComponent  ·  FMaterialComponent  ·  FScriptComponent",
        INK, 9, True)

    # Resource upload flow arrow
    arrow(ax, 8.0, 8.3, 8.0, 7.2, AQUA)
    ax.text(6.8, 7.8, "FTransferHandle\n(async, no block)", color=INK2, fontsize=8.5, ha="center")

    # ═══ MIDDLE: Render Thread ═══
    bracket(ax, 0.3, 3.8, 11.8, 3.2, "RENDER THREAD    (FRenderServer, MahoRender)", ORANGE, 10)

    box(ax, 0.6, 5.4, 5.3, 1.4, "#16283f", BLUE, "FRenderServer\nTAsyncTransferServer\nImport: QueueResourceUpload<T>\nExport: RequestResourceDestroy", INK, 9, True)
    box(ax, 6.2, 5.4, 5.7, 1.4, "#152a1c", GREEN, "RenderFeature Driver\nExecuteStage(Stage, GB)\nPreRender → BasePass →\nTranslucent → PostProcess", INK, 9, True)

    # RenderFeatures box
    box(ax, 0.6, 4.1, 11.6, 1.1, "#242422", YEL,
        "Render Features (Project Source/Render/)\n"
        "FTriangleBasePassFeature  ·  FShadowPassFeature  ·  FPostProcessFeature",
        INK, 9, True)

    # RDG box
    arrow(ax, 8.0, 4.1, 8.0, 3.0, MUTED)
    box(ax, 0.6, 2.2, 11.6, 0.9, "#242422", VIOLET,
        "FRDGBuilder — Declarative Render Graph: Pass scheduling, Resource transitions, Memory aliasing",
        INK, 9, True)

    # ═══ RHI Thread label ═══
    ax.text(1.0, 1.3, "RHI THREAD    (FRHIServer, MahoRHI)", color=CRIT, fontsize=10.5, fontweight="bold")
    box(ax, 0.6, 0.3, 11.6, 1.1, "#331a1a", CRIT,
        "FRHICommandList → Vulkan Backend: VK_KHR_dynamic_rendering · VK_EXT_descriptor_indexing · VMA",
        INK, 9.5, True)

    # ═══ RIGHT: Editor Panel ═══
    bracket(ax, 12.4, 8.0, 3.3, 4.2, "Editor (GAME_WITH_EDITOR)", VIOLET, 9)
    box(ax, 12.5, 10.5, 3.1, 1.4, "#231d34", VIOLET, "EditorLayer\nDockSpace Shell\nImGui 多视口", INK, 9, True)
    box(ax, 12.5, 8.8, 3.1, 1.4, "#231d34", VIOLET, "EditorUIRegistry\nContentBrowser\nDetails · AgentChat", INK, 9, True)

    # ═══ RIGHT: Engine DLL label ═══
    ax.text(12.6, 6.6, "● Maho Engine DLL", color=BLUE, fontsize=9.5, fontweight="bold")
    box(ax, 12.5, 4.5, 3.1, 2.0, "#16283f", BLUE,
        "FAppBase · Platform\nRender · ImGui\nRHI  · RDG · Shader\nThreadedServer",
        INK, 9, True)

    fig.tight_layout()
    fig.savefig("project_architecture.png", facecolor=SURFACE, bbox_inches="tight")
    plt.close(fig)
    print("[OK] project_architecture.png")


# ════════════════════════════════════════════════════════════════════
#  Fig 3 — ThreadedServer inheritance chain
# ════════════════════════════════════════════════════════════════════
def fig_async():
    fig, ax = plt.subplots(figsize=(12, 7), dpi=140)
    fig.patch.set_facecolor(SURFACE); ax.set_facecolor(SURFACE)
    ax.set_xlim(0, 12); ax.set_ylim(0, 7); ax.axis("off")
    ax.set_title("Async Transfer Infrastructure", fontsize=16,
                 fontweight="bold", color=INK, loc="center", pad=10)

    # Base class
    box(ax, 0.5, 1.0, 11.0, 1.2, "#16283f", BLUE,
        "FThreadedServer\nSingle persistent worker thread · FIFO task queue · Submit / Poll / Flush / OnShutdown",
        INK, 10, True)

    # Inheritance arrows
    arrow(ax, 3.0, 2.2, 2.0, 3.5, BLUE)
    arrow(ax, 9.0, 2.2, 10.0, 3.5, BLUE)

    # Middle tier
    box(ax, 0.5, 3.5, 5.5, 1.6, "#152a1c", GREEN,
        "FResourceServer\nImport: file read → codec decode\nExport: serialize → write\nExecuteRequest override",
        INK, 9.5, True)
    box(ax, 6.5, 3.5, 5.0, 1.6, "#2e2611", WARN,
        "FRenderServer\nImport: CPU snapshot → RHI upload\nExport: GPU → readback\nExecuteRequest override",
        INK, 9.5, True)

    # Top arrows
    arrow(ax, 2.0, 5.1, 2.0, 5.8, GREEN)
    arrow(ax, 10.0, 5.1, 10.0, 5.8, WARN)

    # Template base
    box(ax, 0.5, 5.8, 11.0, 0.9, "#231d34", VIOLET,
        "TAsyncTransferServer<TRequest, TResult>    Submit(Request) → FTransferHandle · RetrieveResult(Handle) → TResult",
        INK, 10, True)

    # FTransferHandle
    ax.text(6.0, 0.4, "FTransferHandle: lightweight status token — only query InProgress / Failed / Succeeded",
            color=INK2, fontsize=10, ha="center")

    fig.tight_layout()
    fig.savefig("async_infrastructure.png", facecolor=SURFACE, bbox_inches="tight")
    plt.close(fig)
    print("[OK] async_infrastructure.png")


# ════════════════════════════════════════════════════════════════════
#  Fig 4 — Resource Pipeline (Disk → GPU)
# ════════════════════════════════════════════════════════════════════
def fig_resource_pipeline():
    fig, ax = plt.subplots(figsize=(14, 8), dpi=140)
    fig.patch.set_facecolor(SURFACE); ax.set_facecolor(SURFACE)
    ax.set_xlim(0, 14); ax.set_ylim(0, 8); ax.axis("off")
    ax.set_title("Resource Pipeline — Disk → CPU → GPU", fontsize=16,
                 fontweight="bold", color=INK, loc="center", pad=10)

    # Stage boxes
    stages = [
        ("1. Disk IO", BLUE,
         "FResourceServer\nAsync file read\n.casset / .png / .fbx",
         "#16283f"),
        ("2. Codec Decode", AQUA,
         "TextureImageCodec · MeshModelCodec\nWIC / Assimp\nBulkData → UResource",
         "#152a1c"),
        ("3. CPU Snapshot", GREEN,
         "ResourceSnapshotConverters\nTryBuild*CpuSnapshot\nExtract metadata + pixels/verts",
         "#152a1c"),
        ("4. GPU Upload", WARN,
         "FRenderServer\nQueueResourceUpload<T>\nTRenderResourceExporter",
         "#2e2611"),
        ("5. RHI Objects", ORANGE,
         "FRHIResourceManager\nAcquireTexture / AcquireBuffer\nVkImage / VkBuffer (VMA)",
         "#2e2611"),
        ("6. Render", CRIT,
         "RDG: GB.AddRasterPass\nBindless descriptor\nGPU indirect draw",
         "#331a1a"),
    ]

    w = 2.0; gap = 0.12; x = 0.5
    for label, ec, txt, fc in stages:
        box(ax, x, 3.5, w, 3.0, fc, ec, label, INK, 11, True)
        ax.text(x + w/2, 3.5 + 2.4, txt, ha="center", va="top", color=INK, fontsize=7.8,
                linespacing=1.5)
        x += w + gap

    # Thread division line
    ax.plot([0.3, 13.7], [1.9, 1.9], '--', color=MUTED, lw=1.3, zorder=0)
    ax.text(0.6, 1.7, "FResourceServer worker thread", color=INK2, fontsize=9, fontweight="bold")
    ax.text(8.5, 1.7, "Render thread (MahoRender)", color=INK2, fontsize=9, fontweight="bold")

    # FTransferHandle marker
    ax.text(7.0, 3.3, "── FTransferHandle (async) ──", color=GOOD, fontsize=9.5,
            fontweight="bold", ha="center")
    arrow(ax, 5.5, 2.5, 8.3, 2.5, GOOD)

    # Bottom tips
    ax.text(0.5, 0.7, "① GameU* types  are CPU‑only — never hold GPU handles    ② FTransferHandle is status-only — no GPU object on the handle",
            color=INK2, fontsize=9.5)

    fig.tight_layout()
    fig.savefig("resource_pipeline.png", facecolor=SURFACE, bbox_inches="tight")
    plt.close(fig)
    print("[OK] resource_pipeline.png")


# ════════════════════════════════════════════════════════════════════
#  Main
# ════════════════════════════════════════════════════════════════════
if __name__ == "__main__":
    import os
    base = os.path.dirname(os.path.abspath(__file__))
    os.makedirs(os.path.join(base, "diagrams"), exist_ok=True)
    os.chdir(os.path.join(base, "diagrams"))
    fig_engine_arch()
    fig_project_arch()
    fig_async()
    fig_resource_pipeline()
    print("All diagrams generated in diagrams/")
