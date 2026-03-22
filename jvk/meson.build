project('jvk', 'cpp',
  version : '0.1.0',
  default_options : ['cpp_std=c++17', 'warning_level=2'])

inc = include_directories('include')

llvm_dep = dependency('llvm', version : '>=18', required : true,
  modules : ['core', 'support', 'target', 'x86codegen',
             'aarch64codegen', 'executionengine', 'mcjit',
             'runtimedyld', 'object', 'bitreader', 'passes'])

jvk_sources = [
  'src/main.cpp',
  'src/joc_loader.cpp',
  'src/runtime.cpp',
  'hal/hal_linux.cpp',
  'hal/hal_macos.cpp',
  'hal/hal_windows.cpp',
]

executable('jvk',
  sources : jvk_sources,
  include_directories : inc,
  dependencies : [llvm_dep],
  install : true)
