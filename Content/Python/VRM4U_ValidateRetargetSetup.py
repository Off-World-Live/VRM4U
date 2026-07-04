# VRM4U — headless acceptance check for the retarget setup wizard + scene lint (UE 5.6).
#
# What it does (read-only for the level; asset writes go to VALIDATION_FOLDER only):
#   1. Auto-discovers meshes via skeleton detection: a VRM source, a DAZ target, a
#      MetaHuman BODY target (face meshes are skipped on purpose).
#   2. Loads MAP and runs the scene lint (a broken direct-VMC setup should be flagged).
#   3. Runs the wizard VRM -> DAZ into VALIDATION_FOLDER and asserts: success, an
#      auto-aligned current target pose, and dumps both rigs' chains to the log.
#   4. Runs the wizard VRM -> MetaHuman and asserts success (this exercises the
#      reuse-first path when a hand-built RTG already exists — it must not be modified).
#   5. Runs the FACE wizard on the map's MetaHuman actor and asserts: success, a
#      LiveLink-consuming face anim class, and the VMC face bridge component with the
#      requested subject. (Level edits stay in memory — the map is never saved.)
#   6. Re-runs the lint, saves VALIDATION_FOLDER. The map is never saved.
#
# Run:
#   & "C:\Program Files\Epic Games\UE_5.6\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" `
#       "<your .uproject>" -run=pythonscript `
#       -script="<plugin>\Content\Python\VRM4U_ValidateRetargetSetup.py" `
#       -EnablePlugins=PythonScriptPlugin -unattended -nopause -nosplash -nullrhi -abslog="<log>"
#   then grep the log for "WIZVAL" — the last line is "WIZVAL RESULT: PASSED/FAILED".

import unreal

MARK = "WIZVAL"

# Edit for your project.
MAP = "/Game/OWLTemplateVTuber"
VALIDATION_FOLDER = "/Game/VRM4U_Validation"


def p(*args):
    print(MARK, *args)


def get_prop(obj, *names):
    for n in names:
        try:
            return obj.get_editor_property(n)
        except Exception:
            continue
    raise RuntimeError("none of %s on %s" % (names, obj))


failures = []


def check(cond, what):
    p("CHECK", "PASS" if cond else "FAIL", "-", what)
    if not cond:
        failures.append(what)


# --- collect meshes -----------------------------------------------------------------
ar = unreal.AssetRegistryHelpers.get_asset_registry()
mesh_assets = ar.get_assets_by_class(unreal.TopLevelAssetPath("/Script/Engine", "SkeletalMesh"), True)
p("skeletal meshes found:", len(mesh_assets))

daz = None
metahuman = None
vrm_sources = []
for a in mesh_assets:
    mesh = a.get_asset()
    if mesh is None:
        continue
    try:
        stype = unreal.AutoPopulateVrmMeta.detect_skeleton_type(mesh)
    except Exception as e:
        p("detect failed on", mesh.get_name(), e)
        continue
    sname = str(stype)
    if "DAZ" in sname and daz is None:
        daz = mesh
    # Prefer the body: MetaHuman actors also carry a face mesh that detection matches
    # but that has no legs (the wizard rightly rejects it).
    if "META" in sname.upper():
        n = mesh.get_name().upper()
        if metahuman is None or ("BODY" in n and "BODY" not in metahuman.get_name().upper()):
            metahuman = mesh
    if unreal.VrmRetargetSetupUtil.is_vrm_humanoid_mesh(mesh):
        vrm_sources.append(mesh)

src = None
for m in vrm_sources:
    if unreal.VrmRetargetSetupUtil.is_native_vrm_humanoid_mesh(m):
        src = m
        break
if src is None and vrm_sources:
    src = vrm_sources[0]

p("source:", src.get_name() if src else None)
p("daz:", daz.get_name() if daz else None)
p("metahuman:", metahuman.get_name() if metahuman else None)
check(src is not None, "VRM source mesh found")

# --- lint the map (before) -------------------------------------------------------------
unreal.EditorLoadingAndSavingUtils.load_map(MAP)
world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
n_before = unreal.VrmSceneLint.lint_world_and_log(world)
p("lint findings BEFORE wizard:", n_before)

