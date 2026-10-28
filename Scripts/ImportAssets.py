"""Editor import step: turns Data/Assets/raw (+ edited/, generated/) into Content/Keycard/ assets.

Run headless through Scripts/ImportAssets.bat (UnrealEditor-Cmd -run=pythonscript). Idempotent: existing
assets are replaced. Writes Saved/KcImportReport.json listing every mesh and its material slots.
Creates:
  /Game/Keycard/Materials/M_KcWorld      world-aligned PBR (architecture; tiling never stretches)
  /Game/Keycard/Materials/M_KcSurface    UV PBR (imported models)
  /Game/Keycard/Materials/M_KcGlass      translucent glass
  /Game/Keycard/Materials/M_KcEmissive   light panels, LEDs, signs
  /Game/Keycard/Materials/M_KcScreen     self-lit displays (CCTV wall, terminals, fire panel)
  /Game/Keycard/Materials/M_KcSky        HDRI sky sphere (lat-long, unlit)
  /Game/Keycard/Materials/M_KcHighlight  focus overlay (fresnel)
  /Game/Keycard/Materials/Instances/MI_* one instance per surface and per model material slot
"""
import json
import os
import struct
import zlib

import unreal

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
RAW = os.path.join(ROOT, "Data", "Assets", "raw")
EDITED = os.path.join(ROOT, "Data", "Assets", "edited")
GAME = "/Game/Keycard"
MAT = GAME + "/Materials"
MI = MAT + "/Instances"
TOOLS = unreal.AssetToolsHelpers.get_asset_tools()
MEL = unreal.MaterialEditingLibrary
EAL = unreal.EditorAssetLibrary
report = {"meshes": {}, "materials": [], "errors": []}


def log(msg):
    unreal.log("KcImport: " + msg)


def err(msg):
    unreal.log_error("KcImport: " + msg)
    report["errors"].append(msg)


def source(rel):
    edited = os.path.join(EDITED, rel)
    return edited if os.path.exists(edited) else os.path.join(RAW, rel)


# ---------------------------------------------------------------- import helpers
def import_file(path, dest, name=None, options=None):
    task = unreal.AssetImportTask()
    task.set_editor_property("filename", path)
    task.set_editor_property("destination_path", dest)
    if name:
        task.set_editor_property("destination_name", name)
    task.set_editor_property("automated", True)
    task.set_editor_property("replace_existing", True)
    task.set_editor_property("save", True)
    if options is not None:
        task.set_editor_property("options", options)
    TOOLS.import_asset_tasks([task])
    paths = list(task.get_editor_property("imported_object_paths") or [])
    if not paths:
        err("nothing imported from " + path)
    return paths


def write_png(path, rgb):
    """4x4 solid-colour PNG without third-party modules."""
    raw = b"".join(b"\x00" + bytes(rgb) * 4 for _ in range(4))
    def chunk(tag, data):
        return struct.pack(">I", len(data)) + tag + data + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)
    png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", 4, 4, 8, 2, 0, 0, 0)) + chunk(b"IDAT", zlib.compress(raw)) + chunk(b"IEND", b"")
    with open(path, "wb") as f:
        f.write(png)


def setup_texture(asset_path, kind):
    tex = unreal.load_asset(asset_path)
    if kind == "normal":
        tex.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_NORMALMAP)
        tex.set_editor_property("srgb", False)
        tex.set_editor_property("lod_group", unreal.TextureGroup.TEXTUREGROUP_WORLD_NORMAL_MAP)
    elif kind == "mask":
        tex.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_MASKS)
        tex.set_editor_property("srgb", False)
    EAL.save_asset(asset_path, only_if_is_dirty=False)
    return tex


def import_texture(rel, dest):
    path = source(rel)
    name = "T_" + os.path.splitext(os.path.basename(path))[0]
    kind = "normal" if "_nor_dx" in name else "mask" if name.endswith(("_arm_1k", "_alpha_1k")) else "color"
    imported = import_file(path, dest, name)
    return setup_texture(dest + "/" + name, kind) if imported else None


# ---------------------------------------------------------------- material graph helpers
def new_material(name):
    path = MAT + "/" + name
    if EAL.does_asset_exist(path):
        # Reuse the asset (other assets may reference it) and rebuild its graph from scratch.
        m = unreal.load_asset(path)
        MEL.delete_all_material_expressions(m)
    else:
        m = TOOLS.create_asset(name, MAT, unreal.Material, unreal.MaterialFactoryNew())
    # The hotel draws repeated geometry as instanced static meshes; a cooked game silently falls
    # back to the default material for any material without this usage flag.
    m.set_editor_property("used_with_instanced_static_meshes", True)
    return m


def expr(m, cls, x, y, **props):
    e = MEL.create_material_expression(m, cls, x, y)
    for k, v in props.items():
        e.set_editor_property(k, v)
    return e


def link(a, a_out, b, b_in):
    if not MEL.connect_material_expressions(a, a_out, b, b_in):
        err("link failed %s.%s -> %s.%s" % (a.get_name(), a_out, b.get_name(), b_in))


def prop(e, out, p):
    if not MEL.connect_material_property(e, out, p):
        err("property link failed %s.%s" % (e.get_name(), out))


def finish(m):
    MEL.recompile_material(m)
    EAL.save_asset(m.get_path_name(), only_if_is_dirty=False)
    report["materials"].append(m.get_path_name())
    return m


