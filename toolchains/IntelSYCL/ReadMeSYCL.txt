This IntelSYCL cmake module enables the Intel® oneAPI DPC++/C++ Compiler.
You may need to set your CXX or CMAKE_CXX_COMPILER string to "icx" or "icpx" e.t.c 
and this module passes appropriate compiler and linker flags to enable SYCL.

----------------------------------------------------------
Steps to enable SYCL compiler:
----------------------------------------------------------
(1) To be able to use this module, User's environment should be setup so that
    "icx/icpx" binary is locatable(i.e in search path)
(2) Please make sure to use cmake version of 3.22.1(Linux)/3.23(Windows) or above.
    Please provide at the top in user's CMakeLists.txt before enabling the SYCL compiler mode.
    Eg:
       # Windows
       cmake_minimum_required(VERSION 3.23)
       # Linux
       cmake_minimum_required(VERSION 3.22.1)

(3) After defining the project, Add IntelSYCL package to Project's CMakeLists.txt file.

   find_package(IntelSYCL REQUIRED)

(4) Enable SYCL for the specific source files through the Project's CMakeLists.txt file.

     # Compile specific sources for SYCL and build target for SYCL
     add_executable(target_proj A.cpp B.cpp offload1.cpp offload2.cpp)
     add_sycl_to_target(TARGET target_proj SOURCES offload1.cpp offload2.cpp)

(5) Set -DCMAKE_C_COMPILER/-DCMAKE_CXX_COMPILER to Intel's ICX compiler.
    The define CMAKE_C_COMPILER/CMAKE_CXX_COMPILER (or) env variables CC/CXX
    can be used to override the default system C/C++ compilers.
    The value to the define/env can be fully specified or, if detectable,
    just the binary name.

    Defines:
    Eg: Linux: cmake -DCMAKE_C_COMPILER=icx -DCMAKE_CXX_COMPILER=icpx
        Windows: -DCMAKE_C_COMPILER=icx -DCMAKE_CXX_COMPILER=icx

        (OR)
             cmake \
             -DCMAKE_C_COMPILER=/disk/user/custominstall/icx \
             -DCMAKE_CXX_COMPILER=/disk/user/custominstall/icpx

    Env:
    Alternatively, one can export CC and CXX to icx
    Eg:
        export CC=icx (or) setenv CC icx
        export CXX=icpx (or) setenv CXX icpx

(5) Run Cmake ( Eg: cmake <Path to CmakeList.txt>)
(6) Build app ( Eg: cmake --build .)

-------------------------------------------------------
Additional steps for Custom layout of compiler tools
-------------------------------------------------------
The user needs to follow these additional steps in case of a custom layout
of compiler tools (eg: python layout, conda-packaged compilers)
where in the library and include paths are in non-default locations
relative to compiler binary.

--> provide Hints for "include" and "lib" directories by setting up
    the following environment variables.
    Set the following variables to the parent directory of include/lib directories.
    Env variables:
        SYCL_INCLUDE_DIR_HINT
        SYCL_LIBRARY_DIR_HINT

        Example:
         set SYCL_INCLUDE_DIR_HINT="Intel/oneAPI/compiler/latest/windows/user_include_testdir"
         set SYCL_LIBRARY_DIR_HINT="Intel/oneAPI/compiler/latest/windows/someother_lib_dir"

The module sets tests the SYCL compiler, checks the required compiler macros.

This module updates

``CMAKE_CXX_FLAGS``
  Appends CMAKE_CXX_FLAGS with ``SYCL_FLAGS``

This will define the following variables:

``IntelSYCL_FOUND``
  True if the system has the SYCL library.
``SYCL_LANGUAGE_VERSION``
  The SYCL language spec version by Compiler.
``SYCL_INCLUDE_DIR``
  Include directories needed to use SYCL.
``SYCL_IMPLEMENTATION_ID``
  The SYCL compiler variant.
``SYCL_FLAGS``
  SYCL specific flags for the compiler.
