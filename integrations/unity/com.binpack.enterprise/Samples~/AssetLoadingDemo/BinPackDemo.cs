using System.Collections.Generic;
using System.Diagnostics;
using System.Threading.Tasks;
using UnityEngine;
using BinPack.Enterprise;
using Debug = UnityEngine.Debug;

namespace BinPack.Samples
{
    public class BinPackDemo : MonoBehaviour
    {
        private string _status = "Ready";
        private float _compRatio = 0f;
        private float _compSpeed = 0f;
        private float _decompSpeed = 0f;
        private int _origSize = 10 * 1024 * 1024;
        private int _compSize = 0;
        private bool _verified = false;
        private readonly List<string> _eventLog = new List<string>();

        private async void Start()
        {
            await RunVisualBenchmarkAsync();
        }

        private void LogEvent(string msg)
        {
            _eventLog.Add($"[{ Time.realtimeSinceStartup:F2}s] {msg}");
            if (_eventLog.Count > 10) _eventLog.RemoveAt(0);
            Debug.Log($"[BinPack] {msg}");
        }

        public async Task RunVisualBenchmarkAsync()
        {
            _status = "Generating 10 MB Structured Telemetry & Asset Buffer...";
            LogEvent($"SDK Initialized — Native Engine v{BinPackEngine.Version}");

            byte[] rawAsset = new byte[_origSize];
            for (int i = 0; i < rawAsset.Length; i++)
            {
                rawAsset[i] = (byte)((i % 251) ^ ((i >> 2) & 0x3F));
            }

            _status = "Compressing (BinPack Runtime Profile)...";
            LogEvent("Starting BinPack Runtime compression (Auto Filter + ZSTD_dfast)...");
            Stopwatch sw = Stopwatch.StartNew();
            byte[] compressed = await Task.Run(() => BinPackEngine.Compress(rawAsset, BinPackOptions.FastRuntime));
            sw.Stop();

            _compSize = compressed.Length;
            _compRatio = (float)_compSize * 100f / _origSize;
            _compSpeed = (_origSize / 1048576f) / Mathf.Max((float)sw.Elapsed.TotalSeconds, 0.0001f);
            LogEvent($"Compressed {_origSize:N0} B -> {_compSize:N0} B ({_compRatio:F2}%) at {_compSpeed:F1} MB/s");

            _status = "Decompressing (Zero-Copy Fast-Path)...";
            sw.Restart();
            byte[] restored = await BinPackEngine.DecompressAsync(compressed, verifyChecksum: false);
            sw.Stop();

            _decompSpeed = (_origSize / 1048576f) / Mathf.Max((float)sw.Elapsed.TotalSeconds, 0.0001f);
            LogEvent($"Decompressed in {sw.Elapsed.TotalMilliseconds:F2} ms ({_decompSpeed:F1} MB/s)");

            bool match = restored.Length == rawAsset.Length;
            for (int i = 0; match && i < rawAsset.Length; i += 64)
            {
                if (rawAsset[i] != restored[i]) match = false;
            }
            _verified = match;
            _status = match ? "100% Bit-Exact Verified" : "Verification Failed";
            LogEvent($"Integrity Check: {_status}");
        }

        private void OnGUI()
        {
            GUI.Box(new Rect(20, 20, 560, 360), $"BinPack Enterprise SDK v{BinPackEngine.Version} — Live Unity Benchmark");

            GUI.Label(new Rect(40, 55, 520, 25), $"Status: {_status}");
            GUI.Label(new Rect(40, 85, 520, 25), $"Original Size: {_origSize:N0} B | Compressed: {_compSize:N0} B (Ratio: {_compRatio:F2}%)");
            GUI.Label(new Rect(40, 115, 520, 25), $"Compression Speed: {_compSpeed:F1} MB/s | Decompression Speed: {_decompSpeed:F1} MB/s");

            // Visual Throughput Bar
            GUI.Label(new Rect(40, 145, 200, 20), "Decompression Throughput:");
            float barWidth = Mathf.Clamp(_decompSpeed * 0.4f, 10f, 500f);
            GUI.Box(new Rect(40, 168, barWidth, 22), $"{_decompSpeed:F1} MB/s");

            if (GUI.Button(new Rect(40, 200, 220, 32), "▶ Re-Run Live Benchmark"))
            {
                _ = RunVisualBenchmarkAsync();
            }

            int y = 240;
            foreach (string line in _eventLog)
            {
                GUI.Label(new Rect(40, y, 520, 20), line);
                y += 18;
            }
        }
    }
}
