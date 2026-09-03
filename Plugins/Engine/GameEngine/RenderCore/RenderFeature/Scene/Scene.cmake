# Scene plugin: no third-party dependencies.

# FScene loads via MAHO_DECLARE_LAYER(FScene, "RenderScene.dll") at runtime
# (FLayerCollector::Install resolves that exact DLL name), so the target's
# output must be RenderScene.dll even though the plugin target is `Scene`.
set_target_properties(Scene PROPERTIES OUTPUT_NAME "RenderScene")
