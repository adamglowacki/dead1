set(MODULE TRUE)

set( LLVM_LINK_COMPONENTS support mc)

add_clang_library(DeadMethod DeadMethod.cpp)

add_dependencies(DeadMethod
  ClangAttrClasses
  ClangAttrList
  ClangCommentNodes
  ClangDeclNodes
  ClangDiagnosticCommon
  ClangStmtNodes
  )

target_link_libraries(DeadMethod
  clangFrontend
  clangAST
  )

set_target_properties(DeadMethod
  PROPERTIES
  LINKER_LANGUAGE CXX
  PREFIX "")
