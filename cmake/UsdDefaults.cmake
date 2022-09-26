#
# Copyright 2016 Pixar
#
# Licensed under the Apache License, Version 2.0 (the "Apache License")
# with the following modification; you may not use this file except in
# compliance with the Apache License and the following modification to it:
# Section 6. Trademarks. is deleted and replaced with:
#
# 6. Trademarks. This License does not grant permission to use the trade
#    names, trademarks, service marks, or product names of the Licensor
#    and its affiliates, except as required to comply with Section 4(c) of
#    the License and to reproduce the content of the NOTICE file.
#
# You may obtain a copy of the Apache License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the Apache License with the above modification is
# distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
# KIND, either express or implied. See the Apache License for the specific
# language governing permissions and limitations under the Apache License.
#

# This files includes most compiler and linker flags that USD uses. We need
# them to be able to include the headers without warnings, and to fix some issues.

# Helper functions to avoid duplicate code.
function(_add_define definition)
  list(APPEND _GUC_CXX_DEFINITIONS "-D${definition}")
  set(_GUC_CXX_DEFINITIONS ${_GUC_CXX_DEFINITIONS} PARENT_SCOPE)
endfunction()

function(_disable_warning flag)
  if(MSVC)
    list(APPEND _GUC_CXX_WARNING_FLAGS "/wd${flag}")
  else()
    list(APPEND _GUC_CXX_WARNING_FLAGS "-Wno-${flag}")
  endif()
  set(_GUC_CXX_WARNING_FLAGS ${_GUC_CXX_WARNING_FLAGS} PARENT_SCOPE)
endfunction()

# Compiler-specific defines and warnings.
if(MSVC)
  # Enable exception handling.
  set(_GUC_CXX_FLAGS "${_GUC_CXX_FLAGS} /EHsc")

  # Standards compliant.
  set(_GUC_CXX_FLAGS "${_GUC_CXX_FLAGS} /Zc:rvalueCast /Zc:strictStrings")

  # The /Zc:inline option strips out the "arch_ctor_<name>" symbols used for
  # library initialization by ARCH_CONSTRUCTOR starting in Visual Studio 2019,
  # causing release builds to fail. Disable the option for this and later
  # versions.
  #
  # For more details, see:
  # https://developercommunity.visualstudio.com/content/problem/914943/zcinline-removes-extern-symbols-inside-anonymous-n.html
  if(MSVC_VERSION GREATER_EQUAL 1920)
    set(_GUC_CXX_FLAGS "${_GUC_CXX_FLAGS} /Zc:inline-")
  else()
    set(_GUC_CXX_FLAGS "${_GUC_CXX_FLAGS} /Zc:inline")
  endif()

  # Turn on all but informational warnings.
  set(_GUC_CXX_FLAGS "${_GUC_CXX_FLAGS} /W3")

  # truncation from 'double' to 'float' due to matrix and vector classes in `Gf`
  _disable_warning("4244")
  _disable_warning("4305")

  # conversion from size_t to int. While we don't want this enabled
  # it's in the Python headers. So all the Python wrap code is affected.
  _disable_warning("4267")

  # no definition for inline function
  # this affects Glf only
  _disable_warning("4506")

  # 'typedef ': ignored on left of '' when no variable is declared
  # XXX: figure out why we need this
  _disable_warning("4091")

  # qualifier applied to function type has no meaning; ignored
  # tbb/parallel_for_each.h
  _disable_warning("4180")

  # '<<': result of 32-bit shift implicitly converted to 64 bits
  # tbb/enumerable_thread_specific.h
  _disable_warning("4334")

  # Disable warning C4996 regarding fopen(), strcpy(), etc.
  _add_define("_CRT_SECURE_NO_WARNINGS")

  # Disable warning C4996 regarding unchecked iterators for std::transform,
  # std::copy, std::equal, et al.
  _add_define("_SCL_SECURE_NO_WARNINGS")

  # Make sure WinDef.h does not define min and max macros which
  # will conflict with std::min() and std::max().
  _add_define("NOMINMAX")

  # Needed to prevent YY files trying to include unistd.h
  # (which doesn't exist on Windows)
  _add_define("YY_NO_UNISTD_H")

  # Exclude headers from unnecessary Windows APIs to improve build
  # times and avoid annoying conflicts with macros defined in those
  # headers.
  _add_define("WIN32_LEAN_AND_MEAN")

  # These files require /bigobj compiler flag
  #   Vt/arrayPyBuffer.cpp
  #   Usd/crateFile.cpp
  #   Usd/stage.cpp
  # Until we can set the flag on a per file basis, we'll have to enable it
  # for all translation units.
  set(_GUC_CXX_FLAGS "${_GUC_CXX_FLAGS} /bigobj")

  # Enable PDB generation.
  set(_GUC_CXX_FLAGS "${_GUC_CXX_FLAGS} /Zi")

  # Enable multiprocessor builds.
  set(_GUC_CXX_FLAGS "${_GUC_CXX_FLAGS} /MP")
  set(_GUC_CXX_FLAGS "${_GUC_CXX_FLAGS} /Gm-")

  # Disable Boost and TBB auto-linking, as this causes errors when building
  # debug guc against their release versions. No direct linking is involved, anyway.
  _add_define("BOOST_ALL_NO_LIB")
  _add_define("__TBB_NO_IMPLICIT_LINKAGE")
else()
  # Enable all warnings.
  set(_GUC_CXX_FLAGS "${_GUC_CXX_FLAGS} -Wall -Wformat-security")

  # We use hash_map, suppress deprecation warning.
  _disable_warning("deprecated")
  _disable_warning("deprecated-declarations")
endif()

# General defines.
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
  _add_define(BUILD_OPTLEVEL_DEV)
endif()

# Convert lists to strings.
set(_GUC_CXX_FLAGS ${_GUC_CXX_FLAGS} ${_GUC_CXX_WARNING_FLAGS})
string(REPLACE ";" " " GUC_CXX_FLAGS "${_GUC_CXX_FLAGS}")
string(REPLACE ";" " " GUC_CXX_DEFINITIONS "${_GUC_CXX_DEFINITIONS}")
