# TSALab — cœur scientifique (C++20 pur : ni Qt, ni OpenCASCADE, ni modèle TSA).
#   tsalab_science : numerics (solveurs instrumentés, valeurs propres, comparaison de solveurs),
#                    solveurs d'ossatures planes (API tsalab::planar, MetDeDeplacement),
#                    validation (benchmarks à solution analytique, validation croisée)
#   tsalab-bench   : console — exécute le banc de validation sur tous les solveurs
#   tsalab_science_tests : tests sans Qt
# Utilisé par TSALab (IDE) ET par TSA (moteur « custom2d ») : TSA/docs/TSARALOHA_ARCHITECTURE.md.
cmake_minimum_required(VERSION 3.20)
project(TSALabScience VERSION 0.1.0 LANGUAGES CXX)

if(NOT TARGET MetDeDeplacement)
    add_subdirectory(engines/MetDeDeplacement ${CMAKE_CURRENT_BINARY_DIR}/MetDeDeplacement)
endif()

add_library(tsalab_science STATIC
    include/tsalab/numerics/Matrix.h
    include/tsalab/numerics/LinearAlgebra.h
    include/tsalab/numerics/SolverComparison.h
    include/tsalab/planar/PlanarSolver.h
    include/tsalab/planar/PlanarSolvers.h
    include/tsalab/validation/PlanarBenchmarks.h
    src/LinearAlgebra.cpp
    src/SolverComparison.cpp
    src/MddBridge.h
    src/MddBridge.cpp
    src/MetDeDeplacementSolver.cpp
    src/OpenSeesPlanarSolver.cpp
    src/PlanarBenchmarks.cpp
)
add_library(tsalab::science ALIAS tsalab_science)
target_include_directories(tsalab_science PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(tsalab_science PUBLIC cxx_std_20)
target_link_libraries(tsalab_science PUBLIC MetDeDeplacement)
if(MSVC)
    target_compile_options(tsalab_science PRIVATE /W4 /permissive- /utf-8)
endif()

# OpenSees (solveur de référence du banc, processus externe) : chemin par défaut des dépôts de développement ;
# une application le fixe par tsalab::planar::setOpenSeesExecutable, un poste par TSALAB_OPENSEES.
if(NOT TSALAB_OPENSEES_DEFAULT)
    foreach(candidate "${CMAKE_CURRENT_SOURCE_DIR}/../thirdparty/OpenSees/bin/OpenSees.exe"
                      "${CMAKE_CURRENT_SOURCE_DIR}/../../TSA/thirdparty/OpenSees/bin/OpenSees.exe")
        if(EXISTS "${candidate}" AND NOT TSALAB_OPENSEES_DEFAULT)
            get_filename_component(TSALAB_OPENSEES_DEFAULT "${candidate}" ABSOLUTE)
        endif()
    endforeach()
endif()
if(TSALAB_OPENSEES_DEFAULT)
    target_compile_definitions(tsalab_science PRIVATE TSALAB_OPENSEES_DEFAULT_PATH="${TSALAB_OPENSEES_DEFAULT}")
endif()

option(TSALAB_SCIENCE_TOOLS "Compiler tsalab-bench et les tests du cœur scientifique" ON)
if(TSALAB_SCIENCE_TOOLS)
    add_executable(tsalab-bench tools/tsalab_bench.cpp)
    target_link_libraries(tsalab-bench PRIVATE tsalab_science)
    add_executable(tsalab_science_tests tests/ScienceTests.cpp)
    target_link_libraries(tsalab_science_tests PRIVATE tsalab_science)
    if(MSVC)
        target_compile_options(tsalab-bench PRIVATE /W4 /permissive- /utf-8)
        target_compile_options(tsalab_science_tests PRIVATE /W4 /permissive- /utf-8)
    endif()
    if(COMMAND add_test)
        add_test(NAME tsalab_science_tests COMMAND tsalab_science_tests)
        add_test(NAME tsalab_bench COMMAND tsalab-bench)
    endif()
endif()
