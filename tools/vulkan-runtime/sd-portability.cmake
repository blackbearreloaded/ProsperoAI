# SPDX-License-Identifier: GPL-3.0-or-later
# libstdc++ eagerly instantiates pair<PickleValue,PickleValue> while PickleValue
# is incomplete. Define its special members after the complete class, preserving
# the upstream parser and ownership behavior. Keep the immutable checkout intact.
set(pickle_source "${PROS_ROOT}/.deps/stable-diffusion-vulkan/src/model_io/pickle_io.cpp")
file(READ "${pickle_source}" pickle_content)
set(pickle_start "struct PickleValue {")
set(pickle_end "    PickleTensorInfo tensor;\n};")
string(FIND "${pickle_content}" "${pickle_start}" pickle_start_at)
string(FIND "${pickle_content}" "${pickle_end}" pickle_end_at)
if(pickle_start_at LESS 0 OR pickle_end_at LESS 0)
    message(FATAL_ERROR "Pinned SD PickleValue layout changed; review the portability patch")
endif()
string(REPLACE "${pickle_start}" "${pickle_start}
    PickleValue();
    ~PickleValue();
    PickleValue(const PickleValue&);
    PickleValue(PickleValue&&) noexcept;
    PickleValue& operator=(const PickleValue&);
    PickleValue& operator=(PickleValue&&) noexcept;" pickle_content "${pickle_content}")
string(REPLACE "${pickle_end}" "${pickle_end}
PickleValue::PickleValue() = default;
PickleValue::~PickleValue() = default;
PickleValue::PickleValue(const PickleValue&) = default;
PickleValue::PickleValue(PickleValue&&) noexcept = default;
PickleValue& PickleValue::operator=(const PickleValue&) = default;
PickleValue& PickleValue::operator=(PickleValue&&) noexcept = default;" pickle_content "${pickle_content}")
set(patched_pickle "${CMAKE_CURRENT_BINARY_DIR}/sd-patches/pickle_io.cpp")
if(EXISTS "${patched_pickle}")
    file(READ "${patched_pickle}" previous_pickle)
else()
    set(previous_pickle "")
endif()
if(NOT previous_pickle STREQUAL pickle_content)
    file(WRITE "${patched_pickle}" "${pickle_content}")
endif()
get_target_property(sd_sources stable-diffusion SOURCES)
list(REMOVE_ITEM sd_sources "${pickle_source}")
list(APPEND sd_sources "${patched_pickle}")
set_property(TARGET stable-diffusion PROPERTY SOURCES "${sd_sources}")
set_source_files_properties("${patched_pickle}"
    DIRECTORY "${PROS_ROOT}/.deps/stable-diffusion-vulkan"
    PROPERTIES INCLUDE_DIRECTORIES
    "${PROS_ROOT}/.deps/stable-diffusion-vulkan/src/model_io")
