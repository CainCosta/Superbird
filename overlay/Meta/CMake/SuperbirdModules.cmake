include_guard()

# Build-time toggles for Superbird's pick-and-choose modules.
# Disable any module at configure time (e.g. -DSUPERBIRD_MODULE_TREE_TABS=OFF)
# to omit it from the build completely. UI/Qt/Superbird/CMakeLists.txt turns each
# enabled option into a SUPERBIRD_MODULE_<NAME> compile definition on ladybird_impl.
option(SUPERBIRD_MODULE_TREE_TABS "Build the Superbird Tree-Style Tabs module" ON)
option(SUPERBIRD_MODULE_COMMAND_PALETTE "Build the Superbird Command Palette module" ON)
option(SUPERBIRD_MODULE_READER_VIEW "Build the Superbird Reader View module" ON)
option(SUPERBIRD_MODULE_WORKSPACES "Build the Superbird Workspaces module" ON)
option(SUPERBIRD_MODULE_SPLIT_VIEW "Build the Superbird Split View module" ON)
option(SUPERBIRD_MODULE_GLANCE "Build the Superbird Glance module" ON)
