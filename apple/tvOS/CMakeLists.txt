# The tvOS app: an Objective-C++ shell around the Session. Configure with
#   cmake -G Xcode -DCMAKE_SYSTEM_NAME=tvOS -DCMAKE_OSX_SYSROOT=appletvsimulator   (or appletvos)
# Everything, including the asset catalog, is generated here; there is no hand-made Xcode project.

set(DIORASCII_BUNDLE_ID "com.ChristianAhrens.Diorascii" CACHE STRING "Bundle identifier of the tvOS app")
set(DIORASCII_APPLE_TEAM "" CACHE STRING "Apple developer team ID used to sign the tvOS app")
set(DIORASCII_PROVISIONING_PROFILE "" CACHE STRING
    "Name of an installed distribution provisioning profile; if set the app is signed manually with it")
set(DIORASCII_SIGN_IDENTITY "" CACHE STRING "Certificate (name or SHA-1) for manual signing")

# Asset catalog assembled from the icon art in Resources/Icon (single source of truth).
set(DIORASCII_ICON_SOURCE "${CMAKE_SOURCE_DIR}/Resources/Icon/tvOS/App Icon & Top Shelf Image.brandassets")
set(DIORASCII_CATALOG "${CMAKE_CURRENT_BINARY_DIR}/Assets.xcassets")
file(REMOVE_RECURSE "${DIORASCII_CATALOG}")
file(MAKE_DIRECTORY "${DIORASCII_CATALOG}")
file(WRITE "${DIORASCII_CATALOG}/Contents.json" "{ \"info\" : { \"author\" : \"xcode\", \"version\" : 1 } }\n")
file(COPY "${DIORASCII_ICON_SOURCE}/" DESTINATION "${DIORASCII_CATALOG}/AppIcon.brandassets")

# The App Store needs a build number that grows with every upload of the same version.
set(DIORASCII_BUILD_NUMBER "1" CACHE STRING "CFBundleVersion of the tvOS app (scripts/package-tvos.sh passes a growing one)")
set(DIORASCII_PROJECT_VERSION "${PROJECT_VERSION}")
configure_file(Info.plist.in "${CMAKE_CURRENT_BINARY_DIR}/Info.plist.in" @ONLY)

add_executable(Diorascii MACOSX_BUNDLE
  AppDelegate.mm SessionViewController.mm GridView.mm "${DIORASCII_CATALOG}")
target_include_directories(Diorascii PRIVATE .)
target_compile_options(Diorascii PRIVATE -fobjc-arc)
target_link_libraries(Diorascii PRIVATE diorascii_app diorascii_audio
  "-framework UIKit" "-framework AVFoundation" "-framework CoreGraphics" "-framework CoreText" "-framework QuartzCore")
set_source_files_properties("${DIORASCII_CATALOG}" PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
set_target_properties(Diorascii PROPERTIES
  MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_BINARY_DIR}/Info.plist.in"
  XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER "${DIORASCII_BUNDLE_ID}"
  XCODE_ATTRIBUTE_TARGETED_DEVICE_FAMILY 3
  XCODE_ATTRIBUTE_ASSETCATALOG_COMPILER_APPICON_NAME AppIcon
  XCODE_ATTRIBUTE_CODE_SIGN_STYLE Automatic
  XCODE_ATTRIBUTE_DEVELOPMENT_TEAM "${DIORASCII_APPLE_TEAM}"
  XCODE_ATTRIBUTE_CLANG_ENABLE_OBJC_ARC YES
  # Archives only contain targets that are installed: the app, not the libraries.
  XCODE_ATTRIBUTE_SKIP_INSTALL NO
  XCODE_ATTRIBUTE_INSTALL_PATH "$(LOCAL_APPS_DIR)"
  XCODE_GENERATE_SCHEME ON)

# The simulation and the sound synthesis run unoptimised in Xcode's Debug configuration and then
# use most of a core (the audio thread alone about 90 %), which makes the simulator sluggish.
# They stay optimised in every configuration; the shell itself remains debuggable.
set_target_properties(diorascii_core diorascii_app diorascii_audio PROPERTIES
  XCODE_ATTRIBUTE_GCC_OPTIMIZATION_LEVEL 2)

# Manual signing with a downloaded distribution profile (App Store builds without a registered Apple TV).
# Only the app gets the profile: the static libraries cannot have one.
if(DIORASCII_PROVISIONING_PROFILE)
  set_target_properties(Diorascii PROPERTIES
    XCODE_ATTRIBUTE_CODE_SIGN_STYLE Manual
    XCODE_ATTRIBUTE_CODE_SIGN_IDENTITY "${DIORASCII_SIGN_IDENTITY}"
    XCODE_ATTRIBUTE_PROVISIONING_PROFILE_SPECIFIER "${DIORASCII_PROVISIONING_PROFILE}")
endif()
