param(
    [string]$Toolchain='D:\software\MounRiver-Studio-2\MounRiver_Studio2\resources\app\resources\win32\components\WCH\Toolchain\RISC-V Embedded GCC12\bin',
    [string]$BuildDir='build_rev20260913_optional',
    [switch]$UsbBaseline,
    [switch]$LcdKeysOnly,
    [switch]$OfflineInternalOnly
)
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
Push-Location $PSScriptRoot
try {
    $gcc=Join-Path $Toolchain 'riscv-wch-elf-gcc.exe'
    $objcopy=Join-Path $Toolchain 'riscv-wch-elf-objcopy.exe'
    $size=Join-Path $Toolchain 'riscv-wch-elf-size.exe'
    if(!(Test-Path -LiteralPath $gcc)){throw 'Set -Toolchain to the WCH GCC12 bin directory.'}
    $flags=@('-march=rv32imac_zba_zbb_zbc_zbs_xw','-mabi=ilp32','-msmall-data-limit=8','-msave-restore','-Os','-g','-fsigned-char','-ffunction-sections','-fdata-sections','-fno-common','-Wall','-Wextra','-std=gnu99','-DRun_Core=1')
    if(([int]$UsbBaseline.IsPresent+[int]$LcdKeysOnly.IsPresent+[int]$OfflineInternalOnly.IsPresent) -gt 1){throw 'Select only one diagnostic mode.'}
    if($UsbBaseline){$flags+='-DAPP_BOOT_DIAGNOSTIC=1'}
    if($LcdKeysOnly){$flags+='-DAPP_BOOT_DIAGNOSTIC=2'}
    if($OfflineInternalOnly){$flags+='-DAPP_BOOT_DIAGNOSTIC=3'}
    foreach($core in @('V3F','V5F')){
        $out="$BuildDir/$core"
        New-Item -ItemType Directory -Force -Path $out | Out-Null
        $incs=@("-I$core/User",'-ICommon','-ICommon/Debug','-I../SRC/Core','-I../SRC/Peripheral/inc')
        $sources=@(Get-ChildItem "$core/User/*.c" | ForEach-Object { "$core/User/$($_.Name)" })
        $sources+=@(Get-ChildItem '../SRC/Peripheral/src/*.c' | ForEach-Object { "../SRC/Peripheral/src/$($_.Name)" })
        $sources+=@(Get-ChildItem '../SRC/Core/*.c' | ForEach-Object { "../SRC/Core/$($_.Name)" })
        $sources+='Common/Debug/debug.c'
        if($core -eq 'V5F') { $sources+=@(Get-ChildItem 'Common/*.c' | ForEach-Object { "Common/$($_.Name)" }) }
        $sources+="../SRC/Startup/startup_ch32h417_$($core.ToLower()).S"
        $objects=@()
        foreach($source in $sources){
            $name=$source.Replace('../','').Replace('/','_')+'.o'
            $object="$out/$name"
            & $gcc @flags "-DCore_$core" @incs -c $source -o $object
            if($LASTEXITCODE -ne 0){throw "Compile failed: $source"}
            $objects+=$object
        }
        if($core -eq 'V5F'){$objects+='Common/ch32h417_uhsif_it.o'}
        $elf="$out/analyzer_$core.elf"
        & $gcc @flags "-TCommon/Ld/$core/Link_$($core.ToLower()).ld" '-nostartfiles' '-Wl,--gc-sections' "-Wl,-Map,$out/analyzer_$core.map" '--specs=nano.specs' '--specs=nosys.specs' @objects -lm -o $elf
        if($LASTEXITCODE -ne 0){throw "Link failed: $core"}
        & $objcopy -O binary $elf "$out/analyzer_$core.bin"
        if($LASTEXITCODE -ne 0){throw 'objcopy binary failed'}
        & $objcopy -O ihex $elf "$out/analyzer_$core.hex"
        if($LASTEXITCODE -ne 0){throw 'objcopy hex failed'}
        & $size $elf
    }
    # IAP image starts at 0x6000. V3F launcher at +0, V5F at +0x1A000.
    $v3=[IO.File]::ReadAllBytes((Join-Path $PSScriptRoot "$BuildDir/V3F/analyzer_V3F.bin"))
    $v5=[IO.File]::ReadAllBytes((Join-Path $PSScriptRoot "$BuildDir/V5F/analyzer_V5F.bin"))
    if($v3.Length -gt 0x4000 -or $v5.Length -gt 0x20000){throw 'Image exceeds reserved link region'}
    $combined=New-Object byte[] (0x1a000+$v5.Length)
    for($i=0;$i -lt $combined.Length;$i++){$combined[$i]=255}
    [Array]::Copy($v3,0,$combined,0,$v3.Length)
    [Array]::Copy($v5,0,$combined,0x1a000,$v5.Length)
    # Generated build output, not a source edit. Never flash hardware automatically.
    $imageName='analyzer_optional_memory_IAP_0x6000.bin'
    if($UsbBaseline){$imageName='analyzer_usb_baseline_IAP_0x6000.bin'}
    if($LcdKeysOnly){$imageName='analyzer_lcd_keys_only_IAP_0x6000.bin'}
    if($OfflineInternalOnly){$imageName='analyzer_offline_internal_only_IAP_0x6000.bin'}
    $combinedPath=Join-Path $PSScriptRoot "$BuildDir/$imageName"
    [IO.File]::WriteAllBytes($combinedPath,$combined)
    Get-FileHash $combinedPath -Algorithm SHA256
} finally { Pop-Location }
