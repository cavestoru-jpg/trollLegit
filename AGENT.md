# AGENT.md

Working notes for an agent picking this project up. Everything here was learned by
breaking it first — the "why" lines matter more than the "what", because most of these
mistakes look correct right up until the game crashes or the anti-cheat flags you.

## What this is

A native C++ DLL injected into **Minecraft 1.20 through 26.3**. It reaches into the JVM
through JNI/JVMTI, hooks Java methods by rewriting their declaring classes, and draws a menu
with ImGui. The injector carries the DLL inside itself.

- Target: every stable release from 1.20 to 26.3, in whichever of three namespaces the JVM
  is actually using — see "Multi-version" below. One DLL, no per-version builds.
- Build: `tools/build.ps1`. Five steps; step `[0/5]` validates mappings and fails the build.
- Runtime log: `%LOCALAPPDATA%\Temp\enhance_log.txt`. It reaches tens of MB — always filter,
  never read whole.
- Unload: the **End** key detaches every hook and frees the DLL so a new build can be
  injected without restarting Minecraft.

## The three laws

Break any of these and the symptom appears somewhere else entirely.

### 1. `JNIEnv` is per-thread

A `JNIEnv*` belongs to the thread that obtained it. There are two threads that matter:

- **The enhance worker** — where module `run()` functions execute. `enhance::instance->get_env()`
  returns *this* thread's env. Every SDK helper (`sdk::instance->get_world()`,
  `target_selector`, `aim_point`, `world::get_entities`) uses it internally, so **all of them
  are worker-only**.
- **The JVM tick/render thread** — where hook callbacks execute. They must use the `env`
  passed in as the first argument, never the cached one.

Method IDs and field IDs are *not* thread-bound; caching those is fine. The env is.

### 2. Game state must not be mutated from the worker

`setSprinting` from the worker corrupts a non-thread-safe attribute-modifier map and crashed
the game with `ArrayIndexOutOfBoundsException(-1)` inside `Object2ObjectArrayMap.remove`.
The pattern that works, and the one to copy: **the worker records intent, the tick thread
acts on it** — see `enhance/modules/killaura/sprint.{h,cpp}`.

Publishing a decision across the boundary: `enhance/modules/aiming/silent_aim.cpp` is the
reference. Two floats packed into one `std::atomic<uint64_t>` so the reader can never see a
fresh yaw against a stale pitch, plus a timestamp so a stalled producer expires instead of
freezing the consumer onto a dead target.

### 3. Never guess a symbol, and never hand-write one

`method_5695` looks like a getter and is `(F)F`. Guessing costs a debugging session — and
now it would cost 24 of them, because the same guess has to hold on every supported version.

- **Symbols are declared once, by their official Mojang name**, in `tools/symbols/symbols.json`
  (`{id, kind, owner, name, desc}`). Everything else is derived. Do not type a `class_NNNN`
  or an obfuscated name anywhere in C++.
- Find the right one with `tools/find_symbol.py`, which replaced the javap recipe and works
  for any version without the game installed:
  ```
  python tools/find_symbol.py members 26.3 net/minecraft/client/renderer/GameRenderer --grep cam
  python tools/find_symbol.py trace   1.21.11 net/minecraft/client/Minecraft player
  python tools/find_symbol.py drift   swing_hand          # where it exists, across all 24
  ```
  `trace` prints the name in all three namespaces at once; `drift` is how you find out that a
  symbol you rely on vanished in 26.3.
- `tools/gen_mappings.py` rebuilds `sdk/mappings/mappings_gen.inc` from the spec. Run it after
  any spec change. It needs network on a cold cache and nothing afterwards.
- `tools/verify_mappings.py` (build step `[0/5]`) now proves **owner and descriptor** on every
  version in every namespace, and **fails the build**. It also proves the 1.21.11 intermediary
  column still reproduces `tools/symbols/baseline_1.21.11.json` — the exact header the client
  was pinned to before multi-version work. That check is what keeps this a refactor.
