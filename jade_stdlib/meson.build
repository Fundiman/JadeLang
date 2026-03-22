project('jade_stdlib', 'cpp',
  version : '0.1.0',
  default_options : ['cpp_std=c++17', 'warning_level=2'])

inc = include_directories('include', 'gc')

stdlib_sources = [
  'src/interp.cpp',
  'src/io.cpp',
  'src/math.cpp',
  'src/os.cpp',
  'src/fs.cpp',
  'src/collections.cpp',
  'gc/gc.cpp',
]

# shared library — jvk links against this at runtime
jade_stdlib = shared_library('jade_stdlib',
  sources : stdlib_sources,
  include_directories : inc,
  install : true)

# also build a static version for embedding
jade_stdlib_static = static_library('jade_stdlib_static',
  sources : stdlib_sources,
  include_directories : inc,
  install : true)
