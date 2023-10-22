cmake_minimum_required(VERSION 3.13)

project(kh_benchmark)

set(CMAKE_CXX_STANDARD 17)

add_compile_options(-fno-exceptions -fno-rtti -Wall -Wextra -fpermissive)
add_compile_definitions(UNICODE NO_EXCEPTIONS TARGET_CPU="M1" USER_ENGINE)

add_subdirectory(../../../../third-party/benchmark benchmark)

## <repetition table>
add_executable(
    kh-benchmark
    node_benchmark.cpp
    repetition_table_benchmark.cpp
    overall_benchmark.cpp
    visit_history_benchmark.cpp
    main.cpp

    # yaneuraou
    ../../../types.cpp
    ../../../bitboard.cpp
    ../../../misc.cpp
    ../../../movegen.cpp
    ../../../position.cpp
    ../../../usi.cpp
    ../../../usi_option.cpp
    ../../../thread.cpp
    ../../../tt.cpp
    ../../../movepick.cpp
    ../../../timeman.cpp
    ../../../book/book.cpp
    ../../../book/apery_book.cpp
    ../../../extra/bitop.cpp
    ../../../extra/long_effect.cpp
    ../../../extra/sfen_packer.cpp
    ../../../extra/super_sort.cpp
    ../../../mate/mate.cpp
    ../../../mate/mate1ply_without_effect.cpp
    ../../../mate/mate1ply_with_effect.cpp
    ../../../mate/mate_solver.cpp
    ../../../eval/evaluate_bona_piece.cpp
    ../../../eval/evaluate.cpp
    ../../../eval/evaluate_io.cpp
    ../../../eval/evaluate_mir_inv_tools.cpp
    ../../../eval/material/evaluate_material.cpp
    ../../../testcmd/unit_test.cpp
    ../../../testcmd/mate_test_cmd.cpp
    ../../../testcmd/normal_test_cmd.cpp
    ../../../testcmd/benchmark.cpp

    # komoring heights
    ../komoring_heights.cpp

    # dummy engine
    ../dummy_engine.cpp
)
target_include_directories(kh-benchmark PRIVATE ../)
target_link_libraries(kh-benchmark benchmark::benchmark)
## </repetition table>
