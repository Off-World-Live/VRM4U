# IK Retargeter Pipeline — MetaHuman / DAZ live VMC

Status: **BUILT + live-tested (2026-05-31)** for OWLVRM→MetaHuman. The assets exist
(`IK_OWLVRM`, `IK_SKM_MetaHumanCharacter_BodyMesh`, `RTG_OWLVRM_to_MetaHuman`) and the MetaHuman is
driven live via the `UVrmVMCRetargetAnimInstance` C++ class (Layer B, built — see
[ik-retargeter-asset-setup.md](ik-retargeter-asset-setup.md) for the full setup walkthrough). The
plain-`RetargetPoseFromMesh`-in-AnimBP route below (Layer A) needs **no plugin C++** and still works
as a fallback.

**Fidelity sweep results (2026-07-02, `VRM4U.VMC.RetargetPoseReplay.ElbowFidelity`):** with the
auto-aligned retarget pose, the pipeline retargets **rest pose, elbow bends, shoulder swings, and
upper-arm twist essentially perfectly** in every FK rotation mode (0° phantom elbow bend on shoulder
motion; twist does not leak into swing). The one measured defect: **MatchChain under-bends elbows by
14.2°** and adds ~2° direction error, while **Interpolated and OneToOne transfer the bend exactly
(0.0°)** — so arm chains should not use MatchChain. Any live discrepancy beyond ~15° therefore comes
from outside the retargeter (source pose content, node wiring, or post-retarget modification), not
from the RTG/chain config.

**Pose-replay regression: BUILT + PASSING (2026-07-02).** `VRM4U.VMC.RetargetPoseReplay.ElbowBend`
(`Source/VRM4U/Private/Tests/VrmVMCRetargetPoseReplayTest.cpp`) drives Epic's `FIKRetargetProcessor`
headlessly with a synthetic 60° elbow bend on `SK_OWLVRM` through `RTG_OWLVRM_to_MetaHuman`: source
hand moved 25.0 cm → **target hand moved 31.4 cm, target elbow rotated 60.0°** (1:1 transfer). Combined
with the asset dump (see setup doc), the retargeter + assets are **proven to articulate the forearm** —
so the live "limp forearm" must come from upstream: verify during mocap that the **VRoid source's own
forearm moves**. If the VRoid forearm is limp too, the issue is the XR Animator stream / VMC meta
mapping, not this pipeline. (Also check whether MetaHuman *forearm-twist* bones are what looks limp —
that's the body Post-Process AnimBP's job, separate from retargeting.)

> **Live tuning gotchas (from first live test):**
> - **The source rig must be the clean gold-standard pose** — no extra pose-modifying nodes between
>   the VMC node and Output Pose, or the retargeter transfers the distortion too.
> - **Limp forearm/hand that doesn't respond to FK Rotation Mode changes** = the bones aren't in the
>   retargeted chain. Check the arm chain's **End bone** is the hand on BOTH rigs (`J_Bip_L_Hand` /
>   `hand_l`), not the upper arm. Mode (Interpolated/One-to-One/Match Chain) only affects bones already
>   inside the chain.

> **Click-by-click for the assets (steps 1–3 below):** see
> [ik-retargeter-asset-setup.md](ik-retargeter-asset-setup.md).

## Why this instead of the VMC swing correction

For MetaHuman/DAZ, a constant per-bone correction is provably insufficient — right at the
calibration pose, wrong everywhere else (measured ~88–107° on arms when sitting; see
[metahuman-alignment-findings.md](metahuman-alignment-findings.md)). The fix is to stop correcting on
the target and let UE's per-frame, chain-aware retargeter do the cross-rig conversion. (The VMC
node's interim "retarget basis correction" was removed 2026-07-03 for this reason.)

## Architecture

```
XR Animator ──VMC/OSC──> [Mixamo PROXY SkeletalMeshComponent]
                          AnimBP runs FAnimNode_VrmVMC (the gold-standard path, untouched)
                                          │  pose read each frame
                                          ▼
                          [MetaHuman body AnimBP: "Retarget Pose From Mesh" node]
                          IKRetargeterAsset = RTG_Mixamo_To_MetaHuman
                          SourceMeshComponent = the Mixamo proxy (pin)
                                          ▼
                                  MetaHuman renders — chain-aware, pose-stable
```

