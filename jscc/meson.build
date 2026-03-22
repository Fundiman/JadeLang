project('jscc', 'cpp',
  version : '0.1.0',
  default_options : ['cpp_std=c++17', 'warning_level=2'])

inc = include_directories('include')

llvm_dep = dependency('llvm', version : '>=18', required : true,
  modules : ['core', 'support', 'target', 'x86codegen',
             'aarch64codegen', 'passes'])

jscc_sources = [
  'src/main.cpp',
  'src/lexer.cpp',
  'src/error.cpp',
  'src/parser.cpp',
  'src/sema.cpp',
  'src/import_resolver.cpp',
  'src/codegen.cpp',
]

executable('jscc',
  sources : jscc_sources,
  include_directories : inc,
  dependencies : [llvm_dep],
  install : true)
