include_guard()

# Build-time toggles for Superbird's pick-and-choose modules.
# Disable any module at configure time (e.g. -DSUPERBIRD_MODULE_TREE_TABS=OFF)
# to omit it from the build completely.
option(SUPERBIRD_MODULE_TREE_TABS "Build the Superbird Tree-Style Tabs module" ON)

if (SUPERBIRD_MODULE_TREE_TABS)
    add_compile_definitions(SUPERBIRD_MODULE_TREE_TABS=1)
endif()
