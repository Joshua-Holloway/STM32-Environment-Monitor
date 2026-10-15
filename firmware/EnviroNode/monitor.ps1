$ErrorActionPreference = "Stop"

$serialDevice = Get-CimInstance Win32_SerialPort |
    Where-Object { $_.Description -match "STLink|STMicroelectronics" } |
    Select-Object -First 1

if (-not $serialDevice) {
    throw "No ST-LINK virtual COM port was found."
}

$port = New-Object System.IO.Ports.SerialPort $serialDevice.DeviceID,115200,None,8,one
$port.NewLine = "`r`n"
$port.ReadTimeout = 500

try {
    $port.Open()
    Write-Host "Monitoring $($serialDevice.DeviceID) at 115200 baud. Press Ctrl+C to stop."

    while ($true) {
        try {
            $line = $port.ReadLine()
            Write-Host $line
        }
        catch [System.TimeoutException] {
            # No complete line yet. Keep waiting.
        }
    }
}
finally {
    if ($port.IsOpen) {
        $port.Close()
    }
}