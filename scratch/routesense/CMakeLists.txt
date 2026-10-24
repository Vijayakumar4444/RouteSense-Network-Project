# Preserve existing ns-3 targets after separating implementations.
foreach(program IN ITEMS routesense routesense-aodv)
  if(program STREQUAL "routesense")
    set(source custom/routesense.cc)
  else()
    set(source baseline/routesense-aodv.cc)
  endif()
  build_exec(
    EXECNAME ${program}
    EXECNAME_PREFIX scratch_
    SOURCE_FILES ${source}
    LIBRARIES_TO_LINK "${ns3-libs}" "${ns3-contrib-libs}"
    EXECUTABLE_DIRECTORY_PATH "${CMAKE_OUTPUT_DIRECTORY}/scratch/"
  )
endforeach()
