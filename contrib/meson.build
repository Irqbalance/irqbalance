project('irqbalance', 'c', version: '1.9.3', default_options: ['warning_level=1'])
cc = meson.get_compiler('c')

glib_dep = dependency('glib-2.0')
m_dep = cc.find_library('m', required: false)
capng_dep = dependency('libcap-ng', required: get_option('capng'))
ncurses_dep = dependency('curses', required: get_option('ui'))
numa_dep = dependency('numa')
systemd_dep = dependency('libsystemd', required: get_option('systemd'))

cdata = configuration_data()
cdata.set('HAVE_GETOPT_LONG', cc.has_function('getopt_long'))
cdata.set('HAVE_IRQBALANCEUI', ncurses_dep.found())
cdata.set('HAVE_NUMA_H', cc.has_header('numa.h'))
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

executable(
  'irqbalance',
  '../activate.c',
  '../bitmap.c',
  '../classify.c',
  '../cputree.c',
  '../irqbalance.c',
  '../irqlist.c',
  '../numa.c',
  '../placement.c',
  '../procinterrupts.c',
  dependencies: [glib_dep, m_dep, capng_dep, numa_dep, systemd_dep],
  install: true,
)

install_man(
  '../irqbalance.1',
  install_dir: get_option('mandir') + '/man1',
  install_mode: 'rw-r--r--',
  locale: 'en',
)
