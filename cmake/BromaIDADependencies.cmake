# Third-party dependencies, fetched through CPM.
include(get_cpm)

# Broma's own CMake builds a test executable we have no use for;
# EXCLUDE_FROM_ALL keeps it out of the default build.
CPMAddPackage(
    NAME Broma
    GITHUB_REPOSITORY geode-sdk/Broma
    GIT_TAG 6793a5657813593ec1091e1d7704b39dae62b1d7
    EXCLUDE_FROM_ALL YES
)

CPMAddPackage("gh:geode-sdk/result@1.4.1")  # same pin Broma uses, so CPM shares it
CPMAddPackage("gh:fmtlib/fmt#12.2.0")
CPMAddPackage("gh:stephenberry/glaze#v9.1.0")
CPMAddPackage("gh:dankmeme01/asp2#v1.6.4")  # fs, iterators, sync: the parts std:: makes tedious

if(BROMAIDA_BUILD_TESTS)
    CPMAddPackage(
        NAME doctest
        GITHUB_REPOSITORY doctest/doctest
        VERSION 2.5.3
        DOWNLOAD_ONLY YES
    )
endif()

if(BROMAIDA_BUILD_PLUGIN)
    # The SDK is only needed for its headers, import libraries and CMake helpers.
    if(BROMAIDA_IDASDK_DIR)
        set(_bida_idasdk "${BROMAIDA_IDASDK_DIR}")
    elseif(DEFINED ENV{IDASDK} AND NOT "$ENV{IDASDK}" STREQUAL "")
        set(_bida_idasdk "$ENV{IDASDK}")
    else()
        CPMAddPackage(
            NAME idasdk
            GITHUB_REPOSITORY HexRaysSA/ida-sdk
            GIT_TAG ${BROMAIDA_IDASDK_TAG}
            DOWNLOAD_ONLY YES
        )
        set(_bida_idasdk "${idasdk_SOURCE_DIR}")
    endif()
    if(NOT EXISTS "${_bida_idasdk}/src/include/pro.h")
        message(FATAL_ERROR "'${_bida_idasdk}' does not look like an IDA SDK (src/include/pro.h is missing)")
    endif()
    set(IDASDK "${_bida_idasdk}/src" CACHE PATH "IDA SDK root (the folder holding include/ and lib/)" FORCE)
    set(IDABIN "${CMAKE_BINARY_DIR}/ida" CACHE PATH "Where the plugin is written" FORCE)
    # Not find_package: CPM registers a stub Findidasdk.cmake that would make it a no-op.
    include("${IDASDK}/cmake/idasdkConfig.cmake")

    # IDA ships its own, namespaced Qt (QT::QWidget...). A plugin only needs a stock Qt of the same
    # major.minor for headers and moc; a newer one can reference symbols IDA's runtime doesn't have.
    set(_bida_qt_hints "${BROMAIDA_QT_DIR}" "$ENV{QT_ROOT_DIR}" "$ENV{QTDIR}")
    file(GLOB _bida_qt_kits
        "$ENV{HOME}/Qt/${BROMAIDA_QT_VERSION}.*/gcc_64" "$ENV{HOME}/Qt/${BROMAIDA_QT_VERSION}.*/gcc_arm64"
        "$ENV{HOME}/Qt/${BROMAIDA_QT_VERSION}.*/macos" "$ENV{USERPROFILE}/Qt/${BROMAIDA_QT_VERSION}.*/msvc*_64")
    list(PREPEND CMAKE_PREFIX_PATH ${_bida_qt_hints} ${_bida_qt_kits})
    find_package(Qt6 ${BROMAIDA_QT_VERSION} QUIET COMPONENTS Core Gui Widgets)
    if(NOT Qt6_FOUND)
        message(FATAL_ERROR
            "Qt ${BROMAIDA_QT_VERSION}.x was not found. IDA 9.4 ships Qt 6.8, so that is the version to build against.\n"
            "Install it (for example `aqt install-qt linux desktop 6.8.2 linux_gcc_64 --archives qtbase -O ~/Qt`) and "
            "pass -DBROMAIDA_QT_DIR=<kit folder>, or set QT_ROOT_DIR.")
    endif()
    if(NOT Qt6_VERSION VERSION_LESS "6.9" AND NOT BROMAIDA_ALLOW_NEWER_QT)
        message(FATAL_ERROR "Found Qt ${Qt6_VERSION}, but IDA 9.4 ships Qt 6.8. Use 6.8.x or pass -DBROMAIDA_ALLOW_NEWER_QT=ON.")
    endif()
    message(STATUS "Qt ${Qt6_VERSION} from ${Qt6_DIR}")
endif()
