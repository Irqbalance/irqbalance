project('irqbalance', 'c', version: '1.9.3', default_options: ['warning_level=1'])
cc = meson.get_compiler('c')

glib_dep = dependency('glib-2.0')
m_dep = cc.find_library('m', required: false)
capng_dep = dependency('libcap-ng', required: get_option('capng'))
ncurses_dep = dependency('curses', required: get_option('ui'))
numa_dep = dependency('numa', required: get_option('numa'))
libnl_3_dep = dependency('libnl-3.0', required: get_option('thermal'))
libnl_genl_3_dep = dependency('libnl-genl-3.0', required: get_option('thermal'))
systemd_dep = dependency('libsystemd', required: get_option('systemd'))

cdata = configuration_data()
cdata.set('HAVE_GETOPT_LONG', cc.has_function('getopt_long'))
cdata.set('HAVE_IRQBALANCEUI', ncurses_dep.found())
cdata.set('HAVE_NUMA_H', cc.has_header('numa.h') and numa_dep.found())
cdata.set('HAVE_THERMAL', libnl_3_dep.found() and libnl_genl_3_dep.found())
cdata.set('HAVE_LIBCAP_NG', capng_dep.found())
cdata.set('HAVE_LIBSYSTEMD', systemd_dep.found())
cdata.set_quoted('VERSION', meson.project_version())
cfile = configure_file(output: 'config.h', configuration: cdata)

if cdata.get('HAVE_IRQBALANCEUI')
  add_project_arguments('-D_GNU_SOURCE', language: 'c')

  executable(
    'irqbalance-ui',
    '../ui/helpers.c',
    '../ui/irqbalance-ui.c',
    '../ui/ui.c',
    dependencies: [glib_dep, ncurses_dep],
    install: true,
  )

  install_man(
    '../irqbalance-ui.1',
    install_dir: get_option('mandir') + '/man1',
    install_mode: 'rw-r--r--',
    locale: 'en',
  )
endif

irqbalance_sources = [
  '../activate.c',
  '../bitmap.c',
  '../classify.c',
  '../cputree.c',
  '../irqbalance.c',
  '../irqlist.c',
  '../numa.c',
  '../placement.c',
  '../procinterrupts.c',
]

if libnl_3_dep.found() and libnl_genl_3_dep.found()
  irqbalance_sources += [
    '../thermal.c',
  ]
endif

executable(
  'irqbalance',
  irqbalance_sources,
  dependencies: [glib_dep, m_dep, capng_dep, libnl_3_dep, libnl_genl_3_dep, numa_dep, systemd_dep],
  install: true,
)

install_man(
  '../irqbalance.1',
  install_dir: get_option('mandir') + '/man1',
  install_mode: 'rw-r--r--',
  locale: 'en',
)
