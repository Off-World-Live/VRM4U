# IK Retargeter Pipeline: MetaHuman / DAZ live VMC

This is the **manual, step-by-step** version of the MetaHuman/DAZ retarget setup. The one-click
wizard (right-click the actor ▸ **VRM4U: Auto-Setup VMC Retarget**, see
[retarget-setup-wizard.md](retarget-setup-wizard.md)) automates all of it. Follow this when you
want to build the assets by hand, understand what the wizard produced, or repair a setup it could
not complete. The click-by-click for each asset is in
[ik-retargeter-asset-setup.md](ik-retargeter-asset-setup.md).

**Chain-mode note:** for arm chains use **Interpolated** or **One-to-One** FK rotation mode, not
**Match Chain** (Match Chain under-bends elbows). If a limb looks limp and does not respond to
changing the FK Rotation Mode, its bones are not inside the retargeted chain (see the gotchas
below). A distorted source pose retargets the distortion too, so confirm the source rig looks
correct first.

> **Live tuning gotchas:**
> - **The source rig must be the clean gold-standard pose.** No extra pose-modifying nodes between
>   the VMC node and Output Pose, or the retargeter transfers the distortion too.
> - **Limp forearm/hand that doesn't respond to FK Rotation Mode changes** = the bones aren't in the
>   retargeted chain. Check the arm chain's **End bone** is the hand on BOTH rigs (`J_Bip_L_Hand` /
>   `hand_l`), not the upper arm. Mode (Interpolated/One-to-One/Match Chain) only affects bones already
>   inside the chain.

> **Click-by-click for the assets (steps 1–3 below):** see
> [ik-retargeter-asset-setup.md](ik-retargeter-asset-setup.md).

## Why this instead of the VMC swing correction

