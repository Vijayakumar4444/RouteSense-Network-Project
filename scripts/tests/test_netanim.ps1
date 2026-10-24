param(
    [string]$Distribution = 'Ubuntu-24.04',
    [string]$LinuxRoot = '/home/vijayakumar/network-project/ns-3',
    [string]$Output = 'results/verification/netanim',
    [string]$AnimationDirectory = 'results/netanim'
)
$ErrorActionPreference = 'Stop'
$root = '\\wsl.localhost\' + $Distribution + $LinuxRoot.Replace('/', '\')
function Disk([string]$relative) { return $root + '\' + $relative.Replace('/', '\') }
function Require([bool]$condition, [string]$message) {
    if (-not $condition) { throw $message }
}
function Hash([string]$path) { return (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash }
Require (-not (Test-Path -LiteralPath (Disk $Output))) 'Use a fresh test output directory.'
New-Item -ItemType Directory -Path (Disk $Output) | Out-Null
$summary = @()
function Run([string]$protocol, [string]$case, [bool]$dynamic, [bool]$cross, [bool]$enabled, [string]$xml) {
    $duration = if ($dynamic) { 25 } else { 6 }
    $dir = "$Output/$protocol-$case"
    $exe = "$LinuxRoot/build/scratch/ns3.47-$protocol-default"
    $arguments = @("--simulationTime=$duration", "--dynamicScenario=$([int]$dynamic)",
        "--crossTraffic=$([int]$cross)", '--seed=7', '--run=2',
        "--enableAnimation=$([int]$enabled)", "--animationFile=$xml", "--outputDir=$dir")
    & wsl -d $Distribution --cd $LinuxRoot -- $exe @arguments *> (Disk "$Output/$protocol-$case.log")
    Require ($LASTEXITCODE -eq 0) "$protocol $case failed; inspect its log."
    $originalCase = if ($dynamic) { 'dynamic-cross' } else { 'static-single' }
    $previous = "results/generated/validation-final/$protocol-$originalCase"
    foreach ($file in @('metrics.csv','provenance.csv')) {
        Require ((Hash (Disk "$dir/$file")) -eq (Hash (Disk "$previous/$file"))) "$protocol $case changed $file"
    }
    $flowmon = if ($protocol -eq 'routesense') { 'routesense-flowmon.xml' } else { 'aodv-flowmon.xml' }
    Require ((Hash (Disk "$dir/$flowmon")) -eq (Hash (Disk "$previous/$flowmon"))) "$protocol $case changed FlowMonitor XML"
    return $dir
}
foreach ($protocol in @('routesense','routesense-aodv')) {
    foreach ($case in @('static','dynamic-cross')) {
        $dynamic = $case -eq 'dynamic-cross'
        $xml = "$AnimationDirectory/$protocol-$case.xml"
        Run $protocol $case $dynamic $dynamic $true $xml | Out-Null
        $doc = [xml]([IO.File]::ReadAllText((Disk $xml)))
        Require ($doc.DocumentElement.Name -eq 'anim') 'Wrong XML root.'
        Require ($doc.SelectNodes('/anim/node').Count -eq 12) 'Expected all 12 nodes.'
        Require ($doc.SelectNodes("/anim/pr[@meta-info]").Count -gt 0) 'Missing packet metadata/transmissions.'
        Require ($doc.SelectNodes('/anim/wpr').Count -gt 0) 'Missing wireless receptions.'
        Require ($doc.SelectNodes("/anim/nu[@p='d']").Count -ge 12) 'Missing readable descriptions.'
        Require ($doc.SelectNodes("/anim/nu[@p='c']").Count -ge 12) 'Missing node colors.'
        Require ($doc.SelectNodes("/anim/nu[@p='s']").Count -ge 12) 'Missing node sizes.'
        foreach ($n in 0..11) {
            $ip = '10.1.1.' + ($n + 1)
            Require ($doc.SelectNodes("/anim/ip[@n='$n']/address[text()='$ip']").Count -gt 0) "Missing N$n IP address."
        }
        Require ($doc.SelectNodes("/anim/node[@id='0' and @locX='0' and @locY='0']").Count -eq 1) 'Source moved.'
        Require ($doc.SelectNodes("/anim/node[@id='5' and @locX='400' and @locY='0']").Count -eq 1) 'Destination moved.'
        if ($dynamic) {
            Require ($doc.SelectNodes("/anim/nu[@p='p' and @id='1' and @t='12' and @x='80' and @y='160']").Count -gt 0) 'Missing actual move at 12s.'
            Require ($doc.SelectNodes("/anim/nu[@p='p' and @id='1' and @t='22' and @x='80' and @y='0']").Count -gt 0) 'Missing actual return at 22s.'
            foreach ($role in @('X1-TX','X2-TX','X1-RX','X2-RX')) {
                Require ($doc.SelectNodes("/anim/nu[@p='d' and contains(@descr,'$role')]").Count -gt 0) "Missing role $role."
            }
        }
        $summary += [pscustomobject]@{
            protocol=$protocol; scenario=$case; nodes=$doc.SelectNodes('/anim/node').Count
            tx=$doc.SelectNodes('/anim/pr').Count; rx=$doc.SelectNodes('/anim/wpr').Count
            metadata=$doc.SelectNodes("/anim/pr[@meta-info]").Count
            link_property_records=$doc.SelectNodes('/anim/nonp2plinkproperties').Count
            xml=$xml
        }
    }
    # Disabled mode must not even inspect or modify an already existing animation file.
    $xml = "$AnimationDirectory/$protocol-dynamic-cross.xml"
    $before = Hash (Disk $xml)
    Run $protocol 'disabled' $true $true $false $xml | Out-Null
    Require ((Hash (Disk $xml)) -eq $before) 'Disabled animation changed an existing XML.'
    $exe = "$LinuxRoot/build/scratch/ns3.47-$protocol-default"
    & wsl -d $Distribution --cd $LinuxRoot -- $exe --simulationTime=6 --enableAnimation=1 "--animationFile=$xml" "--outputDir=$Output/$protocol-overwrite-rejected" *> (Disk "$Output/$protocol-overwrite-rejected.log")
    Require ($LASTEXITCODE -ne 0) 'Existing animation file was not rejected.'
    Require ((Hash (Disk $xml)) -eq $before) 'Overwrite protection altered XML.'
}
$summary | Export-Csv -NoTypeInformation -LiteralPath (Disk "$Output/xml-checks.csv")
$summary | Format-Table
Write-Output 'PASS: XML structure, actual mobility, metadata, animation on/off metric equivalence and overwrite protection.'

