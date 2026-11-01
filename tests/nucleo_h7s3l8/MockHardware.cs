// Author: Ruslan Kovtun (shpegun60), codexAi. SPDX-License-Identifier: MIT.
// Offline fault injection only. No USB/SWD/COM APIs or external tools are used.
using System;
using System.IO;
using System.Security.Cryptography;
using System.Text;

internal static class MockHardware
{
    private static int Main(string[] arguments)
    {
        string mode = Environment.GetEnvironmentVariable("TELEMETRY_MOCK_MODE") ?? "pass";
        string flash = Environment.GetEnvironmentVariable("TELEMETRY_MOCK_FLASH");
        string calls = Environment.GetEnvironmentVariable("TELEMETRY_MOCK_CALLS");
        string executable = Path.GetFileName(Environment.GetCommandLineArgs()[0]);
        File.AppendAllText(calls, executable + " " + String.Join(" ", arguments) + "\n");
        if (executable == "MockGit.exe")
        {
            if (arguments.Length == 4 && arguments[0] == "-C" && arguments[2] == "rev-parse" && arguments[3] == "HEAD")
            {
                Console.WriteLine(new string('1', 40));
                return 0;
            }
            return 93;
        }
        if (executable.StartsWith("arm-none-eabi-objcopy", StringComparison.Ordinal))
            return ConvertElf(arguments);
        if (executable.StartsWith("arm-none-eabi-", StringComparison.Ordinal))
        {
            Console.WriteLine("OFFLINE MOCK tool: " + executable);
            return 0;
        }
        if (executable == "MockGui.exe")
            return Gui(arguments, mode);
        if (executable == "MockReadMany.exe" || executable == "tested-read-many.exe")
            return ReadMany(arguments, mode);
        if (Array.IndexOf(arguments, "--version") >= 0)
        {
            Console.WriteLine("OFFLINE MOCK programmer; no hardware");
            return 0;
        }
        Console.WriteLine("ST-LINK SN  : 002A001F3033510135393935");
        if (Array.IndexOf(arguments, "-l") >= 0)
            return 0;
        Console.WriteLine("Board       : NUCLEO-H7S3L8");
        Console.WriteLine("Device ID   : " + (mode == "wrongdevice" ? "0x480" : "0x485"));
        Console.WriteLine("Device name : STM32H7RSxx");
        Console.WriteLine("Flash size  : 64 KBytes (default)");
        int read = Array.IndexOf(arguments, "-u");
        if (read >= 0)
        {
            string output = arguments[read + 3];
            File.Copy(flash, output, false);
            if (mode == "readbackwrong" && Path.GetFileName(output).StartsWith("restored-"))
            {
                byte[] altered = File.ReadAllBytes(output);
                altered[0] ^= 1;
                File.WriteAllBytes(output, altered);
            }
            return 0;
        }
        int write = Array.IndexOf(arguments, "-w");
        if (write >= 0)
        {
            bool restore = Path.GetFileName(arguments[write + 1]).StartsWith("original-");
            if (restore && mode == "restorefail")
                return 8;
            if (restore && mode == "readmany-restore-retry" && !File.Exists(flash + ".restore-attempted"))
            {
                File.WriteAllText(flash + ".restore-attempted", "offline first restore attempt");
                return 8;
            }
            byte[] image = File.ReadAllBytes(arguments[write + 1]);
            byte[] bytes = new byte[65536];
            for (int i = 0; i < bytes.Length; ++i)
                bytes[i] = 255;
            Array.Copy(image, bytes, mode == "flashfail" && !restore ? image.Length / 2 : image.Length);
            File.WriteAllBytes(flash, bytes);
            if (mode == "flashfail" && !restore)
                return 7;
            Console.WriteLine("Download verified successfully");
        }
        return 0;
    }

