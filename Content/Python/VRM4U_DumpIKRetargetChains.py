# VRM4U — headless IK Rig / IK Retargeter verifier (UE 5.6).
#
# Dumps, without opening the editor UI:
#   - each IK Rig's retarget root + every chain's start/end bone
#   - each RTG op: enabled state, its OWN target IK rig (per-op, the usual stale culprit),
#     per-op chain mapping, and full op settings (incl. FK RotationMode per chain)
#
# Run (repo scratch log path optional):
#   & "C:\Program Files\Epic Games\UE_5.6\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" `
#       "<your .uproject>" -run=pythonscript `
#       -script="<plugin>\Content\Python\VRM4U_DumpIKRetargetChains.py" `
#       -EnablePlugins=PythonScriptPlugin -unattended -nopause -nosplash -nullrhi -abslog="<log path>"
#   then grep the log for CHAINDUMP.
#
# 5.6 Python API gotchas (cost a few iterations to find):
#   - BoneChain fields need get_editor_property("start_bone"); attribute access fails.
#   - IKRetargeterController.get_source_chain(target_chain_name, op_name) — target chain FIRST.
#   - get_retarget_op_enabled takes the op INDEX; get_target_ik_rig_for_op takes the op NAME.
#   - Op settings: get_op_controller(i).get_settings().export_text() shows everything,
#     including properties the struct repr hides (e.g. ChainsToRetarget RotationMode).

import unreal

MARK = "CHAINDUMP"

# Edit these for your project (or keep as the OWLVRM defaults).
IK_RIGS = [
    "/Game/VTuber/Characters/VRM/IK_OWLVRM",
    "/Game/MetaHumans/MetaHumanCharacter/Body/IK_SKM_MetaHumanCharacter_BodyMesh",
]
RETARGETERS = [
    "/Game/VTuber/Characters/VRM/RTG_OWLVRM_to_MetaHuman",
]


def p(*args):
    print(MARK, *args)


def dump_rig(path):
    rig = unreal.load_asset(path)
    p("=== RIG:", path, "loaded:", bool(rig))
    if not rig:
        return []
    ctrl = unreal.IKRigController.get_controller(rig)
    p("retarget_root:", ctrl.get_retarget_root())
    chain_names = []
    for c in ctrl.get_retarget_chains():
        name = c.get_editor_property("chain_name")
        chain_names.append(str(name))
        start = ctrl.get_retarget_chain_start_bone(name)
        end = ctrl.get_retarget_chain_end_bone(name)
        p("CHAIN name=%s start=%s end=%s" % (name, start, end))
    return chain_names


def dump_rtg(path, target_chains):
    rtg = unreal.load_asset(path)
    p("=== RTG:", path, "loaded:", bool(rtg))
    if not rtg:
        return
    ctrl = unreal.IKRetargeterController.get_controller(rtg)
    p("source ik rig:", ctrl.get_ik_rig(unreal.RetargetSourceOrTarget.SOURCE))
    p("target ik rig (default):", ctrl.get_ik_rig(unreal.RetargetSourceOrTarget.TARGET))
    n = ctrl.get_num_retarget_ops()
    p("num ops:", n)
    for i in range(n):
        op_name = ctrl.get_op_name(i)
        rig = ctrl.get_target_ik_rig_for_op(op_name)
        p("OP", i, "name=%s enabled=%s target_rig=%s" % (
            op_name, ctrl.get_retarget_op_enabled(i), rig.get_path_name() if rig else "None"))
        for tc in target_chains:
            p("  MAP op=%s target=%s <- source=%s" % (op_name, tc, ctrl.get_source_chain(tc, op_name)))
        oc = ctrl.get_op_controller(i)
        if oc and hasattr(oc, "get_settings"):
            try:
                p("  SETTINGS:", oc.get_settings().export_text())
            except Exception as e:
                p("  SETTINGS ERROR:", e)


all_chains = []
for rig_path in IK_RIGS:
    all_chains = dump_rig(rig_path) or all_chains
for rtg_path in RETARGETERS:
    dump_rtg(rtg_path, all_chains)
p("DONE")