The Mixamo proxy can stay hidden (`SetVisibility(false)` / `bHiddenInGame`) — it still
ticks and evaluates, which is all the retargeter needs.

## Engine node reference (UE 5.6, verified from engine source)

`FAnimNode_RetargetPoseFromMesh` — `Engine/Plugins/Animation/IKRig/Source/IKRig/Public/AnimNodes/AnimNode_RetargetPoseFromMesh.h` (verified UE 5.6):

- `RetargetFrom` (`ERetargetSourceMode`, default `ParentSkeletalMeshComponent`) — **the source
  selector in 5.6.** Set it to `CustomSkeletalMeshComponent` to use an explicit `SourceMeshComponent`,
  or `SourcePosePin` to feed the source via the anim-graph pose pin. (The old `bUseAttachedParent`
  bool is now `bUseAttachedParent_DEPRECATED` — don't use it.)
- `SourceMeshComponent` (`TWeakObjectPtr<USkeletalMeshComponent>`, transient, **shown as a pin**) —
  wire the Mixamo proxy here; only used when `RetargetFrom = CustomSkeletalMeshComponent`. Note the
  comment in the engine: "Assumed to be animated and tick BEFORE this anim instance" → see Step 6.
- `IKRetargeterAsset` (`TObjectPtr<UIKRetargeter>`) — the `RTG_*` asset.
- `CustomRetargetProfile` (`FRetargetProfile`) — runtime overrides of the retargeter's settings.
- `bSuppressWarnings`, `LODThreshold`, `LODThresholdForIK` — perf/debug knobs.

> Note: UE 5.6's node has **no** `bCopyCurves` or `CustomScale` (those existed in older versions).
> Curve/scale handling differs — don't rely on them here.

## Walkthrough

### Step 1 — Mixamo proxy driven by VMC (reuse the gold standard)
1. Place a Mixamo skeletal mesh in the level (or as a component on your character actor).
   This is the same rig that is the §5 ghost gold standard.
2. Give it an AnimBP whose graph runs **VRM VMC** (`FAnimNode_VrmVMC`) → Output Pose.
3. Assign the Mixamo `UVrmMetaObject` to the node's **Vrm Meta Object** pin (non-VRM rigs do
   NOT auto-resolve a meta — assign it explicitly).
4. Start XR Animator. Confirm the (possibly hidden) Mixamo proxy moves correctly. This is the
   known-good baseline; if it's wrong here, fix it before touching the retargeter.

### Step 2 — IK Rig for the Mixamo source
1. Content Browser → right-click the Mixamo skeletal mesh → **Create → IK Rig** → `IK_Mixamo`.
2. Set the **Retarget Root** to the pelvis (`mixamorig:Hips`).
3. Add **Retarget Chains** (Add New Chain from selected start/end bone) for: Spine, Neck,
   Head, LeftArm, RightArm, LeftLeg, RightLeg. Add hand/foot/twist chains only if needed later.
   Name chains conventionally (Spine, LeftArm, …) so auto-mapping in Step 4 lines up.

### Step 3 — IK Rig for the MetaHuman target
1. MetaHuman bodies usually ship an IK Rig (`IK_metahuman` / `IK_Body`). If present, reuse it.
2. If not, create one on the MetaHuman **body** skeletal mesh, set Retarget Root = pelvis,
   and add the matching chains (same names as Step 2).

### Step 4 — IK Retargeter asset
1. Right-click `IK_Mixamo` → **Create → IK Retargeter** → `RTG_Mixamo_To_MetaHuman`.
2. Source IK Rig = `IK_Mixamo`; Target IK Rig = the MetaHuman IK Rig.
3. **Retarget poses** (the bind reconciliation that defeated the constant correction): edit the
   **target** retarget pose so the MetaHuman matches the source's pose convention — get both
   into the same reference pose (T-pose↔T-pose or A-pose↔A-pose). This is the single most
   important alignment step.
4. Verify **chain mapping**: Spine→Spine, LeftArm→LeftArm, etc. Fix any auto-map mistakes.
5. Preview with an animation on the source to confirm limbs track before going live.

