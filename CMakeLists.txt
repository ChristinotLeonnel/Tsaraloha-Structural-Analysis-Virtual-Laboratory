cmake_minimum_required(VERSION 3.20)
# TSALab — Tsaraloha Structural Analysis Laboratory : environnement scientifique (IDE) de l'écosystème.
#
# Architecture : TSA/docs/TSARALOHA_ARCHITECTURE.md (ADR-024). TSALab est une application à part entière
# (sa propre fenêtre, ses propres espaces de travail) construite sur les bibliothèques PARTAGÉES avec TSA,
# compilées depuis le dépôt TSA voisin avec l'identité de TSALab :
#   TSALab_Model    : modèle de projet, commandes, undo/redo, IO, analyse, résultats (sans widget)
#   TSALab_Graphics : viewport OCCT, caméra, grilles, accrochage, sélection, outils de dessin, thème
#   TSALab_Widgets  : composants d'interface partagés (arbre, propriétés, console, synchronisation…)
# Ce dépôt ne contient que ce qui est propre au laboratoire :
#   product/   identité (nom, .tsalab, signature TSLB, QSettings, CLSID de l'extension Explorateur)
#   lab/       Research (solveurs instrumentés, exemples, SOLVER LAB), LabUI, LabApp (fenêtre), Tests
#   resources/ icônes et ressources Windows de TSALab
project(TSALab VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON) # analyse IDE (.clangd)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# =============================================================================
# Base commune : dépôt TSA (par défaut le dossier voisin ../TSA)
# =============================================================================
set(TSA_ROOT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../TSA" CACHE PATH "Racine du dépôt TSA (bibliothèques partagées)")
get_filename_component(TSA_ROOT_DIR "${TSA_ROOT_DIR}" ABSOLUTE)
if(NOT EXISTS "${TSA_ROOT_DIR}/cmake/TSAProduct.cmake")
    message(FATAL_ERROR "Base commune TSA introuvable dans « ${TSA_ROOT_DIR} ».\n"
                        "Cloner le dépôt TSA à côté de TSALab (../TSA) ou indiquer -DTSA_ROOT_DIR=<chemin>.")
endif()
message(STATUS "Base commune TSA : ${TSA_ROOT_DIR}")

# Qt 6, OpenCASCADE (SDK partagés avec TSA : téléchargés une seule fois dans le dépôt TSA)
find_package(Qt6 6.2 REQUIRED COMPONENTS Core Gui Widgets Svg Network)
include("${TSA_ROOT_DIR}/cmake/SetupDependencies.cmake")
setup_external_dependencies()
find_package(OpenCASCADE REQUIRED)
message(STATUS "OpenCASCADE Version: ${OpenCASCADE_VERSION}")

include("${TSA_ROOT_DIR}/cmake/TSAProduct.cmake")
tsa_setup_compiler_launchers()

set(LAB_DIR "${CMAKE_CURRENT_SOURCE_DIR}/lab")
set(LAB_PRODUCT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/product")

# Bibliothèques partagées avec TSA, compilées avec l'identité de TSALab.
tsaraloha_add_shared_libraries(PREFIX TSALab PRODUCT_DIR ${LAB_PRODUCT_DIR} OVERLAY_DIRS ${LAB_DIR})

# =============================================================================
# Recherche (sans interface) : solveurs instrumentés, exemples, expérience SOLVER LAB — testée
# =============================================================================
add_library(TSALab_Research OBJECT
    ${LAB_DIR}/Research/Numerics/LinearAlgebra.h
    ${LAB_DIR}/Research/Numerics/LinearAlgebra.cpp
    ${LAB_DIR}/Research/Examples/ExampleModels.h
    ${LAB_DIR}/Research/Examples/ExampleModels.cpp
    ${LAB_DIR}/Research/Solver/SolverExperiment.h
    ${LAB_DIR}/Research/Solver/SolverExperiment.cpp
)
tsa_apply_common_settings(TSALab_Research ${LAB_PRODUCT_DIR} ${LAB_DIR})
target_precompile_headers(TSALab_Research REUSE_FROM TSALab_Model)
target_link_libraries(TSALab_Research PUBLIC TSALab_Model)

# =============================================================================
# Application TSALab.exe
# =============================================================================
add_executable(TSALab
    ${LAB_DIR}/LabApp/main.cpp
    ${LAB_DIR}/LabApp/LabMainWindow.h
    ${LAB_DIR}/LabApp/LabMainWindow.cpp
    ${LAB_DIR}/LabUI/LabStartPanel.h
    ${LAB_DIR}/LabUI/LabStartPanel.cpp
    ${LAB_DIR}/LabUI/SolverLabPage.h
    ${LAB_DIR}/LabUI/SolverLabPage.cpp
    ${TSA_ROOT_DIR}/resources/resources.qrc
    ${CMAKE_CURRENT_SOURCE_DIR}/resources/lab.qrc
    ${CMAKE_CURRENT_SOURCE_DIR}/resources/TSALab.rc
)
tsa_apply_common_settings(TSALab ${LAB_PRODUCT_DIR} ${LAB_DIR})
target_precompile_headers(TSALab REUSE_FROM TSALab_Model)
target_link_libraries(TSALab PRIVATE TSALab_Model TSALab_Graphics TSALab_Widgets TSALab_Research Qt6::Svg)
tsaraloha_configure_application(TSALab)

# Extension Explorateur : miniatures des fichiers .tsalab (CLSID propre, product/ProductShellIds.h)
tsaraloha_add_thumbnail_provider(TSALabThumbnailProvider ${LAB_PRODUCT_DIR})
if(TARGET TSALabThumbnailProvider)
    add_dependencies(TSALab TSALabThumbnailProvider)
endif()

# =============================================================================
# Tests du laboratoire (la base commune est couverte par les tests de TSA)
# =============================================================================
if(TSA_BUILD_TESTS)
    enable_testing()
    add_executable(TSALab_Tests ${LAB_DIR}/Tests/LabTests.cpp)
    tsa_apply_common_settings(TSALab_Tests ${LAB_PRODUCT_DIR} ${LAB_DIR})
    target_precompile_headers(TSALab_Tests REUSE_FROM TSALab_Model)
    target_link_libraries(TSALab_Tests PRIVATE TSALab_Model TSALab_Research)
    set_target_properties(TSALab_Tests PROPERTIES OUTPUT_NAME "TSALab_TestSuite")
    add_test(NAME TSALab_LabTests COMMAND TSALab_Tests)
endif()
