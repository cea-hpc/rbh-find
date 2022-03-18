librbhfind = library(
    'rbh-find',
    sources: [
        'actions.c',
        'filters.c',
        'parser.c',
        'utils.c',
    ],
    version: meson.project_version(),
    dependencies: [librobinhood],
    include_directories: rbhfind_include,
    install: true,
)
