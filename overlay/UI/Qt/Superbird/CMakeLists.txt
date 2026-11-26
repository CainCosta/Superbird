target_sources(ladybird_impl PRIVATE
    Superbird/Core/Action.h
    Superbird/Core/Confirm.cpp
    Superbird/Core/Confirm.h
    Superbird/Core/Module.h
    Superbird/Core/ModuleRegistry.cpp
    Superbird/Core/ModuleSettings.cpp
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
        Superbird/Modules/TreeTabs/TreeTabFilter.cpp
        Superbird/Modules/TreeTabs/TreeTabFilter.h
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

if (SUPERBIRD_MODULE_READER_VIEW)
    target_compile_definitions(ladybird_impl PUBLIC SUPERBIRD_MODULE_READER_VIEW=1)
    target_sources(ladybird_impl PRIVATE
        Superbird/Modules/ReaderView/ArticleExtractor.cpp
        Superbird/Modules/ReaderView/ArticleExtractor.h
        Superbird/Modules/ReaderView/HtmlTree.cpp
        Superbird/Modules/ReaderView/HtmlTree.h
        Superbird/Modules/ReaderView/ReaderDocument.cpp
        Superbird/Modules/ReaderView/ReaderDocument.h
        Superbird/Modules/ReaderView/ReaderOverlay.cpp
        Superbird/Modules/ReaderView/ReaderOverlay.h
        Superbird/Modules/ReaderView/ReaderViewActions.cpp
        Superbird/Modules/ReaderView/ReaderViewActions.h
        Superbird/Modules/ReaderView/ReaderViewModule.cpp
        Superbird/Modules/ReaderView/ReaderViewModule.h
        Superbird/Modules/ReaderView/UrlResolver.cpp
        Superbird/Modules/ReaderView/UrlResolver.h
    )
endif()

if (SUPERBIRD_MODULE_WORKSPACES)
    target_compile_definitions(ladybird_impl PUBLIC SUPERBIRD_MODULE_WORKSPACES=1)
    target_sources(ladybird_impl PRIVATE
        Superbird/Modules/Workspaces/WorkspaceModel.cpp
        Superbird/Modules/Workspaces/WorkspaceModel.h
        Superbird/Modules/Workspaces/WorkspaceGlyphs.cpp
        Superbird/Modules/Workspaces/WorkspaceGlyphs.h
        Superbird/Modules/Workspaces/WorkspacePicker.cpp
        Superbird/Modules/Workspaces/WorkspacePicker.h
        Superbird/Modules/Workspaces/WorkspacePresets.cpp
        Superbird/Modules/Workspaces/WorkspacePresets.h
        Superbird/Modules/Workspaces/WorkspaceStore.cpp
        Superbird/Modules/Workspaces/WorkspaceStore.h
        Superbird/Modules/Workspaces/WorkspaceSwitcher.cpp
        Superbird/Modules/Workspaces/WorkspaceSwitcher.h
        Superbird/Modules/Workspaces/WorkspacesActions.cpp
        Superbird/Modules/Workspaces/WorkspacesActions.h
        Superbird/Modules/Workspaces/WorkspacesModule.cpp
        Superbird/Modules/Workspaces/WorkspacesModule.h
    )
endif()

if (SUPERBIRD_MODULE_SPLIT_VIEW)
    target_compile_definitions(ladybird_impl PUBLIC SUPERBIRD_MODULE_SPLIT_VIEW=1)
    target_sources(ladybird_impl PRIVATE
        Superbird/Modules/SplitView/SplitLayout.cpp
        Superbird/Modules/SplitView/SplitLayout.h
        Superbird/Modules/SplitView/SplitStore.cpp
        Superbird/Modules/SplitView/SplitStore.h
        Superbird/Modules/SplitView/SplitViewActions.cpp
        Superbird/Modules/SplitView/SplitViewActions.h
        Superbird/Modules/SplitView/SplitViewHost.cpp
        Superbird/Modules/SplitView/SplitViewHost.h
        Superbird/Modules/SplitView/SplitViewModule.cpp
        Superbird/Modules/SplitView/SplitViewModule.h
    )
endif()

if (SUPERBIRD_MODULE_GLANCE)
    target_compile_definitions(ladybird_impl PUBLIC SUPERBIRD_MODULE_GLANCE=1)
    target_sources(ladybird_impl PRIVATE
        Superbird/Modules/Glance/GlanceActions.cpp
        Superbird/Modules/Glance/GlanceActions.h
        Superbird/Modules/Glance/GlanceModel.cpp
        Superbird/Modules/Glance/GlanceModel.h
        Superbird/Modules/Glance/GlanceModule.cpp
        Superbird/Modules/Glance/GlanceModule.h
        Superbird/Modules/Glance/GlanceOverlay.cpp
        Superbird/Modules/Glance/GlanceOverlay.h
    )
endif()

if (SUPERBIRD_MODULE_CONTENT_BLOCKING)
    target_compile_definitions(ladybird_impl PUBLIC SUPERBIRD_MODULE_CONTENT_BLOCKING=1)
    target_sources(ladybird_impl PRIVATE
        Superbird/Modules/ContentBlocking/ContentBlockingActions.cpp
        Superbird/Modules/ContentBlocking/ContentBlockingActions.h
        Superbird/Modules/ContentBlocking/ContentBlockingModule.cpp
        Superbird/Modules/ContentBlocking/ContentBlockingModule.h
    )
endif()

if (SUPERBIRD_MODULE_PASSWORDS)
    target_compile_definitions(ladybird_impl PUBLIC SUPERBIRD_MODULE_PASSWORDS=1)
    find_package(CURL REQUIRED)
    target_sources(ladybird_impl PRIVATE
        Superbird/Modules/Passwords/Core/BitwardenCrypto.cpp
        Superbird/Modules/Passwords/Core/BitwardenCrypto.h
        Superbird/Modules/Passwords/Core/BitwardenProtocol.cpp
        Superbird/Modules/Passwords/Core/BitwardenProtocol.h
        Superbird/Modules/Passwords/Core/CsvTransfer.cpp
        Superbird/Modules/Passwords/Core/CsvTransfer.h
        Superbird/Modules/Passwords/Core/LocalVaultCrypto.cpp
        Superbird/Modules/Passwords/Core/LocalVaultCrypto.h
        Superbird/Modules/Passwords/Core/OnePasswordParser.cpp
        Superbird/Modules/Passwords/Core/OnePasswordParser.h
        Superbird/Modules/Passwords/Core/PasswordGenerator.cpp
        Superbird/Modules/Passwords/Core/PasswordGenerator.h
        Superbird/Modules/Passwords/Core/PasswordProvider.h
        Superbird/Modules/Passwords/Core/PasswordsActions.cpp
        Superbird/Modules/Passwords/Core/PasswordsActions.h
        Superbird/Modules/Passwords/Core/Totp.cpp
        Superbird/Modules/Passwords/Core/Totp.h
        Superbird/Modules/Passwords/Core/UriMatcher.cpp
        Superbird/Modules/Passwords/Core/UriMatcher.h
        Superbird/Modules/Passwords/Keyring/Keyring.h
        Superbird/Modules/Passwords/PasswordManagerService.cpp
        Superbird/Modules/Passwords/PasswordManagerService.h
        Superbird/Modules/Passwords/PasswordsModule.cpp
        Superbird/Modules/Passwords/PasswordsModule.h
        Superbird/Modules/Passwords/Providers/BitwardenProvider.cpp
        Superbird/Modules/Passwords/Providers/BitwardenProvider.h
        Superbird/Modules/Passwords/Providers/LocalVaultProvider.cpp
        Superbird/Modules/Passwords/Providers/LocalVaultProvider.h
        Superbird/Modules/Passwords/Providers/OnePasswordProvider.cpp
        Superbird/Modules/Passwords/Providers/OnePasswordProvider.h
        Superbird/Modules/Passwords/Storage/PasswordsStore.cpp
        Superbird/Modules/Passwords/Storage/PasswordsStore.h
        Superbird/Modules/Passwords/UI/InlineAutofillPopover.cpp
        Superbird/Modules/Passwords/UI/InlineAutofillPopover.h
        Superbird/Modules/Passwords/UI/PasswordPopover.cpp
        Superbird/Modules/Passwords/UI/PasswordPopover.h
        Superbird/Modules/Passwords/UI/ProviderSetupDialog.cpp
        Superbird/Modules/Passwords/UI/ProviderSetupDialog.h
        Superbird/Modules/Passwords/UI/SaveCredentialBar.cpp
        Superbird/Modules/Passwords/UI/SaveCredentialBar.h
    )
    target_link_libraries(ladybird_impl PRIVATE CURL::libcurl OpenSSL::Crypto)
    if (APPLE)
        target_sources(ladybird_impl PRIVATE Superbird/Modules/Passwords/Keyring/KeyringMacOS.mm)
        set_source_files_properties(
            Superbird/Modules/Passwords/Keyring/KeyringMacOS.mm
            PROPERTIES COMPILE_FLAGS "-fobjc-arc"
        )
        target_link_libraries(ladybird_impl PRIVATE "-framework Security" "-framework LocalAuthentication")
    elseif (LINUX AND NOT ANDROID)
        target_sources(ladybird_impl PRIVATE Superbird/Modules/Passwords/Keyring/KeyringLinux.cpp)
    endif()
endif()
