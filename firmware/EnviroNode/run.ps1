$ErrorActionPreference = "Stop"

$programmer = "C:\Program Files\STMicroelectronics\STM32Cube\STM32CubeProgrammer\bin\STM32_Programmer_CLI.exe"
$elf = ".\build\Debug\EnviroNode.elf"

if (-not (Test-Path $programmer)) {
    throw "STM32CubeProgrammer CLI not found at: $programmer"
}

Write-Host "Building EnviroNode..."
& cube cmake --build --preset Debug
if ($LASTEXITCODE -ne 0) {
    throw "Build failed."
}

Write-Host "Flashing EnviroNode..."
& $programmer -c port=SWD -w $elf -rst
if ($LASTEXITCODE -ne 0) {
    throw "Flash failed."
}

Write-Host ""
Write-Host "EnviroNode is flashed and running."
Write-Host "The OLED works without a serial terminal."
