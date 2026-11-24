target_sources(ladybird_impl PRIVATE
    Superbird/Core/Action.h
    Superbird/Core/Module.h
    Superbird/Core/ModuleRegistry.cpp
    Superbird/Core/ModuleRegistry.h
    Superbird/Core/WindowHost.cpp
    Superbird/Core/WindowHost.h
)

if (SUPERBIRD_MODULE_TREE_TABS)
    target_sources(ladybird_impl PRIVATE
        Superbird/Modules/TreeTabs/TreeTabController.cpp
        Superbird/Modules/TreeTabs/TreeTabController.h
        Superbird/Modules/TreeTabs/TreeTabModel.cpp
        Superbird/Modules/TreeTabs/TreeTabModel.h
        Superbird/Modules/TreeTabs/TreeTabStore.cpp
        Superbird/Modules/TreeTabs/TreeTabStore.h
        Superbird/Modules/TreeTabs/TreeTabView.cpp
        Superbird/Modules/TreeTabs/TreeTabView.h
        Superbird/Modules/TreeTabs/TreeTabsModule.cpp
        Superbird/Modules/TreeTabs/TreeTabsModule.h
    )
endif()
