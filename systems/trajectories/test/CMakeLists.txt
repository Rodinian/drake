add_executable(testPiecewisePolynomial testPiecewisePolynomial.cpp)
target_link_libraries(testPiecewisePolynomial drakePiecewisePolynomial)
include_directories( .. )
include_directories (${CMAKE_SOURCE_DIR}/util/test)
add_test(NAME testPiecewisePolynomial COMMAND testPiecewisePolynomial)
