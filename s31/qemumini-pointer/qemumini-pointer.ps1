<#
qemumini-pointer.ps1: VMDisp9x 1.2025.0.119's QEMU display driver
(QEMUMINI.DRV) made to leave the mouse pointer to Windows' own DIB engine,
by one byte, inside a disk image.

Why (research\FLICKER-AUDIT.md, BUILD-NOTES section 25): as released, the
driver draws the pointer itself and rubs it out and draws it again around
every drawing Windows does anywhere on the screen.  On tiny386 each of those
takes about 18 ms of the guest's time, so the pointer blinks, breaks up or
shows as a black shape while anything moves, and Windows spends a large part
of its time on it.  The driver has the other way built in (Windows' DIB
engine draws the pointer, only where it has to); one line of its source,
init.c 207, "mouse_vxd = TRUE;", turns it off.  In the released file that
line is the six bytes C7 06 B8 06 01 00 at offset 0x2064; this script makes
the 01 at 0x2068 a 00, and nothing else.

    qemumini-pointer IMAGE            check, say what it will do, ask, do it
    qemumini-pointer IMAGE -Check     only say what is there
    qemumini-pointer IMAGE -Undo      put the 01 back
    qemumini-pointer IMAGE -Yes       do not ask
    qemumini-pointer IMAGE -Out FILE  a changed copy in FILE, IMAGE left as it is

IMAGE is either
  * the driver's own floppy image (vmdisp9x-1.2025.0.119b-driver-2d.img):
    a changed copy is made beside it, IMAGE-ptrfix.img (or -Out), and the
    original is not touched; then Have Disk from the copy as TEST-PLAN 13
    says; or
  * a Windows 95 hard disk image with the driver installed (win95.img, with
    Windows shut down from the Start menu): changed in place, that one byte
    of C:\WINDOWS\SYSTEM\QEMUMINI.DRV, after every check below (or a changed
    copy with -Out).

Checks before anything is written: the image is a FAT12, FAT16 or FAT32
volume, or holds one in its partition table; QEMUMINI.DRV is found by name
through the directories and the FAT (the same bytes are in the other
drivers on the floppy, so it is never searched for by its contents); the
file is 15,534 bytes, its SHA-256 is the released file's (or, already
changed, the changed one's), and the six bytes are where they should be.
After the write the file is read back through the FAT and its SHA-256 must
be the changed (or, with -Undo, the released) file's.

Exit status: 0 done, or nothing to do; 1 refused or failed (nothing
written, unless it says otherwise); 2 not found.