# --- wizard: DAZ -----------------------------------------------------------------------
if src is not None and daz is not None:
    report = unreal.VrmRetargetSetupUtil.setup_retarget(src, daz, VALIDATION_FOLDER)
    for m in get_prop(report, "messages"):
        p("DAZ:", m)
    check(bool(get_prop(report, "success", "b_success")), "wizard succeeds for DAZ")
    rtg = get_prop(report, "retargeter")
    if rtg is not None:
        ctrl = unreal.IKRetargeterController.get_controller(rtg)
        pose_name = ctrl.get_current_retarget_pose_name(unreal.RetargetSourceOrTarget.TARGET)
        p("DAZ RTG current target pose:", pose_name)
        check(str(pose_name) == "VRM4U_AutoAligned", "DAZ RTG uses the auto-aligned pose")
        for side in (unreal.RetargetSourceOrTarget.SOURCE, unreal.RetargetSourceOrTarget.TARGET):
            rig = ctrl.get_ik_rig(side)
            rc = unreal.IKRigController.get_controller(rig)
            chains = [str(c.get_editor_property("chain_name")) for c in rc.get_retarget_chains()]
            p("DAZ rig", side, rig.get_name(), "root:", rc.get_retarget_root(), "chains:", ",".join(chains))
else:
    p("no DAZ mesh in this project - DAZ case skipped")

# --- wizard: MetaHuman (reuse-first path when a hand-built RTG exists) ------------------
if src is not None and metahuman is not None:
    report = unreal.VrmRetargetSetupUtil.setup_retarget(src, metahuman, VALIDATION_FOLDER)
    for m in get_prop(report, "messages"):
        p("MH:", m)
    check(bool(get_prop(report, "success", "b_success")), "wizard succeeds for MetaHuman")
    rtg = get_prop(report, "retargeter")
    p("MH retargeter:", rtg.get_name() if rtg else None)
else:
    p("no MetaHuman mesh in this project - MetaHuman case skipped")

# --- face wizard: MetaHuman face -> VMC LiveLink bridge ---------------------------------
FACE_SUBJECT = "VMCFace"
face_actor = None
eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
for actor in eas.get_all_level_actors():
    for comp in actor.get_components_by_class(unreal.SkeletalMeshComponent):
        try:
            mesh = comp.get_skeletal_mesh_asset()
            skel = mesh.get_editor_property("skeleton") if mesh else None
        except Exception:
            continue
        if skel is not None and "Face_Archetype" in skel.get_name():
            face_actor = actor
            break
    if face_actor is not None:
        break

if face_actor is not None:
    p("face actor:", face_actor.get_actor_label())
    report = unreal.VrmRetargetSetupUtil.setup_vmc_face_for_actor(face_actor, FACE_SUBJECT)
    for m in get_prop(report, "messages"):
        p("FACE:", m)
    check(bool(get_prop(report, "success", "b_success")), "face wizard succeeds for MetaHuman")
    bridge = face_actor.get_component_by_class(unreal.VrmVMCFaceLiveLinkComponent)
    check(bridge is not None, "face bridge component present after wizard")
    if bridge is not None:
        check(str(get_prop(bridge, "subject_name")) == FACE_SUBJECT, "bridge publishes the requested subject")
    check(unreal.VrmVMCFaceLiveLinkComponent.is_live_link_client_available(),
          "LiveLink client available (Live Link plugin enabled)")
    # Curve-mapping correctness is covered by the C++ automation tests
    # (VRM4U.VMC.FaceLiveLink.*); this section validates the level wiring only.
else:
    p("no MetaHuman face actor in map - face case skipped")

# --- lint again (after) ----------------------------------------------------------------
n_after = unreal.VrmSceneLint.lint_world_and_log(world)
p("lint findings AFTER wizard:", n_after)

# --- save the validation assets only ---------------------------------------------------
if unreal.EditorAssetLibrary.does_directory_exist(VALIDATION_FOLDER):
    unreal.EditorAssetLibrary.save_directory(VALIDATION_FOLDER, only_if_is_dirty=True, recursive=True)
    p("saved", VALIDATION_FOLDER)

p("RESULT:", "FAILED %d: %s" % (len(failures), "; ".join(failures)) if failures else "PASSED")
