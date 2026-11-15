# Uploads one file to S3 as a multipart upload (see "Upload packages to
# dl.luckgram.cc." in .github/workflows/build.yml).
#
#   s3_multipart_upload.ps1 -Path <file> -Url <https://bucket.s3.region.amazonaws.com/key>
#                           -ContentType <type> [-CheckUrl <public url of the object>]
#
# From xs-master every single connection is cut after about 50 s (run
# 43691567256: curl 56 at 50-55 s via the proxy and direct alike, 0.2-0.84
# MB/s), so one PUT of 76 MB never gets through. Parts of 5 MiB take about
# 10 s each; every request is retried on its own, alternating the runner's
# proxy (HTTPS_PROXY) and a direct connection, starting with the route that
# worked last.
#
# Signing: curl --aws-sigv4 with AWS_ACCESS_KEY_ID / AWS_SECRET_ACCESS_KEY /
# AWS_SESSION_TOKEN from the environment; the keys go to curl via stdin
# (-K -), never argv or the log. The log has per part its size, tries,
# route, seconds and speed, and S3 error codes (never response bodies).
#
# Exit code 0 when the object is complete (and, with -CheckUrl, served with
# the right size), otherwise 1 after trying to abort the upload.
param(
	[Parameter(Mandatory)][string]$Path,
	[Parameter(Mandatory)][string]$Url,
	[Parameter(Mandatory)][string]$ContentType,
	[string]$CheckUrl = '',
	[string]$Region = 'ap-southeast-1',
	[int]$PartMiB = 5, # S3 minimum for every part but the last.
	[int]$Tries = 5
)
$ProgressPreference = 'SilentlyContinue'
$ErrorActionPreference = 'Stop'

