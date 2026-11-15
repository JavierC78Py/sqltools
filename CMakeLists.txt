cmake_minimum_required(VERSION 3.21)
project(SQLToolsMac LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Oracle Instant Client (Basic + SDK) copiado en el proyecto
set(ORACLE_IC_DIR "${CMAKE_SOURCE_DIR}/instantclient_19_16" CACHE PATH "Carpeta del Oracle Instant Client")

# Las Command Line Tools de este Mac no traen las cabeceras libc++ en su sitio: se apunta al SDK.
if(APPLE AND CMAKE_OSX_SYSROOT)
  add_compile_options($<$<COMPILE_LANGUAGE:CXX>:-isystem${CMAKE_OSX_SYSROOT}/usr/include/c++/v1>)
endif()

# Núcleo portado de SQLTools: capa OCI8 + utilidades Common (sin MFC)
file(GLOB OCI8_SOURCES   CONFIGURE_DEPENDS src/OCI8/*.cpp)
file(GLOB COMMON_SOURCES CONFIGURE_DEPENDS src/Common/*.cpp)
add_library(sqlt_core STATIC ${OCI8_SOURCES} ${COMMON_SOURCES})
target_include_directories(sqlt_core PUBLIC
  ${CMAKE_SOURCE_DIR}/src/compat ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/src/Common
  ${ORACLE_IC_DIR}/sdk/include)
target_link_directories(sqlt_core PUBLIC ${ORACLE_IC_DIR})
target_link_libraries(sqlt_core PUBLIC clntsh)
set_target_properties(sqlt_core PROPERTIES BUILD_RPATH ${ORACLE_IC_DIR})
target_compile_options(sqlt_core PRIVATE -w)   # el código original genera muchos avisos; se irán limpiando

# Biblioteca MetaDictionary de SQLTools: genera el DDL de los objetos (tablas con índices, restricciones, triggers, comentarios y permisos…)
file(GLOB META_SOURCES CONFIGURE_DIRECTORY CONFIGURE_DEPENDS src/MetaDictionary/*.cpp)
add_library(sqlt_meta STATIC ${META_SOURCES} src/SQLWorksheet/PlsSqlParser.cpp src/Common/StrHelpers.cpp)
target_include_directories(sqlt_meta PUBLIC ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/src/compat ${CMAKE_SOURCE_DIR}/src/Common)
target_link_libraries(sqlt_meta PUBLIC sqlt_core)
target_compile_options(sqlt_meta PRIVATE -w)

# Prueba de integración de la etapa 1 (usa OCI8::Connect y AutoCursor del propio SQLTools)
add_executable(sqlt_oci8_test etapa1/sqlt_oci8_test.cpp)
target_link_libraries(sqlt_oci8_test PRIVATE sqlt_core)
set_target_properties(sqlt_oci8_test PROPERTIES BUILD_RPATH ${ORACLE_IC_DIR})

add_executable(sqlt_types_test etapa1/sqlt_types_test.cpp)
target_link_libraries(sqlt_types_test PRIVATE sqlt_core)
set_target_properties(sqlt_types_test PROPERTIES BUILD_RPATH ${ORACLE_IC_DIR})

add_executable(sqlt_perf_test etapa1/sqlt_perf_test.cpp)
target_link_libraries(sqlt_perf_test PRIVATE sqlt_core)
set_target_properties(sqlt_perf_test PROPERTIES BUILD_RPATH ${ORACLE_IC_DIR})

add_executable(sqlt_bind_test etapa1/sqlt_bind_test.cpp)
target_link_libraries(sqlt_bind_test PRIVATE sqlt_core)
set_target_properties(sqlt_bind_test PROPERTIES BUILD_RPATH ${ORACLE_IC_DIR})

add_executable(sqlt_datefmt_test etapa1/sqlt_datefmt_test.cpp)
target_link_libraries(sqlt_datefmt_test PRIVATE sqlt_core)
set_target_properties(sqlt_datefmt_test PROPERTIES BUILD_RPATH ${ORACLE_IC_DIR})

# ---- Interfaz Qt (etapa 2) ----
set(QT_ROOT "$ENV{HOME}/Qt/6.8.3/macos" CACHE PATH "Instalación de Qt 6")
list(APPEND CMAKE_PREFIX_PATH ${QT_ROOT})
find_package(Qt6 COMPONENTS Widgets Test PrintSupport)
if(Qt6_FOUND)
  set(CMAKE_AUTOMOC ON)
  set(CMAKE_AUTORCC ON)
  set(APP_ICON ${CMAKE_SOURCE_DIR}/ui/icon/SQLTools.icns)
  set_source_files_properties(${APP_ICON} PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
  add_executable(SQLToolsMac MACOSX_BUNDLE ${APP_ICON} ui/app.qrc ui/help.qrc
    ui/main.cpp ui/MainWindow.cpp ui/MainCommands.cpp ui/MainWorkspace.cpp ui/MainPanels.cpp ui/FindInFilesDialog.cpp ui/TextCodec.cpp ui/SqlFormatter.cpp ui/SettingsDialog.cpp ui/ExportDialog.cpp ui/DdlOptions.cpp ui/DdlPreferencesDialog.cpp ui/ManualShots.cpp ui/ResultExporter.cpp ui/MainExtras.cpp ui/FoldRegions.cpp ui/MainResults.cpp ui/BindParser.cpp ui/HistoryStore.cpp ui/CommandMenu.cpp ui/FindReplaceDialog.cpp ui/icons.qrc ui/DbWorker.cpp ui/ResultModel.cpp ui/SqlHighlighter.cpp ui/SyntaxColors.cpp ui/ConnectionManager.cpp ui/ConnectionStore.cpp ui/Keychain.cpp ui/SqlSplitter.cpp ui/SqlEditor.cpp ui/ObjectBrowser.cpp ui/ObjectListDefs.cpp ui/ObjectListModel.cpp ui/FilterHeader.cpp ui/ViewerText.cpp ui/icons_obj.qrc ui/SqlContext.cpp ui/ConnectionImporter.cpp ui/WalletResolver.cpp)
  target_link_libraries(SQLToolsMac PRIVATE Qt6::Widgets Qt6::Test Qt6::PrintSupport sqlt_meta sqlt_core "-framework Security" "-framework CoreFoundation")
  set_target_properties(SQLToolsMac PROPERTIES BUILD_RPATH "${ORACLE_IC_DIR};${QT_ROOT}/lib"
    MACOSX_BUNDLE_ICON_FILE SQLTools.icns MACOSX_BUNDLE_BUNDLE_NAME "SQLTools Mac" MACOSX_BUNDLE_GUI_IDENTIFIER "com.sqltoolsmac.app")
endif()

# Pruebas del partidor de sentencias (solo QtCore; no necesitan Oracle)
find_package(Qt6 COMPONENTS Core)
if(Qt6_FOUND)
  set(CMAKE_AUTOMOC ON)
  add_executable(split_test tests/split_test.cpp ui/SqlSplitter.cpp)
  target_link_libraries(split_test PRIVATE Qt6::Core)
  set_target_properties(split_test PROPERTIES BUILD_RPATH "${QT_ROOT}/lib")
endif()

if(Qt6_FOUND)
  add_executable(editor_test tests/editor_test.cpp ui/SqlEditor.cpp ui/TextCodec.cpp ui/SqlSplitter.cpp ui/SqlHighlighter.cpp ui/SyntaxColors.cpp ui/SqlContext.cpp ui/FoldRegions.cpp)
  target_link_libraries(editor_test PRIVATE Qt6::Widgets)
  set_target_properties(editor_test PROPERTIES BUILD_RPATH "${QT_ROOT}/lib")
endif()

if(Qt6_FOUND)
  add_executable(context_test tests/context_test.cpp ui/SqlContext.cpp ui/SqlSplitter.cpp)
  target_link_libraries(context_test PRIVATE Qt6::Core)
  set_target_properties(context_test PROPERTIES BUILD_RPATH "${QT_ROOT}/lib")
endif()

find_package(Qt6 COMPONENTS Test)
if(Qt6_FOUND AND TARGET Qt6::Test)
  add_executable(completer_ui_test tests/completer_ui_test.cpp ui/SqlEditor.cpp ui/TextCodec.cpp ui/SqlSplitter.cpp ui/SqlHighlighter.cpp ui/SyntaxColors.cpp ui/SqlContext.cpp ui/FoldRegions.cpp)
  target_link_libraries(completer_ui_test PRIVATE Qt6::Widgets Qt6::Test)
  set_target_properties(completer_ui_test PROPERTIES BUILD_RPATH "${QT_ROOT}/lib")
endif()

if(Qt6_FOUND AND APPLE)
  add_executable(store_test tests/store_test.cpp ui/ConnectionStore.cpp ui/Keychain.cpp)
  target_link_libraries(store_test PRIVATE Qt6::Core "-framework Security" "-framework CoreFoundation")
  set_target_properties(store_test PROPERTIES BUILD_RPATH "${QT_ROOT}/lib")
endif()

if(Qt6_FOUND AND APPLE)
  add_executable(dialog_shot tests/dialog_shot.cpp ui/FilterHeader.cpp ui/ConnectionManager.cpp ui/ConnectionStore.cpp ui/Keychain.cpp ui/ConnectionImporter.cpp ui/WalletResolver.cpp)
  target_link_libraries(dialog_shot PRIVATE Qt6::Widgets "-framework Security" "-framework CoreFoundation")
  set_target_properties(dialog_shot PROPERTIES BUILD_RPATH "${QT_ROOT}/lib")
endif()

if(Qt6_FOUND AND APPLE AND EXISTS "${CMAKE_SOURCE_DIR}/tests/import_test.cpp")
  add_executable(import_test tests/import_test.cpp ui/ConnectionImporter.cpp ui/ConnectionStore.cpp ui/Keychain.cpp)
  target_link_libraries(import_test PRIVATE Qt6::Core "-framework Security" "-framework CoreFoundation")
  set_target_properties(import_test PROPERTIES BUILD_RPATH "${QT_ROOT}/lib")
endif()

if(Qt6_FOUND)
  add_executable(wallet_test tests/wallet_test.cpp ui/WalletResolver.cpp)
  target_link_libraries(wallet_test PRIVATE Qt6::Core)
  set_target_properties(wallet_test PROPERTIES BUILD_RPATH "${QT_ROOT}/lib")
endif()

if(Qt6_FOUND)
  add_executable(tns_check tests/tns_check.cpp ui/WalletResolver.cpp)
  target_link_libraries(tns_check PRIVATE Qt6::Core)
  set_target_properties(tns_check PROPERTIES BUILD_RPATH "${QT_ROOT}/lib")
endif()

if(Qt6_FOUND AND APPLE)
  add_executable(import_connections tools/import_connections.cpp ui/ConnectionImporter.cpp ui/ConnectionStore.cpp ui/Keychain.cpp)
  target_link_libraries(import_connections PRIVATE Qt6::Core "-framework Security" "-framework CoreFoundation")
  set_target_properties(import_connections PROPERTIES BUILD_RPATH "${QT_ROOT}/lib")
endif()

find_package(Qt6 COMPONENTS Gui)
if(Qt6_FOUND AND TARGET Qt6::Gui)
  add_executable(make_icon tools/make_icon.cpp)
  target_link_libraries(make_icon PRIVATE Qt6::Gui)
  set_target_properties(make_icon PROPERTIES BUILD_RPATH "${QT_ROOT}/lib")
  add_executable(slice_object_icons tools/slice_object_icons.cpp)
  target_link_libraries(slice_object_icons PRIVATE Qt6::Gui)
  set_target_properties(slice_object_icons PROPERTIES BUILD_RPATH "${QT_ROOT}/lib")
  add_executable(slice_icons tools/slice_icons.cpp)
  target_link_libraries(slice_icons PRIVATE Qt6::Gui)
  set_target_properties(slice_icons PROPERTIES BUILD_RPATH "${QT_ROOT}/lib")
endif()

if(Qt6_FOUND)
  add_executable(bind_test tests/bind_test.cpp ui/BindParser.cpp ui/HistoryStore.cpp)
  target_link_libraries(bind_test PRIVATE Qt6::Core)
  set_target_properties(bind_test PROPERTIES BUILD_RPATH "${QT_ROOT}/lib")
endif()

if(Qt6_FOUND)
  add_executable(fold_test tests/fold_test.cpp ui/FoldRegions.cpp ui/SqlSplitter.cpp)
  target_link_libraries(fold_test PRIVATE Qt6::Core)
  set_target_properties(fold_test PROPERTIES BUILD_RPATH "${QT_ROOT}/lib")
endif()

if(Qt6_FOUND)
  add_executable(format_test tests/format_test.cpp ui/SqlFormatter.cpp ui/SqlSplitter.cpp)
  target_link_libraries(format_test PRIVATE Qt6::Core)
  set_target_properties(format_test PROPERTIES BUILD_RPATH "${QT_ROOT}/lib")
endif()

if(Qt6_FOUND)
  add_executable(viewer_text_test tests/viewer_text_test.cpp ui/ViewerText.cpp ui/DdlOptions.cpp)
  target_link_libraries(viewer_text_test PRIVATE Qt6::Widgets)
  set_target_properties(viewer_text_test PROPERTIES BUILD_RPATH "${QT_ROOT}/lib")
endif()

if(Qt6_FOUND)
  add_executable(export_test tests/export_test.cpp ui/ResultExporter.cpp)
  target_link_libraries(export_test PRIVATE Qt6::Core)
  set_target_properties(export_test PROPERTIES BUILD_RPATH "${QT_ROOT}/lib")
endif()

if(Qt6_FOUND AND TARGET Qt6::Gui)
  add_executable(build_manual tools/build_manual.cpp)
  target_link_libraries(build_manual PRIVATE Qt6::Gui)
  set_target_properties(build_manual PROPERTIES BUILD_RPATH "${QT_ROOT}/lib")
endif()

if(Qt6_FOUND)
  add_executable(meta_test tests/meta_test.cpp)
  target_link_libraries(meta_test PRIVATE sqlt_meta sqlt_core)
  set_target_properties(meta_test PROPERTIES BUILD_RPATH "${ORACLE_IC_DIR}")
endif()
