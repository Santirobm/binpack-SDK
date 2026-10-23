using System.Diagnostics;
using System.Text;
using System.Threading.Tasks;
using UnityEngine;
using BinPack.Enterprise;
using Debug = UnityEngine.Debug;

namespace BinPack.Samples
{
    public class BinPackDemo : MonoBehaviour
    {
        private async void Start()
        {
            Debug.Log($"[BinPack] SDK Initialized. Native Engine Version: {BinPackEngine.Version}");

            // 1. Generate 10 MB simulated game payload
            byte[] rawAsset = new byte[10 * 1024 * 1024];
            for (int i = 0; i < rawAsset.Length; i++)
            {
                rawAsset[i] = (byte)(i % 251);
            }

            Debug.Log($"[BinPack] Original Asset Size: {rawAsset.Length:N0} bytes (10.0 MB)");

            // 2. Compress using High-Throughput Runtime Profile
            Stopwatch sw = Stopwatch.StartNew();
            byte[] compressed = BinPackEngine.Compress(rawAsset, BinPackOptions.FastRuntime);
            sw.Stop();

            double compRatio = (double)compressed.Length * 100.0 / rawAsset.Length;
            Debug.Log($"[BinPack] Compressed Size: {compressed.Length:N0} bytes ({compRatio:F2}%) in {sw.ElapsedMilliseconds} ms");

            // 3. Decompress Asynchronously with Zero-Copy Fast-Path
            sw.Restart();
            byte[] restored = await BinPackEngine.DecompressAsync(compressed, verifyChecksum: false);
            sw.Stop();

            double decompSpeed = (rawAsset.Length / 1048576.0) / (sw.Elapsed.TotalSeconds > 0 ? sw.Elapsed.TotalSeconds : 0.001);
            Debug.Log($"[BinPack] Async Decompression Complete in {sw.Elapsed.TotalMilliseconds:F2} ms (Throughput: {decompSpeed:F1} MB/s)");

            // Verify integrity
            bool match = true;
            for (int i = 0; i < rawAsset.Length; i++)
            {
                if (rawAsset[i] != restored[i])
                {
                    match = false;
                    break;
                }
            }

            Debug.Log($"[BinPack] Bit-for-bit Verification: {(match ? "PASS (100% Identical)" : "FAIL")}");
        }
    }
}