- Sanity-check the validator itself occasionally by corrupting one name and confirming it
  fails. "195 names checked" proves nothing about *your* name.

## Multi-version

One DLL runs on 24 stable releases. The range is not uniform — it breaks in two at a place
that has nothing to do with the game's API:

- **1.20 – 1.21.11** ship obfuscated jars. Under Fabric the loader remaps them, so the live
  names are **intermediary** (`class_310`); on a vanilla launcher they are the raw
  **obfuscated** names (`gfj`), which are re-rolled every release.
- **26.1 and later are not obfuscated at all.** Mojang stopped publishing `client_mappings`
  and Fabric publishes `intermediary 0.0.0` for them, because there is nothing to map: the
  jar carries **official** names (`net/minecraft/client/Minecraft`). Fabric is optional there.

So a symbol has three possible spellings and the client has to know which one is live.

**Detection** (`sdk/version/version.cpp`) runs before any lookup. It reads `version.json`
from the classpath — a *resource*, not a class, so finding it needs no mapping — and falls
back to scanning `java.class.path` and `sun.java.command`, because under Fabric the class
path holds the loader rather than the game. An unrecognised id (a snapshot) is pinned to the
nearest supported table and **logged as a guess**; `sdk::version::exact()` is false and the
menu says so.

**Namespace** is then probed by resolving the client class *and* its static singleton field
in each candidate table. Both, because an obfuscated class name is three letters long and
could belong to anything on the class path.

**The table** (`sdk/mappings/mappings_gen.inc`, generated) is
`[namespace][version][symbol] -> {name, signature, owner}` as string-pool indices. The owner
travels with the symbol because members *move*: `GameRenderer.pick` became `Minecraft.pick`
in 26.1 and `GameRenderer.getFov` became `Camera.getFov`. A hook that pairs a method constant
with a separately chosen class constant will silently target the wrong class — ask
`sdk::mappings::owner_of("update_crosshair_target")` instead.

Everything in `sdk/mappings/mappings.hpp` is now `extern const char*`, bound once by
`sdk::mappings::bind()` from `enhance_client::attach()`. **A symbol the running version does
not have binds to `""`** — the same sentinel the header always used for absent symbols, so
`if (!name[0])` guards keep working. `sdk::mappings::have(x)` spells that test.

**Verified on real games**, one per namespace and per JVM:

| instance | namespace | Java | symbols | note |
|---|---|---|---|---|
| 1.20.4 Fabric | intermediary | 17 | 209/225 | oldest tested; uses every pre-1.20.5 path |
| 1.21.4 Fabric | intermediary | 21 | 213/225 | |
| 1.21.4 vanilla | **obfuscated** | 21 | 213/225 | no loader at all: `find_class` falls through to `FindClass` |
| 1.21.11 Fabric | intermediary | 21 | 217/225 | the version the client used to be pinned to |
| 26.2 Fabric | **official** | 25 | 216/225 | |
| 26.3 Fabric | official | 25 | 216/225 | SDL window, RenderPearl on its OpenGL backend |

**Every feature sdk::caps tracks resolves on all 24 versions.** The one entry that
reports absent is `rotation echo`, and that is a property of the game: no
rotation-only teleport packet is sent before 1.21.2, so there is nothing to
intercept. The runtime `[caps]` line is the authority -- it separates "unavailable"
from "not applicable" for exactly this reason.

Where a version could not do something, the client does it another way rather
than switching the feature off:

| feature | 1.20 - 1.20.4 | 1.20.5 - 1.21.1 | 1.21.2 - 1.21.4 | 1.21.5+ |
|---|---|---|---|---|
| reach | `MultiPlayerGameMode.getPickRange` (float) | the interaction-range attribute (double) | same | same |
| team colours | `DyeableLeatherItem.getColor` | the dyed-colour component | same | same |
| model pitch | `LivingEntityRenderer.render` | same | the render state | same |
| movement input | `Input` impulse fields | same | the same fields on `ClientInput` | `ClientInput.moveVector` |
| swing | `swing(Hand)` | same | same | `swing(Hand, SwingAnimation, boolean)` on 26.3 |
| field of view | `getFov(...)` returning double | returning float | same | no-arg `Camera.getFov` on 26.1+ |

The vanilla instance is `PrismLauncher/instances/vanilla-1.21.4` -- a copy of the
Fabric one with the loader and intermediary components removed. Prism only
rescans instances at startup, so a hand-made folder needs the launcher
restarted before `--launch <id>` can find it.

### Working on it

- Coverage per version: `tools/symbols/coverage.txt` (regenerated with the tables).
- A symbol that exists but whose **call shape changed** must not be bound. Mark it
  `blocked_by` in `tools/symbols/overrides.json`: the discovered triple stays recorded, the
  symbol reports absent, and the feature degrades visibly instead of calling a method with
  the wrong arguments. Two are blocked today:
  - `get_fov` — `GameRenderer.getFov(Camera,float,boolean)` became a no-arg `Camera.getFov()`
    in 26.1; `sdk/render/render_view.cpp` still passes three arguments.
  - `swing_hand` — 26.3 turned `swing(InteractionHand)` into
    `swing(InteractionHand, SwingAnimation, boolean)Z`.
- Per-version corrections live in `overrides.json` as `versions: [{since, until, ...}]`
  rules, copied into the spec by the seeder. Later matching rules win.
- `tools/symbols/mappings_pinned_1.21.11.hpp` is the pre-multi-version header, kept as the
  seed source and as documentation of *why* each symbol was chosen. `tools/seed_symbols.py`
  reads it, not the live header — the live one has no values any more.
- The Java renderer is compiled `--release 17`, not 21: 1.20–1.20.4 run Java 17 and would
  throw `UnsupportedClassVersionError`, which reads as "the renderer does nothing".

## Hooking

`utils/jnihook-master` (heavily modified). It redefines the **whole declaring class**, makes
the target method native, and `RegisterNatives` an implementation.

Things that cost time here:

- **Result `3` is `ERR_ADD_JVMTI_CAPS`, not "already initialised".** Treating it as success
  left `g_jnihook` null and every hook silently absent.
- **`can_suspend` is a solo-per-JVM capability** and is not released promptly on
  `DisposeEnvironment`. After a re-injection it is often unavailable, attaching without it
  leaves a window where the method is native with no implementation, and a thread calling it
  right then throws `UnsatisfiedLinkError` and the game stops. Hence the degradation ladder
  and the explicit **Force attach** opt-in.
- **`UnregisterNatives` is per-class.** Attaching a second hook to a class silently unbound
  the first until `RegisterAllNatives` re-bound every hook after each redefinition.
- **Redefinition preserves Mixin transforms.** `JNIHook_ProbeClassMixins` showed `Entity` and
  `GameRenderer` keep their Fabric/Sodium handlers after redefinition. The old blanket refusal
  to touch Mixin-instrumented classes was over-cautious and is gone.
- **The renamed copy does not always verify, and the failure is not about your method.**
  To let a hook call the original body, JNIHook defines a copy of the class under a new name.
  That makes `this` a type no other class knows, so any method that hands `this` to code
  expecting the real class fails verification — and one such method sinks the whole copy:

  ```
  VerifyError: Bad type on operand stack
    Location: Player_<uuid>.<init>(Level, GameProfile)V @128: invokespecial
    Reason:   Type 'Player_<uuid>' is not assignable to 'Player'
  ```

  Seen on 26.2 from a Fabric API Mixin handler inside `Player` and on 26.3 from `Player`'s
  own constructor. Attach now catches this, stubs out the method the verifier named (native,
  no body), rebuilds the copy and retries — the accumulated stubs live in `g_forced_stubs`.
  Stubbing *everything* except the hooked method would be simpler and wrong: a hooked method
  that calls a private sibling needs that sibling's body.