def pbr_tail(m, base, normal_out, arm, x):
    """Common scalar/vector parameters and outputs: Tint, RoughnessScale, MetallicScale."""
    tint = expr(m, unreal.MaterialExpressionVectorParameter, x, -300, parameter_name="Tint", default_value=unreal.LinearColor(1, 1, 1, 1))
    mul = expr(m, unreal.MaterialExpressionMultiply, x + 250, -250)
    link(base[0], base[1], mul, "A")
    link(tint, "", mul, "B")
    prop(mul, "", unreal.MaterialProperty.MP_BASE_COLOR)
    prop(normal_out[0], normal_out[1], unreal.MaterialProperty.MP_NORMAL)
    for i, (pname, channel, mp, default) in enumerate([("RoughnessScale", "G", unreal.MaterialProperty.MP_ROUGHNESS, 1.0),
                                                       ("MetallicScale", "B", unreal.MaterialProperty.MP_METALLIC, 1.0),
                                                       ("AOScale", "R", unreal.MaterialProperty.MP_AMBIENT_OCCLUSION, 1.0)]):
        mask = expr(m, unreal.MaterialExpressionComponentMask, x, 50 + i * 150, r=channel == "R", g=channel == "G", b=channel == "B", a=False)
        link(arm[0], arm[1], mask, "")
        scale = expr(m, unreal.MaterialExpressionScalarParameter, x, 120 + i * 150, parameter_name=pname, default_value=default)
        out = expr(m, unreal.MaterialExpressionMultiply, x + 250, 80 + i * 150)
        link(mask, "", out, "A")
        link(scale, "", out, "B")
        prop(out, "", mp)


def build_surface(defaults):
    m = new_material("M_KcSurface")
    m.set_editor_property("used_with_skeletal_mesh", True)  # uniforms and the focus overlay on the cast
    uv = expr(m, unreal.MaterialExpressionTextureCoordinate, -1200, 0)
    uvs = expr(m, unreal.MaterialExpressionScalarParameter, -1200, 120, parameter_name="UVScale", default_value=1.0)
    uvm = expr(m, unreal.MaterialExpressionMultiply, -1000, 40)
    link(uv, "", uvm, "A")
    link(uvs, "", uvm, "B")
    samples = {}
    for i, (pname, tex, sampler) in enumerate([("BaseColorMap", defaults["color"], unreal.MaterialSamplerType.SAMPLERTYPE_COLOR),
                                               ("NormalMap", defaults["normal"], unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL),
                                               ("ARMMap", defaults["mask"], unreal.MaterialSamplerType.SAMPLERTYPE_MASKS)]):
        s = expr(m, unreal.MaterialExpressionTextureSampleParameter2D, -800, -300 + i * 300, parameter_name=pname, texture=tex, sampler_type=sampler)
        link(uvm, "", s, "UVs")
        samples[pname] = s
    pbr_tail(m, (samples["BaseColorMap"], "RGB"), (samples["NormalMap"], "RGB"), (samples["ARMMap"], "RGB"), -400)
    return finish(m)


def build_foliage(defaults):
    """Leaves: UV PBR like M_KcSurface, alpha-masked by the model's opacity map and two-sided."""
    m = new_material("M_KcFoliage")
    m.set_editor_property("blend_mode", unreal.BlendMode.BLEND_MASKED)
    m.set_editor_property("two_sided", True)
    samples = {}
    for i, (pname, tex, sampler) in enumerate([("BaseColorMap", defaults["color"], unreal.MaterialSamplerType.SAMPLERTYPE_COLOR),
                                               ("NormalMap", defaults["normal"], unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL),
                                               ("ARMMap", defaults["mask"], unreal.MaterialSamplerType.SAMPLERTYPE_MASKS),
                                               ("OpacityMap", defaults["mask"], unreal.MaterialSamplerType.SAMPLERTYPE_MASKS)]):
        samples[pname] = expr(m, unreal.MaterialExpressionTextureSampleParameter2D, -800, -300 + i * 300, parameter_name=pname, texture=tex, sampler_type=sampler)
    pbr_tail(m, (samples["BaseColorMap"], "RGB"), (samples["NormalMap"], "RGB"), (samples["ARMMap"], "RGB"), -400)
    prop(samples["OpacityMap"], "R", unreal.MaterialProperty.MP_OPACITY_MASK)
    return finish(m)


def build_world(defaults):
    """World-aligned colour and roughness; the surface's own normal (no normal map). The WorldAlignedNormal path
    left walls facing X black and the top of B1 walls dark (its normals tilted), and the bump is invisible at 2-4 m repeats."""
    m = new_material("M_KcWorld")
    wat = unreal.load_asset("/Engine/Functions/Engine_MaterialFunctions01/Texturing/WorldAlignedTexture.WorldAlignedTexture")
    size = expr(m, unreal.MaterialExpressionVectorParameter, -1400, 0, parameter_name="TileSize", default_value=unreal.LinearColor(200, 200, 200, 0))
    outs = {}
    for i, (pname, tex, sampler) in enumerate([("BaseColorMap", defaults["color"], unreal.MaterialSamplerType.SAMPLERTYPE_COLOR),
                                               ("ARMMap", defaults["mask"], unreal.MaterialSamplerType.SAMPLERTYPE_MASKS)]):
        obj = expr(m, unreal.MaterialExpressionTextureObjectParameter, -1200, -300 + i * 300, parameter_name=pname, texture=tex, sampler_type=sampler)
        call = expr(m, unreal.MaterialExpressionMaterialFunctionCall, -900, -300 + i * 300)
        call.set_editor_property("material_function", wat)
        link(obj, "", call, "TextureObject")
        link(size, "", call, "TextureSize")
        outs[pname] = call
    flat = expr(m, unreal.MaterialExpressionConstant3Vector, -900, 300, constant=unreal.LinearColor(0, 0, 1, 1))
    pbr_tail(m, (outs["BaseColorMap"], "XYZ Texture"), (flat, ""), (outs["ARMMap"], "XYZ Texture"), -500)
    return finish(m)


