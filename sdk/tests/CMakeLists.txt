foreach(test IN ITEMS dx11_backend_test fsr3_dx11_test)
    add_executable(${test} ${test}.cpp)
    target_compile_features(${test} PRIVATE cxx_std_17)
    target_compile_definitions(${test} PRIVATE NOMINMAX UNICODE _UNICODE)
    target_link_libraries(${test} PRIVATE ffx_backend_dx11_${FFX_PLATFORM_NAME}
        ffx_fsr3_${FFX_PLATFORM_NAME} d3d11 dxgi dxguid d3dcompiler)
endforeach()

add_test(NAME DX11BackendContracts COMMAND dx11_backend_test)
add_test(NAME FSR3DX11Warp COMMAND fsr3_dx11_test warp)
add_test(NAME FSR3DX11WarpFP32 COMMAND fsr3_dx11_test warp fp32)
set_tests_properties(DX11BackendContracts PROPERTIES SKIP_RETURN_CODE 77)
