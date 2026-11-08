# Guides {#guides}

@htmlonly
<script>window.location.replace("core_guide.html");</script>
@endhtmlonly

One guide per engine module, in the same order as the folders in `src/` and `include/astra2d/`
from the bottom layer up (see @ref architecture "Architecture"). Each guide explains how the module
works and why, with short code examples; the [Modules](topics.html) reference lists every class
and function.

| Guide | Module | What you learn |
|---|---|---|
| @subpage core_guide "Core" | `core/` | the main loop, the fixed timestep, input, action mapping, events, logging |
| @subpage renderer_guide "Renderer" | `renderer/`, `assets/` | the camera, batching, sprites and animation, text, post-processing, the asset cache |
| @subpage ecs_guide "ECS and scenes" | `ecs/`, `scene/` | entities, sparse sets, views, signals, systems, the hierarchy, scene files |
| @subpage physics_guide "Physics" | `physics/` | bodies, the broad and narrow phase, the solver, one-way platforms, tile maps, bullets |
| @subpage audio_guide "Audio" | `audio/` | sound effects, music, voices and buses, running without a sound device |
| @subpage debug_guide "Debug tools" | `debug/` | the physics debug view and frame statistics |
