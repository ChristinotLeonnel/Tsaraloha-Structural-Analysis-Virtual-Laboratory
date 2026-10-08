cmake_minimum_required(VERSION 3.20)
# TSALab — Tsaraloha Structural Analysis Laboratory.
#
# TSALab est construit sur la base technique de TSA, dont il compile directement les sources
# (src/, tests/, ressources communes) : aucune copie, un bug corrigé dans TSA l'est dans TSALab.
# Ce dépôt ne contient que ce qui est propre au laboratoire :
#   product/   identité du produit (nom, .tsalab, signature TSLB, CLSID…) et points d'extension ;
#   lab/       modules du laboratoire (Research : solveurs, exemples ; LabUI : espaces, Start Center) ;
#   resources/ icônes et ressources Windows de TSALab.
project(TSALab VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON) # analyse IDE (.clangd)

# Activer l'infrastructure Qt
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# =============================================================================
# Base commune : dépôt TSA (par défaut le dossier voisin ../TSA)
# =============================================================================
set(TSA_ROOT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../TSA" CACHE PATH "Racine du dépôt TSA (base technique commune)")
get_filename_component(TSA_ROOT_DIR "${TSA_ROOT_DIR}" ABSOLUTE)
if(NOT EXISTS "${TSA_ROOT_DIR}/cmake/TSAProduct.cmake")
    message(FATAL_ERROR "Base commune TSA introuvable dans « ${TSA_ROOT_DIR} ».\n"
                        "Cloner le dépôt TSA à côté de TSALab (../TSA) ou indiquer -DTSA_ROOT_DIR=<chemin>.")
endif()
message(STATUS "Base commune TSA : ${TSA_ROOT_DIR}")

# =============================================================================
# Qt 6, OpenCASCADE (SDK partagés avec TSA : téléchargés une seule fois dans le dépôt TSA)
# =============================================================================
find_package(Qt6 6.2 REQUIRED COMPONENTS Core Gui Widgets Svg Network)

include("${TSA_ROOT_DIR}/cmake/SetupDependencies.cmake")
setup_external_dependencies()
find_package(OpenCASCADE REQUIRED)
message(STATUS "OpenCASCADE Version: ${OpenCASCADE_VERSION}")
message(STATUS "OpenCASCADE Include: ${OpenCASCADE_INCLUDE_DIR}")

# =============================================================================
# Produit TSALab sur la base commune (cmake/TSAProduct.cmake du dépôt TSA)
# =============================================================================
include("${TSA_ROOT_DIR}/cmake/TSAProduct.cmake")
tsa_setup_compiler_launchers()

set(LAB_DIR "${CMAKE_CURRENT_SOURCE_DIR}/lab")

# Modules du laboratoire sans interface : compilés dans TSALab_Core, donc testés par TSALab_Tests.
set(LAB_CORE_SOURCES
    ${LAB_DIR}/Research/Numerics/LinearAlgebra.h
    ${LAB_DIR}/Research/Numerics/LinearAlgebra.cpp
    ${LAB_DIR}/Research/Examples/ExampleModels.h
    ${LAB_DIR}/Research/Examples/ExampleModels.cpp
    ${LAB_DIR}/Research/Solver/SolverExperiment.h
    ${LAB_DIR}/Research/Solver/SolverExperiment.cpp
)

# Interface du laboratoire (exécutable seulement).
set(LAB_APP_SOURCES
    ${LAB_DIR}/LabUI/LabStartPanel.h
    ${LAB_DIR}/LabUI/LabStartPanel.cpp
    ${LAB_DIR}/LabUI/LabWorkspaceHost.h
    ${LAB_DIR}/LabUI/LabWorkspaceHost.cpp
    ${LAB_DIR}/LabUI/SolverLabPage.h
    ${LAB_DIR}/LabUI/SolverLabPage.cpp
)

tsa_add_product(
    NAME TSALab
    PRODUCT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/product"
    OVERLAY_DIRS "${LAB_DIR}"
    RESOURCES
        "${CMAKE_CURRENT_SOURCE_DIR}/resources/lab.qrc"
        "${CMAKE_CURRENT_SOURCE_DIR}/resources/TSALab.rc"
    CORE_SOURCES ${LAB_CORE_SOURCES}
    APP_SOURCES ${LAB_APP_SOURCES}
    TEST_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/resources/lab.qrc"
)
