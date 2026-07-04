# IK Retargeter asset setup — click-by-click (UE 5.6)

Companion to [ik-retargeter-pipeline.md](ik-retargeter-pipeline.md). Covers steps 1–3: the two IK
Rigs and the IK Retargeter asset. Naming: `<yourVRM>` = your VRoid/VRM source mesh.

> Bone-name cheat sheet (so you pick the right bones below):
> - **VRoid/VRM:** hips=`J_Bip_C_Hips`, spine=`J_Bip_C_Spine`/`J_Bip_C_Chest`/`J_Bip_C_UpperChest`,
>   neck=`J_Bip_C_Neck`, head=`J_Bip_C_Head`, L upper arm=`J_Bip_L_UpperArm`, L hand=`J_Bip_L_Hand`,
>   L upper leg=`J_Bip_L_UpperLeg`, L foot=`J_Bip_L_Foot`. (R = `_R_`.)
> - **MetaHuman:** hips=`pelvis`, spine=`spine_01`…`spine_05`, neck=`neck_01`, head=`head`,
>   L upper arm=`upperarm_l`, L hand=`hand_l`, L upper leg=`thigh_l`, L foot=`foot_l`.

---

## Step 1 — IK Rig for your VRM (the source)

1. In the **Content Browser**, find your VRM **Skeletal Mesh** asset (`SK_<yourVRM>` — the mesh, not the skeleton or the BP).
2. **Right-click it → Create → IK Rig.** (If you don't see it: right-click → *Create IK Rig*, or use the **Animation** submenu.) Name it `IK_<yourVRM>`. Double-click to open the **IK Rig editor**.
3. **Set the retarget root.** In the **Hierarchy** panel (left), find `J_Bip_C_Hips` → **right-click it → `Set Pelvis`.** (In UE 5.6 the menu item is literally "Set Pelvis" — this *is* the retarget root.) It gets a small marker. *This tells the retargeter which bone carries whole-body motion.*
4. **Add retarget chains.** ⚠️ **Do NOT Ctrl-multi-select start+end** — in UE 5.6 that creates one
   chain *per selected bone* (you get a popup per bone, each with start=end). Instead, do one chain at
   a time using the **dropdowns inside the dialog**:
   - Click **one** bone, right-click → **`New Retarget Chain`** (one popup appears).
   - In the popup set **Chain Name**, then pick **Start Bone** and **End Bone** from their **dropdowns**
     (override whatever pre-filled), leave **Goal = None**, click OK.
   Repeat per row in the table. Single-bone chains (Neck, Head) just use the same bone for Start and End.
   *(The other right-click items — New IK Goal, Exclude Selected Bone from Solve — are for foot-planting
   IK and are NOT needed for retargeting.)*

   | Chain name | Start bone | End bone |
   |---|---|---|
   | `Spine` | `J_Bip_C_Spine` | `J_Bip_C_UpperChest` (or `J_Bip_C_Chest` if no UpperChest) |
   | `Neck` | `J_Bip_C_Neck` | `J_Bip_C_Neck` |
   | `Head` | `J_Bip_C_Head` | `J_Bip_C_Head` |
   | `LeftArm` | `J_Bip_L_UpperArm` | `J_Bip_L_Hand` |
   | `RightArm` | `J_Bip_R_UpperArm` | `J_Bip_R_Hand` |
   | `LeftLeg` | `J_Bip_L_UpperLeg` | `J_Bip_L_Foot` |
   | `RightLeg` | `J_Bip_R_UpperLeg` | `J_Bip_R_Foot` |
   | `LeftShoulder` | `J_Bip_L_Shoulder` | `J_Bip_L_Shoulder` |
   | `RightShoulder` | `J_Bip_R_Shoulder` | `J_Bip_R_Shoulder` |

   *(A single-bone chain just has the same start and end. Skip fingers/toes for the first pass.)*

   > **The Shoulder chains are NOT optional for live mocap.** Measured 2026-07-02: XR Animator puts
   > roughly HALF of a full arm raise into the VRM shoulder bones (56–69° of bone rotation). Without
   > these chains the target's arms stop ~50° short of overhead while everything else looks right.

   **Finger chains (for hand tracking / fists)** — add these too, same pattern on both rigs
   (validated 2026-07-02: source fist curl 101.7° → target 101.6° with them; 12° without):

   | Chain name | VRoid start → end | MetaHuman start → end |
   |---|---|---|
   | `LeftThumb` | `J_Bip_L_Thumb1` → `J_Bip_L_Thumb3` | `thumb_01_l` → `thumb_03_l` |
   | `LeftIndex` | `J_Bip_L_Index1` → `J_Bip_L_Index3` | `index_01_l` → `index_03_l` |
   | `LeftMiddle` | `J_Bip_L_Middle1` → `J_Bip_L_Middle3` | `middle_01_l` → `middle_03_l` |
   | `LeftRing` | `J_Bip_L_Ring1` → `J_Bip_L_Ring3` | `ring_01_l` → `ring_03_l` |
   | `LeftPinky` | `J_Bip_L_Little1` → `J_Bip_L_Little3` | `pinky_01_l` → `pinky_03_l` |
   | *(Right side same with `_R_`/`_r`)* | | |

   After adding chains on both rigs, re-map in the RTG (Auto-Map Chains → Map All Exact — remember
   the FK op keeps its OWN mapping) and re-run Auto-Align on the new bones in the target retarget
   pose. MetaHuman metacarpal + corrective bones (`*_mcp_*`, `*_bulge_*`, …) stay OUT of chains —
   the body Post-Process AnimBP drives them.
5. Confirm all 7 chains show in the **Retarget Chains** panel (right side). **Ctrl+S** to save.

> You do **not** need to add any IK solver/goals for retargeting — chains + retarget root are enough.
> Goals are only for foot-planting IK, which you can add later if feet slide.

---

## Step 2 — IK Rig for the MetaHuman (the target)

1. **First check if one already exists.** MetaHumans usually ship `IK_metahuman` (or `IKRig_...`) in the MetaHuman/Common folders. If you find it, **open it and just verify** it has a retarget root (`pelvis`) and chains — if so, skip to step 3 and use it. *Reusing the shipped rig saves work and is already correct.*
2. If none exists: Content Browser → find the MetaHuman **body** Skeletal Mesh (the body, not face) → **right-click → Create → IK Rig** → name `IK_MetaHuman`.
3. **Set retarget root:** Hierarchy → right-click `pelvis` → **Set Retarget Root.**
4. **Add the same-named chains** (names MUST match Step 1 exactly):

   | Chain name | Start bone | End bone |
   |---|---|---|
   | `Spine` | `spine_01` | `spine_05` (or highest spine_0N) |
   | `Neck` | `neck_01` | `neck_01` |
   | `Head` | `head` | `head` |
   | `LeftArm` | `upperarm_l` | `hand_l` |
   | `RightArm` | `upperarm_r` | `hand_r` |
   | `LeftLeg` | `thigh_l` | `foot_l` |
   | `RightLeg` | `thigh_r` | `foot_r` |
   | `LeftShoulder` | `clavicle_l` | `clavicle_l` |
   | `RightShoulder` | `clavicle_r` | `clavicle_r` |

5. **Ctrl+S** to save.

---

## Step 3 — IK Retargeter asset

1. Content Browser → **right-click `IK_<yourVRM>` (the SOURCE rig) → Create → IK Retargeter.** It may ask for the source rig — pick `IK_<yourVRM>`. Name it `RTG_<yourVRM>_to_MetaHuman`. Double-click to open the **IK Retargeter editor**.
2. **Set the target.** Top of the editor there are two rig slots / a **Target IKRig Asset** field in **Details** → set it to `IK_MetaHuman` (your Step-2 rig). Now you see **two characters** side by side: source (left) and target (right).
3. **Check chain mapping.** In the **Chain Mapping** panel (bottom), each source chain should line up with the same-named target chain (Spine↔Spine, LeftArm↔LeftArm, …). Fix any blank/wrong rows via the dropdown. *If both rigs use the names above, this auto-maps.*
4. **Fix the retarget pose — the important part.** This makes both rigs strike the *same* reference pose so motion translates cleanly:
   - Click **Edit Pose** (a.k.a. "Edit Retarget Pose") in the toolbar; pick the **Target** (MetaHuman) from the pose target dropdown.
   - Use the **Auto-Align** button if present (Auto-Align All), OR manually rotate the MetaHuman's arms/legs in the viewport so its pose matches the source's stance (both T-pose, or both A-pose — just the *same*).
   - Click **Done/Stop Editing Pose** when they match.
   *This bind reconciliation is exactly what the constant swing-correction couldn't do — it's the crux.*
5. **Preview.** With an idle/walk animation playing on the source (or just live later), the target should follow. **Ctrl+S** to save.

---

## After step 3 → step 4 (wiring) is in [ik-retargeter-pipeline.md](ik-retargeter-pipeline.md) §Step 5
Set the MetaHuman body mesh's **Anim Class** to `UVrmVMCRetargetAnimInstance`, then set its
`SourceMeshComponent` = your live VRM component and `Retargeter` = `RTG_<yourVRM>_to_MetaHuman`.

## ROOT CAUSE (from UE 5.6 engine source): per-op IK Rig, not the asset-level target
The IK Retargeter's op stack is the real config. Confirmed in engine source:
- `FIKRetargetFKChainsOpSettings` has its **own** `IKRigAsset` (FKChainsOp.h:176-177) and the FK op
  keeps its **own** `FRetargetChainMapping ChainMapping` (FKChainsOp.h:260, "this op maintains its own
  chain mapping table").
- `FIKRetargetRootMotionOpSettings` has its own `SourceRoot`, `TargetRoot`, `TargetPelvis`
  (`FBoneReference`s — RootMotionGeneratorOp.h:36-51).
- The asset-level **Default Target IK Rig** dropdown (`UIKRetargeterController::SetIKRig`) only pushes
  to ops where `UsesDefaultIKRig()` is true — ops holding a **custom** IK rig are skipped.

When you Create-IK-Retargeter from `IK_OWLVRM`, `OnAddedToStack` runs `ApplyIKRigs(default source,
default target)` where target ALSO defaulted to `IK_OWLVRM`, baking the VRoid rig + VRoid chain
mapping into each op. Changing the Default Target dropdown afterward does NOT rewrite those ops →
the FK op still resolves "Spine"→`J_Bip_C_*` and looks for them in the MetaHuman mesh → "could not
find J_Bip_* in mesh SKM_MetaHumanCharacter_BodyMesh".

**THE FIX — re-point each op's own IK Rig, or recreate clean:**
- **Cleanest:** delete the RTG. Before recreating, set the project/editor up so the target resolves
  to the MetaHuman, OR immediately after Create, in EACH op's Details set its **IK Rig Asset** (FK
  Chains op, IK Chains op) to `IK_SKM_MetaHumanCharacter_BodyMesh`, then use the chain panel's
  **Auto-Map Chains → Map All (Exact)** so chains rebind to MetaHuman bones. For the Root Motion op
  set **Source Root**=`root` (or VRoid root), **Target Root**=`root`, **Target Pelvis**=`pelvis`.
- After re-pointing, the "out of sync / J_Bip not found" warnings clear because each op now resolves
  chains against the MetaHuman rig.
- Engine-confirmed button: the chain panel's auto-map menu has **Map All (Exact)** / **Map All
  (Fuzzy)** / **Map Only Empty** / **Clear All Mappings** (SIKRetargetChainMapList.cpp). Exact match
  works since both rigs use identical chain names.

## GOTCHA: "missing target pelvis bone J_Bip_C_Hips" / "chain data out of sync" after creating the RTG
Confirmed 2026-05-30 by inspecting the .uasset bytes: the two IK rigs were perfect, but the
retargeter had a stale VRoid bone (`J_Bip_C_Hips`) baked into its **Root Motion op**. Cause: when you
right-click an IK Rig → Create IK Retargeter, UE defaults the **Target to the same rig as the Source**
(VRoid) and bakes the op-stack against VRoid bones. Switching the Target to the MetaHuman re-resolves
the **chain mappings by name** (so most errors clear) but the **Root Motion op keeps the old root bone**.
Big blocks of "could not find J_Bip_*_Foot in MetaHuman" usually come from a transient source/target
**swap** and are stale log spam, not the saved state.

**Fix (cleanest): delete and recreate the RTG**, set Target = the MetaHuman IK rig immediately, recheck
the log. If a lone Root Motion / target-root note remains, open the **op stack** panel, select the
**Root Motion** op, **delete it and re-add it** (re-adding reads `pelvis` fresh from the target rig).
Note: for **live VMC mocap, root motion retargeting is not in the critical path** — the body pose comes
from the **FK Chain** ops and hips come from the VMC stream — so a lingering root-motion warning usually
doesn't break the visual result.

> **Step 3.4 status (2026-07-02): DONE programmatically.** The RTG shipped with zero-offset "Default
> Pose" on both sides (Step 3.4 had been skipped — this made the MetaHuman's arms ride ~45° low while
> elbows still articulated). Fixed by creating target pose **`VRM4U_AutoAligned`** via the Python
> controller API (`create_retarget_pose` + `set_current_retarget_pose` + `auto_align_all_bones(TARGET)`)
> — offsets landed at upperarm ±52.7°, lowerarm ±36.6°, exactly the A→T reconciliation. "Default Pose"
> is untouched; to roll back, pick it in the RTG editor's pose dropdown.

## Verify the assets headlessly (no editor clicking)
`Content/Python/VRM4U_DumpIKRetargetChains.py` dumps both IK rigs (root + every chain's start/end
bone) and the RTG's op stack (per-op target rig, per-op chain mapping, full op settings incl. FK
RotationMode) via `UnrealEditor-Cmd -run=pythonscript` — run command in the script header, then grep
the log for `CHAINDUMP`. Ran 2026-07-02 on `RTG_OWLVRM_to_MetaHuman`: everything checked out (arm
chains end at the hands on both rigs, all 7 chains mapped, MatchChain saved on every chain) — so if
a limb is limp with a clean dump, the problem is the **source pose or runtime wiring**, not these assets.

## If something looks off
- **A whole limb doesn't move** → that chain is missing or unmapped (Step 1.4 / Step 3.3).
- **Pose is right standing but wrong elsewhere** → that's the old constant-correction failure; the
  retarget pose (Step 3.4) is what fixes it — re-check both rigs are in the *same* reference pose.
- **Can't find "Create → IK Rig"** → enable the **IK Rig** plugin (Edit → Plugins → search "IK Rig"),
  restart, retry. (VRM4U already depends on it, so it's normally on.)
