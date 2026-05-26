param(
    [int]$Port = 8080
)

$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$listener = New-Object System.Net.HttpListener
$listener.Prefixes.Add("http://127.0.0.1:$Port/")
$listener.Start()

Write-Host "PCM monitor: http://127.0.0.1:$Port/pcm_serial_monitor.html"
Write-Host "Root: $root"
Write-Host "Press Ctrl+C to stop."

while ($listener.IsListening) {
    $context = $listener.GetContext()
    $path = $context.Request.Url.LocalPath.TrimStart('/')
    if ([string]::IsNullOrEmpty($path)) {
        $path = "pcm_serial_monitor.html"
    }

    $file = Join-Path $root ($path -replace '/', [IO.Path]::DirectorySeparatorChar)

    try {
        if (Test-Path $file -PathType Leaf) {
            $bytes = [IO.File]::ReadAllBytes($file)
            $ext = [IO.Path]::GetExtension($file).ToLowerInvariant()
            $contentType = switch ($ext) {
                ".html" { "text/html; charset=utf-8" }
                ".js" { "application/javascript; charset=utf-8" }
                ".css" { "text/css; charset=utf-8" }
                default { "application/octet-stream" }
            }

            $context.Response.StatusCode = 200
            $context.Response.ContentType = $contentType
            $context.Response.ContentLength64 = $bytes.Length
            $context.Response.OutputStream.Write($bytes, 0, $bytes.Length)
        } else {
            $context.Response.StatusCode = 404
            $msg = [Text.Encoding]::UTF8.GetBytes("Not found: $path")
            $context.Response.OutputStream.Write($msg, 0, $msg.Length)
        }
    } finally {
        $context.Response.Close()
    }
}