    private static int Gui(string[] arguments, string mode)
    {
        if (Array.IndexOf(arguments, "--check-device") >= 0)
        {
            int report = Array.IndexOf(arguments, "--report");
            string json = "{\"passed\":" + (mode == "checkfail" ? "false" : "true") +
                ",\"profile\":\"nucleo-h7s3l8-demo\",\"port\":\"COM6\",\"baud\":115200," +
                "\"voltage\":true,\"exactCounter\":true,\"demonstration\":true,\"scalarCount\":9," +
                "\"fields\":[{\"field\":\"measure.Voltage\",\"values\":[{\"value\":\"230.1\"}]}," +
                "{\"field\":\"device.Counter\",\"values\":[{\"value\":\"18446744073709551615\"}]}," +
                "{\"field\":\"device.Status\",\"values\":[{\"value\":\"true\"}]}]}";
            File.WriteAllText(arguments[report + 1], json);
            return mode == "checkfail" ? 9 : 0;
        }
        if (mode == "guifail")
            return 9;
        if (mode == "guitimeout")
            System.Threading.Thread.Sleep(60000);
        int screenshot = Array.IndexOf(arguments, "--screenshot");
        // The harness tests the runner's signature/size acceptance, not rendering.
        byte[] png = new byte[200];
        byte[] signature = { 137, 80, 78, 71, 13, 10, 26, 10 };
        Array.Copy(signature, png, signature.Length);
        File.WriteAllBytes(arguments[screenshot + 1], png);
        Console.WriteLine("Inspector QML smoke: published=1 roots=1 endpointRows=3 valueRows=3");
        return 0;
    }

    private static int ConvertElf(string[] arguments)
    {
        // Minimal file-only objcopy stand-in. RunHardware has already checked all
        // LOADs; no real compiler, readelf, programmer or hardware is invoked.
        byte[] elf = File.ReadAllBytes(arguments[arguments.Length - 2]);
        int offset = (int)BitConverter.ToUInt32(elf, 28);
        int count = BitConverter.ToUInt16(elf, 44);
        uint end = 0x08000000;
        for (int i = 0; i < count; ++i)
        {
            int at = offset + 32 * i;
            if (BitConverter.ToUInt32(elf, at) == 1)
                end = Math.Max(end, BitConverter.ToUInt32(elf, at + 12) + BitConverter.ToUInt32(elf, at + 16));
        }
        byte[] output = new byte[end - 0x08000000];
        for (int i = 0; i < output.Length; ++i)
            output[i] = 255;
        for (int i = 0; i < count; ++i)
        {
            int at = offset + 32 * i;
            if (BitConverter.ToUInt32(elf, at) != 1)
                continue;
            int size = (int)BitConverter.ToUInt32(elf, at + 16);
            if (size != 0)
                Array.Copy(elf, BitConverter.ToUInt32(elf, at + 4), output,
                    BitConverter.ToUInt32(elf, at + 12) - 0x08000000, size);
        }
        File.WriteAllBytes(arguments[arguments.Length - 1], output);
        return 0;
    }

    private static string Hex(byte[] bytes)
    {
        return BitConverter.ToString(bytes).Replace("-", "").ToLowerInvariant();
    }

    private static string Sha(byte[] bytes)
    {
        using (SHA256 hash = SHA256.Create())
            return Hex(hash.ComputeHash(bytes));
    }

    private static byte[] Reply(int count, bool mixed, string mode)
    {
        using (MemoryStream stream = new MemoryStream())
        using (BinaryWriter writer = new BinaryWriter(stream))
        {
            writer.Write((ushort)count);
            for (int index = 0; index < count; ++index)
            {
                uint id = (uint)(index + 1);
                if (mode == "readmany-duplicate" && index == 1)
                    id = 1;
                byte status = (byte)(mixed && index == 1 ? 3 : 0);
                if (mode == "readmany-status" && index == 0)
                    status = 3;
                writer.Write(id);
                writer.Write(status);
                writer.Write((byte)0);
                writer.Write((ushort)(status == 0 ? 4 : 0));
                if (status == 0)
                    writer.Write((uint)index);
            }
            return stream.ToArray();
        }
    }

