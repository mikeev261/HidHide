[CmdletBinding()]
param(
    [string[]]$Scenarios = @('dirty-100', 'clean-100', 'minimum-100', 'dirty-125', 'dirty-150', 'minimum-200'),
    [string]$OutputPath = ''
)
$ErrorActionPreference = 'Stop'
if (!$OutputPath) { $OutputPath = Join-Path $PSScriptRoot '..\artifacts\ui-validation' }
Add-Type -AssemblyName System.Drawing
Add-Type -ReferencedAssemblies System.Drawing -TypeDefinition @'
using System;
using System.Drawing;
using System.Drawing.Imaging;
using System.Runtime.InteropServices;
public static class ProfilesCapture {
    public delegate bool EnumProc(IntPtr window, IntPtr data);
    [DllImport("user32.dll")] static extern bool EnumWindows(EnumProc callback, IntPtr data);
    [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr window, out uint process);
    [DllImport("user32.dll")] static extern bool IsWindowVisible(IntPtr window);
    [DllImport("user32.dll")] static extern bool GetClientRect(IntPtr window, out Rect rect);
    [DllImport("user32.dll")] static extern IntPtr WindowFromPoint(Point point);
    [DllImport("user32.dll")] static extern IntPtr GetAncestor(IntPtr window,uint flags);
    [DllImport("user32.dll")] static extern bool ClientToScreen(IntPtr window, ref Point point);
    [DllImport("user32.dll")] static extern bool SetForegroundWindow(IntPtr window);
    [DllImport("user32.dll",SetLastError=true)] static extern bool SetWindowPos(IntPtr window, IntPtr after, int x,int y,int width,int height,uint flags);
    [DllImport("user32.dll")] static extern bool RedrawWindow(IntPtr window, IntPtr rect, IntPtr region, uint flags);
    [DllImport("dwmapi.dll")] static extern int DwmFlush();
    [StructLayout(LayoutKind.Sequential)] public struct Rect { public int left,top,right,bottom; }
    public static IntPtr Find(int pid) {
        IntPtr found=IntPtr.Zero;
        EnumWindows((window,data)=>{uint id;GetWindowThreadProcessId(window,out id);if(id==pid&&IsWindowVisible(window)){found=window;return false;}return true;},IntPtr.Zero);
        return found;
    }
    public static void Capture(IntPtr window,string path) {
        Rect rect; if(!GetClientRect(window,out rect))throw new Exception("GetClientRect failed");
        // Temporarily raise only our isolated test window so the captured client is unoccluded.
        if(!SetWindowPos(window,new IntPtr(-1),0,0,0,0,0x03))throw new Exception("Cannot raise presentation window: "+new System.ComponentModel.Win32Exception().Message);SetForegroundWindow(window);RedrawWindow(window,IntPtr.Zero,IntPtr.Zero,0x185);DwmFlush();System.Threading.Thread.Sleep(500);DwmFlush();
        Point origin=new Point(0,0);ClientToScreen(window,ref origin);
        foreach(var point in new[]{new Point(origin.X+4,origin.Y+4),new Point(origin.X+rect.right/2,origin.Y+rect.bottom/2),new Point(origin.X+rect.right-4,origin.Y+rect.bottom-4)})
            if(GetAncestor(WindowFromPoint(point),2)!=window)throw new Exception("Presentation client is occluded or outside the desktop; screenshot rejected");
        using(var bitmap=new Bitmap(rect.right,rect.bottom,PixelFormat.Format32bppArgb)) {
            using(var graphics=Graphics.FromImage(bitmap)) {try {graphics.CopyFromScreen(origin,Point.Empty,bitmap.Size,CopyPixelOperation.SourceCopy);}catch(Exception error){throw new Exception("Desktop pixels unavailable for presentation capture: "+error.Message,error);}}
            bitmap.Save(path,ImageFormat.Png);
        }
        SetWindowPos(window,new IntPtr(-2),0,0,0,0,0x13);
    }
}
'@
$out = [IO.Path]::GetFullPath($OutputPath)
[IO.Directory]::CreateDirectory($out) | Out-Null
$client = (Resolve-Path (Join-Path $PSScriptRoot '..\bin\Release\x64\HidHideClient.exe')).Path
$results = @()
foreach ($scenario in $Scenarios) {
    $token = [Guid]::NewGuid().ToString('D')
    $root = Join-Path ([IO.Path]::GetTempPath()) "HidHide-Profiles-Restart-Test-$token"
    $readyName = "Local\HidHide.Presentation.Ready.$token"
    $commandName = "Local\HidHide.Presentation.Command.$token"
    $completedName = "Local\HidHide.Presentation.Completed.$token"
    $ready = [Threading.EventWaitHandle]::new($false,'ManualReset',$readyName)
    $command = [Threading.EventWaitHandle]::new($false,'ManualReset',$commandName)
    $completed = [Threading.EventWaitHandle]::new($false,'ManualReset',$completedName)
    $start = [Diagnostics.ProcessStartInfo]::new($client)
    $start.Arguments = "--profile-restart-test presentation-$scenario `"$root`" $readyName $commandName $completedName"
    $start.UseShellExecute = $false
    $start.CreateNoWindow = $true
    $process = [Diagnostics.Process]::Start($start)
    try {
        if (!$ready.WaitOne(20000)) {
            $diagnostic = Join-Path $root '.acceptance-error.txt'
            if (Test-Path -LiteralPath $diagnostic) { throw (Get-Content -Raw -LiteralPath $diagnostic) }
            throw "Presentation $scenario did not become ready"
        }
        $window = [ProfilesCapture]::Find($process.Id)
        if ($window -eq [IntPtr]::Zero) { throw 'Actual executable window was not found' }
        $path = Join-Path $out "$scenario.png"
        [ProfilesCapture]::Capture($window,$path)
        $results += [pscustomobject]@{scenario=$scenario;status='PASS';screenshot=$path;repository=$root;executableSha256=(Get-FileHash $client).Hash;dpiEvidence='Real executable with injected DPI layout/font metrics; OS monitor scaling unchanged'}
        Write-Output "PASS: $scenario"
    } finally {
        $command.Set() | Out-Null
        if (!$process.HasExited) { $process.WaitForExit(10000) | Out-Null }
        if (!$process.HasExited) { $process.Kill(); $process.WaitForExit() }
        $ready.Dispose(); $command.Dispose(); $completed.Dispose(); $process.Dispose()
    }
}
$results | ConvertTo-Json -Depth 4 | Set-Content -Encoding UTF8 (Join-Path $out 'presentation-results.json')
