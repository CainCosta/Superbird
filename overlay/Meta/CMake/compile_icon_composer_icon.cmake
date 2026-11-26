# Compiles an Icon Composer icon into an asset catalog and puts the catalog in the app bundle. actool can finish without
# writing the catalog (and writes why only to its output), so then the bundle keeps just its flat app_icon.icns, which
# macOS shows in place of the catalog's icon, and the build says what actool said.
#
# Arguments: ICON (the .icon document), CATALOG (a directory to compile into), DEPLOYMENT_TARGET, RESOURCES (the
# bundle's Contents/Resources).

file(MAKE_DIRECTORY "${CATALOG}")
file(REMOVE "${CATALOG}/Assets.car")

execute_process(
    COMMAND xcrun actool "${ICON}" --compile "${CATALOG}"
        --app-icon AppIcon --platform macosx --target-device mac
        --minimum-deployment-target "${DEPLOYMENT_TARGET}"
        --output-partial-info-plist "${CATALOG}/partial.plist"
        --errors --warnings --output-format human-readable-text
    OUTPUT_VARIABLE output
    ERROR_VARIABLE output
    RESULT_VARIABLE result
)

if (result EQUAL 0 AND EXISTS "${CATALOG}/Assets.car")
    file(COPY_FILE "${CATALOG}/Assets.car" "${RESOURCES}/Assets.car")
    return()
endif()

file(REMOVE "${RESOURCES}/Assets.car")
message(WARNING "actool made no asset catalog (exit ${result}), so the app bundle only gets the flat icon. actool said:\n${output}")