VMDisp9x is Jaroslav Hensl's, MIT licensed (github.com/JHRobotics/vmdisp9x).
This script carries none of its bytes but the six it looks for.
#>
param(
    [Parameter(Mandatory = $true, Position = 0)][string]$Image,
    [switch]$Check,
    [switch]$Undo,
    [switch]$Yes,
    [string]$Out
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 2

$SizeWant = 15534
$ShaOrig = '0b3584b11420b3bc96652d9ae147df2188572e0d1b967a098ac8513eb97b713d'
$ShaPatched = 'e4c47e9aa983db1cebbeb09e625c53e71986e78868b2ff9094a22abfec0538e9'
$At = 0x2064                              # mov word ptr [06B8h],1
$Bytes = [byte[]](0xC7, 0x06, 0xB8, 0x06)  # then the 1 (01 00) at 0x2068

function Say([string]$s) { Write-Host $s }
function Fail([string]$s, [int]$code = 1) { Write-Host "qemumini-pointer: $s"; exit $code }

function Read-At($fs, [long]$off, [int]$n) {
    $buf = New-Object byte[] $n
    [void]$fs.Seek($off, [System.IO.SeekOrigin]::Begin)
    $got = 0
    while ($got -lt $n) {
        $r = $fs.Read($buf, $got, $n - $got)
        if ($r -le 0) { throw "the image ends at $($off + $got), before $($off + $n)" }
        $got += $r
    }
    return ,$buf
}

function U16($b, [int]$o) { return [int]([BitConverter]::ToUInt16($b, $o)) }
function U32($b, [int]$o) { return [long]([BitConverter]::ToUInt32($b, $o)) }

function Sha256Hex([byte[]]$b) {
    $h = [System.Security.Cryptography.SHA256]::Create()
    try { return ([BitConverter]::ToString($h.ComputeHash($b))).Replace('-', '').ToLowerInvariant() }
    finally { $h.Dispose() }
}

# a FAT boot sector at off: its figures, or $null
function Get-Volume($fs, [long]$off) {
    $s = Read-At $fs $off 512
    if ($s[510] -ne 0x55 -or $s[511] -ne 0xAA) { return $null }
    if ($s[0] -ne 0xEB -and $s[0] -ne 0xE9) { return $null }
    $bps = U16 $s 11
    $spc = [int]$s[13]
    if ($bps -notin 512, 1024, 2048, 4096 -or $spc -lt 1 -or ($spc -band ($spc - 1)) -ne 0) { return $null }
    $rsvd = U16 $s 14
    $nfats = [int]$s[16]
    $rootEnt = U16 $s 17
    $tot = U16 $s 19
    if ($tot -eq 0) { $tot = U32 $s 32 }
    $fatSz = U16 $s 22
    $rootClus = 0
    if ($fatSz -eq 0) { $fatSz = U32 $s 36; $rootClus = U32 $s 44 }
    if ($nfats -lt 1 -or $fatSz -lt 1 -or $rsvd -lt 1 -or $tot -lt 1) { return $null }
    $rootSecs = [long][math]::Floor(($rootEnt * 32 + $bps - 1) / $bps)
    $firstData = [long]$rsvd + [long]$nfats * $fatSz + $rootSecs
    if ($tot -le $firstData) { return $null }
    $clusters = [long][math]::Floor(($tot - $firstData) / $spc)
    $kind = if ($clusters -lt 4085) { 12 } elseif ($clusters -lt 65525) { 16 } else { 32 }
    if ($kind -eq 32 -and $rootClus -lt 2) { return $null }
    return [pscustomobject]@{
        Off = $off; Bps = $bps; Spc = $spc; Rsvd = $rsvd; NFats = $nfats; RootEnt = $rootEnt
        FatSz = $fatSz; RootSecs = $rootSecs; FirstData = $firstData; Clusters = $clusters
        Kind = $kind; RootClus = $rootClus; ClusterBytes = [long]$bps * $spc
    }
}

function Next-Cluster($fs, $v, [long]$c) {
    $fat = $v.Off + [long]$v.Rsvd * $v.Bps
    switch ($v.Kind) {
        12 {
            $b = Read-At $fs ($fat + $c + [math]::Floor($c / 2)) 2
            $x = U16 $b 0
            if ($c % 2) { $x = $x -shr 4 } else { $x = $x -band 0xFFF }
            if ($x -ge 0xFF8) { return -1 }
            return [long]$x
        }
        16 {
            $x = U16 (Read-At $fs ($fat + 2 * $c) 2) 0
            if ($x -ge 0xFFF8) { return -1 }
            return [long]$x
        }
        default {
            $x = (U32 (Read-At $fs ($fat + 4 * $c) 4) 0) -band 0x0FFFFFFF
            if ($x -ge 0x0FFFFFF8) { return -1 }
            return [long]$x
        }
    }
}

function Cluster-Offset($v, [long]$c) {
    if ($c -lt 2 -or $c -ge $v.Clusters + 2) { throw "cluster $c is outside the volume" }
    return $v.Off + ($v.FirstData + ($c - 2) * $v.Spc) * $v.Bps
}

# the clusters of a chain from c, at most max of them
function Get-Chain($fs, $v, [long]$c, [long]$max) {
    $list = New-Object System.Collections.Generic.List[long]
    while ($c -ge 2 -and $list.Count -lt $max) {
        if ($c -ge $v.Clusters + 2) { throw "a cluster chain leaves the volume ($c)" }
        $list.Add($c)
        $c = Next-Cluster $fs $v $c
    }
    return ,$list
}

# the 32-byte entries of a directory (cluster 0: the root)
function Get-Dir($fs, $v, [long]$clus) {
    if ($clus -eq 0 -and $v.Kind -ne 32) {
        $data = Read-At $fs ($v.Off + ($v.Rsvd + [long]$v.NFats * $v.FatSz) * $v.Bps) ($v.RootEnt * 32)
    } else {
        if ($clus -eq 0) { $clus = $v.RootClus }
        $chain = Get-Chain $fs $v $clus 4096
        $ms = New-Object System.IO.MemoryStream
        foreach ($c in $chain) {
            $b = Read-At $fs (Cluster-Offset $v $c) ([int]$v.ClusterBytes)
            $ms.Write($b, 0, $b.Length)
        }
        $data = $ms.ToArray()
    }
    $entries = @()
    for ($i = 0; $i + 32 -le $data.Length; $i += 32) {
        $first = $data[$i]
        if ($first -eq 0) { break }
        if ($first -eq 0xE5) { continue }
        $attr = $data[$i + 11]
        if (($attr -band 0x0F) -eq 0x0F) { continue }       # a long name's part
        if ($attr -band 0x08) { continue }                   # the volume label
        $name = [System.Text.Encoding]::ASCII.GetString($data, $i, 11)
        $start = [long](U16 $data ($i + 26))
        if ($v.Kind -eq 32) { $start = $start + [long](U16 $data ($i + 20)) * 65536 }
        $entries += [pscustomobject]@{ Name = $name; Attr = $attr; Start = $start; Size = (U32 $data ($i + 28)) }
    }
    return ,$entries
}

function Find-Entry($entries, [string]$name83, [bool]$dir) {
    foreach ($e in $entries) {
        if ($e.Name -ceq $name83 -and ((($e.Attr -band 0x10) -ne 0) -eq $dir)) { return $e }
    }
    return $null
}

function Read-File($fs, $v, $chain) {
    $buf = New-Object byte[] $SizeWant
    $done = 0
    foreach ($c in $chain) {
        $n = [int][math]::Min($v.ClusterBytes, $SizeWant - $done)
        if ($n -le 0) { break }
        $b = Read-At $fs (Cluster-Offset $v $c) $n
        [Array]::Copy($b, 0, $buf, $done, $n)
        $done += $n
    }
    if ($done -ne $SizeWant) { throw "the file's clusters end after $done of $SizeWant bytes" }
    return ,$buf
}

# ---- what is in the image
if (-not (Test-Path -LiteralPath $Image -PathType Leaf)) { Fail "no such file: $Image" 2 }
$Image = (Resolve-Path -LiteralPath $Image).ProviderPath
$len = (Get-Item -LiteralPath $Image).Length

function Inspect([string]$path) {
    $fs = [System.IO.File]::Open($path, [System.IO.FileMode]::Open, [System.IO.FileAccess]::Read, [System.IO.FileShare]::Read)
    try {
        $v = Get-Volume $fs 0
        $where = 'the image itself (no partition table)'
        if (-not $v) {
            $mbr = Read-At $fs 0 512
            if ($mbr[510] -eq 0x55 -and $mbr[511] -eq 0xAA) {
                for ($p = 0; $p -lt 4 -and -not $v; $p++) {
                    $e = 446 + 16 * $p
                    $type = $mbr[$e + 4]
                    if ($type -in 0x01, 0x04, 0x06, 0x0B, 0x0C, 0x0E) {
                        $lba = U32 $mbr ($e + 8)
                        $v = Get-Volume $fs ($lba * 512)
                        if ($v) { $where = "partition $($p + 1) (type 0x{0:X2}, from sector $lba)" -f $type }
                    }
                }
            }
        }
        if (-not $v) { Fail "no FAT volume in $path (neither the image itself nor a partition in it)" 2 }
        $root = Get-Dir $fs $v 0
        $file = Find-Entry $root 'QEMUMINIDRV' $false
        $shown = 'QEMUMINI.DRV'
        if (-not $file) {
            $w = Find-Entry $root 'WINDOWS    ' $true
            if ($w) {
                $sys = Find-Entry (Get-Dir $fs $v $w.Start) 'SYSTEM     ' $true
                if ($sys) {
                    $file = Find-Entry (Get-Dir $fs $v $sys.Start) 'QEMUMINIDRV' $false
                    $shown = 'WINDOWS\SYSTEM\QEMUMINI.DRV'
                }
            }
        }
        if (-not $file) { Fail "no QEMUMINI.DRV in the root or in WINDOWS\SYSTEM of $where" 2 }
        if ($file.Size -ne $SizeWant) {
            Fail "$shown is $($file.Size) bytes, not ${SizeWant}: not VMDisp9x 1.2025.0.119's file; nothing changed"
        }
        $chain = Get-Chain $fs $v $file.Start 1000
        $data = Read-File $fs $v $chain
        $sha = Sha256Hex $data
        $c = [long][math]::Floor(0x2068 / $v.ClusterBytes)
        $byteOff = (Cluster-Offset $v $chain[[int]$c]) + (0x2068 % $v.ClusterBytes)
        $direct = (Read-At $fs $byteOff 1)[0]
        return [pscustomobject]@{
            Vol = $v; Where = $where; Shown = $shown; Data = $data; Sha = $sha
            ByteOff = $byteOff; Direct = $direct; Floppy = ($shown -eq 'QEMUMINI.DRV')
        }
    } finally { $fs.Dispose() }
}

$f = Inspect $Image
$state = 'unknown'
if ($f.Sha -eq $ShaOrig) { $state = 'released' } elseif ($f.Sha -eq $ShaPatched) { $state = 'changed' }
$six = ($f.Data[$At..($At + 5)] | ForEach-Object { '{0:X2}' -f $_ }) -join ' '
Say "Image:   $Image ($len bytes)"
Say "Volume:  FAT$($f.Vol.Kind) in $($f.Where), $($f.Vol.ClusterBytes)-byte clusters"
Say "File:    $($f.Shown), $SizeWant bytes, SHA-256 $($f.Sha)"
Say ("At 0x{0:X4}: {1}  (image offset {2}, byte there {3:X2})" -f $At, $six, $f.ByteOff, $f.Direct)
switch ($state) {
    'released' { Say 'State:   as released: the driver draws the pointer itself (mouse_vxd = TRUE).' }
    'changed' { Say "State:   already changed: Windows' DIB engine draws the pointer." }
    default { Fail "this QEMUMINI.DRV is not VMDisp9x 1.2025.0.119's, changed or not (SHA-256 above); nothing changed" }
}
for ($i = 0; $i -lt 4; $i++) { if ($f.Data[$At + $i] -ne $Bytes[$i]) { Fail 'the bytes at 0x2064 are not the expected instruction; nothing changed' } }
if ($f.Data[0x2069] -ne 0 -or $f.Data[0x2068] -ne $f.Direct) { Fail 'the byte at 0x2068 does not read back the same through the FAT; nothing changed' }
if ($Check) { exit 0 }

$want = if ($Undo) { 'released' } else { 'changed' }
if ($state -eq $want) { Say 'Nothing to do.'; exit 0 }
$newByte = [byte]$(if ($Undo) { 1 } else { 0 })
$newSha = if ($Undo) { $ShaOrig } else { $ShaPatched }

# ---- where the change goes
$target = $Image
if ($Out) {
    $target = $Out
} elseif ($f.Floppy) {
    $dir = [System.IO.Path]::GetDirectoryName($Image)
    $base = [System.IO.Path]::GetFileNameWithoutExtension($Image)
    $ext = [System.IO.Path]::GetExtension($Image)
    $target = Join-Path $dir ("$base-ptrfix$ext")
    if ($Undo) { $target = Join-Path $dir ("$base-released$ext") }
}
if ($target -ne $Image) {
    $target = [System.IO.Path]::GetFullPath($target)
    if (Test-Path -LiteralPath $target) { Fail "$target is there already; nothing changed (remove it, or name another with -Out)" }
}
if ($target -eq $Image) {
    Say ("Change:  the byte at image offset {0} from {1:X2} to {2:X2}, in this image, nothing else." -f $f.ByteOff, $f.Direct, $newByte)
    Say '         Windows must not be running from this image now (shut down from the Start menu first).'
} else {
    Say "Change:  a copy of the image to $target, then in the copy the byte at"
    Say ("         offset {0} from {1:X2} to {2:X2}; the image named stays as it is." -f $f.ByteOff, $f.Direct, $newByte)
}
if (-not $Yes) {
    $a = Read-Host 'Go ahead? (y/n)'
    if ($a -notmatch '^[yY]') { Say 'Nothing changed.'; exit 1 }
}
if ($target -ne $Image) {
    Copy-Item -LiteralPath $Image -Destination $target
    $g = Inspect $target
    if ($g.Sha -ne $f.Sha -or $g.ByteOff -ne $f.ByteOff) { Fail "the copy $target does not read back as the image did; left as copied, not changed" }
}
$fs = [System.IO.File]::Open($target, [System.IO.FileMode]::Open, [System.IO.FileAccess]::ReadWrite, [System.IO.FileShare]::None)
try {
    [void]$fs.Seek($f.ByteOff, [System.IO.SeekOrigin]::Begin)
    $fs.WriteByte($newByte)
    $fs.Flush($true)
} finally { $fs.Dispose() }
$g = Inspect $target
if ($g.Sha -ne $newSha) {
    Fail ("after the write the file's SHA-256 is {0}, not {1}: the one byte at offset {2} of {3} was written, and is wrong; put it back with -Undo or from a copy" -f $g.Sha, $newSha, $f.ByteOff, $target)
}
Say "Done:    $($g.Shown) in $target now has SHA-256 $($g.Sha)"
if ($Undo) { Say "         (as released: the driver draws the pointer itself again)." }
else { Say "         (Windows' DIB engine draws the pointer; restart Windows from this image to use it)." }
exit 0