$name = Split-Path -Leaf $Path
$work = Join-Path ([IO.Path]::GetTempPath()) ("s3mp-" + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Force $work | Out-Null
$partFile = Join-Path $work 'part.bin'
$bodyFile = Join-Path $work 'body.xml'
$outFile = Join-Path $work 'response.xml'
$headFile = Join-Path $work 'headers.txt'
$emptyFile = Join-Path $work 'empty'
[IO.File]::WriteAllBytes($emptyFile, [byte[]]@())

$script:route = 'proxy' # the one that worked last goes first
$script:requests = 0

function Format-MB([double]$bytes) { '{0:N2}' -f ($bytes / 1MB) }

function Test-Body([string]$Pattern) {
	(Test-Path $outFile) -and (Select-String -Quiet -Path $outFile -Pattern $Pattern)
}

# A header of the last response in a curl -D dump (after redirects or
# "100 Continue" the last block counts).
function Get-Header([string]$Name) {
	if (-not (Test-Path $headFile)) { return '' }
	$value = ''
	foreach ($l in Get-Content $headFile) {
		if ($l -match '^HTTP/') { $value = '' }
		elseif ($l -match "^$([regex]::Escape($Name)):\s*(.*?)\s*$") { $value = $Matches[1] }
	}
	$value
}

# One signed request. Returns exit code, HTTP status, speed, ETag, seconds
# and the S3 error code of the response (if any).
function Invoke-S3([string]$Method, [string]$RequestUrl, [string]$Route,
		[string]$UploadFile = '', [string]$PostFile = '', [string]$Type = '',
		[int]$MaxTime = 60) {
	$script:requests++
	$extra = @()
	if ($Route -eq 'direct') { $extra += @('--noproxy', '*') }
	if ($env:AWS_SESSION_TOKEN) { $extra += @('-H', "x-amz-security-token: $env:AWS_SESSION_TOKEN") }
	if ($Type) { $extra += @('-H', "Content-Type: $Type") }
	if ($UploadFile) { $extra += @('-T', $UploadFile) }
	if ($PostFile) { $extra += @('--data-binary', "@$PostFile") }
	Remove-Item -Force $outFile, $headFile -ErrorAction SilentlyContinue
	$start = Get-Date
	# curl's own error message goes to stderr (the log), no secrets in it.
	# No --speed-limit: curl counts bytes as sent once they are in the
	# socket buffer, which Windows makes several MB large, so on a slow line
	# the first MBs of a part "go" at once and then nothing moves for tens
	# of seconds while the buffer drains (run 43696110572: every part was
	# given up after 31-36 s at "less than 20 KB/s", the line did ~128 KB/s).
	# --max-time alone bounds a try.
	$res = "user = `"$($env:AWS_ACCESS_KEY_ID):$($env:AWS_SECRET_ACCESS_KEY)`"" | curl.exe -sS -K - @extra `
		-X $Method --aws-sigv4 "aws:amz:$($Region):s3" -H 'x-amz-content-sha256: UNSIGNED-PAYLOAD' `
		--connect-timeout 20 --max-time $MaxTime `
		-D $headFile -o $outFile -w '%{http_code} %{speed_upload}' $RequestUrl
	$exit = $LASTEXITCODE
	$secs = ((Get-Date) - $start).TotalSeconds
	$http, $speed = if ("$res" -match '^(\d{3}) (\d+)') { $Matches[1], $Matches[2] } else { '000', '0' }
	$code = ''
	if ((Test-Path $outFile) -and ((Get-Item $outFile).Length -gt 0)) {
		$text = Get-Content -Raw $outFile
		if ($text -match '<Code>([A-Za-z]+)</Code>') { $code = $Matches[1] }
	}
	[pscustomobject]@{
		Exit = $exit; Http = $http; Speed = [double]$speed; Etag = (Get-Header 'ETag')
		Secs = $secs; Code = $code; Route = $Route
	}
}

# Retries one request up to $Tries times, alternating the routes. $Accept
# decides on a result; returns the accepted result or $null.
function Invoke-Retried([string]$What, [scriptblock]$Request, [scriptblock]$Accept) {
	$routes = if ($script:route -eq 'proxy') { 'proxy', 'direct' } else { 'direct', 'proxy' }
	for ($try = 1; $try -le $Tries; $try++) {
		$r = & $Request $routes[($try - 1) % 2]
		if (& $Accept $r) {
			$script:route = $r.Route
			$r | Add-Member -NotePropertyName Try -NotePropertyValue $try
			return $r
		}
		Write-Host ("  {0} try {1} ({2}): curl {3}, HTTP {4}{5}, {6:N0} s" -f $What, $try, $r.Route,
			$r.Exit, $r.Http, $(if ($r.Code) { " $($r.Code)" } else { '' }), $r.Secs)
		Start-Sleep -Seconds ([Math]::Min(3 * $try, 10))
	}
	return $null
}

$size = (Get-Item $Path).Length
$partSize = $PartMiB * 1MB
$count = [Math]::Max(1, [int][Math]::Ceiling($size / $partSize))
$started = Get-Date
Write-Host "$name ($(Format-MB $size) MB): $count parts of $PartMiB MiB"

# CreateMultipartUpload.
$create = Invoke-Retried 'create' {
	param($route) Invoke-S3 POST "$($Url)?uploads" $route -PostFile $emptyFile -Type $ContentType -MaxTime 40
} { param($r) $r.Exit -eq 0 -and $r.Http -eq '200' -and (Test-Body '<UploadId>') }
if (-not $create) {
	Write-Host "::error::$name`: could not start the multipart upload"
	Remove-Item -Recurse -Force $work
	exit 1
}
$uploadId = ([xml](Get-Content -Raw $outFile)).InitiateMultipartUploadResult.UploadId
$query = "uploadId=$([uri]::EscapeDataString($uploadId))"

$etags = @{}
$ok = $true
$stream = [IO.File]::OpenRead($Path)
try {
	$buffer = New-Object byte[] $partSize
	for ($n = 1; $n -le $count; $n++) {
		$read = 0
		while ($read -lt $partSize) {
			$got = $stream.Read($buffer, $read, $partSize - $read)
			if ($got -le 0) { break }
			$read += $got
		}
		$fs = [IO.File]::Create($partFile)
		$fs.Write($buffer, 0, $read)
		$fs.Dispose()
		$part = Invoke-Retried "part $n/$count" {
			param($route) Invoke-S3 PUT "$($Url)?partNumber=$n&$query" $route -UploadFile $partFile -MaxTime 60
		} { param($r) $r.Exit -eq 0 -and $r.Http -eq '200' -and $r.Etag }
		if (-not $part) {
			Write-Host "::error::$name`: part $n/$count not uploaded after $Tries tries"
			$ok = $false
			break
		}
		$etags[$n] = $part.Etag
		Write-Host ("  part {0}/{1}: {2} MB, try {3} ({4}), {5:N0} s, {6} MB/s" -f $n, $count,
			(Format-MB $read), $part.Try, $part.Route, $part.Secs, (Format-MB $part.Speed))
	}
} finally {
	$stream.Dispose()
}

if ($ok) {
	# CompleteMultipartUpload. S3 can answer 200 with an <Error> body, so
	# the body must be a CompleteMultipartUploadResult. A retry after a cut
	# connection may find the upload already completed (NoSuchUpload): the
	# check through -CheckUrl below decides then.
	$xml = New-Object Text.StringBuilder
	[void]$xml.Append('<CompleteMultipartUpload>')
	for ($n = 1; $n -le $count; $n++) {
		$etag = [Security.SecurityElement]::Escape($etags[$n])
		[void]$xml.Append("<Part><PartNumber>$n</PartNumber><ETag>$etag</ETag></Part>")
	}
	[void]$xml.Append('</CompleteMultipartUpload>')
	[IO.File]::WriteAllText($bodyFile, $xml.ToString())
	$complete = Invoke-Retried 'complete' {
		param($route) Invoke-S3 POST "$($Url)?$query" $route -PostFile $bodyFile -Type 'application/xml' -MaxTime 120
	} { param($r)
		($r.Exit -eq 0 -and $r.Http -eq '200' -and (Test-Body '<CompleteMultipartUploadResult')) -or
		($r.Http -eq '404' -and $r.Code -eq 'NoSuchUpload' -and $CheckUrl)
	}
	if (-not $complete) {
		Write-Host "::error::$name`: could not complete the multipart upload"
		$ok = $false
	} elseif ($complete.Code -eq 'NoSuchUpload') {
		Write-Host "  complete: NoSuchUpload on try $($complete.Try), an earlier try went through; checking the object"
	}
}

if ($ok -and $CheckUrl) {
	# The object as everyone gets it (dl.luckgram.cc): same size.
	$seen = ''
	foreach ($try in 1..$Tries) {
		$route = if ($try % 2) { $script:route } elseif ($script:route -eq 'proxy') { 'direct' } else { 'proxy' }
		$px = if ($route -eq 'direct') { @('--noproxy', '*') } else { @() }
		Remove-Item -Force $headFile -ErrorAction SilentlyContinue
		$http = curl.exe -sS -I @px --max-time 30 -D $headFile -o NUL -w '%{http_code}' $CheckUrl
		$seen = "$http $(Get-Header 'Content-Length')"
		if ($seen -eq "200 $size") { break }
		Start-Sleep -Seconds 5
	}
	if ($seen -ne "200 $size") {
		Write-Host "::error::$name`: $CheckUrl answers '$seen', expected '200 $size'"
		$ok = $false
	}
}

if (-not $ok) {
	# Parts of an unfinished upload are kept (and billed) until aborted.
	$abort = Invoke-Retried 'abort' {
		param($route) Invoke-S3 DELETE "$($Url)?$query" $route -MaxTime 30
	} { param($r) $r.Exit -eq 0 -and ($r.Http -eq '204' -or $r.Code -eq 'NoSuchUpload') }
	if ($abort) {
		Write-Host "  aborted the multipart upload"
	} else {
		Write-Host "::warning::$name`: multipart upload not aborted, its parts stay in the bucket until a lifecycle rule (AbortIncompleteMultipartUpload) removes them"
	}
	Remove-Item -Recurse -Force $work
	exit 1
}

$secs = ((Get-Date) - $started).TotalSeconds
Write-Host ("{0}: done, {1} parts, {2} requests, {3:N0} s, {4} MB/s on average" -f $name, $count,
	$script:requests, $secs, (Format-MB ($size / [Math]::Max($secs, 1))))
Remove-Item -Recurse -Force $work
exit 0
