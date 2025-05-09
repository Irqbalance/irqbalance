project('irqbalance', 'c',
    version: '1.9.4',
    default_options: ['warning_level=1'],
    meson_version: '>=0.54.0',
)

cc = meson.get_compiler('c')

glib_dep = dependency('glib-2.0')
m_dep = cc.find_library('m', required: false)
capng_dep = dependency('libcap-ng', required: get_option('capng'))
ncurses_dep = dependency('curses', required: get_option('ui'))
numa_dep = cc.find_library('numa', required: get_option('numa'))
libnl_3_dep = dependency('libnl-3.0', required: get_option('thermal'))
libnl_genl_3_dep = dependency('libnl-genl-3.0', required: get_option('thermal'))
systemd_dep = dependency('libsystemd', required: get_option('systemd'))
systemd_dir_dep = dependency('systemd', required: get_option('systemd'))

systemdsystemunitdir = systemd_dir_dep.get_variable(
  pkgconfig: 'systemdsystemunitdir',
  default_value: get_option('prefix') / 'lib/systemd/system'
)


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
    'ui/helpers.c',
    'ui/irqbalance-ui.c',
    'ui/ui.c',
    dependencies: [glib_dep, ncurses_dep],
    install: true,
    install_dir : get_option('sbindir'),
  )

  install_man('irqbalance-ui.1')
endif

irqbalance_sources = files(
  'activate.c',
  'bitmap.c',
  'classify.c',
  'cputree.c',
  'irqbalance.c',
  'irqlist.c',
  'numa.c',
  'placement.c',
  'procinterrupts.c',
)

if libnl_3_dep.found() and libnl_genl_3_dep.found()
  irqbalance_sources += files(
    'thermal.c',
  )
endif

executable(
  'irqbalance',
  irqbalance_sources,
  dependencies: [glib_dep, m_dep, capng_dep, libnl_3_dep, libnl_genl_3_dep, numa_dep, systemd_dep],
  install: true,
  install_dir : get_option('sbindir'),
)

install_man('irqbalance.1')

if systemd_dep.found()
  pkgconfdir = get_option('pkgconfdir')
  usrconfdir = get_option('usrconfdir')

# Set defaults
  if pkgconfdir == ''
     pkgconfdir = get_option('prefix') / 'etc/default'
  endif
  if usrconfdir == ''
     usrconfdir = get_option('sysconfdir') / 'default'
  endif

  idata = configuration_data()
  idata.set('usrconfdir', usrconfdir)
  idata.set('pkgconfdir', pkgconfdir)

  configure_file(
    input: 'misc/irqbalance.service.in',
    output: 'irqbalance.service',
    install_dir: systemdsystemunitdir,
    configuration: idata
  )
  configure_file(
    input: 'misc/irqbalance.env',
    output: 'irqbalance.env',
    install_dir: pkgconfdir,
    configuration: idata
  )
endif
