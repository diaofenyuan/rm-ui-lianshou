param([switch]$Setup, [switch]$Package)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
Set-Location $projectRoot
function Check-Exit { if ($LASTEXITCODE -ne 0) { throw "命令执行失败，退出码 $LASTEXITCODE" } }
if ($Setup) {
    if (!(Test-Path '.venv/Scripts/python.exe')) { py -3.11 -m venv .venv; Check-Exit }
    & .venv/Scripts/python.exe -m pip install -i https://pypi.tuna.tsinghua.edu.cn/simple -r tools/requirements.txt aqtinstall==3.3.0 cmake ninja
    Check-Exit
    if (!(Test-Path '.tools/Qt/6.8.3/mingw_64/bin/qmake.exe')) {
        & .venv/Scripts/python.exe -m aqt install-qt windows desktop 6.8.3 win64_mingw -O .tools/Qt --archives qtbase -b https://download.qt.io
        Check-Exit
    }
    if (!(Test-Path '.tools/Qt/Tools/mingw1310_64/bin/g++.exe')) {
        & .venv/Scripts/python.exe -m aqt install-tool windows desktop tools_mingw1310 qt.tools.win64_mingw1310 -O .tools/Qt -b https://download.qt.io
        Check-Exit
    }
}
$qtRoot = Join-Path $projectRoot '.tools/Qt/6.8.3/mingw_64'
$compilerRoot = Join-Path $projectRoot '.tools/Qt/Tools/mingw1310_64/bin'
$env:PATH = "$compilerRoot;$qtRoot/bin;$projectRoot/.venv/Scripts;$env:PATH"
$cmakeArgs = @('-S','.', '-B','build','-G','Ninja','-DCMAKE_BUILD_TYPE=Release',"-DCMAKE_C_COMPILER=$compilerRoot/gcc.exe", "-DCMAKE_CXX_COMPILER=$compilerRoot/g++.exe", "-DCMAKE_PREFIX_PATH=$qtRoot")
if (Test-Path '.tools/sources/protobuf-21.12') { $cmakeArgs += "-DFETCHCONTENT_SOURCE_DIR_PROTOBUF=$projectRoot/.tools/sources/protobuf-21.12" }
if (Test-Path '.tools/sources/paho.mqtt.c-1.3.14') { $cmakeArgs += "-DFETCHCONTENT_SOURCE_DIR_PAHO=$projectRoot/.tools/sources/paho.mqtt.c-1.3.14" }
cmake @cmakeArgs; Check-Exit
cmake --build build -j 8; Check-Exit
ctest --test-dir build --output-on-failure; Check-Exit
& build/_deps/protobuf-build/protoc.exe --python_out=build/generated --proto_path=proto proto/game_status.proto; Check-Exit
if ($Package) {
    New-Item -ItemType Directory -Force dist | Out-Null
    Copy-Item build/rm_client.exe,build/rm_receive.exe dist -Force
    & "$qtRoot/bin/windeployqt.exe" --release --no-translations --no-system-d3d-compiler --no-opengl-sw dist/rm_client.exe
    Check-Exit
    $ffmpegSource = (Get-Command ffmpeg -ErrorAction Stop).Source
    Copy-Item -LiteralPath $ffmpegSource -Destination dist/ffmpeg.exe -Force
    New-Item -ItemType Directory -Force dist/licenses | Out-Null
    $protobufSource = if (Test-Path '.tools/sources/protobuf-21.12') { '.tools/sources/protobuf-21.12' } else { 'build/_deps/protobuf-src' }
    $pahoSource = if (Test-Path '.tools/sources/paho.mqtt.c-1.3.14') { '.tools/sources/paho.mqtt.c-1.3.14' } else { 'build/_deps/paho-src' }
    Copy-Item "$protobufSource/LICENSE" dist/licenses/protobuf.txt -Force
    Copy-Item "$pahoSource/edl-v10" dist/licenses/paho-edl.txt -Force
    New-Item -ItemType Directory -Force dist/licenses/Qt | Out-Null
    foreach ($name in @('LGPL-3.0-only.txt','GPL-3.0-only.txt','Qt-GPL-exception-1.0.txt')) {
        $destination = Join-Path 'dist/licenses/Qt' $name
        if (!(Test-Path $destination)) { Invoke-WebRequest "https://raw.githubusercontent.com/qt/qtbase/v6.8.3/LICENSES/$name" -OutFile $destination -UseBasicParsing }
    }
    New-Item -ItemType Directory -Force dist/licenses/gcc | Out-Null
    Copy-Item "$compilerRoot/../licenses/gcc/*" dist/licenses/gcc -Force
    & .venv/Scripts/python.exe -c "import subprocess,pathlib; r=subprocess.run(['ffmpeg','-L'],capture_output=True); pathlib.Path('dist/licenses/ffmpeg.txt').write_bytes(r.stdout+r.stderr)"
    Check-Exit
}