def build_glass():
    m = new_material("M_KcGlass")
    m.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
    m.set_editor_property("translucency_lighting_mode", unreal.TranslucencyLightingMode.TLM_VOLUMETRIC_NON_DIRECTIONAL)  # cheap: large facades
    m.set_editor_property("two_sided", True)
    tint = expr(m, unreal.MaterialExpressionVectorParameter, -600, -200, parameter_name="Tint", default_value=unreal.LinearColor(0.75, 0.85, 0.88, 1))
    prop(tint, "", unreal.MaterialProperty.MP_BASE_COLOR)
    for i, (pname, default, mp) in enumerate([("Opacity", 0.18, unreal.MaterialProperty.MP_OPACITY),
                                              ("Roughness", 0.05, unreal.MaterialProperty.MP_ROUGHNESS),
                                              ("Specular", 1.0, unreal.MaterialProperty.MP_SPECULAR)]):
        p = expr(m, unreal.MaterialExpressionScalarParameter, -600, i * 150, parameter_name=pname, default_value=default)
        prop(p, "", mp)
    return finish(m)


def build_emissive():
    m = new_material("M_KcEmissive")
    color = expr(m, unreal.MaterialExpressionVectorParameter, -700, 0, parameter_name="Color", default_value=unreal.LinearColor(1, 0.95, 0.88, 1))
    inten = expr(m, unreal.MaterialExpressionScalarParameter, -700, 200, parameter_name="Intensity", default_value=8.0)
    mul = expr(m, unreal.MaterialExpressionMultiply, -400, 50)
    link(color, "", mul, "A")
    link(inten, "", mul, "B")
    prop(mul, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    black = expr(m, unreal.MaterialExpressionConstant3Vector, -400, -150, constant=unreal.LinearColor(0.02, 0.02, 0.02, 1))
    prop(black, "", unreal.MaterialProperty.MP_BASE_COLOR)
    rough = expr(m, unreal.MaterialExpressionScalarParameter, -400, 250, parameter_name="Roughness", default_value=0.3)
    prop(rough, "", unreal.MaterialProperty.MP_ROUGHNESS)
    return finish(m)


def build_sky():
    """Unlit night dome on an inside-out sphere: a city glow at the horizon fading to a dark zenith, dark ground below.
    No photo: a photographed sky brings its own street, trees and lamp posts at its own height, and the windows
    are on every floor from G to 14. The skyline itself is geometry (M_KcSkyline). The HDRI only lights the scene."""
    m = new_material("M_KcSky")
    m.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    m.set_editor_property("two_sided", True)
    m.set_editor_property("is_sky", False)  # a plain unlit dome; IsSky needs full-screen coverage
    cam = expr(m, unreal.MaterialExpressionCameraVectorWS, -1300, 0)
    up = expr(m, unreal.MaterialExpressionComponentMask, -1100, 0, r=False, g=False, b=True, a=False)
    link(cam, "", up, "")
    neg = expr(m, unreal.MaterialExpressionMultiply, -900, 0)  # the camera vector points at the eye: flip it
    link(up, "", neg, "A")
    minus = expr(m, unreal.MaterialExpressionConstant, -1100, 150, r=-1.0)
    link(minus, "", neg, "B")
    # Sky: horizon glow to zenith over the first ~25 degrees.
    k = expr(m, unreal.MaterialExpressionConstant, -900, 150, r=2.5)
    steep = expr(m, unreal.MaterialExpressionMultiply, -700, 0)
    link(neg, "", steep, "A")
    link(k, "", steep, "B")
    t = expr(m, unreal.MaterialExpressionSaturate, -550, 0)
    link(steep, "", t, "")
    horizon = expr(m, unreal.MaterialExpressionVectorParameter, -550, -250, parameter_name="Horizon", default_value=unreal.LinearColor(0.1, 0.075, 0.07, 1))
    zenith = expr(m, unreal.MaterialExpressionVectorParameter, -550, -100, parameter_name="Zenith", default_value=unreal.LinearColor(0.004, 0.006, 0.014, 1))
    sky = expr(m, unreal.MaterialExpressionLinearInterpolate, -350, -150)
    link(horizon, "", sky, "A")
    link(zenith, "", sky, "B")
    link(t, "", sky, "Alpha")
    # Ground: below the horizon, beyond the modelled quay and streets.
    shift = expr(m, unreal.MaterialExpressionAdd, -700, 250)
    link(neg, "", shift, "A")
    edge = expr(m, unreal.MaterialExpressionConstant, -900, 300, r=0.02)
    link(edge, "", shift, "B")
    gain = expr(m, unreal.MaterialExpressionMultiply, -550, 250)
    link(shift, "", gain, "A")
    k2 = expr(m, unreal.MaterialExpressionConstant, -700, 400, r=25.0)
    link(k2, "", gain, "B")
    alpha = expr(m, unreal.MaterialExpressionSaturate, -400, 250)
    link(gain, "", alpha, "")
    ground = expr(m, unreal.MaterialExpressionVectorParameter, -400, 400, parameter_name="Ground", default_value=unreal.LinearColor(0.006, 0.007, 0.009, 1))
    mix = expr(m, unreal.MaterialExpressionLinearInterpolate, -200, 0)
    link(ground, "", mix, "A")
    link(sky, "", mix, "B")
    link(alpha, "", mix, "Alpha")
    inten = expr(m, unreal.MaterialExpressionScalarParameter, -200, 250, parameter_name="Intensity", default_value=1.0)
    mul = expr(m, unreal.MaterialExpressionMultiply, -50, 50)
    link(mix, "", mul, "A")
    link(inten, "", mul, "B")
    prop(mul, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    return finish(m)


def build_skyline():
    """The city round the harbour: drawn silhouettes with lit windows (T_Skyline*), unlit, cut out by the alpha."""
    m = new_material("M_KcSkyline")
    m.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    m.set_editor_property("blend_mode", unreal.BlendMode.BLEND_MASKED)
    m.set_editor_property("two_sided", True)
    tex = expr(m, unreal.MaterialExpressionTextureSampleParameter2D, -700, 0, parameter_name="SkylineTexture",
               sampler_type=unreal.MaterialSamplerType.SAMPLERTYPE_COLOR)
    inten = expr(m, unreal.MaterialExpressionScalarParameter, -700, 250, parameter_name="Intensity", default_value=4.0)
    mul = expr(m, unreal.MaterialExpressionMultiply, -400, 50)
    link(tex, "RGB", mul, "A")
    link(inten, "", mul, "B")
    prop(mul, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    prop(tex, "A", unreal.MaterialProperty.MP_OPACITY_MASK)
    return finish(m)


def build_screen(defaults):
    """Self-lit display: a texture shown as emissive light over a dark glossy base."""
    m = new_material("M_KcScreen")
    tex = expr(m, unreal.MaterialExpressionTextureSampleParameter2D, -700, 0, parameter_name="ScreenTexture", texture=defaults["color"],
               sampler_type=unreal.MaterialSamplerType.SAMPLERTYPE_COLOR)
    inten = expr(m, unreal.MaterialExpressionScalarParameter, -700, 250, parameter_name="Intensity", default_value=12.0)
    mul = expr(m, unreal.MaterialExpressionMultiply, -400, 50)
    link(tex, "RGB", mul, "A")
    link(inten, "", mul, "B")
    prop(mul, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    black = expr(m, unreal.MaterialExpressionConstant3Vector, -400, -150, constant=unreal.LinearColor(0.01, 0.01, 0.012, 1))
    prop(black, "", unreal.MaterialProperty.MP_BASE_COLOR)
    rough = expr(m, unreal.MaterialExpressionConstant, -400, 250, r=0.45)  # at 0.15 the ceiling lamps flared on the CCTV wall
    prop(rough, "", unreal.MaterialProperty.MP_ROUGHNESS)
    return finish(m)


def build_highlight():
    m = new_material("M_KcHighlight")
    m.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
    m.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    # The overlay also goes on people (skinned, with face shapes); without these the game draws the default material.
    m.set_editor_property("used_with_skeletal_mesh", True)
    m.set_editor_property("used_with_morph_targets", True)
    fres =expr(m, unreal.MaterialExpressionFresnel, -600, 100, exponent=2.5)
    color = expr(m, unreal.MaterialExpressionVectorParameter, -600, -150, parameter_name="Color", default_value=unreal.LinearColor(0.29, 0.61, 0.77, 1))  # harbour blue (KcUI Accent)
    em = expr(m, unreal.MaterialExpressionMultiply, -300, -100)
    link(color, "", em, "A")
    link(fres, "", em, "B")
    boost = expr(m, unreal.MaterialExpressionMultiply, -100, -100)
    k = expr(m, unreal.MaterialExpressionConstant, -300, 0, r=4.0)
    link(em, "", boost, "A")
    link(k, "", boost, "B")
    prop(boost, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    op = expr(m, unreal.MaterialExpressionMultiply, -300, 200)
    half = expr(m, unreal.MaterialExpressionScalarParameter, -500, 300, parameter_name="Opacity", default_value=0.65)
    link(fres, "", op, "A")
    link(half, "", op, "B")
    prop(op, "", unreal.MaterialProperty.MP_OPACITY)
    return finish(m)


# ---------------------------------------------------------------- instances
def instance(name, parent, textures=None, scalars=None, vectors=None):
    path = MI + "/" + name
    if EAL.does_asset_exist(path):
        mi = unreal.load_asset(path)
        MEL.clear_all_material_instance_parameters(mi)
    else:
        mi = TOOLS.create_asset(name, MI, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
    MEL.set_material_instance_parent(mi, parent)
    for k, v in (textures or {}).items():
        MEL.set_material_instance_texture_parameter_value(mi, k, v)
    for k, v in (scalars or {}).items():
        MEL.set_material_instance_scalar_parameter_value(mi, k, v)
    for k, v in (vectors or {}).items():
        MEL.set_material_instance_vector_parameter_value(mi, k, unreal.LinearColor(*v))
    MEL.update_material_instance(mi)
    EAL.save_asset(path, only_if_is_dirty=False)
    return mi


def texture_set(asset_id, part=""):
    """Imports diff / nor_dx / arm for an asset (or one part of a model) and returns the parameter map."""
    dest = GAME + "/Textures/" + asset_id
    prefix = asset_id + ("_" + part if part else "")
    out = {}
    for key, pname in [("diff", "BaseColorMap"), ("nor_dx", "NormalMap"), ("arm", "ARMMap"), ("alpha", "OpacityMap")]:
        rel = "polyhaven/%s/%s_%s_1k.jpg" % (asset_id, prefix, key)
        if not os.path.exists(source(rel)):
            rel = rel[:-4] + ".png"
        if os.path.exists(source(rel)):
            tex = import_texture(rel, dest)
            if tex:
                out[pname] = tex
    return out


def import_model(asset_id):
    dest = GAME + "/Models/" + asset_id
    opts = unreal.FbxImportUI()
    opts.set_editor_property("import_mesh", True)
    opts.set_editor_property("import_materials", False)
    opts.set_editor_property("import_textures", False)
    opts.set_editor_property("import_as_skeletal", False)
    sm = opts.get_editor_property("static_mesh_import_data")
    sm.set_editor_property("combine_meshes", True)
    sm.set_editor_property("auto_generate_collision", True)
    sm.set_editor_property("generate_lightmap_u_vs", False)
    fbx = source("polyhaven/%s/%s_1k.fbx" % (asset_id, asset_id))
    if not os.path.exists(fbx):
        err("model %s not downloaded (run Scripts/FetchAssets.py); the hotel uses its stand-in" % asset_id)
        return
    paths = import_file(fbx, dest, "SM_" + asset_id, opts)
    meshes = [p for p in paths if isinstance(unreal.load_asset(p), unreal.StaticMesh)]
    if not meshes:
        err("no static mesh for " + asset_id)
        return
    mesh = unreal.load_asset(meshes[0])
    parts = sorted({f[len(asset_id) + 1:-len("_diff_1k.jpg")] for f in os.listdir(os.path.join(RAW, "polyhaven", asset_id))
                    if f.endswith("_diff_1k.jpg") and f != asset_id + "_diff_1k.jpg"})
    slots = []
    for i, mat in enumerate(mesh.get_editor_property("static_materials")):
        slot = str(mat.get_editor_property("material_slot_name"))
        part = next((p for p in parts if p.lower() in slot.lower()), "")
        textures = texture_set(asset_id, part)
        # Leaf cards come with an opacity map: masked and two-sided, or they render as solid blobs.
        mi = instance("MI_%s%s" % (asset_id, "_" + part if part else ""), FOLIAGE if "OpacityMap" in textures else SURFACE, textures)
        mesh.set_material(i, mi)
        slots.append({"slot": slot, "part": part, "material": mi.get_path_name(), "textures": len(textures)})
        if not textures:
            err("no textures matched slot %s of %s" % (slot, asset_id))
    mesh.set_editor_property("nanite_settings", unreal.MeshNaniteSettings(enabled=False))
    EAL.save_asset(mesh.get_path_name(), only_if_is_dirty=False)
    bounds = mesh.get_bounding_box()
    report["meshes"][asset_id] = {"path": mesh.get_path_name(), "slots": slots,
                                  "size_cm": [round(bounds.max.x - bounds.min.x, 1), round(bounds.max.y - bounds.min.y, 1), round(bounds.max.z - bounds.min.z, 1)],
                                  "min_z": round(bounds.min.z, 1)}
    log("model %s: %d slots" % (asset_id, len(slots)))


# ---------------------------------------------------------------- main
def manifest_ids():
    """Slot -> Poly Haven id, from Data/AssetManifest.json (FetchAssets.py picks the first candidate that exists)."""
    path = os.path.join(ROOT, "Data", "AssetManifest.json")
    if not os.path.exists(path):
        err("Data/AssetManifest.json missing: run python Scripts/FetchAssets.py --resolve; surfaces fall back to flat tints")
        return {}
    return {a.get("slot", a["id"]): a["id"] for a in json.load(open(path, encoding="utf-8"))["assets"]}


unreal.SystemLibrary.execute_console_command(None, "Interchange.FeatureFlags.Import.FBX False")
IDS = manifest_ids()
tmp = os.path.join(ROOT, "Saved", "KcImportTmp")
os.makedirs(tmp, exist_ok=True)
defaults = {}
for kind, name, rgb in [("color", "T_KcWhite", (255, 255, 255)), ("mask", "T_KcDefaultARM", (255, 140, 0)), ("normal", "T_KcFlatNormal", (128, 128, 255))]:
    write_png(os.path.join(tmp, name + ".png"), rgb)
    import_file(os.path.join(tmp, name + ".png"), GAME + "/Textures/Defaults", name)
    defaults[kind] = setup_texture(GAME + "/Textures/Defaults/" + name, kind)

hdri = None
if "night_sky" in IDS:
    sky_id = IDS["night_sky"]
    hdr_paths = import_file(os.path.join(RAW, "polyhaven", sky_id, sky_id + "_2k.hdr"), GAME + "/HDRI", "T_NightCity")
    hdri = unreal.load_asset(hdr_paths[0]) if hdr_paths else None
log("HDRI imported as %s" % (hdri.get_class().get_name() if hdri else "nothing"))

SURFACE = build_surface(defaults)
WORLD = build_world(defaults)
FOLIAGE = build_foliage(defaults)
GLASS = build_glass()
EMISSIVE = build_emissive()
build_highlight()
build_sky()
if not (hdri and isinstance(hdri, unreal.TextureCube)):
    err("night HDRI is not a TextureCube; the sky light falls back to the dome's colour")


def world_surface(name, slot, tile, tint, rough, metal=0.0):
    """World-aligned architecture surface. TileSize is the real size of one texture repeat in cm.
    Without the textures (asset not fetched) the instance still exists as a flat tint, so the hotel never shows the default grid."""
    textures = texture_set(IDS[slot]) if slot in IDS else {}
    if slot and not textures:
        err("surface %s: no textures for slot %s, flat tint used" % (name, slot))
    parent = WORLD if textures else SURFACE
    vectors = {"Tint": tint + (1,)}
    if textures:
        vectors["TileSize"] = (tile, tile, tile, 0)
    instance(name, parent, textures, {"MetallicScale": metal, "RoughnessScale": rough}, vectors)
    log("surface " + name)


# Architecture. Tints are chosen so untextured fallbacks still read as the right material.
for name, slot, tile, tint, rough in [
        ("MI_FloorParquet", "parquet", 160, (1, 1, 1), 2.4),               # LP office, manager's office
        ("MI_FloorMarble", "marble", 240, (0.95, 0.94, 0.92), 0.6),        # lobby
        ("MI_StoneCounter", "marble", 120, (0.22, 0.22, 0.24), 0.9),       # front desk and bar tops: dark stone (0.5 sparkled)
        ("MI_FloorTile", "tiles", 60, (0.93, 0.93, 0.92), 0.8),            # guest bathrooms, staff toilets
        ("MI_FloorConcrete", "concrete_floor", 300, (0.8, 0.8, 0.8), 1.3), # B1 back of house, loading dock
        ("MI_Concrete", "concrete_wall", 280, (0.75, 0.75, 0.74), 1.4),    # stair shafts, B1 walls
        ("MI_Facade", "concrete_wall", 400, (0.55, 0.56, 0.58), 1.2),      # exterior
        ("MI_Asphalt", "asphalt", 400, (0.6, 0.6, 0.6), 1.4),              # street and garage ramp
        ("MI_Paving", "paving", 200, (0.8, 0.79, 0.76), 1.3),              # Quay Promenade
        ("MI_Carpet", "wool", 45, (0.3, 0.32, 0.34), 2.0),                 # LP office, CCTV room
        ("MI_RoomCarpet", "wool", 40, (0.42, 0.38, 0.33), 2.0),            # guest rooms
        ("MI_Rug", "wool", 35, (0.52, 0.48, 0.42), 2.0),
        ("MI_FabricDark", "wool", 30, (0.12, 0.13, 0.15), 2.0),
        ("MI_FabricAccent", "wool", 30, (0.18, 0.36, 0.48), 2.0),          # harbour-blue upholstery
        ("MI_AcousticPanel", "wool", 60, (0.4, 0.46, 0.52), 1.0),         # at 0.24-0.36 the CCTV room walls read black
        ("MI_Linen", "linen", 40, (0.95, 0.94, 0.91), 1.8),                # bed linen, towels
        ("MI_Leather", "leather", 50, (0.2, 0.12, 0.08), 0.9),             # bar stools, lobby chairs
        ("MI_WallPlaster", "plaster", 260, (0.93, 0.91, 0.88), 1.0),
        ("MI_WallPaint", "plaster", 260, (0.82, 0.83, 0.82), 1.1),         # back-of-house corridors
        ("MI_StairPaint", "plaster", 260, (0.7, 0.72, 0.7), 1.1),
        ("MI_WoodOak", "oak", 140, (0.8, 0.75, 0.7), 1.5),                   # less orange; satin: at 0.9 the glossy grain lines streaked white
        ("MI_WoodWalnut", "walnut", 140, (0.6, 0.48, 0.42), 0.85),          # untinted it read orange pine
        ("MI_DoorPaint", "oak", 140, (0.42, 0.33, 0.24), 0.8),             # guest-room doors: stained oak
        ("MI_Cork", "wool", 25, (0.62, 0.46, 0.3), 1.8),                   # LP office notice board
        ("MI_FloorVinyl", "concrete_floor", 120, (0.42, 0.45, 0.48), 0.7)]:  # housekeeping, fire command: sheet vinyl, not guest carpet
    world_surface(name, slot, tile, tint, rough)

# Untextured surfaces: defaults are white base, ARM = (AO 1, rough 0.55, metal 0) scaled below.
for name, tint, rough, metal in [
        ("MI_Graphite", (0.06, 0.065, 0.075), 0.75, 1.0), ("MI_Chrome", (0.8, 0.8, 0.82), 0.3, 1.0),
        ("MI_Steel", (0.55, 0.56, 0.58), 0.5, 1.0), ("MI_SteelDark", (0.2, 0.2, 0.21), 0.7, 1.0), ("MI_Brass", (0.75, 0.55, 0.3), 0.35, 1.0),
        ("MI_Mirror", (0.62, 0.64, 0.66), 0.05, 1.0),  # at 0.95 it read as a blank white panel (walk 14)
        ("MI_CeilingWhite", (0.86, 0.86, 0.85), 1.6, 0.0), ("MI_Skirting", (0.12, 0.12, 0.13), 1.2, 0.0),
        ("MI_DeviceBlack", (0.02, 0.02, 0.025), 0.6, 0.0), ("MI_PlasticGrey", (0.35, 0.36, 0.38), 0.8, 0.0),
        ("MI_Paper", (0.9, 0.9, 0.88), 1.7, 0.0), ("MI_Ceramic", (0.92, 0.92, 0.9), 0.35, 0.0),
        ("MI_Cardboard", (0.55, 0.42, 0.28), 1.7, 0.0), ("MI_FireRed", (0.6, 0.04, 0.03), 0.6, 0.0),
        ("MI_SafetyYellow", (0.85, 0.65, 0.05), 0.8, 0.0), ("MI_Leaf", (0.12, 0.28, 0.1), 1.2, 0.0),
        ("MI_Blossom", (0.85, 0.7, 0.75), 1.4, 0.0), ("MI_BottleAmber", (0.35, 0.16, 0.03), 0.15, 0.0),
        ("MI_BottleGreen", (0.05, 0.2, 0.08), 0.15, 0.0),
        # Uniforms: LP officers in black, engineering in grey, managers in navy suits.
        ("MI_UniformBlack", (0.025, 0.025, 0.03), 1.5, 0.0), ("MI_UniformGrey", (0.22, 0.23, 0.25), 1.5, 0.0),
        ("MI_SuitNavy", (0.04, 0.06, 0.12), 1.4, 0.0)]:
    instance(name, SURFACE, {}, {"RoughnessScale": rough / 0.55, "MetallicScale": metal}, {"Tint": tint + (1,)})
instance("MI_Glass", GLASS)
instance("MI_GlassDark", GLASS, {}, {"Opacity": 0.45, "Roughness": 0.04}, {"Tint": (0.1, 0.12, 0.14, 1)})        # CCTV room window, lift doors' vision panels
instance("MI_Sheer", GLASS, {}, {"Opacity": 0.4, "Roughness": 0.7, "Specular": 0.2}, {"Tint": (0.95, 0.94, 0.9, 1)})  # guest-room sheer curtains
instance("MI_Water", GLASS, {}, {"Opacity": 0.7, "Roughness": 0.02}, {"Tint": (0.04, 0.12, 0.18, 1)})             # harbour
instance("MI_LightPanel", EMISSIVE, {}, {"Intensity": 60.0}, {"Color": (1, 0.97, 0.92, 1)})
# Lamp shades and pendant drums: at 45 the shades and the ballroom drums blew out to white (walk 12).
instance("MI_LampWarm", EMISSIVE, {}, {"Intensity": 22.0}, {"Color": (1.0, 0.78, 0.5, 1)})
instance("MI_LedStrip", EMISSIVE, {}, {"Intensity": 70.0}, {"Color": (0.95, 0.97, 1.0, 1)})
instance("MI_SignGlow", EMISSIVE, {}, {"Intensity": 30.0}, {"Color": (0.29, 0.61, 0.77, 1)})  # hotel name and wayfinding
instance("MI_LedGreen", EMISSIVE, {}, {"Intensity": 25.0}, {"Color": (0.1, 0.9, 0.25, 1)})
instance("MI_LedRed", EMISSIVE, {}, {"Intensity": 25.0}, {"Color": (0.95, 0.08, 0.05, 1)})
instance("MI_LedAmber", EMISSIVE, {}, {"Intensity": 25.0}, {"Color": (1.0, 0.55, 0.05, 1)})
# Back-of-house ceilings: faintly self-lit so they read as bounce-lit white above the down-facing panels, not black;
# fully rough, or the downlights streak across them like a mirror.
instance("MI_CeilingSoft", EMISSIVE, {}, {"Intensity": 6.0, "Roughness": 1.0}, {"Color": (0.88, 0.89, 0.9, 1)})
# CCTV room: kept dark for the screens; at 6 the exposure lifted the ceiling to a glowing white slab.
instance("MI_CeilingDim", EMISSIVE, {}, {"Intensity": 0.8, "Roughness": 1.0}, {"Color": (0.88, 0.89, 0.9, 1)})
# B1 work rooms and corridor: between the two; at 0.8 the ceilings read as a black band over the walls.
instance("MI_CeilingLow", EMISSIVE, {}, {"Intensity": 1.4, "Roughness": 1.0}, {"Color": (0.88, 0.89, 0.9, 1)})  # housekeeping, under battens
instance("MI_CeilingMid", EMISSIVE, {}, {"Intensity": 2.5, "Roughness": 1.0}, {"Color": (0.88, 0.89, 0.9, 1)})
instance("MI_Highlight", unreal.load_asset(MAT + "/M_KcHighlight"))
# People: a faint rim only. At full strength the whole body glowed blue and the open mouth and teeth lit up.
instance("MI_HighlightPerson", unreal.load_asset(MAT + "/M_KcHighlight"), {}, {"Opacity": 0.2}, {"Color": (0.12, 0.25, 0.32, 1)})

# Our own textures (Scripts/GenerateTextures.py): screen UIs, signs, whiteboard, prints, carpet and wallpaper patterns.
GEN = os.path.join(ROOT, "Data", "Assets", "generated")
gen = {}
for f in sorted(os.listdir(GEN)) if os.path.isdir(GEN) else []:
    if f.endswith(".png"):
        name = os.path.splitext(f)[0]
        import_file(os.path.join(GEN, f), GAME + "/Textures/Generated", name)
        gen[name] = setup_texture(GAME + "/Textures/Generated/" + name, "color")
        if name.startswith(("T_Screen", "T_FloorPlan", "T_Whiteboard", "T_DndTag")):  # text must stay legible: keep every mip resident
            gen[name].set_editor_property("never_stream", True)
            EAL.save_asset(gen[name].get_path_name(), only_if_is_dirty=False)
if not gen:
    err("no generated textures: run python Scripts/GenerateTextures.py first")
screen = build_screen(defaults)
# The CCTV pages at 10 drove the exposure down until the room round the video wall went black (walk 12).
for mi, tex, inten in [("MI_ScreenCctv1", "T_ScreenCctv1", 6.0), ("MI_ScreenCctv2", "T_ScreenCctv2", 6.0),
                       ("MI_ScreenCctv3", "T_ScreenCctv3", 6.0), ("MI_ScreenCctv4", "T_ScreenCctv4", 6.0),
                       ("MI_ScreenShiftBoard", "T_ScreenShiftBoard", 12.0), ("MI_ScreenFrontOffice", "T_ScreenFrontOffice", 5.0),
                       ("MI_ScreenReport", "T_ScreenReport", 9.0), ("MI_ScreenExport", "T_ScreenExport", 12.0),
                       ("MI_ScreenPanel", "T_ScreenPanel", 8.0), ("MI_ExitSign", "T_ExitSign", 20.0)]:
    if tex in gen:
        instance(mi, screen, {"ScreenTexture": gen[tex]}, {"Intensity": inten})
for mi, tex, rough in [("MI_Whiteboard", "T_Whiteboard", 0.5), ("MI_ArtHarbourA", "T_ArtHarbourA", 1.5), ("MI_ArtHarbourB", "T_ArtHarbourB", 1.5),
                       ("MI_ArtHarbourC", "T_ArtHarbourC", 1.5), ("MI_DndTag", "T_DndTag", 1.2), ("MI_FloorPlan", "T_FloorPlan", 0.6)]:
    if tex in gen:
        instance(mi, SURFACE, {"BaseColorMap": gen[tex]}, {"RoughnessScale": rough, "MetallicScale": 0.0})
# Patterned guest-floor carpet, corridor wallpaper and the kitchen quarry tile: world-aligned so the repeat stays true to scale on every wall.
for mi, tex, tile, rough in [("MI_CorridorCarpet", "T_CorridorCarpet", 120, 2.0), ("MI_Wallpaper", "T_Wallpaper", 90, 1.3),
                             ("MI_KitchenTile", "T_QuarryTile", 60, 1.6), ("MI_Tile", "T_WallTile", 60, 0.5),  # MI_Tile: bathroom and kitchen walls
                             ("MI_BallroomCarpet", "T_BallroomCarpet", 180, 2.0)]:
    if tex in gen:
        instance(mi, WORLD, {"BaseColorMap": gen[tex]}, {"MetallicScale": 0.0, "RoughnessScale": rough},
                 {"Tint": (1, 1, 1, 1), "TileSize": (tile, tile, tile, 0)})
instance("MI_Sky", unreal.load_asset(MAT + "/M_KcSky"), {}, {"Intensity": 1.0})
skyline = build_skyline()
for side in ["FarN", "FarS", "FarW", "FarE", "NearS", "NearW", "NearE"]:
    tex = "T_Skyline" + side
    if tex in gen:
        instance("MI_Skyline" + side, skyline, {"SkylineTexture": gen[tex]}, {"Intensity": 3.0})
    else:
        err("%s missing: run python Scripts/GenerateTextures.py" % tex)
# The buildings across the street (KcHotel BuildOutside), dimmer than the far towers: they are close.
for side in ["W", "S", "E"]:
    tex = "T_Street" + side
    if tex in gen:
        instance("MI_Street" + side, skyline, {"SkylineTexture": gen[tex]}, {"Intensity": 2.2})  # at 1.4 the street was too dim to read through the lobby glass (walk 14)
    else:
        err("%s missing: run python Scripts/GenerateTextures.py" % tex)

for asset_id in ["Chandelier_03", "bar_chair_round_01", "modern_coffee_table_01", "sofa_02", "modern_arm_chair_01",
                 "dining_chair_02", "potted_plant_02", "potted_plant_04"]:
    try:
        import_model(asset_id)
    except Exception as e:  # keep going so one bad asset does not block the rest
        err("model %s failed: %s" % (asset_id, e))

with open(os.path.join(ROOT, "Saved", "KcImportReport.json"), "w", encoding="utf-8") as f:
    json.dump(report, f, indent=1)
log("done: %d meshes, %d materials, %d errors (audio: Scripts/ImportAudio.py)" % (len(report["meshes"]), len(report["materials"]), len(report["errors"])))
