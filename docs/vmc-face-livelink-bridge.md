# VMC → LiveLink ARKit face bridge

Implements **Option A** of [`vmc-face-findings.md`](vmc-face-findings.md): the VMC blendshape
stream (`/VMC/Ext/Blend/Val`) is republished as a **LiveLink subject with the ARKit schema**, so
the MetaHuman template's stock **`ABP_MH_LiveLink` → ARKit mapping → RigLogic** stack consumes it
unchanged. One capture app (e.g. XR Animator) now drives body *and* face. The template's face
assets are never modified.

## Runtime pieces (`VRM4UCapture`)

| Piece | File | Role |
|---|---|---|
| `FVrmVMCLiveLinkSource` | `Private/VrmVMCLiveLinkSource.h/.cpp` | `ILiveLinkSource`. `Update()` (game thread, LiveLink client tick) polls the VMC subsystem's curve snapshot and pushes basic-role frames. Static data = the 61 `EARFaceBlendShape` names in enum order, byte-identical schema to Epic's Live Link Face source. Holds neutral until the endpoint receives its first packet. |
| Curve mapping | `Public/VrmVMCFaceLiveLink.h` (+ impl in the source cpp) | Pure functions (automation-testable): `BlendShape.` prefix strip → case-insensitive ARKit pass-through (PerfectSync) → lossy classic-VRM fallback (`A/I/U/E/O`, `aa/ih/ou/ee/oh`, `Blink*`, `Joy/Angry/Sorrow/Fun`, `happy/sad/relaxed/surprised`, `Look*`). Unknown keys ignored. Max wins on collision. |
| `UVRM4U_VMCSubsystem::StartVMCFaceLiveLink / StopVMCFaceLiveLink / IsVMCFaceLiveLinkActive` | `VRM4U_VMCSubsystem.h/.cpp` | Game-thread lifecycle: creates the OSC server if needed, registers/removes the source with the LiveLink client. All sources removed on `Deinitialize` (before server teardown). |
| `UVrmVMCFaceLiveLinkComponent` | `Public/VrmVMCFaceLiveLinkComponent.h` | Level wiring: `SubjectName` (default `VMCFace`), `ServerAddress`/`Port` (defaults match the VMC node: `0.0.0.0:39539`). `BeginPlay` starts the bridge **and** pushes `SubjectName` into the face anim instance's `FLiveLinkSubjectName` variable by reflection (prefers names containing `Face`/`Subj`, skips `Head`, matching `ABP_MH_LiveLink`'s "LLink Face Subj" without editing the ABP). Retries ~10 s for late anim instances, then warns. |

## Editor pieces (`VRM4UCaptureEditor`)

- **`UVrmRetargetSetupUtil::SetupVMCFaceForActor(Actor, SubjectName)`**: the wizard step
  (actor context menu: **"VRM4U: Auto-Setup VMC Face (LiveLink)"**, shown when the actor has a
  `Face_Archetype` mesh or a component named `Face`). Transactional: assigns `ABP_MH_LiveLink`
  (found by name via the asset registry, and an existing LiveLink-consuming face ABP is kept) and
  adds/configures the bridge component. Python: `unreal.VrmRetargetSetupUtil.setup_vmc_face_for_actor`.
- **Scene lint** (three new checks, only on MetaHuman-face components whose sibling body is
  VMC-driven): `FaceNotDriven` (face ABP consumes no LiveLink → frozen face), `FaceBridgeMissing`
  (LiveLink ABP but no publisher on the actor, which is fine if an iPhone drives it), and
  `LiveLinkPluginDisabled` (Error: bridge present, plugin off).

## The dependency is optional by construction

`LiveLinkInterface` is an **engine-core Runtime module** (types + the modular-feature handle).
It's present in every UE install, zero user burden. The actual client comes from the engine-bundled
**LiveLink plugin**, resolved at runtime via
`IModularFeatures::IsModularFeatureAvailable(ILiveLinkClient::ModularFeatureName)`: with the
plugin disabled the bridge logs one warning and no-ops. Nothing else in VRM4U is affected.
`VRM4U.uplugin` intentionally does **not** reference the LiveLink plugin. The editor module has
no LiveLink dependency at all (lint detects LiveLink ABPs by struct *name*).

## Sender behavior

- **PerfectSync senders** (XR Animator with PerfectSync, VSeeFace + iFacialMocap, …) emit ARKit
  names: pass-through, full 52-shape fidelity.
- **Classic-VRM senders** get the fallback table: mouth opens, eyes blink, gaze and the four
  emotions read. Lossy and hand-tuned, not studio-grade.
- **Head/eye rotation curves** (`HeadYaw` … `RightEyeRoll`) are published as 0. VMC carries
  head/eye motion as bones, which the body pipeline already handles.

## Validation

- Automation tests `VRM4U.VMC.FaceLiveLink.{Schema,PassThrough,ClassicVrmFallback}`
  (`Private/Tests/VrmVMCFaceLiveLinkTest.cpp`): schema shape (61, enum order, unique),
  PerfectSync pass-through + prefix strip, fallback fan-out, max-on-collision, ARKit keys never
  fan out, unknown keys ignored.
- Final visual check (XR Animator → MetaHuman face in PIE) is manual, per usual.
