// Regular build on a debian-like system:
local debian_pipeline(name, image,
        arch='amd64',
        deps='g++',
        build_type='Release',
        werror=true,
        ccache=true,
        cmake_extra='',
        benchmark='./randomx-benchmark --mine --jit --init $$(nproc)',
        extra_cmds=[],
        allow_fail=false) = {
    kind: 'pipeline',
    type: 'docker',
    name: name,
    platform: { arch: arch },
    steps: [
        {
            name: 'build',
            image: image,
            [if allow_fail then "failure"]: "ignore",
            commands: [
                'echo "man-db man-db/auto-update boolean false" | debconf-set-selections',
                'apt-get update',
                'apt-get install -y eatmydata',
                'eatmydata apt-get dist-upgrade -y',
                'eatmydata apt-get install -y cmake ninja-build ccache ' + deps,
                'mkdir build',
                'cd build',
                'cmake .. -G Ninja -DCMAKE_C_FLAGS=-fdiagnostics-color=always -DCMAKE_CXX_FLAGS=-fdiagnostics-color=always ' +
                    '-DCMAKE_C_COMPILER_LAUNCHER=ccache -DCMAKE_CXX_COMPILER_LAUNCHER=ccache ' +
                    '-DCMAKE_BUILD_TYPE='+build_type+' ' +
                    cmake_extra,
                'ninja -v',
                './randomx-tests',
                benchmark,
            ] + extra_cmds,
        }
    ],
};

// Macos build
local mac_builder(name, build_type='Release', cmake_extra='', extra_cmds=[], allow_fail=false) = {
    kind: 'pipeline',
    type: 'exec',
    name: name,
    platform: { os: 'darwin', arch: 'amd64' },
    steps: [
        {
            name: 'build',
            commands: [
                // If you don't do this then the C compiler doesn't have an include path containing
                // basic system headers.  WTF apple:
                'export SDKROOT="$(xcrun --sdk macosx --show-sdk-path)"',
                'mkdir build',
                'cd build',
                'cmake .. -G Ninja -DCMAKE_C_FLAGS=-fcolor-diagnostics -DCMAKE_CXX_FLAGS=-fcolor-diagnostics ' +
                    '-DCMAKE_C_COMPILER_LAUNCHER=ccache -DCMAKE_CXX_COMPILER_LAUNCHER=ccache ' +
                    '-DCMAKE_BUILD_TYPE='+build_type+' ' +
                    cmake_extra,
                'ninja -v',
                './randomx-tests',
                './randomx-benchmark --mine --jit --init $$(nproc)',
            ] + extra_cmds,
        }
    ]
};


[
    // Various debian builds
    debian_pipeline("Debian sid (amd64)", "debian:sid"),
    debian_pipeline("Debian sid/Debug (amd64)", "debian:sid", build_type='Debug'),
    debian_pipeline("Debian sid/clang-11 (amd64)", "debian:sid", deps='cmake clang-11',
        cmake_extra='-DCMAKE_C_COMPILER=clang-11 -DCMAKE_CXX_COMPILER=clang++-11'),
    debian_pipeline("Debian buster (i386)", "i386/debian:buster", cmake_extra='-DARCH_ID=i386',
        benchmark='./randomx-benchmark --verify --softAes --nonces 10'),
    debian_pipeline("Ubuntu focal (amd64)", "ubuntu:focal"),

    // ARM builds (ARM64 and armhf)
    debian_pipeline("Debian sid (ARM64)", "debian:sid", arch="arm64",
        benchmark='./randomx-benchmark --jit --verify --nonces 10 --softAes'),
    debian_pipeline("Ubuntu bionic (ARM64)", "ubuntu:bionic", arch="arm64",
        benchmark='./randomx-benchmark --jit --verify --nonces 10 --softAes'),
    debian_pipeline("Debian buster (armhf)", "arm32v7/debian:buster", arch="arm64", cmake_extra='-DARCH_ID=armhf',
        benchmark='./randomx-benchmark --verify --nonces 10 --softAes'),

    // Macos builds:
    mac_builder('macOS (Release)'),
    mac_builder('macOS (Debug)', build_type='Debug'),
]