### Step 5 — Wire the retarget node into the MetaHuman body AnimBP
1. Open the MetaHuman **body** AnimBP. (Per the audit, this is the `ABP_MetahumanVMC`/body
   graph — and there must be **no leftover VMC node** anywhere on the MetaHuman, e.g. the stray
   one previously in `ABP_Body_PostProcess`. The MetaHuman is now driven *only* by the
   retarget node.)
2. Add **Retarget Pose From Mesh** → feed it into Output Pose.
3. Set `IKRetargeterAsset = RTG_Mixamo_To_MetaHuman`.
4. Set **Retarget From = Custom Skeletal Mesh Component**, then expose the **Source Mesh Component**
   pin: in the AnimBP event graph, store a reference to the Mixamo proxy component (a variable set
   on BeginPlay) and plug it into the pin. (Leaving Retarget From on its default *Parent* mode
   instead requires the MetaHuman mesh to be attached under the proxy — explicit Custom is cleaner,
   especially since MetaHuman has several skeletal mesh components.)

### Step 6 — Tick order (the main runtime gotcha)
The proxy must **evaluate before** the MetaHuman reads it, or you get a 1-frame-late / wrong
pose. On BeginPlay, add a tick prerequisite so the MetaHuman ticks after the proxy:

```
MetaHumanBodyMesh->AddTickPrerequisiteComponent(MixamoProxyMesh);
```

(In Blueprint: *Add Tick Prerequisite Component*.) If feet float or sink, fix it via the IK
Retargeter's retarget pose / chain settings or a root-height adjustment — not on the node (5.6's
node has no `CustomScale`).

### Step 7 — Validate (don't skip — this is the whole point)
Run the **stand → sit error-curve test**:
1. Ghost compare: live MetaHuman vs. ghost Mixamo, same VMC stream (debug panel §5).
2. Neutral standing → record per-bone direction error (expect ~0).
3. Slowly sit; watch worst-bone error.
4. **Pass = the error stays flat across the range** (contrast: the swing correction grew to
   ~88–107° on arms). Use a fixed XR Animator source pose for comparable captures.

## Open risks / things to watch
- **Retarget pose quality** dominates results. If arms/spine look off everywhere (not just
  far from calibration), the target retarget pose in Step 4 is wrong — fix that first.
- **Mixamo IK Rig chains** must be complete; a missing chain = that limb won't retarget.
- **Twist/forearm roll**: the IK Retargeter handles this far better than the swing correction,
  but if residual twist remains, it's tuned in the retargeter chain settings, not in VMC roll
  offsets.
- **DAZ**: same pipeline, different IK Rig + RTG asset (`RTG_Mixamo_To_DAZ`).

## After Layer A passes → Layer B (C++, separate task)
A VRM4U component/helper that spawns + hides the Mixamo proxy, wires the shared
`UVrmVMCObject` OSC stream to it, sets the retarget `SourceMeshComponent` + tick prerequisite
automatically, and surfaces it in the VMC debug panel — so one dropped VRM4U actor yields
MetaHuman live mocap with no manual leader/follower setup.

> **Do NOT subclass `UVrmAnimInstanceRetargetFromMannequin` for a MetaHuman target.** That class
> already wraps Epic's `FAnimNode_RetargetPoseFromMesh`, BUT its proxy `Evaluate()` returns early
> if `DstVrmAssetList == nullptr` and then runs VRM spring-bone/constraint passes — it is built for
> **VRM targets**, not MetaHuman/DAZ. For a non-VRM target, either (a) use the stock node in the
> AnimBP (Layer A, zero C++), or (b) write a small standalone `UAnimInstance` whose proxy just runs
> `FAnimNode_RetargetPoseFromMesh` with `RetargetFrom = CustomSkeletalMeshComponent`. The convenience
> value of Layer B is the proxy spawn + auto-wire + tick-prereq, not reusing the VRM retarget class.

The C++ for Layer B references `UIKRetargeter`/the node types, so its module needs `IKRig` in
`.Build.cs` (the `VRM4U` module already has it; `VRM4UCapture` does not). Do not start Layer B
until the Layer A error curve above is confirmed flat.