For MetaHuman/DAZ, a constant per-bone correction is provably insufficient: right at the
calibration pose, wrong everywhere else (measured ~88–107° on arms when sitting. See
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

The Mixamo proxy can stay hidden (`SetVisibility(false)` / `bHiddenInGame`). It still
ticks and evaluates, which is all the retargeter needs.

## Engine node reference (UE 5.6, verified from engine source)

`FAnimNode_RetargetPoseFromMesh`: `Engine/Plugins/Animation/IKRig/Source/IKRig/Public/AnimNodes/AnimNode_RetargetPoseFromMesh.h` (verified UE 5.6):

- `RetargetFrom` (`ERetargetSourceMode`, default `ParentSkeletalMeshComponent`): **the source
  selector in 5.6.** Set it to `CustomSkeletalMeshComponent` to use an explicit `SourceMeshComponent`,
  or `SourcePosePin` to feed the source via the anim-graph pose pin. (The old `bUseAttachedParent`
  bool is now `bUseAttachedParent_DEPRECATED`, so don't use it.)
- `SourceMeshComponent` (`TWeakObjectPtr<USkeletalMeshComponent>`, transient, **shown as a pin**):
  wire the Mixamo proxy here. Only used when `RetargetFrom = CustomSkeletalMeshComponent`. Note the
  comment in the engine: "Assumed to be animated and tick BEFORE this anim instance" → see Step 6.
- `IKRetargeterAsset` (`TObjectPtr<UIKRetargeter>`): the `RTG_*` asset.
- `CustomRetargetProfile` (`FRetargetProfile`): runtime overrides of the retargeter's settings.
- `bSuppressWarnings`, `LODThreshold`, `LODThresholdForIK`: perf/debug knobs.

> Note: UE 5.6's node has **no** `bCopyCurves` or `CustomScale` (those existed in older versions).
> Curve/scale handling differs. Don't rely on them here.

## Walkthrough

### Step 1: Mixamo proxy driven by VMC (reuse the gold standard)
1. Place a Mixamo skeletal mesh in the level (or as a component on your character actor).
   This is the same rig that is the §5 ghost gold standard.
2. Give it an AnimBP whose graph runs **VRM VMC** (`FAnimNode_VrmVMC`) → Output Pose.
3. Assign the Mixamo `UVrmMetaObject` to the node's **Vrm Meta Object** pin (non-VRM rigs do
   NOT auto-resolve a meta, so assign it explicitly).
4. Start XR Animator. Confirm the (possibly hidden) Mixamo proxy moves correctly. This is the
   known-good baseline, so if it's wrong here, fix it before touching the retargeter.

### Step 2: IK Rig for the Mixamo source
1. Content Browser → right-click the Mixamo skeletal mesh → **Create → IK Rig** → `IK_Mixamo`.
2. Set the **Retarget Root** to the pelvis (`mixamorig:Hips`).
3. Add **Retarget Chains** (Add New Chain from selected start/end bone) for: Spine, Neck,
   Head, LeftArm, RightArm, LeftLeg, RightLeg. Add hand/foot/twist chains only if needed later.
   Name chains conventionally (Spine, LeftArm, …) so auto-mapping in Step 4 lines up.

### Step 3: IK Rig for the MetaHuman target
1. MetaHuman bodies usually ship an IK Rig (`IK_metahuman` / `IK_Body`). If present, reuse it.
2. If not, create one on the MetaHuman **body** skeletal mesh, set Retarget Root = pelvis,
   and add the matching chains (same names as Step 2).

### Step 4: IK Retargeter asset
1. Right-click `IK_Mixamo` → **Create → IK Retargeter** → `RTG_Mixamo_To_MetaHuman`.
2. Source IK Rig = `IK_Mixamo`, Target IK Rig = the MetaHuman IK Rig.
3. **Retarget poses** (the bind reconciliation that defeated the constant correction): edit the
   **target** retarget pose so the MetaHuman matches the source's pose convention. Get both
   into the same reference pose (T-pose↔T-pose or A-pose↔A-pose). This is the single most
   important alignment step.
4. Verify **chain mapping**: Spine→Spine, LeftArm→LeftArm, etc. Fix any auto-map mistakes.
5. Preview with an animation on the source to confirm limbs track before going live.

### Step 5: Wire the retarget node into the MetaHuman body AnimBP
1. Open the MetaHuman **body** AnimBP. Make sure there is **no leftover VMC node** anywhere on the
   MetaHuman (including its Post-Process AnimBP). The MetaHuman is driven *only* by the retarget node.
2. Add **Retarget Pose From Mesh** → feed it into Output Pose.
3. Set `IKRetargeterAsset = RTG_Mixamo_To_MetaHuman`.
4. Set **Retarget From = Custom Skeletal Mesh Component**, then expose the **Source Mesh Component**
   pin: in the AnimBP event graph, store a reference to the Mixamo proxy component (a variable set
   on BeginPlay) and plug it into the pin. (Leaving Retarget From on its default *Parent* mode
   instead requires the MetaHuman mesh to be attached under the proxy. Explicit Custom is cleaner,
   especially since MetaHuman has several skeletal mesh components.)

### Step 6: Tick order (the main runtime gotcha)
The proxy must **evaluate before** the MetaHuman reads it, or you get a 1-frame-late / wrong
pose. On BeginPlay, add a tick prerequisite so the MetaHuman ticks after the proxy:

```
MetaHumanBodyMesh->AddTickPrerequisiteComponent(MixamoProxyMesh);
```

(In Blueprint: *Add Tick Prerequisite Component*.) If feet float or sink, fix it via the IK
Retargeter's retarget pose / chain settings or a root-height adjustment, not on the node (5.6's
node has no `CustomScale`).

### Step 7: Validate (don't skip: this is the whole point)
Run the **stand → sit error-curve test**:
1. Ghost compare: live MetaHuman vs. ghost Mixamo, same VMC stream (debug panel §5).
2. Neutral standing → record per-bone direction error (expect ~0).
3. Slowly sit and watch worst-bone error.
4. **Pass = the error stays flat across the range** (contrast: the swing correction grew to
   ~88–107° on arms). Use a fixed XR Animator source pose for comparable captures.

## Open risks / things to watch
- **Retarget pose quality** dominates results. If arms/spine look off everywhere (not just
  far from calibration), the target retarget pose in Step 4 is wrong. Fix that first.
- **Mixamo IK Rig chains** must be complete. A missing chain = that limb won't retarget.
- **Twist/forearm roll**: the IK Retargeter handles this far better than the swing correction,
  but if residual twist remains, it's tuned in the retargeter chain settings, not in VMC roll
  offsets.
- **DAZ**: same pipeline, different IK Rig + RTG asset (`RTG_Mixamo_To_DAZ`).

## Automating this (the wizard)
The one-click **VRM4U: Auto-Setup VMC Retarget** does everything above for you: it creates the IK
rigs and the `RTG_*` asset, auto-aligns the target retarget pose, spawns and hides the source
proxy, wires the retarget source component and tick prerequisite, and sets the actor's Anim Class
to `VrmVMCRetargetAnimInstance`. Use this manual walkthrough only to understand or repair what the
wizard produced. See [retarget-setup-wizard.md](retarget-setup-wizard.md).
