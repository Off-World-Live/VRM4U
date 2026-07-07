# IK Retargeter asset setup: click-by-click (UE 5.6)

Companion to [ik-retargeter-pipeline.md](ik-retargeter-pipeline.md). Covers steps 1–3: the two IK
Rigs and the IK Retargeter asset. Naming: `<yourVRM>` = your VRoid/VRM source mesh.

> Bone-name cheat sheet (so you pick the right bones below):
> - **VRoid/VRM:** hips=`J_Bip_C_Hips`, spine=`J_Bip_C_Spine`/`J_Bip_C_Chest`/`J_Bip_C_UpperChest`,
>   neck=`J_Bip_C_Neck`, head=`J_Bip_C_Head`, L upper arm=`J_Bip_L_UpperArm`, L hand=`J_Bip_L_Hand`,
>   L upper leg=`J_Bip_L_UpperLeg`, L foot=`J_Bip_L_Foot`. (R = `_R_`.)
> - **MetaHuman:** hips=`pelvis`, spine=`spine_01`…`spine_05`, neck=`neck_01`, head=`head`,
>   L upper arm=`upperarm_l`, L hand=`hand_l`, L upper leg=`thigh_l`, L foot=`foot_l`.

---

## Step 1: IK Rig for your VRM (the source)

1. In the **Content Browser**, find your VRM **Skeletal Mesh** asset (`SK_<yourVRM>`, the mesh, not the skeleton or the BP).
2. **Right-click it → Create → IK Rig.** (If you don't see it: right-click → *Create IK Rig*, or use the **Animation** submenu.) Name it `IK_<yourVRM>`. Double-click to open the **IK Rig editor**.
3. **Set the retarget root.** In the **Hierarchy** panel (left), find `J_Bip_C_Hips` → **right-click it → `Set Pelvis`.** (In UE 5.6 the menu item is literally "Set Pelvis": this *is* the retarget root.) It gets a small marker. *This tells the retargeter which bone carries whole-body motion.*
4. **Add retarget chains.** ⚠️ **Do NOT Ctrl-multi-select start+end.** In UE 5.6 that creates one
   chain *per selected bone* (you get a popup per bone, each with start=end). Instead, do one chain at
   a time using the **dropdowns inside the dialog**:
   - Click **one** bone, right-click → **`New Retarget Chain`** (one popup appears).
   - In the popup set **Chain Name**, then pick **Start Bone** and **End Bone** from their **dropdowns**
     (override whatever pre-filled), leave **Goal = None**, click OK.
   Repeat per row in the table. Single-bone chains (Neck, Head) just use the same bone for Start and End.
   *(The other right-click items (New IK Goal, Exclude Selected Bone from Solve) are for foot-planting
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

   > **The Shoulder chains are NOT optional for live mocap.** XR Animator puts roughly HALF of a
   > full arm raise into the VRM shoulder bones. Without these chains the target's arms stop ~50°
   > short of overhead while everything else looks right.

   **Finger chains (for hand tracking / fists):** add these too, same pattern on both rigs
   (without them a fist barely curls on the target):

   | Chain name | VRoid start → end | MetaHuman start → end |
   |---|---|---|
   | `LeftThumb` | `J_Bip_L_Thumb1` → `J_Bip_L_Thumb3` | `thumb_01_l` → `thumb_03_l` |
   | `LeftIndex` | `J_Bip_L_Index1` → `J_Bip_L_Index3` | `index_01_l` → `index_03_l` |
   | `LeftMiddle` | `J_Bip_L_Middle1` → `J_Bip_L_Middle3` | `middle_01_l` → `middle_03_l` |
   | `LeftRing` | `J_Bip_L_Ring1` → `J_Bip_L_Ring3` | `ring_01_l` → `ring_03_l` |
   | `LeftPinky` | `J_Bip_L_Little1` → `J_Bip_L_Little3` | `pinky_01_l` → `pinky_03_l` |
   | *(Right side same with `_R_`/`_r`)* | | |

   After adding chains on both rigs, re-map in the RTG (Auto-Map Chains → Map All Exact, remembering
   the FK op keeps its OWN mapping) and re-run Auto-Align on the new bones in the target retarget
   pose. MetaHuman metacarpal + corrective bones (`*_mcp_*`, `*_bulge_*`, …) stay OUT of chains.
   The body Post-Process AnimBP drives them.
5. Confirm all 7 chains show in the **Retarget Chains** panel (right side). **Ctrl+S** to save.

> You do **not** need to add any IK solver/goals for retargeting: chains + retarget root are enough.
> Goals are only for foot-planting IK, which you can add later if feet slide.

---

## Step 2: IK Rig for the MetaHuman (the target)

1. **First check if one already exists.** MetaHumans usually ship `IK_metahuman` (or `IKRig_...`) in the MetaHuman/Common folders. If you find it, **open it and just verify** it has a retarget root (`pelvis`) and chains. If so, skip to step 3 and use it. *Reusing the shipped rig saves work and is already correct.*
2. If none exists: Content Browser → find the MetaHuman **body** Skeletal Mesh (the body, not face) → **right-click → Create → IK Rig** → name `IK_MetaHuman`.
3. **Set the retarget root:** Hierarchy → right-click `pelvis` → **Set Pelvis.** (Same action as Step 1.3. In UE 5.6 the menu item is literally "Set Pelvis", and it *is* the retarget root.)
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

## Step 3: IK Retargeter asset

1. Content Browser → **right-click `IK_<yourVRM>` (the SOURCE rig) → Create → IK Retargeter.** It may ask for the source rig: pick `IK_<yourVRM>`. Name it `RTG_<yourVRM>_to_MetaHuman`. Double-click to open the **IK Retargeter editor**.
2. **Set the target.** Top of the editor there are two rig slots / a **Target IKRig Asset** field in **Details** → set it to `IK_MetaHuman` (your Step-2 rig). Now you see **two characters** side by side: source (left) and target (right).
3. **Check chain mapping.** In the **Chain Mapping** panel (bottom), each source chain should line up with the same-named target chain (Spine↔Spine, LeftArm↔LeftArm, …). Fix any blank/wrong rows via the dropdown. *If both rigs use the names above, this auto-maps.*
4. **Fix the retarget pose: the important part.** This makes both rigs strike the *same* reference pose so motion translates cleanly:
   - Click **Edit Pose** (a.k.a. "Edit Retarget Pose") in the toolbar, then pick the **Target** (MetaHuman) from the pose target dropdown.
   - Use the **Auto-Align** button if present (Auto-Align All), OR manually rotate the MetaHuman's arms/legs in the viewport so its pose matches the source's stance (both T-pose, or both A-pose, just the *same*).
   - Click **Done/Stop Editing Pose** when they match.
   *This bind reconciliation is exactly what the constant swing-correction couldn't do: it's the crux.*
5. **Preview.** With an idle/walk animation playing on the source (or just live later), the target should follow. **Ctrl+S** to save.

---

## After step 3 → step 4 (wiring) is in [ik-retargeter-pipeline.md](ik-retargeter-pipeline.md) §Step 5
Set the MetaHuman body mesh's **Anim Class** to `UVrmVMCRetargetAnimInstance`, then set its
`SourceMeshComponent` = your live VRM component and `Retargeter` = `RTG_<yourVRM>_to_MetaHuman`.

## Background: the per-op IK Rig, not the asset-level target
In UE 5.6 the IK Retargeter's op stack is the real configuration:
- The **FK Chains op** carries its own IK Rig reference and its own chain-mapping table.
- The **Root Motion op** carries its own source root, target root, and target pelvis.
- The asset-level **Default Target IK Rig** dropdown only updates ops that still use the default
  rig. Ops holding a custom IK rig are left untouched.

When you create the retargeter from `IK_<yourVRM>`, UE defaults the target rig to that same source
rig and bakes the VRoid rig and VRoid chain mapping into each op. Changing the Default Target
dropdown afterward does not rewrite those ops, so the FK op still resolves "Spine" to `J_Bip_C_*`
and fails to find those bones in the MetaHuman mesh ("could not find J_Bip_* in mesh …").

**The fix (re-point each op's own IK Rig, or recreate clean):**
- **Cleanest:** delete the RTG. Before recreating, set the project/editor up so the target resolves
  to the MetaHuman, OR immediately after Create, in EACH op's Details set its **IK Rig Asset** (FK
  Chains op, IK Chains op) to `IK_MetaHuman`, then use the chain panel's
  **Auto-Map Chains → Map All (Exact)** so chains rebind to MetaHuman bones. For the Root Motion op
  set **Source Root**=`root` (or VRoid root), **Target Root**=`root`, **Target Pelvis**=`pelvis`.
- After re-pointing, the "out of sync / J_Bip not found" warnings clear because each op now resolves
  chains against the MetaHuman rig.
- The chain panel's auto-map menu has **Map All (Exact)** / **Map All (Fuzzy)** / **Map Only
  Empty** / **Clear All Mappings**. Exact match works since both rigs use identical chain names.

## If you see "missing target pelvis bone J_Bip_C_Hips" / "chain data out of sync" after creating the RTG
In practice the two IK rigs are usually fine, but the retargeter can have a stale VRoid bone
(`J_Bip_C_Hips`) baked into its **Root Motion op**. Cause: when you
right-click an IK Rig → Create IK Retargeter, UE defaults the **Target to the same rig as the Source**
(VRoid) and bakes the op-stack against VRoid bones. Switching the Target to the MetaHuman re-resolves
the **chain mappings by name** (so most errors clear) but the **Root Motion op keeps the old root bone**.
Big blocks of "could not find J_Bip_*_Foot in MetaHuman" usually come from a transient source/target
**swap** and are stale log spam, not the saved state.

**Fix (cleanest): delete and recreate the RTG**, set Target = the MetaHuman IK rig immediately, recheck
the log. If a lone Root Motion / target-root note remains, open the **op stack** panel, select the
**Root Motion** op, **delete it and re-add it** (re-adding reads `pelvis` fresh from the target rig).
Note: for **live VMC mocap, root motion retargeting is not in the critical path**: the body pose comes
from the **FK Chain** ops and hips come from the VMC stream, so a lingering root-motion warning usually
doesn't break the visual result.

> **The retarget pose is what the wizard automates.** A freshly created RTG has a zero-offset
> "Default Pose" on both sides. Left unaligned, the MetaHuman's arms ride ~45° low while the elbows
> still articulate. The wizard creates an aligned target pose (**`VRM4U_AutoAligned`**) and runs
> Auto-Align on it. "Default Pose" stays untouched, so you can roll back by picking it in the RTG
> editor's pose dropdown.

## If something looks off
- **A whole limb doesn't move** → that chain is missing or unmapped (Step 1.4 / Step 3.3).
- **Pose is right standing but wrong elsewhere** → that's the old constant-correction failure. The
  retarget pose (Step 3.4) is what fixes it: re-check both rigs are in the *same* reference pose.
- **Can't find "Create → IK Rig"** → enable the **IK Rig** plugin (Edit → Plugins → search "IK Rig"),
  restart, retry. (VRM4U already depends on it, so it's normally on.)