- **Verification is lazy, so `DefineClass` succeeding means nothing.** A broken copy defines
  fine and only throws when the class is linked — which happened at the `GetMethodID` that
  fetches the original method, long after the copy was cached. Attach forces the link itself,
  while the copy can still be rebuilt.
- **A failed attach used to say only `failed=8`.** `JNIHook_LastErrorDetail()` now carries the
  Java exception's `toString()`, which is what turned the above from a guess into a fix.
- Attach from the **client thread**, not the worker (`enhance::client_thread::post`).

**Always check the log for the `hook attached` lines after injecting.** A failed attach is
silent, and "the feature does nothing" looks identical to "the feature was never installed".
That has cost two full sessions.

## The rotation system

The core idea is **scope by call boundary**: swap the player's angle around a call, and
everything inside sees the silent value while everything outside sees the real one.
LiquidBounce does the same thing with MixinExtras at specific call sites; we do it by
bracketing whole methods.

`enhance/modules/aiming/tick_movement_hook.cpp` holds all of it. One helper,
`resolve_silent_angles`, decides what the server should be told; one `scoped_yaw_swap`
applies it. Every hook asks the same helper — they each used to carry a private copy and
only one handled pitch, which is exactly how a consumer quietly keeps using the real angle.

| what it fixes | hooked method |
|---|---|
| look packet + movement | `ClientPlayerEntity.tick` — `class_746.method_5773` |
| block break / place / click | `GameRenderer.updateCrosshairTarget` — `class_757.method_3190` |
| pearls, food, bow | `ClientPlayerInteractionManager.interactItem` — `class_636.method_2919` |
| teleport ack, rotation reply, vehicle teleport | `ClientPlayNetworkHandler` — `class_634.method_11157 / method_64554 / method_11086` |
| riptide launch direction | `TridentItem.onStoppedUsing` — `class_1835.method_7840` |
| ridden mount rotation | `AbstractHorseEntity.getControlledRotation` — `class_1496.method_49489` |
| model pitch (not the camera) | `LivingEntityRenderer.updateRenderState` — `class_922.method_62355` |

### Traps inside that system

- **The interaction raycast reads `headYaw` (`field_6241`), not the yaw field.** Block
  breaking appeared to work only because `PlayerEntity.tickMovement` assigns
  `headYaw = getYaw()` inside the tick wrap and nothing restores it.
- **Restore by subtracting the delta, never by writing the saved value back.** Mouse input
  and server teleports both legitimately write the rotation *inside* the wrap; overwriting
  discards them. That produced "I can't turn" and a stream of correction packets.
- **The renderer interpolates `lastYaw → yaw` and `lastPitch → pitch`.** Setting only one end
  makes the value sweep between real and silent every frame — it reads as shaking at the
  frame rate, and for most of each tick it still shows the real angle.
- **Yaw and pitch are not symmetric.** `headYaw` turns the model without touching the camera;
  pitch has no equivalent, both read `getPitch(tickDelta)`. The separation is *when* each
  reads, which is why the model pitch needs its own render-state hook.
- **`updateRenderState` runs once per frame per visible entity**, not at 20 Hz, and fires for
  every living entity — identity-check the local player, and put the cheap boolean test first.
- Some hooks receive an entity as an **argument** (trident, mount) and it is not necessarily
  you. Verify identity or you will visibly wrench a stranger's mount sideways.
- Deliberately **not** restored after the tick: `bodyYaw`, `renderYaw` and their previous-tick
  copies. Each absorbs a different share of the offset, so a blanket subtraction is wrong —
  subtracting the full offset from `bodyYaw` made it spin. `lastYawClient` / `lastPitchClient`
  **must** keep the silent value; `sendMovementPackets` compares against them to decide whether
  to send a look packet at all.

