# 구상안 그림(투명 배경 PNG)의 윤곽선을 따서 좌표(그림 단위)로 저장한다 → build_weapons.py 가 이 윤곽으로 3D 모델을 만든다.
#   powershell -ExecutionPolicy Bypass -File Tools\WeaponBuild\trace_masks.ps1
# 입력: Saved\WeaponBuild\Mask\side_NN.png (옆모습, 1단위 = 4픽셀), Saved\WeaponBuild\FloorArt\T_VHW_Top_NN_*.png (탑뷰, 120단위 = 512픽셀)
# 출력: Saved\WeaponBuild\Contours\side_NN.json, top_NN.json  — {"loops":[{"area":부호 있는 넓이,"pts":[[x,y],...]}, ...]}
#       넓이 부호가 가장 큰 고리와 같으면 바깥 윤곽, 반대면 구멍(방아쇠울 같은).
$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$In = Join-Path $Root "Saved\WeaponBuild"
$Out = Join-Path $In "Contours"
New-Item -ItemType Directory -Force $Out | Out-Null
Add-Type -ReferencedAssemblies System.Drawing -TypeDefinition @"
using System; using System.Collections.Generic; using System.Drawing; using System.Drawing.Imaging; using System.Text; using System.Globalization;
public static class VHTrace {
  // 투명도 >= 128 인 픽셀을 채운 칸으로 보고, 채운 칸과 빈 칸 사이의 픽셀 경계를 이어 닫힌 고리를 만든 뒤 Douglas-Peucker 로 줄인다
  public static string Trace(string path, double pxPerUnit, double eps, double minAreaPx) {
    Bitmap bmp = new Bitmap(path); int W = bmp.Width, H = bmp.Height;
    BitmapData d = bmp.LockBits(new Rectangle(0, 0, W, H), ImageLockMode.ReadOnly, PixelFormat.Format32bppArgb);
    byte[] buf = new byte[d.Stride * H]; System.Runtime.InteropServices.Marshal.Copy(d.Scan0, buf, 0, buf.Length);
    bmp.UnlockBits(d); int stride = d.Stride; bmp.Dispose();
    Func<int,int,bool> F = (x, y) => x >= 0 && y >= 0 && x < W && y < H && buf[y * stride + x * 4 + 3] >= 128;
    var next = new Dictionary<long, List<long>>(); long K = W + 2;
    Func<int,int,long> V = (x, y) => (long)y * K + x;
    Action<long,long> Add = (a, b) => { List<long> l; if (!next.TryGetValue(a, out l)) { l = new List<long>(); next[a] = l; } l.Add(b); };
    for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) {
      if (!F(x, y)) continue;
      if (!F(x, y - 1)) Add(V(x + 1, y), V(x, y));
      if (!F(x - 1, y)) Add(V(x, y), V(x, y + 1));
      if (!F(x, y + 1)) Add(V(x, y + 1), V(x + 1, y + 1));
      if (!F(x + 1, y)) Add(V(x + 1, y + 1), V(x + 1, y));
    }
    var sb = new StringBuilder("{\"w\":" + W + ",\"h\":" + H + ",\"loops\":[");
    bool first = true;
    var keys = new List<long>(next.Keys);
    foreach (long start in keys) {
      List<long> outs; if (!next.TryGetValue(start, out outs) || outs.Count == 0) continue;
      var loop = new List<long>(); long cur = start; long prev = -1;
      while (true) {
        List<long> o; if (!next.TryGetValue(cur, out o) || o.Count == 0) break;
        int pick = 0;
        if (o.Count > 1 && prev >= 0) {   // 대각선으로 맞닿은 곳: 같은 쪽으로 도는 쪽을 골라 고리가 꼬이지 않게 한다
          long px = prev % K, py = prev / K, cx = cur % K, cy = cur / K; long dx = cx - px, dy = cy - py;
          for (int i = 0; i < o.Count; i++) { long nx = o[i] % K - cx, ny = o[i] / K - cy; if (dx * ny - dy * nx > 0) { pick = i; break; } }
        }
        long nxt = o[pick]; o.RemoveAt(pick); loop.Add(cur); prev = cur; cur = nxt;
        if (cur == start) break;
      }
      if (loop.Count < 4) continue;
      var pts = new List<double[]>(); foreach (long v in loop) pts.Add(new double[] { v % K, v / K });
      double area = 0; for (int i = 0; i < pts.Count; i++) { var a = pts[i]; var b = pts[(i + 1) % pts.Count]; area += a[0] * b[1] - b[0] * a[1]; } area *= 0.5;
      if (Math.Abs(area) < minAreaPx) continue;
      var simp = Simplify(pts, eps);
      if (simp.Count < 3) continue;
      if (!first) sb.Append(","); first = false;
      sb.Append("{\"area\":" + (area / (pxPerUnit * pxPerUnit)).ToString("0.###", CultureInfo.InvariantCulture) + ",\"pts\":[");
      for (int i = 0; i < simp.Count; i++) { if (i > 0) sb.Append(","); sb.Append("[" + (simp[i][0] / pxPerUnit).ToString("0.###", CultureInfo.InvariantCulture) + "," + (simp[i][1] / pxPerUnit).ToString("0.###", CultureInfo.InvariantCulture) + "]"); }
      sb.Append("]}");
    }
    sb.Append("]}"); return sb.ToString();
  }
  static List<double[]> Simplify(List<double[]> p, double eps) {
    int n = p.Count; int far = 0; double best = -1;
    for (int i = 1; i < n; i++) { double dd = Dist2(p[0], p[i]); if (dd > best) { best = dd; far = i; } }
    var a = new List<double[]>(); for (int i = 0; i <= far; i++) a.Add(p[i]);
    var b = new List<double[]>(); for (int i = far; i < n; i++) b.Add(p[i]); b.Add(p[0]);
    var ra = DP(a, eps); var rb = DP(b, eps);
    var r = new List<double[]>(ra); r.RemoveAt(r.Count - 1); var rb2 = new List<double[]>(rb); rb2.RemoveAt(rb2.Count - 1); r.AddRange(rb2); return r;
  }
  static List<double[]> DP(List<double[]> p, double eps) {
    if (p.Count < 3) return new List<double[]>(p);
    int idx = 0; double dmax = 0; var s = p[0]; var e = p[p.Count - 1];
    for (int i = 1; i < p.Count - 1; i++) { double dd = SegDist(p[i], s, e); if (dd > dmax) { dmax = dd; idx = i; } }
    if (dmax <= eps) return new List<double[]> { s, e };
    var l = DP(p.GetRange(0, idx + 1), eps); var r = DP(p.GetRange(idx, p.Count - idx), eps);
    l.RemoveAt(l.Count - 1); l.AddRange(r); return l;
  }
  static double Dist2(double[] a, double[] b) { double x = a[0] - b[0], y = a[1] - b[1]; return x * x + y * y; }
  static double SegDist(double[] p, double[] a, double[] b) {
    double vx = b[0] - a[0], vy = b[1] - a[1], L = vx * vx + vy * vy;
    double t = L > 0 ? Math.Max(0, Math.Min(1, ((p[0] - a[0]) * vx + (p[1] - a[1]) * vy) / L)) : 0;
    double x = a[0] + t * vx - p[0], y = a[1] + t * vy - p[1]; return Math.Sqrt(x * x + y * y);
  }
}
"@
Get-ChildItem (Join-Path $In "Mask") -Filter "side_*.png" | ForEach-Object {
	$json = [VHTrace]::Trace($_.FullName, 4.0, 1.1, 60.0)
	$name = $_.BaseName + ".json"; [IO.File]::WriteAllText((Join-Path $Out $name), $json)
	"{0}: 고리 {1}개, 점 {2}개" -f $name, ([regex]::Matches($json, 'area')).Count, ([regex]::Matches($json, '\],\[')).Count
}
Get-ChildItem (Join-Path $In "FloorArt") -Filter "T_VHW_Top_*.png" | ForEach-Object {
	$id = $_.BaseName.Substring(10, 2)
	$json = [VHTrace]::Trace($_.FullName, 512.0 / 120.0, 0.9, 30.0)
	[IO.File]::WriteAllText((Join-Path $Out "top_$id.json"), $json)
	"top_{0}.json: 고리 {1}개" -f $id, ([regex]::Matches($json, 'area')).Count
}
