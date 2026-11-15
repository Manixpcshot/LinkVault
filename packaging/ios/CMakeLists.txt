add_executable(LinkVaultNotes MACOSX_BUNDLE NotesWidget.swift)
set_target_properties(LinkVaultNotes PROPERTIES
    AUTOMOC OFF
    BUNDLE_EXTENSION appex
    XCODE_PRODUCT_TYPE "com.apple.product-type.app-extension"
    XCODE_EXPLICIT_FILE_TYPE "wrapper.app-extension"
    XCODE_ATTRIBUTE_APPLICATION_EXTENSION_API_ONLY YES
    XCODE_ATTRIBUTE_SKIP_INSTALL YES
    XCODE_ATTRIBUTE_SWIFT_VERSION 5.0
    XCODE_ATTRIBUTE_CODE_SIGN_ENTITLEMENTS "${CMAKE_CURRENT_SOURCE_DIR}/AppGroup.entitlements"
    XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER "io.github.manixpcshot.linkvault.notes"
    MACOSX_BUNDLE_GUI_IDENTIFIER "io.github.manixpcshot.linkvault.notes"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
    MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
    MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/Widget-Info.plist"
)
target_link_libraries(LinkVaultNotes PRIVATE "-framework SwiftUI" "-framework WidgetKit")

target_sources(LinkVault PRIVATE NotesWidgetBridge.mm ReloadWidget.swift)
target_include_directories(LinkVault PRIVATE "${PROJECT_SOURCE_DIR}")
target_link_libraries(LinkVault PRIVATE "-framework Foundation" "-framework WidgetKit")
set_target_properties(LinkVault PROPERTIES
    XCODE_ATTRIBUTE_SWIFT_VERSION 5.0
    XCODE_ATTRIBUTE_CODE_SIGN_ENTITLEMENTS "${CMAKE_CURRENT_SOURCE_DIR}/AppGroup.entitlements"
    XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER "io.github.manixpcshot.linkvault"
    XCODE_EMBED_APP_EXTENSIONS LinkVaultNotes
    XCODE_EMBED_APP_EXTENSIONS_CODE_SIGN_ON_COPY YES
)
add_dependencies(LinkVault LinkVaultNotes)
