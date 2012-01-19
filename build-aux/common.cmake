# Copyright (C) 2010-2012 Kamil Dudka <kdudka@redhat.com>
#
# This file is part of predator.
#
# predator is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# any later version.
#
# predator is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with predator.  If not, see <http://www.gnu.org/licenses/>.

# Check Boost availability
set(Boost_USE_STATIC_LIBS ON)
set(Boost_ADDITIONAL_VERSIONS "1.46" "1.47" "1.48" "1.49")
find_package(Boost 1.37)
if(Boost_FOUND)
    link_directories(${Boost_LIBRARY_DIRS})
    include_directories(${Boost_INCLUDE_DIRS})
endif()

# Check for a C compiler flag
include(CheckCCompilerFlag)
macro(ADD_C_FLAG opt_name opt)
    check_c_compiler_flag(${opt} HAVE_${opt_name})
    if(HAVE_${opt_name})
        add_definitions(${opt})
    endif()
endmacro()
macro(ADD_C_ONLY_FLAG opt_name opt)
    check_c_compiler_flag(${opt} C_HAVE_${opt_name})
    if(C_HAVE_${opt_name})
        set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${opt}")
    endif()
endmacro()

# Check for a C++ compiler flag
include(CheckCXXCompilerFlag)
macro(ADD_CXX_ONLY_FLAG opt_name opt)
    check_cxx_compiler_flag(${opt} CXX_HAVE_${opt_name})
    if(CXX_HAVE_${opt_name})
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${opt}")
    endif()
endmacro()

# we use c99 to compile *.c and c++0x to copmile *.cc
ADD_C_ONLY_FLAG(  "STD_C99"         "-std=c99")
ADD_CXX_ONLY_FLAG("STD_CXX_0X"      "-std=c++0x")

# tweak warnings
ADD_C_FLAG(       "PEDANTIC"        "-pedantic")
ADD_C_FLAG(       "W_ALL"           "-Wall")
ADD_C_ONLY_FLAG(  "W_UNDEF"         "-Wundef")
ADD_CXX_ONLY_FLAG("W_NO_DEPRECATED" "-Wno-deprecated")

option(USE_WEXTRA "Set to ON to use -Wextra (recommended)" ON)
if(USE_WEXTRA)
    ADD_C_FLAG("W_EXTRA" "-Wextra")
endif()

option(USE_WERROR "Set to ON to use -Werror (recommended)" OFF)
if(USE_WERROR)
    ADD_C_FLAG("W_ERROR" "-Werror")
endif()

option(USE_INT3_AS_BRK
       "Set to ON to use INT3 for breakpoints (SIGTRAP is used by default)" OFF)
if(USE_INT3_AS_BRK)
    add_definitions("-DUSE_INT3_AS_BRK")
endif()

if("$ENV{GCC_HOST}" STREQUAL "")
    get_filename_component(gcc_host_local "../gcc-install/bin/gcc" ABSOLUTE)
    set(GCC_HOST "${gcc_host_local}" CACHE STRING "host gcc executable")
else()
    set(GCC_HOST "$ENV{GCC_HOST}" CACHE STRING "host gcc executable")
endif()

option(TEST_WITH_VALGRIND "Set to ON to enable valgrind tests" OFF)