    private static int ReadMany(string[] arguments, string mode)
    {
        if (Array.IndexOf(arguments, "--smoke") >= 0 || Array.IndexOf(arguments, "--check-device") >= 0)
            return 91; // A ReadMany run must never enter the old GUI path.
        int reportIndex = Array.IndexOf(arguments, "--report");
        if (reportIndex < 0 || reportIndex + 1 >= arguments.Length)
            return 92;
        if (mode == "readmany-timeout")
            System.Threading.Thread.Sleep(60000);
        if (mode == "readmany-missing")
            return 0;
        string output = arguments[reportIndex + 1];
        if (mode == "readmany-badjson")
        {
            File.WriteAllText(output, "{invalid");
            return 0;
        }
        byte[] capability = new byte[24];
        Array.Copy(Encoding.ASCII.GetBytes("TRM1"), capability, 4);
        capability[4] = 1; capability[5] = 32; capability[6] = 1; capability[7] = 1;
        Array.Copy(BitConverter.GetBytes((ushort)130), 0, capability, 8, 2);
        Array.Copy(BitConverter.GetBytes((ushort)514), 0, capability, 10, 2);
        Array.Copy(BitConverter.GetBytes((uint)2), 0, capability, 12, 4);
        Array.Copy(BitConverter.GetBytes((ulong)123), 0, capability, 16, 8);
        if (mode == "readmany-capability")
            capability[0] = 0;
        StringBuilder json = new StringBuilder("{\"schema\":1,\"profile\":\"nucleo-h7s3l8-read-many\",\"passed\":");
        json.Append(mode == "readmany-failed" ? "false" : "true");
        json.Append(",\"port\":\"COM6\",\"baud\":115200,\"capabilityHex\":\"").Append(Hex(capability));
        json.Append("\",\"descriptorSha256\":\"").Append(new string('a', 64));
        json.Append("\",\"sessions\":").Append(mode == "readmany-session" ? "[2,2]" : "[1,2]");
        json.Append(",\"cases\":[");
        int[] counts = { 8, 12, 16, 32 };
        for (int at = 0; at < counts.Length; ++at)
        {
            if (mode == "readmany-counts" && at == counts.Length - 1)
                break;
            if (at != 0)
                json.Append(',');
            int count = counts[at];
            byte[] raw = Reply(count, false, mode);
            json.Append("{\"count\":").Append(count).Append(",\"passed\":true,\"rawHex\":\"").Append(Hex(raw));
            json.Append("\",\"sha256\":\"").Append(mode == "readmany-hash" ? new string('0', 64) : Sha(raw));
            json.Append("\",\"elapsedMs\":1,\"requestBytes\":").Append(2 + 4 * count);
            json.Append(",\"replyBytes\":").Append(raw.Length).Append('}');
        }
        byte[] mixed = Reply(3, true, "pass");
        json.Append("],\"mixed\":{\"passed\":true,\"ok\":").Append(mode == "readmany-mixed" ? 3 : 2);
        json.Append(",\"unavailable\":1,\"rawHex\":\"").Append(Hex(mixed));
        json.Append("\",\"sha256\":\"").Append(Sha(mixed)).Append("\"},\"lifecycle\":{");
        string[] lifecycle = { "preHandoffTimeout", "preparedTimeout", "callbacksOnce", "lateReplyRejected",
            "reconnect", "staleBindingRejected", "noReplay", "sessionIncreased", "txReleased", "bootstrapComplete", "samePeer" };
        for (int at = 0; at < lifecycle.Length; ++at)
        {
            if (at != 0)
                json.Append(',');
            json.Append('"').Append(lifecycle[at]).Append("\":");
            json.Append(mode == "readmany-recovery" && lifecycle[at] == "lateReplyRejected" ? "false" : "true");
        }
        json.Append("}}");
        File.WriteAllText(output, json.ToString());
        return mode == "readmany-exit" ? 9 : 0;
    }
}
