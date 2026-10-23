# Creates the (empty) L_Harbourfront map asset. The hotel itself is built at runtime by
# AKcHotel (Source/Keycard/Private/World/KcHotel.cpp), so this map only needs to exist.
# Run through Scripts\CreateMap.bat; safe to re-run (does nothing if the map exists).
import unreal

MAP_PATH = "/Game/Maps/L_Harbourfront"

if unreal.EditorAssetLibrary.does_asset_exist(MAP_PATH):
    unreal.log("KcCreateMap: {} already exists".format(MAP_PATH))
else:
    levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    if levels.new_level(MAP_PATH) and levels.save_current_level():
        unreal.log("KcCreateMap: created {}".format(MAP_PATH))
    else:
        unreal.log_error("KcCreateMap: FAILED to create {}".format(MAP_PATH))

unreal.SystemLibrary.quit_editor()