### Aim point selection

`enhance/modules/aiming/multipoint.cpp`, ported from LiquidBounce's `raytraceBoxes`. Scans the
whole hitbox: nearest point, the point already under the crosshair, and a `resolution³` grid.
Distance is measured to where the ray **enters** the box, not to the candidate.

Two modes, and the difference matters:

- **Center** (default) — distance to the middle of the hitbox dominates. The aim points at the
  target.
- **Least turn** — LiquidBounce's default. Because a hitbox is three times taller than it is
  wide, the vertical choice collapses into "whatever height I am already looking at" and the
  pitch stops tracking the target entirely. This is a property of the scoring, not a bug.

Visibility is **not implemented** — there is no world block raycast in the SDK, so every point
reports visible and `walls_range` never applies. One line to change when that lands.

## Current state

Working: silent rotation and silent aim (server, raycast, item use, network echoes, riptide,
mount, model pitch), triggerbot including through-walls via its own ray-AABB scan, ESP,
nametags, and the rest of the module set.

Killaura is **mid-rebuild** as a port of LiquidBounce's, following
`.claude/plans/polished-mapping-pumpkin.md`:

1. ~~Strip the old module~~ — done.
2. ~~Skeleton: gates, target tracker, aiming, one menu function with diagnostics~~ — done.
3. Clicker and the attack itself, through `silent_rotation_hook::queue_attack` so packet order
   stays `Look → Attack`.
4. Sprint / movefixes, on the tick thread.
5. Target render, through the same Java world renderer as ESP and nametags.
6. FailSwing, SpearReach, then AutoBlock last.
7. Menu and config.

Out of scope by decision: **Clone** rotation, and **FightBot** — it writes movement input, and
1.21.11's `PlayerInput` is an immutable record, so it needs a JNI input-writing mechanism that
does not exist yet. The same missing mechanism blocks the `Silent` movement-correction mode;
both become possible together.

Also worth knowing: in the LiquidBounce snapshot vendored here, `MovementCorrection.SILENT` and
`STRICT` are **behaviourally identical** — the only consumer treats everything except `OFF` the
same. The documented input tweak is not implemented there. `STRICT` is already what this client
does, by construction, since the yaw is swapped around `tick()` and `travel()` is inside it.

## Debugging playbook

1. **Filter the log, never read it whole.** `grep "\[saim\]\|\[silent\]\|hook attached\|attach failed"`.
2. **Confirm the hooks attached** before believing anything else.
3. **Diagnostics belong in the menu**, next to the feature, in the shape silent aim uses — a
   target/no-target readout with counts. "It does nothing" and "it never found a target" must
   not look the same. Do not ship a control that does nothing.
4. **Prefer a decisive measurement over a plausible theory.** Several sessions were lost to
   confident wrong diagnoses; the fix each time was logging the value at the exact point of
   doubt.
5. Bisect with `globals::unload_level` if unload misbehaves — a stage that "does nothing" still
   failing is what once pointed outside the staged code entirely.

## Gotchas that are not about Minecraft

- **`windows.h` defines `min`/`max`.** Use `(std::max)(...)` and `(std::numeric_limits<T>::max)()`.
- **Bash heredocs here eat one backslash level.** `"a\\modules"` in a quoted heredoc reaches
  Python as `a\modules`, and `\\t` becomes a literal tab — which silently corrupted a path in
  `enhance.vcxproj`. Write the script to a file and run it, or use the Edit tool.
- **New `.cpp` files must be added to `enhance.vcxproj`** or you get unresolved externals.
- The project is small enough that a full build is ~1–2 minutes; there is no reason to skip it.

## Licence

Ported code comes from **LiquidBounce, GPL-3.0** (`C:\Users\caves\Music\lb+b\LiquidBounce`).
That licence carries into whatever it is combined with. This is stated as a fact, not advice.
