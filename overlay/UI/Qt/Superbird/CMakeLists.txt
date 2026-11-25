target_sources(ladybird_impl PRIVATE
    Superbird/Core/Action.h
    Superbird/Core/Module.h
    Superbird/Core/ModuleRegistry.cpp
    Superbird/Core/ModuleRegistry.h
    Superbird/Core/WindowHost.cpp
    Superbird/Core/WindowHost.h
)

if (SUPERBIRD_MODULE_TREE_TABS)
    # PUBLIC: UI/Qt/Application.h depends on it, so everything that links ladybird_impl must agree.
    target_compile_definitions(ladybird_impl PUBLIC SUPERBIRD_MODULE_TREE_TABS=1)
    target_sources(ladybird_impl PRIVATE
        Superbird/Modules/TreeTabs/TreeTabController.cpp
        Superbird/Modules/TreeTabs/TreeTabController.h
        Superbird/Modules/TreeTabs/TreeTabModel.cpp
        Superbird/Modules/TreeTabs/TreeTabModel.h
        Superbird/Modules/TreeTabs/TreeTabStore.cpp
        Superbird/Modules/TreeTabs/TreeTabStore.h
        Superbird/Modules/TreeTabs/TreeTabView.cpp
        Superbird/Modules/TreeTabs/TreeTabView.h
        Superbird/Modules/TreeTabs/TreeTabsActions.cpp
        Superbird/Modules/TreeTabs/TreeTabsActions.h
        Superbird/Modules/TreeTabs/TreeTabsModule.cpp
        Superbird/Modules/TreeTabs/TreeTabsModule.h
    )
endif()

if (SUPERBIRD_MODULE_COMMAND_PALETTE)
    target_compile_definitions(ladybird_impl PUBLIC SUPERBIRD_MODULE_COMMAND_PALETTE=1)
    target_sources(ladybird_impl PRIVATE
        Superbird/Modules/CommandPalette/CommandPaletteActions.cpp
        Superbird/Modules/CommandPalette/CommandPaletteActions.h
        Superbird/Modules/CommandPalette/CommandPaletteModule.cpp
        Superbird/Modules/CommandPalette/CommandPaletteModule.h
        Superbird/Modules/CommandPalette/CommandPalettePopover.cpp
        Superbird/Modules/CommandPalette/CommandPalettePopover.h
        Superbird/Modules/CommandPalette/FuzzyMatcher.cpp
        Superbird/Modules/CommandPalette/FuzzyMatcher.h
        Superbird/Modules/CommandPalette/MathExpression.cpp
        Superbird/Modules/CommandPalette/MathExpression.h
        Superbird/Modules/CommandPalette/PaletteRanking.cpp
        Superbird/Modules/CommandPalette/PaletteRanking.h
    )
endif()
